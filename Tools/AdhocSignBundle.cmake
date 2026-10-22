# ============================================================================
# AdhocSignBundle.cmake
#
# Final POST_BUILD step for the macOS AU and VST3 bundles (see
# chordengine_adhoc_sign_bundle in CMakeLists.txt). Run as a script:
#
#     cmake -Dbundle=<path to .component/.vst3> -P Tools/AdhocSignBundle.cmake
#
# Why it exists: the Xcode signing phase is disabled for this project, and
# JUCE's own ad-hoc step does not produce a valid bundle signature here -
#
#   * the AU only receives the linker's `-adhoc_codesign` signature, which
#     seals neither Info.plist nor any resource ("code has no resources but
#     signature indicates they must be present");
#   * the VST3 is re-signed by JUCE BEFORE its manifest step writes
#     Contents/Resources/moduleinfo.json, so that file is unsealed ("a sealed
#     resource is missing or invalid").
#
# This step runs after every other POST_BUILD command of the target, signs the
# COMPLETE bundle ad hoc and then refuses the build unless strict verification
# passes. It is repeatable: `--force` replaces whatever signature an earlier
# (incremental) build left behind.
#
# Ad hoc only. Developer ID signing (hardened runtime, secure timestamp)
# happens exclusively in Tools/PackageMacDMG.sh, on staged copies; this step
# never touches a Developer ID identity.
# ============================================================================

if(NOT bundle)
    message(FATAL_ERROR "AdhocSignBundle.cmake: pass -Dbundle=<bundle path>")
endif()

if(NOT IS_DIRECTORY "${bundle}")
    message(FATAL_ERROR "AdhocSignBundle.cmake: bundle not found: ${bundle}")
endif()

find_program(CHORDENGINE_CODESIGN codesign PATHS /usr/bin NO_DEFAULT_PATH)

if(NOT CHORDENGINE_CODESIGN)
    message(FATAL_ERROR "AdhocSignBundle.cmake: /usr/bin/codesign not found")
endif()

# The bundles carry a single Mach-O (universal arm64 + x86_64) and no nested
# code, so signing the bundle itself signs every architecture slice and seals
# Info.plist and Contents/Resources in one pass.
execute_process(
    COMMAND "${CHORDENGINE_CODESIGN}" --force --sign - --timestamp=none "${bundle}"
    RESULT_VARIABLE sign_result
    OUTPUT_VARIABLE sign_output
    ERROR_VARIABLE sign_output)

if(NOT sign_result EQUAL 0)
    message(FATAL_ERROR "ad hoc signing failed for ${bundle}:\n${sign_output}")
endif()

execute_process(
    COMMAND "${CHORDENGINE_CODESIGN}" --verify --deep --strict "${bundle}"
    RESULT_VARIABLE verify_result
    OUTPUT_VARIABLE verify_output
    ERROR_VARIABLE verify_output)

if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "strict signature verification failed for ${bundle}:\n${verify_output}")
endif()

message(STATUS "ad hoc signed and strictly verified: ${bundle}")
