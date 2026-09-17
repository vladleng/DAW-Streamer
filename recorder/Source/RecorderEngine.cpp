#include "RecorderEngine.h"

namespace
{
constexpr std::uint32_t kRequiredSampleRate = 48000;
constexpr int kRequiredBitsPerSample = 24;
}

RecorderEngine::RecorderEngine()
    : juce::Thread("DAW Streamer Recorder transport")
{
    startThread();
}

RecorderEngine::~RecorderEngine()
{
    signalThreadShouldExit();
    waitForThreadToExit(5000);
    closeWriter();
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
    Snapshot result;
    result.transportOpen = transport.isOpen();
    result.sessionActive = sessionActive.load(std::memory_order_relaxed);
    result.writerOpen = writerOpen.load(std::memory_order_relaxed);
    result.producerCallbacks = transport.producerCallbacks();
    result.pendingBlocks = transport.pendingBlocks();
    result.droppedBlocks = transport.droppedBlocks();
    result.oversizedBlocks = transport.oversizedBlocks();
    result.framesWritten = framesWritten.load(std::memory_order_relaxed);
    result.sourceSampleRate = transport.lastSampleRate();
    result.sourceChannels = transport.lastNumChannels();
    result.sourceBlockFrames = transport.lastNumFrames();
    result.fileSampleRate = fileSampleRate.load(std::memory_order_relaxed);
    result.fileChannels = fileChannels.load(std::memory_order_relaxed);

    const juce::ScopedLock lock(textLock);
    result.lastFilePath = lastFilePath;
    result.lastError = lastError;
    return result;
}

void RecorderEngine::run()
{
    dawstreamer::AudioBlock block;

    while (!threadShouldExit())
    {
        const auto command = static_cast<Command>(
            pendingCommand.exchange(static_cast<int>(Command::none), std::memory_order_acq_rel));

        if (command != Command::none)
            handleCommand(command);

        bool consumedAnyBlock = false;

        // Bound each drain pass so GUI Record/Stop commands cannot be starved even
        // if the producer is continuously active.
        for (int i = 0; i < 256 && !threadShouldExit(); ++i)
        {
            if (!transport.pop(block))
                break;

            consumedAnyBlock = true;

            if (sessionActive.load(std::memory_order_relaxed))
                handleAudioBlock(block);
        }

        if (!consumedAnyBlock)
            wait(2);
    }

    sessionActive.store(false, std::memory_order_relaxed);
    closeWriter();
}

void RecorderEngine::handleCommand(Command command)
{
    if (command == Command::start)
    {
        closeWriter();
        transport.discardPending();
        framesWritten.store(0, std::memory_order_relaxed);
        fileSampleRate.store(0, std::memory_order_relaxed);
        fileChannels.store(0, std::memory_order_relaxed);

        {
            const juce::ScopedLock lock(textLock);
            lastFilePath.clear();
            lastError.clear();
        }

        sessionActive.store(true, std::memory_order_release);
        return;
    }

    if (command == Command::stop)
    {
        sessionActive.store(false, std::memory_order_release);
        closeWriter();
    }
}

void RecorderEngine::handleAudioBlock(const dawstreamer::AudioBlock& block)
{
    if (writer == nullptr && !openWriterForBlock(block))
        return;

    if (block.sampleRate != fileSampleRate.load(std::memory_order_relaxed)
        || block.numChannels != fileChannels.load(std::memory_order_relaxed))
    {
        setError("Source format changed during the Stage 3 recording session.");
        return;
    }

    const float* channels[dawstreamer::kMaxChannels] {};
    for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
        channels[channel] = block.samples[channel].data();

    if (!writer->writeFromFloatArrays(channels,
                                      static_cast<int>(block.numChannels),
                                      static_cast<int>(block.numFrames)))
    {
        setError("Failed while writing WAV audio data.");
        return;
    }

    framesWritten.fetch_add(block.numFrames, std::memory_order_relaxed);
}

bool RecorderEngine::openWriterForBlock(const dawstreamer::AudioBlock& block)
{
    if (block.sampleRate != kRequiredSampleRate)
    {
        setError("Stage 3 requires a 48 kHz source. Resampling is intentionally disabled.");
        return false;
    }

    if (block.numChannels == 0 || block.numChannels > dawstreamer::kMaxChannels)
    {
        setError("Unsupported source channel count.");
        return false;
    }

    auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("DAW Streamer Recordings");

    const auto directoryResult = directory.createDirectory();
    if (directoryResult.failed())
    {
        setError("Cannot create recording folder: " + directoryResult.getErrorMessage());
        return false;
    }

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
    auto file = directory.getChildFile("Stage3_" + timestamp + ".wav");
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> fileStream { file.createOutputStream() };
    if (fileStream == nullptr)
    {
        setError("Cannot create WAV file in " + directory.getFullPathName());
        return false;
    }

    juce::WavAudioFormat wavFormat;
    using Options = juce::AudioFormatWriterOptions;

    auto newWriter = wavFormat.createWriterFor(
        fileStream,
        Options {}
            .withSampleRate(static_cast<double>(block.sampleRate))
            .withNumChannels(static_cast<int>(block.numChannels))
            .withBitsPerSample(kRequiredBitsPerSample));

    if (newWriter == nullptr)
    {
        setError("JUCE could not create a 24-bit WAV writer.");
        return false;
    }

    writer = std::move(newWriter);
    fileSampleRate.store(block.sampleRate, std::memory_order_relaxed);
    fileChannels.store(block.numChannels, std::memory_order_relaxed);
    writerOpen.store(true, std::memory_order_release);

    {
        const juce::ScopedLock lock(textLock);
        lastFilePath = file.getFullPathName();
    }

    return true;
}

void RecorderEngine::closeWriter()
{
    writer.reset();
    writerOpen.store(false, std::memory_order_release);
}

void RecorderEngine::setError(const juce::String& message)
{
    {
        const juce::ScopedLock lock(textLock);
        lastError = message;
    }

    sessionActive.store(false, std::memory_order_release);
    closeWriter();
}
