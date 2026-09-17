#include "RecorderEngine.h"

#include <algorithm>
#include <limits>

namespace
{
constexpr std::uint32_t kRequiredSampleRate = 48000;
constexpr int kRequiredBitsPerSample = 24;
}

RecorderEngine::RecorderEngine()
    : juce::Thread("DAW Streamer Recorder multistream")
{
    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        auto& stream = streams[i];
        stream.role = static_cast<dawstreamer::StreamRole>(i);
        stream.transport = std::make_unique<dawstreamer::SharedAudioTransport>(stream.role);
    }

    publishSnapshot();
    startThread();
}

RecorderEngine::~RecorderEngine()
{
    signalThreadShouldExit();
    waitForThreadToExit(5000);
    closeWriters(false);
}

void RecorderEngine::startRecording() noexcept
{
    pendingCommand.store(static_cast<int>(Command::start), std::memory_order_release);
}

void RecorderEngine::stopRecording() noexcept
{
    pendingCommand.store(static_cast<int>(Command::stop), std::memory_order_release);
}

RecorderEngine::Snapshot RecorderEngine::getSnapshot() const
{
    const juce::ScopedLock lock(snapshotLock);
    return publishedSnapshot;
}

void RecorderEngine::run()
{
    while (!threadShouldExit())
    {
        const auto command = static_cast<Command>(
            pendingCommand.exchange(static_cast<int>(Command::none), std::memory_order_acq_rel));

        if (command != Command::none)
            handleCommand(command);

        bool consumedAny = false;

        // One block per stream per pass keeps the four queues approximately at the
        // same cycle and makes the common frontier useful for reconnect padding.
        for (std::size_t i = 0; i < streams.size(); ++i)
        {
            auto& stream = streams[i];
            if (stream.transport == nullptr)
                continue;

            if (sessionActiveInternal && waitingForStreamsInternal && stream.firstBlock.has_value())
                continue;

            dawstreamer::AudioBlock block;
            if (!stream.transport->pop(block))
                continue;

            consumedAny = true;

            if (!sessionActiveInternal)
                continue;

            if (waitingForStreamsInternal)
                stream.firstBlock = block;
            else
                handleAudioBlock(i, block);
        }

        if (sessionActiveInternal && waitingForStreamsInternal)
            tryStartTakeFromFirstBlocks();

        publishSnapshot();

        if (!consumedAny)
            wait(2);
    }

    if (sessionActiveInternal)
        finishTake();

    publishSnapshot();
}

void RecorderEngine::handleCommand(Command command)
{
    if (command == Command::start)
    {
        beginTake();
        return;
    }

    if (command == Command::stop)
        finishTake();
}

void RecorderEngine::beginTake()
{
    closeWriters(false);

    globalTakeFrontier = 0;
    lastError.clear();

    for (auto& stream : streams)
    {
        if (stream.transport != nullptr)
            stream.transport->discardPending();

        stream.writer.reset();
        stream.firstBlock.reset();
        stream.producerAnchorFrame = 0;
        stream.takeBaseOffset = 0;
        stream.framesWritten = 0;
        stream.gapFrames = 0;
        stream.gapEvents = 0;
        stream.fileChannels = 0;
    }

    auto root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("DAW Streamer Recordings");

    const auto rootResult = root.createDirectory();
    if (rootResult.failed())
    {
        failTake("Cannot create recording folder: " + rootResult.getErrorMessage());
        return;
    }

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
    takeDirectory = root.getChildFile(timestamp);

    for (int suffix = 2; takeDirectory.exists() && suffix < 100; ++suffix)
        takeDirectory = root.getChildFile(timestamp + "_" + juce::String(suffix));

    const auto takeResult = takeDirectory.createDirectory();
    if (takeResult.failed())
    {
        failTake("Cannot create take folder: " + takeResult.getErrorMessage());
        return;
    }

    sessionActiveInternal = true;
    waitingForStreamsInternal = true;
}

void RecorderEngine::finishTake()
{
    if (!sessionActiveInternal)
    {
        closeWriters(false);
        waitingForStreamsInternal = false;
        return;
    }

    sessionActiveInternal = false;
    waitingForStreamsInternal = false;
    closeWriters(true);
}

