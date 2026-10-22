cmake_minimum_required(VERSION 3.22)

# Give Apple targets a conservative deployment floor; configure can override it.
#
# 12.0 is the floor this product supports and the one the 0.5.0 release builds
# against - it is what Docs/LOGIC_HOST_TEST_CHECKLIST.md and the release notes
# state. The earlier 11.0 default can no longer be built at all: the installed
# Xcode's macOS SDK refuses any deployment target below 12.0 ("the range of
# supported deployment target versions is 12.0 to 27.0.x"), which fails the C
# and C++ compiler identification before project code is reached.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

# The ONE version this product releases as. Everything that reports a version
# derives from it: the compiled plug-in (juce_add_plugin VERSION), the version
# the licensing account bridge sends as `plugin_version`, the build metadata
# every test and development binary compiles with, and the release packaging
# script's default. Nothing below repeats the literal.
#
# 0.5.0 is the first UNIVERSAL release: it continues the legacy ChordEngine FL
# line (0.4.0) and replaces it - one product, one implementation, one
# entitlement, both plug-in formats and every supported DAW. 0.5.1 moves the
# licence check and the persisted trial clock into the processor.
set(CHORDENGINE_VERSION "0.5.1")

project(ChordEngineNative VERSION ${CHORDENGINE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep Apple builds local: Xcode's own signing phase is disabled and product
# copying/install steps are disabled. The AU and VST3 bundles still end up
# with a VALID ad hoc bundle signature - see chordengine_adhoc_sign_bundle
# below. Developer ID signing happens only in Tools/PackageMacDMG.sh.
if(APPLE)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
endif()

# Ad hoc signs a finished plug-in bundle as the LAST post-build step of its
# target, then fails the build unless `codesign --verify --deep --strict`
# passes. Without it the AU carries only the linker's resource-less signature
# and the VST3's moduleinfo.json (written by JUCE's manifest step after JUCE's
# own re-sign) is unsealed - both fail strict verification. POST_BUILD commands
# run in the order they are added, so this must be called after
# juce_add_plugin has added its manifest step.
option(CHORDENGINE_ADHOC_SIGN_BUNDLES
       "Ad hoc sign and strictly verify each built macOS plug-in bundle" ON)

function(chordengine_adhoc_sign_bundle target)
    if(NOT APPLE OR NOT CHORDENGINE_ADHOC_SIGN_BUNDLES)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-Dbundle=$<TARGET_BUNDLE_DIR:${target}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AdhocSignBundle.cmake"
        COMMENT "Ad hoc signing ${target} bundle"
        VERBATIM)
endfunction()

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

# The internal-audio instrument (synth, piano, bass) the VST3 and the
# ChordEngine Instrument AU play generated chords through.
add_library(ChordEngineAudio STATIC
    Source/Audio/InternalSynth.cpp)
target_include_directories(ChordEngineAudio PUBLIC Source)
target_compile_features(ChordEngineAudio PUBLIC cxx_std_17)
target_link_libraries(ChordEngineAudio PUBLIC juce::juce_audio_basics)

# The licensing subsystem: the pure state model, the service seams and the
# real Music-Prod auth client. It shares no musical behaviour with the core.
add_library(ChordEngineLicensing STATIC
    Source/Licensing/LicensingTypes.cpp
    Source/Licensing/LicensingModel.cpp
    Source/Licensing/LicensingServices.cpp
    Source/Licensing/LicensingBoundary.cpp
    Source/Licensing/LicensingRuntime.cpp
    Source/Licensing/MusicProdAuthService.cpp)
target_include_directories(ChordEngineLicensing PUBLIC Source)
target_compile_features(ChordEngineLicensing PUBLIC cxx_std_17)
target_link_libraries(ChordEngineLicensing PUBLIC juce::juce_core juce::juce_events)

# The version the account bridge reports to the Music-Prod auth endpoint as
# `plugin_version`: the compiled product version, so backend records for a
# 0.5.0 build say 0.5.0 and can never keep reporting the legacy reference's
# 0.4.0.
#
# LicensingTypes.h declares an inline constant from this definition, so EVERY
# translation unit that compiles that header must see the same value - a second
# value elsewhere would be an ODR violation, not merely a stale string. Both
# libraries that include the header get it, PUBLICly, so the plug-in targets and
# every test binary inherit it as well; LicensingTypes.h asserts that it equals
# JucePlugin_VersionString wherever both are visible.
set(CHORDENGINE_VERSION_DEFINITION CHORDENGINE_PRODUCT_VERSION="${CHORDENGINE_VERSION}")

target_compile_definitions(ChordEngineLicensing PUBLIC ${CHORDENGINE_VERSION_DEFINITION})

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

# This library compiles LicensingTypes.h (for the shared Music-Prod origin
# constant), so it must agree on the product version as well - see
# CHORDENGINE_VERSION_DEFINITION above.
target_compile_definitions(ChordEngineUpdates PUBLIC ${CHORDENGINE_VERSION_DEFINITION})

# The REAL Music-Prod wordmark asset (260x86 RGBA), byte-identical to the
# reference ChordEngine-FL/Images/MusicProdLogo.png. Embedded, not redrawn.
# Preset Packs: the pack format, the installed-pack library and the store
# client (catalog + member downloads). Stock presets stay built in.
add_library(ChordEnginePacks STATIC
    Source/Packs/PackFormat.cpp
    Source/Packs/PackLibrary.cpp
    Source/Packs/PackStoreClient.cpp)
target_include_directories(ChordEnginePacks PUBLIC Source)
target_compile_features(ChordEnginePacks PUBLIC cxx_std_17)
target_link_libraries(ChordEnginePacks PUBLIC ChordEngineCore
    juce::juce_core juce::juce_events juce::juce_cryptography)
# Compiles LicensingTypes.h (shared origin constant) - same version definition.
target_compile_definitions(ChordEnginePacks PUBLIC ${CHORDENGINE_VERSION_DEFINITION})

# Inter and Schibsted Grotesk (SIL OFL 1.1; licences beside them in
# Resources/Fonts) are the music-prod.com interface typefaces.
juce_add_binary_data(ChordEngineAssets
    SOURCES
        Resources/MusicProdLogo.png
        Resources/Fonts/Inter-Regular.ttf
        Resources/Fonts/Inter-SemiBold.ttf
        Resources/Fonts/SchibstedGrotesk-SemiBold.ttf)

# The build metadata every TEST and DEVELOPMENT binary compiles with, kept in
# ONE place so no test binary can describe a different product identity or
# version than the shipped plug-in. A juce_add_plugin target generates these
# macros for itself; the test targets consume the same values.
#
# The identity is the SHIPPED one: 0x4D757363 is "Musc", 0x4365666C is "Cefl"
# and the bundle id is the product's. The AU and the VST3 share the code pair
# deliberately - see the two add_chordengine_plugin calls below.
set(CHORDENGINE_PLUGIN_BUILD_DEFINITIONS
    JucePlugin_Name="ChordEngine"
    JucePlugin_IsSynth=0
    JucePlugin_IsMidiEffect=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=1
    JucePlugin_Manufacturer="Music-Prod"
    JucePlugin_ManufacturerCode=0x4D757363
    JucePlugin_PluginCode=0x4365666C
    JucePlugin_Version=${CHORDENGINE_VERSION}
    JucePlugin_VersionString="${CHORDENGINE_VERSION}"
    JucePlugin_CFBundleIdentifier=com.musicprod.chordengine)

# The editor's platform sources: the macOS file-drag source (Objective-C++),
# or the JUCE fallback elsewhere. Compiled into every target that compiles
# PluginEditor.cpp.
if(APPLE)
    set(CHORDENGINE_EDITOR_PLATFORM_SOURCES Source/UI/ExternalFileDrag.mm)
else()
    set(CHORDENGINE_EDITOR_PLATFORM_SOURCES Source/UI/ExternalFileDrag.cpp)
endif()

# variant:
#   MIDI_EFFECT  the AU MIDI FX (`aumi`): MIDI in/out, no audio bus.
#   MIDI_AUDIO   the VST3: MIDI in/out plus a stereo output for internal audio
#                (off by default, so routing MIDI to another instrument works
#                exactly as before).
#   INSTRUMENT   the ChordEngine Instrument AU (`aumu`): internal audio on by
#                default, for hosts such as Logic Pro whose MIDI FX slot
#                cannot carry audio.
function(add_chordengine_plugin target format bundle_id manufacturer_code plugin_code variant)
    set(product_name "ChordEngine")
    set(description "ChordEngine MIDI chord processor")
    set(is_synth FALSE)
    set(is_midi_effect TRUE)
    set(audio_output 0)
    set(audio_default_on 0)
    if(variant STREQUAL "MIDI_AUDIO")
        set(is_synth TRUE)
        set(is_midi_effect FALSE)
        set(audio_output 1)
    elseif(variant STREQUAL "INSTRUMENT")
        set(product_name "ChordEngine Instrument")
        set(description "ChordEngine chord instrument")
        set(is_synth TRUE)
        set(is_midi_effect FALSE)
        set(audio_output 1)
        set(audio_default_on 1)
    elseif(NOT variant STREQUAL "MIDI_EFFECT")
        message(FATAL_ERROR "add_chordengine_plugin: unknown variant ${variant}")
    endif()

    juce_add_plugin(${target}
        VERSION ${CHORDENGINE_VERSION}
        PRODUCT_NAME "${product_name}"
        PLUGIN_NAME "${product_name}"
        DESCRIPTION "${description}"
        COMPANY_NAME "Music-Prod"
        COMPANY_WEBSITE "https://music-prod.com"
        COMPANY_COPYRIGHT "Copyright (c) Music-Prod"
        BUNDLE_ID "${bundle_id}"
        PLUGIN_MANUFACTURER_CODE "${manufacturer_code}"
        PLUGIN_CODE "${plugin_code}"
        FORMATS ${format}
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT ${is_midi_effect}
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        # "Instrument" is required for the VST3 host classes that expose an
        # event/MIDI *input* to a hosted module (verified in FL Studio Patcher:
        # "Fx" and "Fx|Generator" get Parameters/Audio inputs only, while
        # "Instrument" gets Parameters/Events/Audio). Ignored by the AUs.
        VST3_CATEGORIES Instrument
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_compile_definitions(${target} PRIVATE
        CHORDENGINE_AUDIO_OUTPUT=${audio_output}
        CHORDENGINE_INTERNAL_AUDIO_DEFAULT_ON=${audio_default_on})
    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES}
        Source/State/PluginState.cpp)
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target}
        PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    juce_generate_juce_header(${target})

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")

        # Must follow juce_add_plugin, whose VST3 manifest step writes
        # moduleinfo.json in an earlier POST_BUILD command.
        chordengine_adhoc_sign_bundle(${target}_${format})
    endif()
