cmake_minimum_required(VERSION 3.22)
project(BassmentVST VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BassmentVST
    COMPANY_NAME "Sakshham"
    PLUGIN_MANUFACTURER_CODE Sksh
    PLUGIN_CODE Bsmt
    FORMATS VST3 Standalone
    PRODUCT_NAME "Bassment")

target_sources(BassmentVST PRIVATE src/BassmentProcessor.cpp src/ui/BassmentEditor.cpp)

target_compile_definitions(BassmentVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BassmentVST
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP checks. No JUCE needed, so they build in seconds.
option(BASSMENT_BUILD_TESTS "Build the offline DSP tests" OFF)
if(BASSMENT_BUILD_TESTS)
    enable_testing()
    foreach(name pitch vocoder dynamics)
        add_executable(${name}_test tests/${name}_test.cpp)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
endif()
