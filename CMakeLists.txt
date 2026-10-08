cmake_minimum_required(VERSION 3.22)

# Give Apple targets a conservative deployment floor; configure can override it.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(ChordEngineNative VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep Apple builds local and unsigned. Product copying/install steps are disabled.
if(APPLE)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
endif()

option(CHORDENGINE_FETCH_JUCE "Fetch the pinned JUCE source tree with CMake FetchContent" ON)
set(JUCE_PINNED_VERSION "9.0.3")
set(JUCE_PINNED_COMMIT "be29c81492b6151c8ea8d14c840e1311963b3a83")

if(CHORDENGINE_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JUCE_PINNED_COMMIT}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
else()
    message(FATAL_ERROR "CHORDENGINE_FETCH_JUCE=OFF is not supported; use the pinned FetchContent dependency")
endif()

# The AU and VST3 require different compatibility IDs, so each format has its
# own JUCE target while sharing every implementation source and the core library.
add_library(ChordEngineCore STATIC
    Source/Core/ChordEngineCore.cpp
    Source/Core/ScaleSystem.cpp
    Source/Core/ChordPresetSystem.cpp
    Source/Core/TransposeEngine.cpp
    Source/MIDI/MidiNoteTracker.cpp)
target_include_directories(ChordEngineCore PUBLIC Source)
target_compile_features(ChordEngineCore PUBLIC cxx_std_17)

# The licensing subsystem: the pure state model, the service seams and the
# real Music-Prod auth client. It shares no musical behaviour with the core.
add_library(ChordEngineLicensing STATIC
    Source/Licensing/LicensingTypes.cpp
    Source/Licensing/LicensingModel.cpp
    Source/Licensing/LicensingServices.cpp
    Source/Licensing/LicensingBoundary.cpp
    Source/Licensing/MusicProdAuthService.cpp)
target_include_directories(ChordEngineLicensing PUBLIC Source)
target_compile_features(ChordEngineLicensing PUBLIC cxx_std_17)
target_link_libraries(ChordEngineLicensing PUBLIC juce::juce_core juce::juce_events)

# The native update client: one anonymous GET against the Music-Prod release
# service, a strict response validator, a last-successful-answer cache and a
# single-worker delivery thread. It is a SEPARATE module from licensing: it
# reads no account state, and licensing never reads update state.
#
# CHORDENGINE_UPDATE_OFFLINE_TRANSPORT is deliberately NOT defined here: the
# shipped plugin gets the real HTTP transport. Test targets define it for
# themselves so no automated run ever touches the live service.
add_library(ChordEngineUpdates STATIC
    Source/Updates/UpdateTypes.cpp
    Source/Updates/UpdateResponse.cpp
    Source/Updates/UpdateTransport.cpp
    Source/Updates/UpdateCache.cpp
    Source/Updates/UpdateService.cpp)
target_include_directories(ChordEngineUpdates PUBLIC Source)
target_compile_features(ChordEngineUpdates PUBLIC cxx_std_17)
target_link_libraries(ChordEngineUpdates PUBLIC juce::juce_core juce::juce_events)

# The REAL Music-Prod wordmark asset (260x86 RGBA), byte-identical to the
# reference ChordEngine-FL/Images/MusicProdLogo.png. Embedded, not redrawn.
juce_add_binary_data(ChordEngineAssets
    SOURCES Resources/MusicProdLogo.png)

function(add_chordengine_plugin target format bundle_id manufacturer_code plugin_code)
    juce_add_plugin(${target}
        VERSION 0.1.0
        PRODUCT_NAME "ChordEngine"
        PLUGIN_NAME "ChordEngine"
        DESCRIPTION "ChordEngine native MIDI processor development build"
        COMPANY_NAME "Music-Prod"
        COMPANY_WEBSITE "https://music-prod.com"
        COMPANY_COPYRIGHT "Copyright (c) Music-Prod"
        BUNDLE_ID "${bundle_id}"
        PLUGIN_MANUFACTURER_CODE "${manufacturer_code}"
        PLUGIN_CODE "${plugin_code}"
        FORMATS ${format}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/State/PluginState.cpp)
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target}
        PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    juce_generate_juce_header(${target})

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
    endif()
endfunction()

if(APPLE)
    add_chordengine_plugin(ChordEngineAU AU com.myCompany.product Abcd Abcd)
endif()

