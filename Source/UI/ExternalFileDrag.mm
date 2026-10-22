// AppKit first: JUCE's Objective-C helpers (ObjCClass, juceStringToNS) are
// only declared when Foundation is already visible.
#import <AppKit/AppKit.h>
#include <objc/message.h>
#include <objc/runtime.h>

#define JUCE_CORE_INCLUDE_OBJC_HELPERS 1
#include "ExternalFileDrag.h"

namespace chordengine::ui
{
namespace
{
NSView* viewFor(juce::Component& component)
{
    if (auto* peer = component.getPeer())
        return static_cast<NSView*>(peer->getNativeHandle());
    return nil;
}

// What a target may do with the dragged take. Finder offers the same set for
// a file it owns; delete is never offered.
constexpr NSDragOperation offeredOperations = NSDragOperationCopy | NSDragOperationGeneric
                                            | NSDragOperationLink | NSDragOperationMove;

// The dragging source. Registered under a per-binary unique class name (JUCE's
// ObjCClass) so the AU and the VST3 loaded into one host never share it.
class FileDragSource final : public juce::ObjCClass<NSObject<NSDraggingSource>>
{
public:
    struct Members
    {
        std::function<void(int)> onEnded;
        juce::Component::SafePointer<juce::Component> originator;
    };

    static id create(Members members)
    {
        static FileDragSource source;
        id instance = [[source.createInstance() init] autorelease];
        object_setInstanceVariable(instance, "members", new Members(std::move(members)));
        return instance;
    }

private:
    static Members* membersOf(id self) { return juce::getIvar<Members*>(self, "members"); }

    FileDragSource()
        : ObjCClass("ChordEngineFileDragSource_")
    {
        addIvar<Members*>("members");

        addMethod(@selector(dealloc), [](id self, SEL)
        {
            delete membersOf(self);
            sendSuperclassMessage<void>(self, @selector(dealloc));
        });

        addMethod(@selector(draggingSession:sourceOperationMaskForDraggingContext:),
                  [](id, SEL, NSDraggingSession*, NSDraggingContext) -> NSDragOperation
        {
            return offeredOperations;
        });

        addMethod(@selector(ignoreModifierKeysForDraggingSession:),
                  [](id, SEL, NSDraggingSession*) -> BOOL { return YES; });

        addMethod(@selector(draggingSession:endedAtPoint:operation:),
                  [](id self, SEL, NSDraggingSession*, NSPoint point, NSDragOperation operation)
        {
            auto* members = membersOf(self);
            if (members == nullptr)
                return;

            // The originating view gets no mouse-up once a drag session takes
            // over, so one is sent here (as JUCE does) to end the gesture.
            if (members->originator != nullptr)
                if (auto* view = viewFor(*members->originator))
                    if (auto* cgEvent = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp,
                                                                CGPointMake(point.x, point.y),
                                                                kCGMouseButtonLeft))
                    {
                        if (NSEvent* event = [NSEvent eventWithCGEvent: cgEvent])
                            [view mouseUp: event];
                        CFRelease(cgEvent);
                    }

            if (auto onEnded = members->onEnded)
            {
                const auto performed = static_cast<int>(operation);
                juce::MessageManager::callAsync([onEnded, performed] { onEnded(performed); });
            }
        });

        addProtocol(@protocol(NSDraggingSource));
        registerClass();
    }
};
} // namespace

bool startExternalFileDrag(const juce::File& file, juce::Component& source,
                           std::function<void(int operation)> onEnded)
{
    if (!file.existsAsFile())
        return false;

    NSView* view = viewFor(source);
    if (view == nil)
        return false;

    JUCE_AUTORELEASEPOOL
    {
        NSEvent* event = [[view window] currentEvent];
        if (event == nil)
            return false;
        const auto type = [event type];
        const bool mouseEvent = type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged
            || type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseDragged
            || type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseDragged;
        if (!mouseEvent)
            return false; // AppKit only starts a drag session from a mouse event

        NSString* path = juce::juceStringToNS(file.getFullPathName());
        NSURL* url = [NSURL fileURLWithPath: path];
        NSDraggingItem* item = [[[NSDraggingItem alloc] initWithPasteboardWriter: url] autorelease];
        const NSPoint location = [view convertPoint: [event locationInWindow] fromView: nil];
        [item setDraggingFrame: NSMakeRect(location.x - 16.0, location.y - 16.0, 32.0, 32.0)
                      contents: [[NSWorkspace sharedWorkspace] iconForFile: path]];

        id dragSource = FileDragSource::create({ std::move(onEnded), &source });
        NSDraggingSession* session = [view beginDraggingSessionWithItems: [NSArray arrayWithObject: item]
                                                                   event: event
                                                                  source: dragSource];
        if (session == nil)
            return false;
        session.animatesToStartingPositionsOnCancelOrFail = YES;
        session.draggingFormation = NSDraggingFormationNone;
        return true;
    }
}
} // namespace chordengine::ui
