#pragma once

#include <string>

class ChordEngineAudioProcessor;

namespace chordengine::ui
{
// What the editor needs from the format adapter that hosts it (AU or VST3).
// Every call is made on the main thread.
class EditorHost
{
public:
    virtual ~EditorHost() = default;
    virtual ChordEngineAudioProcessor& processor() = 0;
    // Asks the host to resize the editor's window to `width` x `height`
    // (points). Returns false when the host does not support it; the editor
    // then resizes itself and lets the host follow.
    virtual bool requestEditorSize(int width, int height) = 0;
    // A short host hint for wording ("Logic", "FL Studio", "" when unknown).
    virtual std::string hostName() const { return {}; }
};

// The editor geometry (points). The 800x650 design scales between the two
// bounds with its aspect ratio.
inline constexpr int editorDesignWidth = 800;
inline constexpr int editorDesignHeight = 650;
inline constexpr int editorMinimumWidth = 640;
inline constexpr int editorMinimumHeight = 520;
inline constexpr int editorMaximumWidth = 1280;
inline constexpr int editorMaximumHeight = 1040;
} // namespace chordengine::ui

#ifdef __OBJC__
#import <AppKit/AppKit.h>
namespace chordengine::ui
{
// The editor view (AppKit). The view keeps a pointer to `host`; the adapter
// must call detachEditorView() (or detachAllEditorViews() for its host)
// before the host goes away.
NSView* createEditorView(EditorHost& host, int width, int height);
void detachEditorView(NSView* view);
void detachAllEditorViews(EditorHost* host);
// The editor's preferred size for a given width (keeps the design aspect).
void editorSizeForWidth(int width, int& outWidth, int& outHeight);
} // namespace chordengine::ui
#endif

#ifdef __OBJC__
#include <filesystem>
#include <memory>
#include <vector>

#import <CoreGraphics/CoreGraphics.h>

#include "Packs/PackStoreClient.h"
#include "Updates/UpdateTransport.h"

namespace chordengine::ui
{
// Optional seams (development tools and tests): where takes, packs and the
// update cache live, and the network transports. Empty = production.
struct EditorOptions
{
    std::filesystem::path storageDirectory;
    std::unique_ptr<chordengine::updates::UpdateTransport> updateTransport;
    std::unique_ptr<chordengine::packs::PackStoreTransport> packTransport;
};
NSView* createEditorViewWithOptions(EditorHost& host, int width, int height, EditorOptions options);

// ---- programmatic access (tests, snapshot tool) ---------------------------
// Controls are addressed by their identifiers ("info-button", "key-selector",
// ...; see Docs/UI_NATIVE_EDITOR.md). Each call does what the user's click
// or choice would do, through the same code path.
bool editorHasControl(NSView* editor, const std::string& identifier);
bool clickEditorControl(NSView* editor, const std::string& identifier);
bool selectEditorItem(NSView* editor, const std::string& identifier, int itemId);
std::vector<int> editorItemIds(NSView* editor, const std::string& identifier);
int editorSelectedItem(NSView* editor, const std::string& identifier);
std::string editorControlText(NSView* editor, const std::string& identifier);
bool isEditorControlVisible(NSView* editor, const std::string& identifier);
bool isEditorControlEnabled(NSView* editor, const std::string& identifier);
CGRect editorControlBounds(NSView* editor, const std::string& identifier); // editor coordinates
bool setEditorSliderValue(NSView* editor, const std::string& identifier, double value);
bool setEditorFieldText(NSView* editor, const std::string& identifier, const std::string& text);
// One refresh step (what the 20 Hz timer does).
void refreshEditor(NSView* editor);
// Mouse on the piano at a point in editor coordinates; returns the note hit.
int pressEditorPiano(NSView* editor, CGPoint point);
void releaseEditorPiano(NSView* editor);
CGPoint editorPianoKeyCentre(NSView* editor, int note);
// The file a take drag would offer (written now); empty on failure.
std::filesystem::path writeEditorTakeFile(NSView* editor, int index);
// The user's resize gesture ends at this width (the height follows).
void resizeEditorTo(NSView* editor, int width);
// Renders the editor into an image (+1); the caller releases it.
CGImageRef renderEditorImage(NSView* editor);
} // namespace chordengine::ui
#endif
