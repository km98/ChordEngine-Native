#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace chordengine::ui
{
// Starts an operating-system drag of one file from `source`. Must be called
// from that component's mouseDown/mouseDrag.
//
// On macOS this replaces juce::DragAndDropContainer::performExternalDragDropOfFiles,
// which offers the drop target only NSDragOperationCopy. FL Studio's playlist
// refuses a drop it cannot perform with its own operation, so the file is
// offered the way Finder offers it (copy, generic, link and move; never
// delete). `onEnded` receives the operation the target performed (0 = the
// drop was refused or cancelled), on the message thread.
//
// Elsewhere it forwards to JUCE and reports -1 as the operation.
bool startExternalFileDrag(const juce::File& file, juce::Component& source,
                           std::function<void(int operation)> onEnded);
} // namespace chordengine::ui
