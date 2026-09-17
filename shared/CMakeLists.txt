if(NOT WIN32)
    message(FATAL_ERROR "DAW Streamer Stage 3 shared-memory transport currently targets Windows only")
endif()

add_library(DAWStreamerShared STATIC
    Source/SharedAudioTransport.cpp
    Source/SharedAudioTransport.h
)

target_include_directories(DAWStreamerShared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_features(DAWStreamerShared PUBLIC cxx_std_20)

target_compile_definitions(DAWStreamerShared
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(DAWStreamerShared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
