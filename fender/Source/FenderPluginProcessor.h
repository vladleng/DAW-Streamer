#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "RecorderEngine.h"
#include "SharedAudioTransport.h"
#include "SharedMidiTransport.h"
#include "SharedRecorderControl.h"
#include "SharedStreamMetadata.h"

class DAWStreamerFenderProcessor final : public juce::AudioProcessor,
                                         private juce::AudioProcessorParameter::Listener,
                                         private juce::Timer
{
public:
    enum class PluginMode : int
    {
        sender = 0,
        masterRecorder = 1
    };

    enum class MidiMessageType : int
    {
        none = 0,
        noteOn,
        noteOff,
        controller,
        pitchWheel,
        channelPressure,
        polyAftertouch,
        programChange,
        other
    };

    struct DiagnosticsSnapshot
    {
        PluginMode mode = PluginMode::sender;
        std::uint64_t processBlockCount = 0;
        bool playHeadAvailable = false;
        bool positionAvailable = false;
        bool isPlaying = false;
        bool hasTimeInSamples = false;
        std::int64_t timeInSamples = 0;
        bool hasPpqPosition = false;
        double ppqPosition = 0.0;
        bool hasBpm = false;
        double bpm = 0.0;
        bool hasTimeSignature = false;
        int timeSignatureNumerator = 0;
        int timeSignatureDenominator = 0;
        double sampleRate = 0.0;
        int expectedBlockSize = 0;
        int lastNumSamples = 0;
        int inputChannels = 0;
        int outputChannels = 0;

        bool midiInputSeen = false;
        std::uint64_t midiEventCount = 0;
        int midiEventsLastBlock = 0;
        int lastMidiSampleOffset = -1;
        MidiMessageType lastMidiMessageType = MidiMessageType::none;
        int lastMidiChannel = 0;
        int lastMidiData1 = 0;
        int lastMidiData2 = 0;
        bool midiTransportOpen = false;
        bool midiRoleClaimed = false;
        std::uint64_t midiPendingEvents = 0;
        std::uint64_t midiDroppedEvents = 0;
        std::uint64_t midiOversizedEvents = 0;

        dawstreamer::StreamRole streamRole = dawstreamer::StreamRole::Vocal;
        bool transportOpen = false;
        bool roleClaimed = false;
        std::uint64_t transportPendingBlocks = 0;
        std::uint64_t transportDroppedBlocks = 0;
        std::uint64_t transportOversizedBlocks = 0;
        std::uint64_t duplicateRoleClaims = 0;

        bool recorderControlOpen = false;
        dawstreamer::RecorderState recorderState = dawstreamer::RecorderState::offline;
        std::uint64_t recorderHeartbeat = 0;
        std::uint64_t recorderTakeFrames = 0;
        bool recordingParameterOn = false;
        bool recordingControlOnline = false;
        bool recordingControlPending = false;
        bool embeddedRecorderPresent = false;
        bool embeddedRecorderOwner = false;
    };

    DAWStreamerFenderProcessor();
    ~DAWStreamerFenderProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool setPluginMode(PluginMode mode);
    PluginMode getPluginMode() const noexcept;
    void setStreamRole(dawstreamer::StreamRole role) noexcept;
    dawstreamer::StreamRole getStreamRole() const noexcept;

    void setSenderName(juce::String name);
    juce::String getSenderName() const;

    void requestRecord() noexcept;
    void requestStop() noexcept;

    void setMasterOutputRoot(juce::File directory);
    juce::File getMasterOutputRoot() const;
    void setMasterSessionName(juce::String name);
    juce::String getMasterSessionName() const;
    bool hasEmbeddedRecorder() const noexcept;
    RecorderEngine::Snapshot getEmbeddedRecorderSnapshot() const;

    juce::String getPublishedNameForRole(dawstreamer::StreamRole role) const;
    DiagnosticsSnapshot getDiagnosticsSnapshot() const noexcept;

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void timerCallback() override;

    void syncRecordingParameterFromRecorder(bool recording);
    void claimSelectedSenderRole() noexcept;
    void releaseSenderClaims() noexcept;
    void publishSenderMetadata() noexcept;
    void captureTakeStreamNames();
    void renameFinishedTakeFiles(const RecorderEngine::Snapshot& snapshot);
    static juce::String sanitiseStreamFileBaseName(juce::String name, dawstreamer::StreamRole fallbackRole);
    dawstreamer::SharedAudioTransport* transportForRole(dawstreamer::StreamRole role) const noexcept;
    dawstreamer::SharedMidiTransport* midiTransportForRole(dawstreamer::StreamRole role) const noexcept;
    dawstreamer::SharedStreamMetadata* metadataForRole(dawstreamer::StreamRole role) const noexcept;

    std::array<std::unique_ptr<dawstreamer::SharedAudioTransport>, dawstreamer::kStreamRoleCount> audioTransports;
    std::array<std::unique_ptr<dawstreamer::SharedMidiTransport>, dawstreamer::kStreamRoleCount> midiTransports;
    std::array<std::unique_ptr<dawstreamer::SharedStreamMetadata>, dawstreamer::kStreamRoleCount> streamMetadata;
    dawstreamer::SharedRecorderControl recorderControl;
    juce::AudioParameterBool* recordingParameter = nullptr;

    std::atomic<int> pluginMode { static_cast<int>(PluginMode::sender) };
    std::atomic<int> currentRole { static_cast<int>(dawstreamer::StreamRole::Vocal) };
    std::uint64_t ownerToken = 0;
    std::uint64_t producerFrameCounter = 0;

    mutable juce::CriticalSection senderNameLock;
    juce::String configuredSenderName;

    mutable juce::CriticalSection masterConfigLock;
    juce::File configuredMasterOutputRoot;
    juce::String configuredMasterSessionName { "Show" };
    std::unique_ptr<RecorderEngine> embeddedRecorder;
    bool previousEmbeddedSessionActive = false;
    std::array<juce::String, dawstreamer::kStreamRoleCount> takeStreamFileBaseNames {};

    std::atomic<bool> suppressRecordingParameterCommand { false };
    std::atomic<bool> pendingRecordingCommand { false };
    std::atomic<bool> pendingDesiredRecording { false };
    std::atomic<bool> recorderOnlineForControl { false };
    std::uint64_t previousRecorderHeartbeat = 0;
    double lastRecorderHeartbeatChangeMs = 0.0;

    std::atomic<std::uint64_t> processBlockCount { 0 };
    std::atomic<bool> playHeadAvailable { false };
    std::atomic<bool> positionAvailable { false };
    std::atomic<bool> hostIsPlaying { false };
    std::atomic<bool> hasTimeInSamples { false };
    std::atomic<std::int64_t> hostTimeInSamples { 0 };
    std::atomic<bool> hasPpqPosition { false };
    std::atomic<double> hostPpqPosition { 0.0 };
    std::atomic<bool> hasBpm { false };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hasTimeSignature { false };
    std::atomic<int> hostTimeSignatureNumerator { 0 };
    std::atomic<int> hostTimeSignatureDenominator { 0 };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentExpectedBlockSize { 0 };
    std::atomic<int> currentNumSamples { 0 };
    std::atomic<int> currentInputChannels { 0 };
    std::atomic<int> currentOutputChannels { 0 };

    std::atomic<bool> midiInputSeen { false };
    std::atomic<std::uint64_t> midiEventCount { 0 };
    std::atomic<int> midiEventsLastBlock { 0 };
    std::atomic<int> lastMidiSampleOffset { -1 };
    std::atomic<int> lastMidiMessageType { static_cast<int>(MidiMessageType::none) };
    std::atomic<int> lastMidiChannel { 0 };
    std::atomic<int> lastMidiData1 { 0 };
    std::atomic<int> lastMidiData2 { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWStreamerFenderProcessor)
};
