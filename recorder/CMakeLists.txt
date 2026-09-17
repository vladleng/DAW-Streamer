juce_add_gui_app(DAWStreamerRecorder
    COMPANY_NAME "Moon River Studio"
    BUNDLE_ID com.moonriverstudio.dawstreamerrecorder
    PRODUCT_NAME "DAW Streamer Recorder"
)

target_sources(DAWStreamerRecorder
    PRIVATE
        Source/Main.cpp
        Source/RecorderEngine.cpp
        Source/RecorderEngine.h
)

target_compile_definitions(DAWStreamerRecorder
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(DAWStreamerRecorder
    PRIVATE
        DAWStreamerShared
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
