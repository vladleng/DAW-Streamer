juce_add_plugin(DAWStreamerFenderPlugin
    COMPANY_NAME "Moon River Studio"
    BUNDLE_ID com.moonriverstudio.dawstreamer
    PLUGIN_MANUFACTURER_CODE MNRV
    PLUGIN_CODE DSFE
    FORMATS VST3
    PRODUCT_NAME "DAW Streamer"
    VERSION 0.1.0
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DAWStreamerFenderPlugin
    PRIVATE
        Source/FenderPluginProcessor.cpp
        Source/FenderPluginProcessor.h
        Source/FenderPluginEditor.cpp
        Source/FenderPluginEditor.h
        ../recorder/Source/MidiFileExporter.cpp
        ../recorder/Source/MidiFileExporter.h
        ../recorder/Source/RecorderEngine.cpp
        ../recorder/Source/RecorderEngine.h
)

target_include_directories(DAWStreamerFenderPlugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../recorder/Source
)

target_compile_definitions(DAWStreamerFenderPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        DAW_STREAMER_FENDER_BUILD=1
)

target_link_libraries(DAWStreamerFenderPlugin
    PRIVATE
        DAWStreamerShared
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
