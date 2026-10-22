// An operating-system drag of one file (a recorded MIDI take) out of the
// editor. The file is offered the way Finder offers a file it owns - copy,
// generic, link and move, never delete - because FL Studio's playlist refuses
// a drop it cannot perform with its own operation.
#import <AppKit/AppKit.h>

#include "Platform/ObjCNames.h"
#include "TakeDrag.h"

#define CEFileDragSource CE_OBJC(CEFileDragSource)

@interface CEFileDragSource : NSObject <NSDraggingSource>
- (instancetype)initWithEnded:(std::function<void(int)>)ended;
@end

@implementation CEFileDragSource
{
    std::function<void(int)> ended_;
}

- (instancetype)initWithEnded:(std::function<void(int)>)ended
{
    if ((self = [super init]))
        ended_ = std::move(ended);
    return self;
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    (void) session;
    (void) context;
    return NSDragOperationCopy | NSDragOperationGeneric | NSDragOperationLink | NSDragOperationMove;
}

- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session
{
    (void) session;
    return YES;
}

- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)point operation:(NSDragOperation)operation
{
    (void) session;
    (void) point;
    if (ended_)
    {
        auto ended = ended_;
        const int performed = static_cast<int>(operation);
        dispatch_async(dispatch_get_main_queue(), ^{ ended(performed); });
    }
}
@end

namespace chordengine::ui
{
bool startFileDrag(NSView* view, NSEvent* event, const std::filesystem::path& file,
                   std::function<void(int operation)> onEnded)
{
    std::error_code error;
    if (view == nil || event == nil || !std::filesystem::is_regular_file(file, error))
        return false;
    const auto type = event.type;
    const bool mouseEvent = type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged
        || type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseDragged
        || type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseDragged;
    if (!mouseEvent)
        return false; // AppKit starts a drag session only from a mouse event

    @autoreleasepool
    {
        NSString* path = [NSString stringWithUTF8String:file.c_str()];
        NSURL* url = [NSURL fileURLWithPath:path];
        NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
        const NSPoint location = [view convertPoint:event.locationInWindow fromView:nil];
        [item setDraggingFrame:NSMakeRect(location.x - 16.0, location.y - 16.0, 32.0, 32.0)
                      contents:[[NSWorkspace sharedWorkspace] iconForFile:path]];
        CEFileDragSource* source = [[CEFileDragSource alloc] initWithEnded:std::move(onEnded)];
        NSDraggingSession* session = [view beginDraggingSessionWithItems:@[ item ] event:event source:source];
        if (session == nil)
            return false;
        session.animatesToStartingPositionsOnCancelOrFail = YES;
        session.draggingFormation = NSDraggingFormationNone;
        return true;
    }
}
} // namespace chordengine::ui
