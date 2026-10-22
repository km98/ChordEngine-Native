#pragma once

#import <AppKit/AppKit.h>

#include <filesystem>
#include <functional>

namespace chordengine::ui
{
// Starts an OS drag of `file` from `view`; must be called from the view's
// mouse-down or mouse-dragged handling with that `event`. `onEnded` receives
// the operation the drop target performed (0 = refused or cancelled), on the
// main thread.
bool startFileDrag(NSView* view, NSEvent* event, const std::filesystem::path& file,
                   std::function<void(int operation)> onEnded);
} // namespace chordengine::ui
