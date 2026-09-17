#include "RecorderEngine.h"

#include <algorithm>

namespace
{
constexpr std::uint32_t kRequiredSampleRate = 48000;
constexpr int kRequiredBitsPerSample = 24;
}

RecorderEngine::RecorderEngine(juce::File outputRootToUse)
    : juce::Thread("DAW Streamer Recorder multistream"),
      outputRootOverride(std::move(outputRootToUse))
{
    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        auto& stream = streams[i];
        stream.role = static_cast<dawstreamer::StreamRole>(i);
        stream.transport = std::make_unique<dawstreamer::SharedAudioTransport>(stream.role);
    }

    lastSharedCommandWord = recorderControl.snapshot().commandWord;
    publishSnapshot();
    startThread();
}

RecorderEngine::~RecorderEngine()
{
    signalThreadShouldExit();
    waitForThreadToExit(5000);
    closeWriters(false);
    recorderControl.publishOffline();
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

        handleSharedControlCommand();

        bool consumedAny = false;

        // Stage 5A1: never stop draining an early stream while waiting for another
        // sender. Every available stream is written immediately; a stream that
        // appears later is aligned by host sample-time and leading silence.
        for (std::size_t i = 0; i < streams.size(); ++i)
        {
            auto& stream = streams[i];
            if (stream.transport == nullptr)
                continue;

            dawstreamer::AudioBlock block;
            if (!stream.transport->pop(block))
                continue;

            consumedAny = true;

            if (!sessionActiveInternal)
                continue;

            handleAudioBlock(i, block);
            if (!sessionActiveInternal)
                break;
        }

        if (sessionActiveInternal)
            waitingForStreamsInternal = !allStreamsStarted();

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
        if (!sessionActiveInternal)
            beginTake();
        return;
    }

    if (command == Command::stop && sessionActiveInternal)
        finishTake();
}

void RecorderEngine::handleSharedControlCommand()
{
    std::uint64_t currentWord = lastSharedCommandWord;
    dawstreamer::RecorderCommand command = dawstreamer::RecorderCommand::none;

    if (!recorderControl.readCommandAfter(lastSharedCommandWord, currentWord, command))
        return;

    lastSharedCommandWord = currentWord;

    if (command == dawstreamer::RecorderCommand::record)
    {
        if (!sessionActiveInternal)
            beginTake();
        return;
    }

    if (command == dawstreamer::RecorderCommand::stop && sessionActiveInternal)
        finishTake();
}

dawstreamer::RecorderState RecorderEngine::currentSharedState() const noexcept
{
    if (!lastError.isEmpty())
        return dawstreamer::RecorderState::error;
    if (sessionActiveInternal && waitingForStreamsInternal)
        return dawstreamer::RecorderState::waitingForStreams;
    if (sessionActiveInternal)
        return dawstreamer::RecorderState::recording;
    return dawstreamer::RecorderState::idle;
}

void RecorderEngine::beginTake()
{
    closeWriters(false);

    globalTakeFrontier = 0;
    takeOriginEstablished = false;
    takeHostOriginValid = false;
    takeHostOriginSamples = 0;
    lastError.clear();

    for (auto& stream : streams)
    {
        if (stream.transport != nullptr)
        {
            stream.transport->discardPending();
            stream.droppedBaseline = stream.transport->droppedBlocks();
            stream.oversizedBaseline = stream.transport->oversizedBlocks();
            stream.counterBaselineValid = true;
        }
        else
        {
            stream.counterBaselineValid = false;
        }

        stream.writer.reset();
        stream.producerAnchorFrame = 0;
        stream.takeBaseOffset = 0;
        stream.framesWritten = 0;
        stream.gapFrames = 0;
        stream.gapEvents = 0;
        stream.fileChannels = 0;
    }

    auto root = outputRootOverride.getFullPathName().isNotEmpty()
        ? outputRootOverride
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
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

bool RecorderEngine::startStreamFromFirstBlock(std::size_t streamIndex,
                                                const dawstreamer::AudioBlock& firstBlock)
{
    auto& stream = streams[streamIndex];
    if (stream.writer != nullptr)
        return true;

    if (firstBlock.sampleRate != kRequiredSampleRate)
    {
        failTake("Stage 5A1 requires all sources at 48 kHz. Resampling is disabled.");
        return false;
    }

    if (firstBlock.numChannels == 0 || firstBlock.numChannels > dawstreamer::kMaxChannels)
    {
        failTake("Unsupported channel count on stream "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return false;
    }

    stream.producerAnchorFrame = firstBlock.producerFrameStart;

    if (!takeOriginEstablished)
    {
        takeOriginEstablished = true;
        takeHostOriginValid = firstBlock.hostTimeValid != 0;
        takeHostOriginSamples = firstBlock.hostTimeInSamples;
        stream.takeBaseOffset = 0;
    }
    else if (takeHostOriginValid && firstBlock.hostTimeValid != 0)
    {
        if (firstBlock.hostTimeInSamples >= takeHostOriginSamples)
        {
            stream.takeBaseOffset = static_cast<std::uint64_t>(
                firstBlock.hostTimeInSamples - takeHostOriginSamples);
        }
        else
        {
            // A later-observed block can occasionally carry the same/earlier host
            // boundary because plugin callbacks are scheduled independently. We
            // cannot move already-written audio earlier, so clamp it to take zero.
            stream.takeBaseOffset = 0;
        }
    }
    else
    {
        // Host timing is the preferred common clock. If one sender cannot expose it,
        // join that stream at the current recorder frontier rather than moving its
        // first audio to the beginning of the take.
        stream.takeBaseOffset = globalTakeFrontier;
    }

    if (!openWriter(streamIndex, firstBlock))
        return false;

    waitingForStreamsInternal = !allStreamsStarted();
    return true;
}

bool RecorderEngine::allStreamsStarted() const noexcept
{
    return std::all_of(streams.begin(), streams.end(), [](const auto& stream)
    {
        return stream.writer != nullptr;
    });
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

    if (stream.writer == nullptr && !startStreamFromFirstBlock(streamIndex, block))
        return;

    if (stream.writer == nullptr)
        return;

    if (block.sampleRate != kRequiredSampleRate || block.numChannels != stream.fileChannels)
    {
        failTake("Source format changed during the take: "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return;
    }

    if (block.producerFrameStart < stream.producerAnchorFrame)
        return;

    const auto producerRelative = block.producerFrameStart - stream.producerAnchorFrame;
    auto desiredStart = stream.takeBaseOffset + producerRelative;

    // If a sender disappears after it has started, the other streams advance the
    // common frontier. On reconnect this floor preserves wall-time as silence.
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

            const auto droppedNow = state.transport->droppedBlocks();
            const auto oversizedNow = state.transport->oversizedBlocks();
            out.droppedBlocks = state.counterBaselineValid && droppedNow >= state.droppedBaseline
                ? droppedNow - state.droppedBaseline
                : droppedNow;
            out.oversizedBlocks = state.counterBaselineValid && oversizedNow >= state.oversizedBaseline
                ? oversizedNow - state.oversizedBaseline
                : oversizedNow;

            out.duplicateClaims = state.transport->duplicateClaims();
            out.sourceSampleRate = state.transport->lastSampleRate();
            out.sourceChannels = state.transport->lastNumChannels();
            out.sourceBlockFrames = state.transport->lastNumFrames();
        }
    }

    recorderControl.publishState(currentSharedState(), globalTakeFrontier);

    const juce::ScopedLock lock(snapshotLock);
    publishedSnapshot = std::move(result);
}