endfunction()

if(APPLE)
    # The AU was never distributed, so its JUCE placeholder identity
    # (`aumi` / Abcd / Abcd, bundle com.myCompany.product) is not a
    # compatibility constraint on anything. It carries the product's REAL
    # codes now - the same Musc/Cefl pair as the VST3 - and the product bundle
    # id. AU hosts resolve a saved project by the
    # (type, subtype, manufacturer) triple, so this is the one moment those
    # codes can be chosen freely; from the first public release they are
    # frozen, and changing them later would orphan saved projects.
    add_chordengine_plugin(ChordEngineAU AU com.musicprod.chordengine Musc Cefl MIDI_EFFECT)

    # A SECOND AU, added for 0.5.0 on the owner's decision: the same plug-in as
    # an instrument (`aumu` / Cein / Musc), so Logic Pro can play the internal
    # synth. It is a new identity of its own - the MIDI FX above is unchanged -
    # and is frozen from its first release like the others.
    add_chordengine_plugin(ChordEngineInstrumentAU AU com.musicprod.chordengine.instrument
                           Musc Cein INSTRUMENT)
endif()

if(APPLE OR WIN32)
    # The LEGACY-COMPATIBLE identity. JUCE derives the VST3 class id from the
    # manufacturer code and the plug-in code ("Musc" + "Cefl" ->
    # ABCDEF019182FAEB4D7573634365666C), so this pair IS the plugin as every
    # existing FL Studio project recorded it: changing the bundle id or either
    # code would orphan those projects. Freeze all three.
    add_chordengine_plugin(ChordEngineVST3 VST3 com.musicprod.chordenginefl Musc Cefl MIDI_AUDIO)