bool RecorderEngine::tryStartTakeFromFirstBlocks()
{
    for (const auto& stream : streams)
    {
        if (!stream.firstBlock.has_value())
            return false;
    }

    bool allHaveHostTime = true;
    std::int64_t minimumHostTime = std::numeric_limits<std::int64_t>::max();

    for (const auto& stream : streams)
    {
        const auto& block = *stream.firstBlock;

        if (block.sampleRate != kRequiredSampleRate)
        {
            failTake("Stage 4 requires all four sources at 48 kHz. Resampling is disabled.");
            return false;
        }

        if (block.numChannels == 0 || block.numChannels > dawstreamer::kMaxChannels)
        {
            failTake("Unsupported channel count on one of the Stage 4 streams.");
            return false;
        }

        if (block.hostTimeValid == 0)
            allHaveHostTime = false;
        else
            minimumHostTime = std::min(minimumHostTime, block.hostTimeInSamples);
    }

    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        auto& stream = streams[i];
        const auto& block = *stream.firstBlock;
        stream.producerAnchorFrame = block.producerFrameStart;

        if (allHaveHostTime && block.hostTimeInSamples >= minimumHostTime)
            stream.takeBaseOffset = static_cast<std::uint64_t>(block.hostTimeInSamples - minimumHostTime);
        else
            stream.takeBaseOffset = 0;

        if (!openWriter(i, block))
            return false;
    }

    waitingForStreamsInternal = false;

    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        const auto block = *streams[i].firstBlock;
        streams[i].firstBlock.reset();
        handleAudioBlock(i, block);

        if (!sessionActiveInternal)
            return false;
    }

    return true;
}

