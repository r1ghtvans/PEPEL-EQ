cmake_minimum_required(VERSION 3.22)

# Статическая среда выполнения MSVC: плагин работает без установки VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(PepelEQ VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Шрифты (OFL) вшиваются в бинарник; лицензии лежат рядом в Assets/
juce_add_binary_data(PepelAssets SOURCES
    Assets/VT323-Regular.ttf
    Assets/PressStart2P-Regular.ttf)

juce_add_plugin(PepelEQ
    COMPANY_NAME "Pepel"
    PLUGIN_MANUFACTURER_CODE Pepl
    PLUGIN_CODE PpEQ
    PRODUCT_NAME "Pepel EQ"
    FORMATS VST3
    VST3_CATEGORIES Fx EQ
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(PepelEQ)

target_sources(PepelEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PepelEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_options(PepelEQ PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(PepelEQ
    PRIVATE
        PepelAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
