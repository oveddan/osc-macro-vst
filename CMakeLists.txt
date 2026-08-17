cmake_minimum_required(VERSION 3.22)
project(OSCMacro VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(OSCMacro
  PLUGIN_MANUFACTURER_CODE Dovd
  PLUGIN_CODE Cmac
  FORMATS VST3
  PRODUCT_NAME "OSCMacro"
  COMPANY_NAME "oveddan"
  # OSCMacro is an instrument, not an effect - matching OSCpar, which declares
  # the VST3 category "Instrument|Synth" and a MIDI input. This is not cosmetic:
  # Bitwig's per-device smart-suspend sleeps an *effect* whose input is silent,
  # which stops processBlock() and therefore stops VST3 parameter delivery, so
  # modulation freezes and OSC stops until something wakes the device (opening
  # its plugin window, for instance). An instrument has no audio input to be
  # silent and is never suspended this way. It also matches the device slot
  # OSCpar occupied in migrated projects.
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(OSCMacro PRIVATE
  src/MappingConfig.cpp
  src/PluginProcessor.cpp)

target_compile_definitions(OSCMacro PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OSCMacro PRIVATE
  juce::juce_audio_utils
  juce::juce_osc
  juce::juce_cryptography
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

include(CTest)

if(BUILD_TESTING)
  add_executable(OSCMacroTests
    tests/MappingConfigTests.cpp
    src/MappingConfig.cpp)
  target_include_directories(OSCMacroTests PRIVATE src)
  target_link_libraries(OSCMacroTests PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
  add_test(NAME mapping-config COMMAND OSCMacroTests)

  # Regression guard for the OSCpar-compatible VST3 parameter IDs in
  # src/ParamIds.h - see that file and tests/VST3ParamIdTests.cpp for why.
  add_executable(OSCMacroVST3ParamIdTests
    tests/VST3ParamIdTests.cpp
    src/MappingConfig.cpp)
  target_include_directories(OSCMacroVST3ParamIdTests PRIVATE src)
  target_link_libraries(OSCMacroVST3ParamIdTests PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
  add_test(NAME vst3-param-ids COMMAND OSCMacroVST3ParamIdTests)

  # OSCpar -> OSCMacro state-chunk adoption (see MappingConfig.h's
  # "OSCpar migration (adoption)" section and README.md's "Migration from
  # OSCpar").
  add_executable(OSCMacroOscParAdoptionTests
    tests/OscParAdoptionTests.cpp
    src/MappingConfig.cpp)
  target_include_directories(OSCMacroOscParAdoptionTests PRIVATE src)
  target_link_libraries(OSCMacroOscParAdoptionTests PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
  add_test(NAME oscpar-adoption COMMAND OSCMacroOscParAdoptionTests)
endif()
