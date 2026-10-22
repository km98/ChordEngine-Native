#include "ExternalFileDrag.h"

// Non-Apple builds: JUCE's own external drag (the macOS version is in
// ExternalFileDrag.mm).
namespace chordengine::ui
{
bool startExternalFileDrag(const juce::File& file, juce::Component& source,
                           std::function<void(int operation)> onEnded)
{
    return juce::DragAndDropContainer::performExternalDragDropOfFiles(
        juce::StringArray(file.getFullPathName()), false, &source,
        [onEnded = std::move(onEnded)]
        {
            if (onEnded)
                onEnded(-1);
        });
}
} // namespace chordengine::ui
