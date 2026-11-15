#pragma once

// The AppKit peers of the editor's widgets (macOS only): one flipped NSView
// per widget that forwards drawing (through a CGCanvas) and mouse events.
#import <AppKit/AppKit.h>

#include "EditorStyleApple.h"
#include "EditorWidgets.h"
#include "Platform/ObjCNames.h"

#include <functional>
#include <memory>

#define CEWidgetView CE_OBJC(CEWidgetView)
#define CEActionTarget CE_OBJC(CEActionTarget)

@interface CEWidgetView : NSView
@property (nonatomic, assign) chordengine::ui::Widget* widget;
@end

// Runs a C++ callback from an AppKit target/action (menu items).
@interface CEActionTarget : NSObject
- (instancetype)initWithAction:(std::function<void(NSInteger)>)action;
- (void)invoke:(id)sender;
@end

namespace chordengine::ui
{
// Gives `widget` its AppKit view; the caller adds the view to the hierarchy.
NSView* attachAppleWidgetPeer(Widget& widget);
// The widget's view (nil without an AppKit peer).
NSView* viewOf(const Widget& widget);
NSCursor* nsCursor(Cursor cursor);
} // namespace chordengine::ui