else()
    message(FATAL_ERROR "This initial scaffold configures macOS (AU/VST3) and Windows (VST3) only")
endif()

include(CTest)
if(BUILD_TESTING)
    # Preset Packs: format validation, the stock pack equals the built-in maps,
    # the installed-pack library (checksum, atomic replace, scan) and the store
    # client against a local fake transport. No case touches the network.
    add_executable(ChordEnginePackTests Tests/PackTests.cpp)
    target_include_directories(ChordEnginePackTests PRIVATE Source)
    target_compile_definitions(ChordEnginePackTests PRIVATE
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEnginePackTests
        PRIVATE ChordEnginePacks juce::juce_core juce::juce_events juce::juce_cryptography
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEnginePackTests COMMAND ChordEnginePackTests)

    # Internal synth: silence, sample-accurate onsets, headroom,
    # release-to-silence, voice stealing, reset/all-notes-off, additive
    # rendering, the MidiBuffer driver and pitch, for every sound at 44.1 and
    # 96 kHz. Pure DSP: no processor, editor, host or network.
    add_executable(ChordEngineInternalSynthTests Tests/InternalSynthTests.cpp)
    target_compile_definitions(ChordEngineInternalSynthTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ChordEngineInternalSynthTests
        PRIVATE ChordEngineAudio juce::juce_core
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineInternalSynthTests COMMAND ChordEngineInternalSynthTests)

    add_executable(ChordEngineCoreTests
        Tests/CoreTests.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES})
    target_include_directories(ChordEngineCoreTests PRIVATE Source)
    target_compile_definitions(ChordEngineCoreTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_ISOLATED_LICENSING=1
        CHORDENGINE_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/GoldenVectors.json"
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ChordEngineCoreTests
        PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
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
        Source/PluginEditor.cpp
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES})
    target_include_directories(ChordEngineGuiSnapshot PRIVATE Source)
    target_compile_definitions(ChordEngineGuiSnapshot PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_ISOLATED_LICENSING=1
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineGuiSnapshot
        PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
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
        Source/PluginEditor.cpp
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES})
    target_include_directories(ChordEngineProcessorIntegrationTests PRIVATE Source)
    target_compile_definitions(ChordEngineProcessorIntegrationTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_ISOLATED_LICENSING=1
        CHORDENGINE_FL_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/FLReferenceVectors.json"
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ChordEngineProcessorIntegrationTests
        PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
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
        Source/PluginEditor.cpp
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES})
    target_include_directories(ChordEngineFunctionalTests PRIVATE Source)
    target_compile_definitions(ChordEngineFunctionalTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_ISOLATED_LICENSING=1
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineFunctionalTests
        PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
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
        CHORDENGINE_ISOLATED_LICENSING=1
        CHORDENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineUpdateTests
        PRIVATE ChordEngineUpdates juce::juce_core juce::juce_events
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineUpdateTests COMMAND ChordEngineUpdateTests)

    # The verified backend-entitlement contract for the SHARED ChordEngine
    # product: the product-scoped `plugin_entitled` gate (never the
    # product-agnostic `subscribed` membership), both independent routes
    # (Music-Prod+ and an activated lifetime licence), the documented
    # per-route offline windows, licence-key activation including the
    # device limit, and the stable per-installation device identity. Every
    # reply is a local fixture, so no case touches the network.
    add_executable(ChordEngineLicensingContractTests Tests/LicensingContractTests.cpp)
    target_include_directories(ChordEngineLicensingContractTests PRIVATE Source)
    target_compile_definitions(ChordEngineLicensingContractTests PRIVATE
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineLicensingContractTests
        PRIVATE ChordEngineLicensing juce::juce_core juce::juce_events
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineLicensingContractTests COMMAND ChordEngineLicensingContractTests)

    # The processor-owned licensing runtime: the gate decided at load with no
    # editor, the persisted trial budget, background re-checks and the
    # fallback to the trial when a Music-Prod+ membership ends.
    add_executable(ChordEngineLicensingRuntimeTests Tests/LicensingRuntimeTests.cpp)
    target_include_directories(ChordEngineLicensingRuntimeTests PRIVATE Source)
    target_compile_definitions(ChordEngineLicensingRuntimeTests PRIVATE
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(ChordEngineLicensingRuntimeTests
        PRIVATE ChordEngineLicensing juce::juce_core juce::juce_events
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineLicensingRuntimeTests COMMAND ChordEngineLicensingRuntimeTests)

    # Route SWITCHES between the two independent ways to unlock ChordEngine.
    # The product stays entitled while EITHER a Music-Prod+ subscription or an
    # activated device-bound licence grants it, so losing one route must never
    # withdraw access while the other is still valid - and a route the server
    # did not grant must never keep access alive. Drives the real boundary
    # (including the gate the audio path reads) and the real model, with every
    # reply a local fixture and an explicit clock for the grace boundaries.
    add_executable(ChordEngineLicensingRouteSwitchTests Tests/LicensingRouteSwitchTests.cpp)
    target_include_directories(ChordEngineLicensingRouteSwitchTests PRIVATE Source)
    target_compile_definitions(ChordEngineLicensingRouteSwitchTests PRIVATE
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineLicensingRouteSwitchTests
        PRIVATE ChordEngineLicensing juce::juce_core juce::juce_events
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineLicensingRouteSwitchTests
             COMMAND ChordEngineLicensingRouteSwitchTests)

    # Plugin state persistence: serialize/restore round trips, the malformed-
    # payload policy, MIDI output after restore, instance independence and GUI
    # re-synchronisation. Drives real processor and editor instances.
    add_executable(ChordEngineStateTests
        Tests/StateTests.cpp
        Source/State/PluginState.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES})
    target_include_directories(ChordEngineStateTests PRIVATE Source)
    target_compile_definitions(ChordEngineStateTests PRIVATE
        CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_ISOLATED_LICENSING=1
        ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ChordEngineStateTests
        PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)
    add_test(NAME ChordEngineStateTests COMMAND ChordEngineStateTests)

    # Host-boundary probe (macOS): loads the real plug-in BUNDLES through
    # JUCE's head-less plug-in hosting and records the bus/event layout a host
    # derives, plus real MIDI-in -> generated-MIDI-out across that boundary.
    # Automated and non-GUI; it is not a DAW test. Exits 77 (CTest "skipped")
    # when no bundle is installed yet, so it never reports a false pass.
    if(APPLE)
        add_executable(ChordEngineHostBoundaryProbe Tests/HostBoundaryProbe.cpp)
        target_compile_definitions(ChordEngineHostBoundaryProbe PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_AU=1)
        target_link_libraries(ChordEngineHostBoundaryProbe
            PRIVATE juce::juce_audio_processors_headless
            PUBLIC juce::juce_recommended_config_flags
                   juce::juce_recommended_warning_flags)
        # Probe the VST3 THIS build produced. Without an explicit path the
        # probe falls back to the bundles installed in ~/Library, which may be
        # an older build. The build-tree AU is not passed: JUCE resolves an AU
        # through the system's AudioComponent registry, which only knows
        # installed components, so an uninstalled AU can never be probed here.
        add_test(NAME ChordEngineHostBoundaryProbe
                 COMMAND ChordEngineHostBoundaryProbe
                         "$<TARGET_BUNDLE_DIR:ChordEngineVST3_VST3>")
        set_tests_properties(ChordEngineHostBoundaryProbe PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Release-metadata gate: inspects the AU and VST3 bundles THIS BUILD just
    # produced (never an installed copy) and refuses a release candidate whose
    # identity, category, MIDI declaration, bus layout or version is not the
    # frozen one. It reads the plug-in bundles' own metadata and loads them
    # through JUCE's head-less hosting, so it fails on a placeholder
    # identifier, a missing field, a wrong category or a version mismatch.
    #
    # It proves nothing about how a real DAW behaves: the FL Studio and Logic
    # Pro acceptance tests remain manual. Exits 77 (CTest "skipped") when the
    # bundles have not been built yet, so a skipped run is never a pass.
    if(APPLE)
        add_executable(ChordEngineReleaseMetadataTests Tests/ReleaseMetadataTests.cpp)
        target_include_directories(ChordEngineReleaseMetadataTests PRIVATE Source)
        target_compile_definitions(ChordEngineReleaseMetadataTests PRIVATE
            ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
            CHORDENGINE_EXPECTED_VERSION="${CHORDENGINE_VERSION}"
            CHORDENGINE_BUILD_DIR="${CMAKE_BINARY_DIR}"
            # The hosted-view checks load the built bundles through JUCE's
            # head-less hosting, which only exists when the format is enabled
            # at compile time - exactly as the host-boundary probe does.
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_AU=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(ChordEngineReleaseMetadataTests
            PRIVATE ChordEngineLicensing ChordEngineUpdates
                    juce::juce_audio_processors_headless
            PUBLIC juce::juce_recommended_config_flags
                   juce::juce_recommended_warning_flags)
        add_test(NAME ChordEngineReleaseMetadataTests
                 COMMAND ChordEngineReleaseMetadataTests
                         "$<TARGET_BUNDLE_DIR:ChordEngineAU_AU>"
                         "$<TARGET_BUNDLE_DIR:ChordEngineVST3_VST3>"
                         "$<TARGET_BUNDLE_DIR:ChordEngineInstrumentAU_AU>")
        set_tests_properties(ChordEngineReleaseMetadataTests PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Windows processor tests need a larger stack because they exercise code
    # paths with substantial nested temporary state. Keep this test-only; never
    # apply the reserve to production plugin targets.
    if(WIN32)
        set(CHORDENGINE_WINDOWS_TEST_STACK_SIZE "33554432" CACHE STRING
            "Stack reserve for the four Windows processor test executables")
        foreach(test_target IN ITEMS ChordEngineCoreTests ChordEngineProcessorIntegrationTests
                                     ChordEngineFunctionalTests ChordEngineStateTests)
            target_link_options(${test_target} PRIVATE "/STACK:${CHORDENGINE_WINDOWS_TEST_STACK_SIZE}")
        endforeach()
    endif()

    # Optional Windows-only stack probe, excluded from normal builds.
    option(CHORDENGINE_WINDOWS_STACK_DIAGNOSTICS
           "Build diagnostic-only Windows processor stack probe" OFF)
    if(WIN32 AND CHORDENGINE_WINDOWS_STACK_DIAGNOSTICS)
        add_executable(ChordEngineWindowsStackProbe
            Tests/WindowsStackProbe.cpp
            Source/State/PluginState.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            ${CHORDENGINE_EDITOR_PLATFORM_SOURCES}
        ${CHORDENGINE_EDITOR_PLATFORM_SOURCES})
        target_include_directories(ChordEngineWindowsStackProbe PRIVATE Source)
        target_compile_definitions(ChordEngineWindowsStackProbe PRIVATE
            CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1
        CHORDENGINE_ISOLATED_LICENSING=1
            ${CHORDENGINE_PLUGIN_BUILD_DEFINITIONS}
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(ChordEngineWindowsStackProbe
            PRIVATE ChordEngineCore ChordEngineAudio ChordEnginePacks ChordEngineLicensing ChordEngineUpdates ChordEngineAssets
                    juce::juce_audio_utils
            PUBLIC juce::juce_recommended_config_flags
                   juce::juce_recommended_warning_flags)

    endif()
endif()
