cmake_minimum_required(VERSION 3.22)
project(ReverseVocalReverb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # sin dependencias de runtime VC++
endif()

option(RVR_BUILD_PLUGIN     "Compilar el plugin JUCE (VST3)" ON)
option(RVR_BUILD_TESTS      "Compilar las pruebas del núcleo DSP" ON)
option(RVR_BUILD_STANDALONE "Compilar también la aplicación Standalone" OFF)
set(JUCE_PATH "" CACHE PATH "Ruta a una copia local de JUCE (si está vacía se descarga JUCE 8.0.8)")
set(RVR_COMPANY "byDaza" CACHE STRING "Nombre de la empresa/autor en los metadatos del plugin")

# ---------------------------------------------------------------- núcleo DSP (sin JUCE)
add_library(rvr_core STATIC
    Source/ReverbEngine.cpp Source/ReverseProcessor.cpp Source/VoiceDetector.cpp Source/EffectCore.cpp)
target_include_directories(rvr_core PUBLIC Source)
find_package(Threads REQUIRED)
target_link_libraries(rvr_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(rvr_core PRIVATE /W4 /fp:precise)
else()
  target_compile_options(rvr_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(RVR_BUILD_TESTS)
  enable_testing()
  add_executable(ReverseVocalReverbTests Tests/TestMain.cpp)
  target_link_libraries(ReverseVocalReverbTests PRIVATE rvr_core)
  if(NOT MSVC)
    target_compile_options(ReverseVocalReverbTests PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
  add_test(NAME dsp_core COMMAND ReverseVocalReverbTests)
endif()

# ------------------------------------------------------------------------- plugin JUCE
if(RVR_BUILD_PLUGIN)
  if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()

  set(RVR_FORMATS VST3)
  if(RVR_BUILD_STANDALONE)
    list(APPEND RVR_FORMATS Standalone)
  endif()

  juce_add_plugin(ReverseVocalReverb
      COMPANY_NAME "${RVR_COMPANY}"
      PLUGIN_MANUFACTURER_CODE Bdza
      PLUGIN_CODE Rvvr
      FORMATS ${RVR_FORMATS}
      PRODUCT_NAME "Reverse Vocal Reverb"
      IS_SYNTH FALSE
      NEEDS_MIDI_INPUT TRUE
      NEEDS_MIDI_OUTPUT FALSE
      IS_MIDI_EFFECT FALSE
      EDITOR_WANTS_KEYBOARD_FOCUS FALSE
      COPY_PLUGIN_AFTER_BUILD FALSE
      VST3_CATEGORIES Fx Reverb)

  target_sources(ReverseVocalReverb PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(ReverseVocalReverb PUBLIC
      JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(ReverseVocalReverb
      PRIVATE rvr_core juce::juce_audio_utils
      PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endif()

# Prueba de integración con el PluginProcessor real (sin interfaz gráfica)
if(RVR_BUILD_PLUGIN AND RVR_BUILD_TESTS)
  juce_add_console_app(ReverseVocalReverbProcessorTest PRODUCT_NAME "RVR Processor Test")
  target_sources(ReverseVocalReverbProcessorTest PRIVATE
      Tests/ProcessorSmokeTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(ReverseVocalReverbProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(ReverseVocalReverbProcessorTest PRIVATE rvr_core juce::juce_audio_utils juce::juce_recommended_config_flags)
  add_test(NAME processor_integration COMMAND ReverseVocalReverbProcessorTest)
endif()
