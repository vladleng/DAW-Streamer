add_executable(DAWStreamerSharedTests
    SharedAudioTransportTests.cpp
)

target_link_libraries(DAWStreamerSharedTests
    PRIVATE
        DAWStreamerShared
)

target_compile_features(DAWStreamerSharedTests PRIVATE cxx_std_20)

add_test(NAME shared-audio-transport COMMAND DAWStreamerSharedTests)
