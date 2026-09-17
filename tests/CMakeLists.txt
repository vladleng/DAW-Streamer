add_executable(DAWStreamerSharedTests
    SharedAudioTransportTests.cpp
)

target_link_libraries(DAWStreamerSharedTests
    PRIVATE
        DAWStreamerShared
)

target_compile_features(DAWStreamerSharedTests PRIVATE cxx_std_20)

add_test(NAME shared-audio-transport COMMAND DAWStreamerSharedTests)

add_executable(DAWStreamerRecorderControlTests
    SharedRecorderControlTests.cpp
)

target_link_libraries(DAWStreamerRecorderControlTests
    PRIVATE
        DAWStreamerShared
)

target_compile_features(DAWStreamerRecorderControlTests PRIVATE cxx_std_20)

add_test(NAME shared-recorder-control COMMAND DAWStreamerRecorderControlTests)

add_executable(DAWStreamerRecorderStartupTests
    RecorderStartupTests.cpp
    ../recorder/Source/RecorderEngine.cpp
    ../recorder/Source/RecorderEngine.h
)

target_include_directories(DAWStreamerRecorderStartupTests
    PRIVATE
        ../recorder/Source
)

target_link_libraries(DAWStreamerRecorderStartupTests
    PRIVATE
        DAWStreamerShared
        juce::juce_audio_formats
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(DAWStreamerRecorderStartupTests PRIVATE cxx_std_20)

add_test(NAME recorder-stage5a1-startup COMMAND DAWStreamerRecorderStartupTests)