bool RecorderEngine::openWriter(std::size_t streamIndex, const dawstreamer::AudioBlock& firstBlock)
{
    auto& stream = streams[streamIndex];
    auto file = takeDirectory.getChildFile(juce::String(dawstreamer::streamRoleName(stream.role)) + ".wav");
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> fileStream { file.createOutputStream() };
    if (fileStream == nullptr)
    {
        failTake("Cannot create " + file.getFullPathName());
        return false;
    }

    juce::WavAudioFormat wavFormat;
    using Options = juce::AudioFormatWriterOptions;
    auto newWriter = wavFormat.createWriterFor(
        fileStream,
        Options {}
            .withSampleRate(static_cast<double>(firstBlock.sampleRate))
            .withNumChannels(static_cast<int>(firstBlock.numChannels))
            .withBitsPerSample(kRequiredBitsPerSample));

    if (newWriter == nullptr)
    {
        failTake("JUCE could not create a 24-bit WAV writer for "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return false;
    }

    stream.writer = std::move(newWriter);
    stream.fileChannels = firstBlock.numChannels;
    return true;
}

void RecorderEngine::handleAudioBlock(std::size_t streamIndex, const dawstreamer::AudioBlock& block)
{
    auto& stream = streams[streamIndex];
    if (stream.writer == nullptr)
        return;

    if (block.sampleRate != kRequiredSampleRate || block.numChannels != stream.fileChannels)
    {
        failTake("Source format changed during the Stage 4 take: "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return;
    }

    if (block.producerFrameStart < stream.producerAnchorFrame)
        return;

    const auto producerRelative = block.producerFrameStart - stream.producerAnchorFrame;
    auto desiredStart = stream.takeBaseOffset + producerRelative;

    // When a sender is bypassed its processBlock stops, so its local producer counter
    // also stops. Other streams keep advancing. On reconnect, the common frontier
    // restores the missing wall-time as silence instead of compressing the take.
    const auto syncFloor = globalTakeFrontier > block.numFrames
        ? globalTakeFrontier - block.numFrames
        : std::uint64_t { 0 };
    desiredStart = std::max(desiredStart, syncFloor);

    if (desiredStart > stream.framesWritten)
    {
        const auto missingFrames = desiredStart - stream.framesWritten;
        if (!writeSilence(stream, missingFrames))
        {
            failTake("Failed while inserting timeline silence for "
                     + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
            return;
        }

        stream.gapFrames += missingFrames;
        ++stream.gapEvents;
    }

    std::uint64_t overlap = 0;
    if (stream.framesWritten > desiredStart)
        overlap = stream.framesWritten - desiredStart;

    if (overlap >= block.numFrames)
        return;

    const auto framesToWrite = static_cast<std::uint32_t>(block.numFrames - overlap);
    const auto sampleOffset = static_cast<std::uint32_t>(overlap);

    const float* channels[dawstreamer::kMaxChannels] {};
    for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
        channels[channel] = block.samples[channel].data() + sampleOffset;

    if (!stream.writer->writeFromFloatArrays(channels,
                                              static_cast<int>(block.numChannels),
                                              static_cast<int>(framesToWrite)))
    {
        failTake("Failed while writing WAV audio for "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return;
    }

    stream.framesWritten += framesToWrite;
    globalTakeFrontier = std::max(globalTakeFrontier, stream.framesWritten);
}

bool RecorderEngine::writeSilence(StreamState& stream, std::uint64_t frames)
{
    if (stream.writer == nullptr || stream.fileChannels == 0)
        return false;

    const float* zeroChannels[dawstreamer::kMaxChannels] {
        silenceBuffer.data(), silenceBuffer.data()
    };

    while (frames > 0)
    {
        const auto chunk = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(frames, dawstreamer::kMaxFramesPerBlock));

        if (!stream.writer->writeFromFloatArrays(zeroChannels,
                                                  static_cast<int>(stream.fileChannels),
                                                  static_cast<int>(chunk)))
            return false;

        stream.framesWritten += chunk;
        frames -= chunk;
    }

    return true;
}

void RecorderEngine::closeWriters(bool padToCommonEnd)
{
    if (padToCommonEnd)
    {
        std::uint64_t finalFrames = globalTakeFrontier;
        for (const auto& stream : streams)
            finalFrames = std::max(finalFrames, stream.framesWritten);

        for (auto& stream : streams)
        {
            if (stream.writer != nullptr && stream.framesWritten < finalFrames)
            {
                const auto padding = finalFrames - stream.framesWritten;
                if (writeSilence(stream, padding))
                {
                    stream.gapFrames += padding;
                    if (padding > 0)
                        ++stream.gapEvents;
                }
            }
        }

        globalTakeFrontier = finalFrames;
    }

    for (auto& stream : streams)
        stream.writer.reset();
}

void RecorderEngine::failTake(const juce::String& message)
{
    lastError = message;
    sessionActiveInternal = false;
    waitingForStreamsInternal = false;
    closeWriters(false);
}

void RecorderEngine::publishSnapshot()
{
    Snapshot result;
    result.sessionActive = sessionActiveInternal;
    result.waitingForStreams = waitingForStreamsInternal;
    result.takeFrames = globalTakeFrontier;
    result.takeDirectory = takeDirectory.getFullPathName();
    result.lastError = lastError;

    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        const auto& state = streams[i];
        auto& out = result.streams[i];
        out.role = state.role;
        out.writerOpen = state.writer != nullptr;
        out.framesWritten = state.framesWritten;
        out.gapFrames = state.gapFrames;
        out.gapEvents = state.gapEvents;

        if (state.transport != nullptr)
        {
            out.transportOpen = state.transport->isOpen();
            out.producerPresent = state.transport->producerOwner() != 0;
            out.producerCallbacks = state.transport->producerCallbacks();
            out.pendingBlocks = state.transport->pendingBlocks();
            out.droppedBlocks = state.transport->droppedBlocks();
            out.oversizedBlocks = state.transport->oversizedBlocks();
            out.duplicateClaims = state.transport->duplicateClaims();
            out.sourceSampleRate = state.transport->lastSampleRate();
            out.sourceChannels = state.transport->lastNumChannels();
            out.sourceBlockFrames = state.transport->lastNumFrames();
        }
    }

    const juce::ScopedLock lock(snapshotLock);
    publishedSnapshot = std::move(result);
}