if(APPLE OR WIN32)
    add_chordengine_plugin(ChordEngineVST3 VST3 com.musicprod.chordenginefl Musc Cefl)
else()
    message(FATAL_ERROR "This initial scaffold configures macOS (AU/VST3) and Windows (VST3) only")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ChordEngineCoreTests
        Tests/CoreTests.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(ChordEngineCoreTests PRIVATE Source)
    target_compile_definitions(ChordEngineCoreTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/GoldenVectors.json"
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JucePlugin_Name="ChordEngine"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_Manufacturer="Music-Prod"
        JucePlugin_ManufacturerCode=0x41626364
        JucePlugin_PluginCode=0x41626364
        JucePlugin_Version=0.1.0
        JucePlugin_VersionString="0.1.0"
        JucePlugin_CFBundleIdentifier=com.myCompany.product
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ChordEngineCoreTests
        PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineCoreTests COMMAND ChordEngineCoreTests)

    # Development-only GUI snapshot renderer (visual layout review; not a test,
    # never installed or shipped).
    add_executable(ChordEngineGuiSnapshot
        Tools/GuiSnapshot.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(ChordEngineGuiSnapshot PRIVATE Source)
    target_compile_definitions(ChordEngineGuiSnapshot PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        JucePlugin_Name="ChordEngine"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_Manufacturer="Music-Prod"
        JucePlugin_ManufacturerCode=0x41626364
        JucePlugin_PluginCode=0x41626364
        JucePlugin_Version=0.1.0
        JucePlugin_VersionString="0.1.0"
        JucePlugin_CFBundleIdentifier=com.myCompany.product
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineGuiSnapshot
        PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)

    add_executable(ChordEngineReferenceTests Tests/CoreReferenceTests.cpp)
    target_compile_definitions(ChordEngineReferenceTests PRIVATE
        CHORDENGINE_FL_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/FLReferenceVectors.json")
    target_link_libraries(ChordEngineReferenceTests
        PRIVATE ChordEngineCore juce::juce_core
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineReferenceTests COMMAND ChordEngineReferenceTests)

    add_executable(ChordEngineFLReferenceVectorTests Tests/FLReferenceVectorTests.cpp)
    target_include_directories(ChordEngineFLReferenceVectorTests PRIVATE Source)
    target_compile_definitions(ChordEngineFLReferenceVectorTests PRIVATE
        CHORDENGINE_FL_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/FLReferenceVectors.json")
    target_link_libraries(ChordEngineFLReferenceVectorTests
        PRIVATE ChordEngineCore juce::juce_core
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineFLReferenceVectorTests COMMAND ChordEngineFLReferenceVectorTests)

    add_executable(ChordEngineProcessorIntegrationTests
        Tests/ProcessorIntegrationTests.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(ChordEngineProcessorIntegrationTests PRIVATE Source)
    target_compile_definitions(ChordEngineProcessorIntegrationTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_FL_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/FLReferenceVectors.json"
        JucePlugin_Name="ChordEngine"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_Manufacturer="Music-Prod"
        JucePlugin_ManufacturerCode=0x41626364
        JucePlugin_PluginCode=0x41626364
        JucePlugin_Version=0.1.0
        JucePlugin_VersionString="0.1.0"
        JucePlugin_CFBundleIdentifier=com.myCompany.product
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ChordEngineProcessorIntegrationTests
        PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineProcessorIntegrationTests COMMAND ChordEngineProcessorIntegrationTests)

    # Functional parity tests: the recorder / velocity / INFO / trial /
    # licensing / realtime-safety behaviour of the functional pass.
    add_executable(ChordEngineFunctionalTests
        Tests/FunctionalTests.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(ChordEngineFunctionalTests PRIVATE Source)
    target_compile_definitions(ChordEngineFunctionalTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JucePlugin_Name="ChordEngine"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_Manufacturer="Music-Prod"
        JucePlugin_ManufacturerCode=0x41626364
        JucePlugin_PluginCode=0x41626364
        JucePlugin_Version=0.1.0
        JucePlugin_VersionString="0.1.0"
        JucePlugin_CFBundleIdentifier=com.myCompany.product
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineFunctionalTests
        PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineFunctionalTests COMMAND ChordEngineFunctionalTests)

    # The update client's deterministic suite: strict validation, the decision
    # mapping (including noPublishedRelease never being "up to date"), the
    # 503-only retry policy, the cache, the state transitions, the single-worker
    # threading model and the licensing-isolation invariants. Every transport is
    # a local fake or the offline transport, so no case touches the network.
    add_executable(ChordEngineUpdateTests Tests/UpdateTests.cpp)
    target_include_directories(ChordEngineUpdateTests PRIVATE Source)
    target_compile_definitions(ChordEngineUpdateTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JucePlugin_Name="ChordEngine"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_Manufacturer="Music-Prod"
        JucePlugin_ManufacturerCode=0x41626364
        JucePlugin_PluginCode=0x41626364
        JucePlugin_Version=0.1.0
        JucePlugin_VersionString="0.1.0"
        JucePlugin_CFBundleIdentifier=com.myCompany.product
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineUpdateTests
        PRIVATE ChordEngineUpdates juce::juce_core juce::juce_events
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineUpdateTests COMMAND ChordEngineUpdateTests)

    # Plugin state persistence: serialize/restore round trips, the malformed-
    # payload policy, MIDI output after restore, instance independence and GUI
    # re-synchronisation. Drives real processor and editor instances.
    add_executable(ChordEngineStateTests
        Tests/StateTests.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(ChordEngineStateTests PRIVATE Source)
    target_compile_definitions(ChordEngineStateTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        JucePlugin_Name="ChordEngine"
        JucePlugin_IsSynth=0
        JucePlugin_IsMidiEffect=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_Manufacturer="Music-Prod"
        JucePlugin_ManufacturerCode=0x41626364
        JucePlugin_PluginCode=0x41626364
        JucePlugin_Version=0.1.0
        JucePlugin_VersionString="0.1.0"
        JucePlugin_CFBundleIdentifier=com.myCompany.product
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineStateTests
        PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineStateTests COMMAND ChordEngineStateTests)

    # Optional Windows-only stack diagnosis. It is excluded from normal builds
    # and never applies the experimental stack reserve to production plugins.
    option(CHORDENGINE_WINDOWS_STACK_DIAGNOSTICS
           "Build diagnostic-only Windows processor stack probe" OFF)
    set(CHORDENGINE_WINDOWS_TEST_STACK_SIZE "8388608" CACHE STRING
        "Experimental reserve size for the four Windows processor test executables")
    option(CHORDENGINE_WINDOWS_LARGE_TEST_STACK
           "Use the experimental reserve for four Windows diagnostic test executables" OFF)
    if(WIN32 AND CHORDENGINE_WINDOWS_STACK_DIAGNOSTICS)
        add_executable(ChordEngineWindowsStackProbe
            Tests/WindowsStackProbe.cpp
            Source/State/PluginState.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp)
        target_include_directories(ChordEngineWindowsStackProbe PRIVATE Source)
        target_compile_definitions(ChordEngineWindowsStackProbe PRIVATE
            CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
            JucePlugin_Name="ChordEngine"
            JucePlugin_IsSynth=0
            JucePlugin_IsMidiEffect=1
            JucePlugin_WantsMidiInput=1
            JucePlugin_ProducesMidiOutput=1
            JucePlugin_Manufacturer="Music-Prod"
            JucePlugin_ManufacturerCode=0x41626364
            JucePlugin_PluginCode=0x41626364
            JucePlugin_Version=0.1.0
            JucePlugin_VersionString="0.1.0"
            JucePlugin_CFBundleIdentifier=com.myCompany.product
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(ChordEngineWindowsStackProbe
            PRIVATE ChordEngineCore ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                    juce::juce_audio_utils
            PUBLIC juce::juce_recommended_config_flags
                   juce::juce_recommended_warning_flags)

        # Enabled only in a second CI configure after the default-reserve
        # baseline. Never applied to production AU/VST3 targets.
        if(CHORDENGINE_WINDOWS_LARGE_TEST_STACK)
            target_link_options(ChordEngineWindowsStackProbe PRIVATE "/STACK:${CHORDENGINE_WINDOWS_TEST_STACK_SIZE}")
            foreach(test_target IN ITEMS ChordEngineCoreTests ChordEngineProcessorIntegrationTests
                                         ChordEngineFunctionalTests ChordEngineStateTests)
                target_link_options(${test_target} PRIVATE "/STACK:${CHORDENGINE_WINDOWS_TEST_STACK_SIZE}")
            endforeach()
        endif()
    endif()
endif()
