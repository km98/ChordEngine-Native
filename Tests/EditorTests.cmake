# The AppKit editor's tests and its offscreen snapshot tool (nothing here ships).
# Included by Tests/CMakeLists.txt.
if(APPLE)
    set(CHORDENGINE_EDITOR_TEST_DEFINITIONS
        CHORDENGINE_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/Resources"
        ${CHORDENGINE_VERSION_DEFINITIONS})

    chordengine_test(ChordEngineEditorTests
        SOURCES EditorTests.mm ${CHORDENGINE_EDITOR_SOURCES}
        LIBRARIES ChordEngineProcessorIsolated ${CHORDENGINE_EDITOR_FRAMEWORKS}
        DEFINITIONS ${CHORDENGINE_EDITOR_TEST_DEFINITIONS})
    target_compile_options(ChordEngineEditorTests PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

    # Renders the editor to PNG for the same states and sizes the 0.5.1 tool
    # did, and composes before/after comparisons with Docs/Screenshots/0.5.1.
    add_executable(ChordEngineEditorSnapshot ${CMAKE_SOURCE_DIR}/Tools/EditorSnapshot.mm
                   ${CHORDENGINE_EDITOR_SOURCES} ${CHORDENGINE_PLATFORM_APPLE})
    target_include_directories(ChordEngineEditorSnapshot PRIVATE ${CMAKE_SOURCE_DIR}/Source)
    target_compile_options(ChordEngineEditorSnapshot PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    target_compile_definitions(ChordEngineEditorSnapshot PRIVATE
        CHORDENGINE_OBJC_SUFFIX=EditorSnapshot
        CHORDENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        ${CHORDENGINE_EDITOR_TEST_DEFINITIONS})
    target_link_libraries(ChordEngineEditorSnapshot PRIVATE ChordEngineProcessorIsolated ${CHORDENGINE_EDITOR_FRAMEWORKS}
                                                            "-framework ImageIO")
endif()
