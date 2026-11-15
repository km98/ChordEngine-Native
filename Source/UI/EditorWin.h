#pragma once

// =====================================================================
// The ChordEngine editor's Windows back end: one child window (HWND) drawn
// with Direct2D / DirectWrite. Every control is a platform-neutral widget
// (EditorWidgets.h) painted and hit-tested by this window; the licence key is
// a native EDIT control so typing, pasting (Ctrl+V or the right-click menu)
// and IME work as everywhere else in Windows.
//
// Sizes: the editor lays out in points (the 800 x 650 design); the window is
// in pixels. `scale` is pixels per point (the monitor's DPI / 96, or the
// factor a VST3 host sets through IPlugViewContentScaleSupport).
// =====================================================================

#include "EditorHost.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace chordengine::ui
{
class EditorController;
class WinEditor;

// Creates the editor as a child of `parent` (an HWND) with a client area of
// pixelWidth x pixelHeight. scale <= 0: the parent's DPI. The editor keeps a
// pointer to `host`; call destroyWinEditor() (or detachAllWinEditors()) before
// the host goes away.
WinEditor* createWinEditor(EditorHost& host, void* parent, int pixelWidth, int pixelHeight, float scale,
                           EditorOptions options = {});
// Detaches the controller (nothing reaches the host any more) and destroys
// the window.
void destroyWinEditor(WinEditor* editor);
// Detaches every editor of `host` (the plug-in is going away) without
// destroying windows the host still owns.
void detachAllWinEditors(EditorHost* host);

void* winEditorWindow(WinEditor* editor);
EditorController* winEditorController(WinEditor* editor);
float winEditorScale(WinEditor* editor);
// A new pixels-per-point factor (the window keeps its pixel size until the
// host resizes it).
void setWinEditorScale(WinEditor* editor, float scale);
void setWinEditorPixelSize(WinEditor* editor, int pixelWidth, int pixelHeight);
// Called when the editor's monitor DPI changes and no host scale was set;
// receives the new scale. The VST3 view uses it to resize the host window.
void setWinEditorScaleListener(WinEditor* editor, std::function<void(float)> listener);

// Pixels per point for a window (its monitor's DPI / 96; the system DPI for
// a null window). 1 when the process or thread is not DPI aware.
float windowScaleFactor(void* window);

// Renders the editor offscreen at `scale` (1 = one pixel per point) into
// 32-bit BGRA (premultiplied, top-down rows).
bool renderWinEditor(WinEditor* editor, float scale, std::vector<std::uint8_t>& bgra, int& width, int& height);
bool writeWinEditorPng(WinEditor* editor, const std::filesystem::path& file, float scale = 1.0f);
// Writes BGRA pixels (as renderWinEditor returns them) as a PNG.
bool writePng(const std::vector<std::uint8_t>& bgra, int width, int height, const std::filesystem::path& file);

// Starts an OLE drag of one file (CF_HDROP and the shell's formats, as
// Explorer offers it); blocks until the drop. `onEnded` receives the
// DROPEFFECT performed (0 = refused or cancelled), on the main thread.
bool startFileDragWin(const std::filesystem::path& file, std::function<void(int operation)> onEnded);
} // namespace chordengine::ui
