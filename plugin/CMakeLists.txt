juce_add_plugin(DAWStreamerPlugin
    COMPANY_NAME "Moon River Studio"
    BUNDLE_ID com.moonriverstudio.dawstreamer
    PLUGIN_MANUFACTURER_CODE MNRV
    PLUGIN_CODE DSTR
    FORMATS VST3
    PRODUCT_NAME "DAW Streamer"
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DAWStreamerPlugin
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
)

target_compile_definitions(DAWStreamerPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(DAWStreamerPlugin
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
