cmake_minimum_required(VERSION 3.22)

# =============================================================================
# ChordEngine - the JUCE-free build.
#
# Third-party code is limited to Apple's AudioUnitSDK (Apache-2.0) and
# Steinberg's VST 3 SDK (MIT), vendored in ThirdParty/ (see its README).
# Everything else is ChordEngine's own code. No JUCE file, header, library or
# definition is used anywhere; Tools/CheckNoJuce.sh (a CTest) fails the build
# if one comes back, in the sources or in the linked symbols.
# =============================================================================

# 12.0 is the floor this product supports (Docs/LOGIC_HOST_TEST_CHECKLIST.md).
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

# The ONE version this product builds as. Everything that reports a version
# derives from it: the bundles' Info.plist and the AU component version, the
# VST3 class info, the version the licensing bridge sends as `plugin_version`,
# the update client, and the packaging script's default.
#
# 0.6.0 is the JUCE-free rebuild (branch juce-free).
set(CHORDENGINE_VERSION "0.6.0")

project(ChordEngineNative VERSION ${CHORDENGINE_VERSION} LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The AudioComponents version: major << 16 | minor << 8 | patch. Hosts and
# macOS compare it between installed copies, so it must be this build's.
math(EXPR CHORDENGINE_AU_VERSION
     "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
string(REPLACE "." "_" CHORDENGINE_VERSION_TOKEN "${CHORDENGINE_VERSION}")

set(CHORDENGINE_VERSION_DEFINITIONS
    CHORDENGINE_PRODUCT_VERSION="${CHORDENGINE_VERSION}"
    CHORDENGINE_PLUGIN_VERSION_STRING="${CHORDENGINE_VERSION}")

# --- sanitizers (tests and the core libraries) --------------------------------
option(CHORDENGINE_ASAN "ASan + UBSan on the libraries and tests" OFF)
option(CHORDENGINE_TSAN "TSan on the libraries and tests" OFF)
set(CHORDENGINE_SANITIZER_FLAGS "")
if(CHORDENGINE_ASAN)
    set(CHORDENGINE_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -g)
elseif(CHORDENGINE_TSAN)
    set(CHORDENGINE_SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer -g)
endif()
if(CHORDENGINE_SANITIZER_FLAGS)
    add_compile_options(${CHORDENGINE_SANITIZER_FLAGS})
    add_link_options(${CHORDENGINE_SANITIZER_FLAGS})
endif()

if(MSVC)
    set(CHORDENGINE_WARNINGS /W4)
else()
    set(CHORDENGINE_WARNINGS -Wall -Wextra)
endif()

# --- the platform layer (no third-party code) --------------------------------------
add_library(ChordEnginePlatform STATIC
    Source/Platform/Json.cpp
    Source/Platform/WorkerQueue.cpp)
target_include_directories(ChordEnginePlatform PUBLIC Source)
target_compile_options(ChordEnginePlatform PRIVATE ${CHORDENGINE_WARNINGS})
if(APPLE)
    # PlatformApple.mm declares Objective-C classes, so it is compiled into each
    # binary with that binary's class-name suffix (CHORDENGINE_PLATFORM_APPLE).
    target_link_libraries(ChordEnginePlatform PUBLIC
        "-framework Foundation" "-framework AppKit" "-framework Security")
    set(CHORDENGINE_PLATFORM_APPLE ${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/PlatformApple.mm)
elseif(WIN32)
    set(CHORDENGINE_PLATFORM_WIN ${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/PlatformWin.cpp)
    target_link_libraries(ChordEnginePlatform PUBLIC winhttp bcrypt)
endif()

# --- the MIDI engine, synth and host state ----------------------------------------------
add_library(ChordEngineEngine STATIC
    Source/Core/ChordEngineCore.cpp
    Source/Core/ChordPresetSystem.cpp
    Source/Core/ScaleSystem.cpp
    Source/Core/TransposeEngine.cpp
    Source/MIDI/MidiNoteTracker.cpp
    Source/Audio/InternalSynth.cpp
    Source/State/PluginState.cpp
    Source/State/XmlLite.cpp)
target_include_directories(ChordEngineEngine PUBLIC Source)
target_compile_options(ChordEngineEngine PRIVATE ${CHORDENGINE_WARNINGS})

# --- account, update and pack services -----------------------------------------------------
add_library(ChordEngineServices STATIC
    Source/Licensing/LicensingTypes.cpp
    Source/Licensing/LicensingModel.cpp
    Source/Licensing/LicensingServices.cpp
    Source/Licensing/LicensingBoundary.cpp
    Source/Licensing/LicensingRuntime.cpp
    Source/Licensing/MusicProdAuthService.cpp
    Source/Updates/UpdateTypes.cpp
    Source/Updates/UpdateResponse.cpp
    Source/Updates/UpdateTransport.cpp
    Source/Updates/UpdateCache.cpp
    Source/Updates/UpdateService.cpp
    Source/Packs/PackFormat.cpp
    Source/Packs/PackLibrary.cpp
    Source/Packs/PackStoreClient.cpp)
target_include_directories(ChordEngineServices PUBLIC Source)
target_compile_definitions(ChordEngineServices PUBLIC ${CHORDENGINE_VERSION_DEFINITIONS})
target_compile_options(ChordEngineServices PRIVATE ${CHORDENGINE_WARNINGS})
target_link_libraries(ChordEngineServices PUBLIC ChordEngineEngine ChordEnginePlatform)

# --- the processor (host-independent) ------------------------------------------------------
add_library(ChordEngineProcessor STATIC Source/PluginProcessor.cpp)
target_include_directories(ChordEngineProcessor PUBLIC Source)
target_compile_options(ChordEngineProcessor PRIVATE ${CHORDENGINE_WARNINGS})
target_link_libraries(ChordEngineProcessor PUBLIC ChordEngineServices ChordEngineEngine)

# A processor build for the tests: every processor keeps its account and trial
# state in a private temporary folder and starts no timer (see PluginProcessor).
add_library(ChordEngineProcessorIsolated STATIC Source/PluginProcessor.cpp)
target_include_directories(ChordEngineProcessorIsolated PUBLIC Source)
target_compile_definitions(ChordEngineProcessorIsolated PUBLIC CHORDENGINE_ISOLATED_LICENSING=1
                                                              CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1)
target_compile_options(ChordEngineProcessorIsolated PRIVATE ${CHORDENGINE_WARNINGS})
target_link_libraries(ChordEngineProcessorIsolated PUBLIC ChordEngineServices ChordEngineEngine)

if(APPLE)
    # --- AudioUnitSDK (Apache-2.0) -------------------------------------------------------
    set(AUSDK ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/AudioUnitSDK)
    add_library(ChordEngineAUSDK STATIC
        ${AUSDK}/src/AudioUnitSDK/AUBase.cpp
        ${AUSDK}/src/AudioUnitSDK/AUBuffer.cpp
        ${AUSDK}/src/AudioUnitSDK/AUBufferAllocator.cpp
        ${AUSDK}/src/AudioUnitSDK/AUInputElement.cpp
        ${AUSDK}/src/AudioUnitSDK/AUMIDIBase.cpp
        ${AUSDK}/src/AudioUnitSDK/AUOutputElement.cpp
        ${AUSDK}/src/AudioUnitSDK/AUPlugInDispatch.cpp
        ${AUSDK}/src/AudioUnitSDK/AUScopeElement.cpp
        ${AUSDK}/src/AudioUnitSDK/ComponentBase.cpp
        ${AUSDK}/src/AudioUnitSDK/MusicDeviceBase.cpp)
    target_include_directories(ChordEngineAUSDK PUBLIC ${AUSDK}/include)
    # AudioUnitSDK 1.4 needs C++23 (std::expected, header-only); everything that
    # includes its headers is built as C++23 too.
    target_compile_features(ChordEngineAUSDK PUBLIC cxx_std_23)
    set_target_properties(ChordEngineAUSDK PROPERTIES CXX_STANDARD 23)
    target_compile_options(ChordEngineAUSDK PRIVATE -w)
    target_link_libraries(ChordEngineAUSDK PUBLIC
        "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation" "-framework CoreMIDI")
endif()

# --- VST 3 SDK subset (MIT) ------------------------------------------------------------------
set(VST3SDK ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/vst3sdk)
add_library(ChordEngineVST3SDK STATIC
    ${VST3SDK}/base/source/baseiids.cpp
    ${VST3SDK}/base/source/fbuffer.cpp
    ${VST3SDK}/base/source/fdebug.cpp
    ${VST3SDK}/base/source/fobject.cpp
    ${VST3SDK}/base/source/fstreamer.cpp
    ${VST3SDK}/base/source/fstring.cpp
    ${VST3SDK}/base/source/updatehandler.cpp
    ${VST3SDK}/base/thread/source/flock.cpp
    ${VST3SDK}/pluginterfaces/base/coreiids.cpp
    ${VST3SDK}/pluginterfaces/base/funknown.cpp
    ${VST3SDK}/pluginterfaces/base/conststringtable.cpp
    ${VST3SDK}/pluginterfaces/base/ustring.cpp
    ${VST3SDK}/public.sdk/source/common/commonstringconvert.cpp
    ${VST3SDK}/public.sdk/source/common/memorystream.cpp
    ${VST3SDK}/public.sdk/source/common/pluginview.cpp
    ${VST3SDK}/public.sdk/source/vst/vstbus.cpp
    ${VST3SDK}/public.sdk/source/vst/vstcomponent.cpp
    ${VST3SDK}/public.sdk/source/vst/vstcomponentbase.cpp
    ${VST3SDK}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3SDK}/public.sdk/source/vst/utility/stringconvert.cpp)
target_include_directories(ChordEngineVST3SDK PUBLIC ${VST3SDK})
target_compile_definitions(ChordEngineVST3SDK PUBLIC $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
if(MSVC)
    target_compile_options(ChordEngineVST3SDK PRIVATE /W0)
else()
    target_compile_options(ChordEngineVST3SDK PRIVATE -w)
endif()

# --- the editor ---------------------------------------------------------------------------------
if(APPLE)
    file(GLOB CHORDENGINE_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/*.mm)
    set(CHORDENGINE_EDITOR_FRAMEWORKS "-framework Cocoa" "-framework CoreText" "-framework QuartzCore"
        "-framework UniformTypeIdentifiers")
    set(CHORDENGINE_RESOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/MusicProdLogo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/Inter-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/Inter-SemiBold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/SchibstedGrotesk-SemiBold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/Inter-OFL.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/SchibstedGrotesk-OFL.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/Docs/THIRD_PARTY_NOTICES.txt)
endif()

# =============================================================================
# The plug-in bundles
# =============================================================================
# Bundle identifiers: unique per bundle (0.5.1 shipped the MIDI FX AU as
# com.musicprod.chordengine and the VST3 as com.musicprod.chordenginefl).
set(CHORDENGINE_AU_BUNDLE_ID com.musicprod.chordengine.au)
set(CHORDENGINE_INSTRUMENT_AU_BUNDLE_ID com.musicprod.chordengine.instrument.au)
set(CHORDENGINE_VST3_BUNDLE_ID com.musicprod.chordengine.vst3)

set(CHORDENGINE_BUNDLE_DIR ${CMAKE_BINARY_DIR}/Bundles)

if(APPLE)
    function(chordengine_bundle target extension output_name plist objc_suffix)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION ${extension}
            OUTPUT_NAME "${output_name}"
            MACOSX_BUNDLE_INFO_PLIST ${plist}
            LIBRARY_OUTPUT_DIRECTORY ${CHORDENGINE_BUNDLE_DIR}
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
        target_compile_definitions(${target} PRIVATE CHORDENGINE_OBJC_SUFFIX=${objc_suffix}
                                                     ${CHORDENGINE_VERSION_DEFINITIONS})
        target_compile_options(${target} PRIVATE -fobjc-arc ${CHORDENGINE_WARNINGS})
        # The configured plist is copied over the generated one, the classic
        # bundle marker (PkgInfo, type BNDL) is added - Finder shows a bundle
        # without it as a plain folder - then the resources.
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${plist} $<TARGET_BUNDLE_DIR:${target}>/Contents/Info.plist
            COMMAND /usr/bin/install -m 644 ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Bundle/PkgInfo $<TARGET_BUNDLE_DIR:${target}>/Contents/PkgInfo
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_DIR:${target}>/Contents/Resources
            COMMAND /usr/bin/install -m 644 ${CHORDENGINE_RESOURCE_FILES} $<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/
            VERBATIM)
    endfunction()

    # The finished bundle is sealed ad hoc and must pass strict verification.
    # Release signing is the packaging step (Tools/PackageMacDMG.sh).
    function(chordengine_seal target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - $<TARGET_BUNDLE_DIR:${target}>
            COMMAND /usr/bin/codesign --verify --deep --strict $<TARGET_BUNDLE_DIR:${target}>
            VERBATIM)
    endfunction()

    function(chordengine_au target instrument bundle_id output_name au_name au_description subtype type factory)
        set(CE_BUNDLE_NAME "${output_name}")
        set(CE_BUNDLE_ID ${bundle_id})
        set(CE_AU_NAME "${au_name}")
        set(CE_AU_DESCRIPTION "${au_description}")
        set(CE_AU_SUBTYPE ${subtype})
        set(CE_AU_TYPE ${type})
        set(CE_AU_FACTORY ${factory})
        set(CE_AU_VERSION ${CHORDENGINE_AU_VERSION})
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Resources/Bundle/Info-AU.plist.in
                       ${CMAKE_BINARY_DIR}/Plists/${target}-Info.plist @ONLY)
        add_library(${target} MODULE Source/AU/ChordEngineAU.mm ${CHORDENGINE_EDITOR_SOURCES}
                                     ${CHORDENGINE_PLATFORM_APPLE})
        target_compile_definitions(${target} PRIVATE CHORDENGINE_AU_INSTRUMENT=${instrument}
                                                     CHORDENGINE_AUDIO_OUTPUT=${instrument}
                                                     CHORDENGINE_INTERNAL_AUDIO_DEFAULT_ON=${instrument})
        target_link_libraries(${target} PRIVATE ChordEngineProcessor ChordEngineAUSDK ${CHORDENGINE_EDITOR_FRAMEWORKS}
                                                "-framework AudioUnit")
        target_link_options(${target} PRIVATE -Wl,-exported_symbol,_${factory})
        set_target_properties(${target} PROPERTIES CXX_STANDARD 23 OBJCXX_STANDARD 23)
        chordengine_bundle(${target} component "${output_name}" ${CMAKE_BINARY_DIR}/Plists/${target}-Info.plist
                           ${target}_${CHORDENGINE_VERSION_TOKEN})
        chordengine_seal(${target})
    endfunction()

    chordengine_au(ChordEngineAU 0 ${CHORDENGINE_AU_BUNDLE_ID} "ChordEngine"
                   "Music-Prod: ChordEngine" "ChordEngine MIDI chord processor" Cefl aumi ChordEngineAUFactory)
    chordengine_au(ChordEngineInstrumentAU 1 ${CHORDENGINE_INSTRUMENT_AU_BUNDLE_ID} "ChordEngine Instrument"
                   "Music-Prod: ChordEngine Instrument" "ChordEngine chord instrument" Cein aumu
                   ChordEngine_InstrumentAUFactory)

    set(CE_BUNDLE_ID ${CHORDENGINE_VST3_BUNDLE_ID})
    configure_file(Resources/Bundle/Info-VST3.plist.in ${CMAKE_BINARY_DIR}/Plists/ChordEngineVST3-Info.plist @ONLY)
    configure_file(Resources/Bundle/moduleinfo.json.in ${CMAKE_BINARY_DIR}/Plists/moduleinfo.json @ONLY)
    add_library(ChordEngineVST3 MODULE Source/VST3/ChordEngineVST3.mm ${CHORDENGINE_EDITOR_SOURCES}
                                       ${CHORDENGINE_PLATFORM_APPLE})
    target_compile_definitions(ChordEngineVST3 PRIVATE CHORDENGINE_AUDIO_OUTPUT=1)
    target_link_libraries(ChordEngineVST3 PRIVATE ChordEngineProcessor ChordEngineVST3SDK ${CHORDENGINE_EDITOR_FRAMEWORKS})
    target_link_options(ChordEngineVST3 PRIVATE -Wl,-exported_symbol,_GetPluginFactory
                                                -Wl,-exported_symbol,_bundleEntry -Wl,-exported_symbol,_bundleExit)
    chordengine_bundle(ChordEngineVST3 vst3 "ChordEngine" ${CMAKE_BINARY_DIR}/Plists/ChordEngineVST3-Info.plist
                       ChordEngineVST3_${CHORDENGINE_VERSION_TOKEN})
    add_custom_command(TARGET ChordEngineVST3 POST_BUILD
        COMMAND /usr/bin/install -m 644 ${CMAKE_BINARY_DIR}/Plists/moduleinfo.json
                $<TARGET_BUNDLE_DIR:ChordEngineVST3>/Contents/Resources/moduleinfo.json
        VERBATIM)
    chordengine_seal(ChordEngineVST3)

    add_custom_target(ChordEnginePlugins DEPENDS ChordEngineAU ChordEngineInstrumentAU ChordEngineVST3)
elseif(WIN32)
    # Windows: the VST3 (processor, state, MIDI, licensing). The editor view is
    # planned for a later step (Docs/NATIVE_REBUILD.md, "Windows").
    add_library(ChordEngineVST3 MODULE Source/VST3/ChordEngineVST3.mm ${CHORDENGINE_PLATFORM_WIN})
    set_source_files_properties(Source/VST3/ChordEngineVST3.mm PROPERTIES LANGUAGE CXX)
    target_compile_options(ChordEngineVST3 PRIVATE /TP)
    target_compile_definitions(ChordEngineVST3 PRIVATE CHORDENGINE_AUDIO_OUTPUT=1 ${CHORDENGINE_VERSION_DEFINITIONS})
    target_link_libraries(ChordEngineVST3 PRIVATE ChordEngineProcessor ChordEngineVST3SDK)
    # The VST3 bundle layout: ChordEngine.vst3/Contents/x86_64-win/ChordEngine.vst3
    set(CE_WIN_BUNDLE ${CHORDENGINE_BUNDLE_DIR}/ChordEngine.vst3)
    set_target_properties(ChordEngineVST3 PROPERTIES OUTPUT_NAME ChordEngine SUFFIX .vst3
        LIBRARY_OUTPUT_DIRECTORY $<1:${CE_WIN_BUNDLE}/Contents/x86_64-win>)
    configure_file(Resources/Bundle/moduleinfo.json.in ${CMAKE_BINARY_DIR}/Plists/moduleinfo.json @ONLY)
    add_custom_command(TARGET ChordEngineVST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CE_WIN_BUNDLE}/Contents/Resources
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/Plists/moduleinfo.json
                ${CE_WIN_BUNDLE}/Contents/Resources/moduleinfo.json
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Docs/THIRD_PARTY_NOTICES.txt
                ${CE_WIN_BUNDLE}/Contents/Resources/THIRD_PARTY_NOTICES.txt
        VERBATIM)
endif()

# =============================================================================
# Tests and tools (nothing below ships)
# =============================================================================
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()
