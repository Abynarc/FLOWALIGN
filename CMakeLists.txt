cmake_minimum_required(VERSION 3.22)
project(FLOWALIGN VERSION 0.3.2 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED JUCE_PATH)
  set(JUCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../JUCE")
endif()
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found. Pass -DJUCE_PATH=/absolute/path/to/JUCE")
endif()
add_subdirectory("${JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
juce_add_plugin(FLOWALIGN
    COMPANY_NAME "FLOWMINNERS"
    BUNDLE_ID "com.flowminners.flowalign"
    PLUGIN_MANUFACTURER_CODE Flmn
    PLUGIN_CODE Flal
    FORMATS AU
    PRODUCT_NAME "FLOWALIGN"
    PLUGIN_NAME "FLOWALIGN"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(FLOWALIGN)
target_sources(FLOWALIGN PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AlignmentEngine.cpp Source/SystemPreview_mac.mm Source/NativeRegionDrop_mac.mm)
target_compile_definitions(FLOWALIGN PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(FLOWALIGN PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
find_library(FLOWALIGN_AVFOUNDATION AVFoundation REQUIRED)
target_link_libraries(FLOWALIGN PRIVATE ${FLOWALIGN_AVFOUNDATION})
