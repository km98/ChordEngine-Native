# Tests and test tools. Nothing here ships. No third-party framework code, here either.

set(CHORDENGINE_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# A test program: links the given libraries, sees Source/ and Tests/.
function(chordengine_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;DEFINITIONS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${CHORDENGINE_TEST_DIR} ${CMAKE_SOURCE_DIR}/Source)
    target_compile_definitions(${name} PRIVATE
        CHORDENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        CHORDENGINE_FL_GOLDEN_FILE="${CHORDENGINE_TEST_DIR}/FLReferenceVectors.json"
        CHORDENGINE_FIXTURE_DIR="${CHORDENGINE_TEST_DIR}/Fixtures"
        ${ARG_DEFINITIONS})
    target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES})
    if(APPLE)
        target_sources(${name} PRIVATE ${CHORDENGINE_PLATFORM_APPLE})
        set_source_files_properties(${CHORDENGINE_PLATFORM_APPLE} TARGET_DIRECTORY ${name}
                                    PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        target_compile_definitions(${name} PRIVATE CHORDENGINE_OBJC_SUFFIX=${name})
    elseif(WIN32)
        target_sources(${name} PRIVATE ${CHORDENGINE_PLATFORM_WIN})
        # A processor (recorder buffers included) is created on the stack in
        # several tests; MSVC's default 1 MB main-thread stack is too small.
        target_link_options(${name} PRIVATE "/STACK:33554432")
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # Nothing a test runs may touch the developer's own account files.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "CFFIXED_USER_HOME=${CMAKE_BINARY_DIR}/TestHome/${name}")
endfunction()

chordengine_test(ChordEngineCoreReferenceTests SOURCES CoreReferenceTests.cpp LIBRARIES ChordEngineEngine ChordEnginePlatform)
chordengine_test(ChordEngineFLReferenceVectorTests SOURCES FLReferenceVectorTests.cpp LIBRARIES ChordEngineEngine ChordEnginePlatform)
chordengine_test(ChordEngineCoreTests SOURCES CoreTests.cpp LIBRARIES ChordEngineProcessorIsolated)
chordengine_test(ChordEngineInternalSynthTests SOURCES InternalSynthTests.cpp LIBRARIES ChordEngineEngine ChordEnginePlatform)
if(APPLE)
    add_executable(ChordEngineAUProbe AUProbe.mm)
    target_compile_options(ChordEngineAUProbe PRIVATE -fobjc-arc)
    target_link_libraries(ChordEngineAUProbe PRIVATE "-framework AudioToolbox" "-framework AudioUnit"
                                                     "-framework CoreMIDI" "-framework Foundation")
endif()

# The editor's tests and snapshot tool (Source/UI) live in their own file.
if(APPLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/EditorTests.mm AND EXISTS ${CMAKE_SOURCE_DIR}/Tools/EditorSnapshot.mm)
    include(${CMAKE_CURRENT_SOURCE_DIR}/EditorTests.cmake OPTIONAL)
endif()
chordengine_test(ChordEngineStateTests SOURCES StateTests.cpp LIBRARIES ChordEngineProcessorIsolated)
chordengine_test(ChordEngineLicensingRuntimeTests SOURCES LicensingRuntimeTests.cpp LIBRARIES ChordEngineServices)
chordengine_test(ChordEngineLicensingContractTests SOURCES LicensingContractTests.cpp LIBRARIES ChordEngineServices)
chordengine_test(ChordEngineLicensingRouteSwitchTests SOURCES LicensingRouteSwitchTests.cpp LIBRARIES ChordEngineServices)
chordengine_test(ChordEngineUpdateTests SOURCES UpdateTests.cpp LIBRARIES ChordEngineServices
                 DEFINITIONS CHORDENGINE_UPDATE_OFFLINE_TRANSPORT=1)
chordengine_test(ChordEnginePackTests SOURCES PackTests.cpp LIBRARIES ChordEngineServices)
chordengine_test(ChordEngineProcessorIntegrationTests SOURCES ProcessorIntegrationTests.cpp
                 LIBRARIES ChordEngineProcessorIsolated)
chordengine_test(ChordEngineFunctionalTests SOURCES FunctionalTests.cpp LIBRARIES ChordEngineProcessorIsolated)

# Real-time safety: RtProbe.dylib counts allocations/frees/locks on an armed
# thread; CTest injects it. Sanitizer runtimes own malloc themselves, so the
# check runs only in the plain and hardened builds.
if(APPLE AND NOT CHORDENGINE_SANITIZER_FLAGS)
    add_library(ChordEngineRtProbe SHARED RtProbe.c)
    set_target_properties(ChordEngineRtProbe PROPERTIES C_VISIBILITY_PRESET default)
    chordengine_test(ChordEngineRealtimeSafetyTests SOURCES RealtimeSafetyTests.cpp
                     LIBRARIES ChordEngineProcessorIsolated)
    add_dependencies(ChordEngineRealtimeSafetyTests ChordEngineRtProbe)
    set_tests_properties(ChordEngineRealtimeSafetyTests PROPERTIES ENVIRONMENT
        "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ChordEngineRtProbe>;CFFIXED_USER_HOME=${CMAKE_BINARY_DIR}/TestHome/rt")
endif()

# No JUCE: the sources and the built bundles (macOS: symbols, linked
# libraries and strings of all three; Windows: the VST3's imports, exports
# and strings).
if(APPLE)
    add_test(NAME ChordEngineNoJuce
             COMMAND ${CMAKE_SOURCE_DIR}/Tools/CheckNoJuce.sh ${CMAKE_SOURCE_DIR}
                     $<TARGET_BUNDLE_DIR:ChordEngineAU> $<TARGET_BUNDLE_DIR:ChordEngineInstrumentAU>
                     $<TARGET_BUNDLE_DIR:ChordEngineVST3>)
else()
    find_program(CHORDENGINE_BASH bash HINTS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin")
    if(CHORDENGINE_BASH)
        get_filename_component(CHORDENGINE_LINKER_DIR "${CMAKE_LINKER}" DIRECTORY)
        find_program(CHORDENGINE_DUMPBIN dumpbin HINTS "${CHORDENGINE_LINKER_DIR}")
        add_test(NAME ChordEngineNoJuce COMMAND ${CHORDENGINE_BASH} ${CMAKE_SOURCE_DIR}/Tools/CheckNoJuce.sh ${CMAKE_SOURCE_DIR}
                 ${CHORDENGINE_BUNDLE_DIR}/ChordEngine.vst3)
        set_tests_properties(ChordEngineNoJuce PROPERTIES ENVIRONMENT "DUMPBIN=${CHORDENGINE_DUMPBIN}")
    endif()
endif()

# Host-side tests on the built bundles. The full upstream VST 3 SDK (hosting
# helpers and Steinberg's validator) comes from Tools/FetchSdk.sh.
set(CHORDENGINE_FULL_VST3SDK ${CMAKE_SOURCE_DIR}/Build/sdk-src/vst3sdk CACHE PATH "Full VST 3 SDK for test tools")
set(CHORDENGINE_VST3_VALIDATOR ${CMAKE_SOURCE_DIR}/Build/sdk-src/vst3sdk-build/bin/Release/validator
    CACHE FILEPATH "Steinberg's VST3 validator")
if(APPLE)
    # Bracket arguments keep the quotes; ";" would split a CMake list, so the
    # escaped pack name is matched by its prefix.
    add_test(NAME ChordEngineAUValidateMidiFx
             COMMAND ChordEngineAUProbe --validate $<TARGET_BUNDLE_DIR:ChordEngineAU>
                     --fixture ${CHORDENGINE_TEST_DIR}/Fixtures/State051/au-midifx-custom.aupreset
                     --expect [=[keyIndex="3"]=] --expect [=[scale="dorian"]=] --expect [=[chordPreset="neoSoul"]=]
                     --expect [=[fixedVelocity="77"]=] --expect [=[packName="Test &amp]=]
                     --expect [=[recordQuantize="thirdBeat"]=] --expect [=[followHostRecord="on"]=]
                     --fixture ${CHORDENGINE_TEST_DIR}/Fixtures/State051/au-midifx-default.aupreset)
    add_test(NAME ChordEngineAUValidateInstrument
             COMMAND ChordEngineAUProbe --validate $<TARGET_BUNDLE_DIR:ChordEngineInstrumentAU>
                     --fixture ${CHORDENGINE_TEST_DIR}/Fixtures/State051/au-instrument-custom.aupreset
                     --expect [=[keyIndex="3"]=] --expect [=[internalSound="bass"]=] --expect [=[followHostRecord="on"]=]
                     --fixture ${CHORDENGINE_TEST_DIR}/Fixtures/State051/au-instrument-default.aupreset)
    set_tests_properties(ChordEngineAUValidateMidiFx ChordEngineAUValidateInstrument PROPERTIES
        ENVIRONMENT "CFFIXED_USER_HOME=${CMAKE_BINARY_DIR}/TestHome/au")

    if(EXISTS ${CHORDENGINE_FULL_VST3SDK}/public.sdk/source/vst/hosting/module.cpp)
        set(S ${CHORDENGINE_FULL_VST3SDK})
        add_executable(ChordEngineVST3HostTest VST3HostTest.mm
            ${S}/public.sdk/source/vst/hosting/module.cpp ${S}/public.sdk/source/vst/hosting/module_mac.mm
            ${S}/public.sdk/source/vst/hosting/hostclasses.cpp ${S}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
            ${S}/public.sdk/source/vst/hosting/eventlist.cpp ${S}/public.sdk/source/vst/hosting/parameterchanges.cpp
            ${S}/public.sdk/source/vst/hosting/processdata.cpp ${S}/public.sdk/source/vst/utility/stringconvert.cpp
            ${S}/public.sdk/source/common/memorystream.cpp ${S}/public.sdk/source/common/commonstringconvert.cpp
            ${S}/public.sdk/source/vst/vstinitiids.cpp ${S}/pluginterfaces/base/funknown.cpp
            ${S}/pluginterfaces/base/coreiids.cpp ${S}/pluginterfaces/base/conststringtable.cpp
            ${S}/pluginterfaces/base/ustring.cpp ${S}/base/source/fobject.cpp ${S}/base/source/baseiids.cpp
            ${S}/base/source/fdebug.cpp ${S}/base/source/updatehandler.cpp ${S}/base/source/fstring.cpp
            ${S}/base/thread/source/flock.cpp)
        target_include_directories(ChordEngineVST3HostTest PRIVATE ${S})
        target_compile_definitions(ChordEngineVST3HostTest PRIVATE RELEASE=1 ${CHORDENGINE_VERSION_DEFINITIONS})
        target_compile_options(ChordEngineVST3HostTest PRIVATE -fobjc-arc -w)
        target_link_libraries(ChordEngineVST3HostTest PRIVATE "-framework Foundation" "-framework CoreFoundation")
        set_source_files_properties(VST3HostTest.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        add_test(NAME ChordEngineVST3HostTest
                 COMMAND ChordEngineVST3HostTest $<TARGET_BUNDLE_DIR:ChordEngineVST3> ${CHORDENGINE_TEST_DIR}/Fixtures)
        set(CE_VST3_ENV "CFFIXED_USER_HOME=${CMAKE_BINARY_DIR}/TestHome/vst3")
        if(TARGET ChordEngineRtProbe)
            add_dependencies(ChordEngineVST3HostTest ChordEngineRtProbe)
            set(CE_VST3_ENV "${CE_VST3_ENV};DYLD_INSERT_LIBRARIES=$<TARGET_FILE:ChordEngineRtProbe>")
        endif()
        set_tests_properties(ChordEngineVST3HostTest PROPERTIES ENVIRONMENT "${CE_VST3_ENV}")
    endif()

    if(EXISTS ${CHORDENGINE_VST3_VALIDATOR})
        add_test(NAME ChordEngineVST3Validator COMMAND ${CHORDENGINE_VST3_VALIDATOR} $<TARGET_BUNDLE_DIR:ChordEngineVST3>)
        set_tests_properties(ChordEngineVST3Validator PROPERTIES
            ENVIRONMENT "CFFIXED_USER_HOME=${CMAKE_BINARY_DIR}/TestHome/validator"
            FAIL_REGULAR_EXPRESSION "tests failed[^\\n]*[1-9]|ERROR")
    endif()
endif()

if(APPLE)
    add_test(NAME ChordEngineBundleMetadata
             COMMAND ${CMAKE_SOURCE_DIR}/Tools/CheckBundles.sh ${CHORDENGINE_VERSION}
                     $<TARGET_BUNDLE_DIR:ChordEngineAU> $<TARGET_BUNDLE_DIR:ChordEngineInstrumentAU>
                     $<TARGET_BUNDLE_DIR:ChordEngineVST3>)
endif()
