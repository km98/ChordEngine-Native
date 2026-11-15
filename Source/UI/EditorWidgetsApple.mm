// The AppKit peers of the editor's widgets: one flipped NSView per widget
// (CEWidgetView) that draws the widget through a CGCanvas and forwards mouse
// events, tooltips, cursor rects and pop-up menus.
#include "EditorWidgetsApple.h"

#include <algorithm>
#include <cmath>

using namespace chordengine::ui;
namespace st = chordengine::ui::style;

namespace
{
MouseEvent mouseEvent(NSView* view, NSEvent* event)
{
    MouseEvent result;
    result.position = st::fromCG(NSPointToCGPoint([view convertPoint:event.locationInWindow fromView:nil]));
    const NSPoint screen = NSEvent.mouseLocation;
    result.screen = { static_cast<float>(screen.x), static_cast<float>(-screen.y) };
    result.native = (__bridge const void*) event;
    return result;
}
} // namespace

// ===================================================================== views
@implementation CEWidgetView
{
    NSTrackingArea* trackingArea_;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void) event;
    return YES;
}

- (NSView*)hitTest:(NSPoint)point
{
    if (self.widget == nullptr || !self.widget->interactive() || !self.widget->isEnabled() || self.hidden)
        return nil;
    return [super hitTest:point];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void) dirtyRect;
    if (self.widget == nullptr)
        return;
    st::CGCanvas canvas(NSGraphicsContext.currentContext.CGContext);
    // Applied while drawing (not as the view's alphaValue), so offscreen
    // renders and every host show the same dimmed state.
    canvas.beginLayer(self.widget->alpha());
    self.widget->paint(canvas, st::fromCG(NSRectToCGRect(self.bounds)));
    canvas.endLayer();
}

- (void)updateTrackingAreas
{
    if (trackingArea_ != nil)
        [self removeTrackingArea:trackingArea_];
    trackingArea_ = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                 options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved
                                                       | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:trackingArea_];
    [super updateTrackingAreas];
}

- (void)resetCursorRects
{
    if (self.widget != nullptr)
        if (NSCursor* cursor = nsCursor(self.widget->cursor()))
            [self addCursorRect:self.bounds cursor:cursor];
}

- (void)mouseEntered:(NSEvent*)event
{
    (void) event;
    if (self.widget == nullptr)
        return;
    self.widget->hover = true;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void) event;
    if (self.widget == nullptr)
        return;
    self.widget->hover = false;
    self.widget->mouseMove({ -1.0f, -1.0f });
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (self.widget != nullptr)
        self.widget->mouseMove(mouseEvent(self, event).position);
}

- (void)mouseDown:(NSEvent*)event
{
    if (self.widget == nullptr)
        return;
    if ((event.modifierFlags & NSEventModifierFlagControl) != 0)
    {
        self.widget->rightMouseDown(mouseEvent(self, event));
        return;
    }
    self.widget->pressed = true;
    self.widget->mouseDown(mouseEvent(self, event));
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.widget != nullptr)
        self.widget->mouseDrag(mouseEvent(self, event));
}

- (void)mouseUp:(NSEvent*)event
{
    if (self.widget == nullptr)
        return;
    self.widget->pressed = false;
    self.widget->mouseUp(mouseEvent(self, event));
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (self.widget != nullptr)
        self.widget->rightMouseDown(mouseEvent(self, event));
}
@end

@implementation CEActionTarget
{
    std::function<void(NSInteger)> action_;
}

- (instancetype)initWithAction:(std::function<void(NSInteger)>)action
{
    if ((self = [super init]))
        action_ = std::move(action);
    return self;
}

- (void)invoke:(id)sender
{
    if (action_)
        action_([sender respondsToSelector:@selector(tag)] ? [sender tag] : 0);
}
@end

namespace chordengine::ui
{
namespace
{
class AppleWidgetPeer final : public WidgetPeer
{
public:
    explicit AppleWidgetPeer(Widget& widget) : widget_(widget)
    {
        CEWidgetView* view = [[CEWidgetView alloc] initWithFrame:NSZeroRect];
        view.widget = &widget;
        view.identifier = [NSString stringWithUTF8String:widget.identifier().c_str()];
        view_ = view;
    }

    ~AppleWidgetPeer() override
    {
        static_cast<CEWidgetView*>(view_).widget = nullptr;
        [view_ removeFromSuperview];
    }

    NSView* view() const noexcept { return view_; }

    void boundsChanged() override
    {
        view_.frame = NSRectFromCGRect(st::toCG(widget_.bounds()));
        [view_.window invalidateCursorRectsForView:view_];
    }

    void visibilityChanged() override { view_.hidden = !widget_.isVisible(); }

    void tooltipChanged() override
    {
        const auto& tooltip = widget_.tooltip();
        view_.toolTip = tooltip.empty() ? nil : [NSString stringWithUTF8String:tooltip.c_str()];
    }

    void repaint() override { [view_ setNeedsDisplay:YES]; }

    int showMenu(const std::vector<MenuEntry>& entries, Point at, float fontHeight) override
    {
        NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
        menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        menu.autoenablesItems = NO;
        if (fontHeight > 0.0f)
            menu.font = (__bridge NSFont*) st::ctFont(st::uiFont(fontHeight));
        // The chosen item's action runs while the menu tracks (before
        // popUpMenuPositioningItem returns).
        auto chosen = std::make_shared<NSInteger>(0);
        CEActionTarget* target = [[CEActionTarget alloc] initWithAction:[chosen](NSInteger tag) { *chosen = tag; }];
        for (const auto& entry : entries)
        {
            if (entry.kind == MenuEntry::Kind::separator)
            {
                [menu addItem:[NSMenuItem separatorItem]];
                continue;
            }
            NSString* title = [NSString stringWithUTF8String:entry.text.c_str()];
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
            if (entry.kind == MenuEntry::Kind::heading)
            {
                item.enabled = NO;
                item.attributedTitle = [[NSAttributedString alloc]
                    initWithString:title
                        attributes:@{ NSFontAttributeName : (__bridge NSFont*) st::ctFont(st::captionFont(11.0f, 0.12f)) }];
            }
            else
            {
                item.action = @selector(invoke:);
                item.target = target;
                item.tag = entry.itemId;
                item.enabled = YES;
                item.state = entry.checked ? NSControlStateValueOn : NSControlStateValueOff;
            }
            [menu addItem:item];
        }
        [menu popUpMenuPositioningItem:nil atLocation:NSPointFromCGPoint(st::toCG(at)) inView:view_];
        return static_cast<int>(*chosen);
    }

private:
    Widget& widget_;
    NSView* view_ = nil;
};
} // namespace

NSView* attachAppleWidgetPeer(Widget& widget)
{
    auto peer = std::make_unique<AppleWidgetPeer>(widget);
    NSView* view = peer->view();
    widget.setPeer(std::move(peer));
    return view;
}

NSView* viewOf(const Widget& widget)
{
    auto* peer = dynamic_cast<AppleWidgetPeer*>(widget.peer());
    return peer != nullptr ? peer->view() : nil;
}

NSCursor* nsCursor(Cursor cursor)
{
    switch (cursor)
    {
        case Cursor::pointingHand: return [NSCursor pointingHandCursor];
        case Cursor::openHand: return [NSCursor openHandCursor];
        case Cursor::iBeam: return [NSCursor IBeamCursor];
        case Cursor::crosshair: return [NSCursor crosshairCursor];
        case Cursor::arrow: break;
    }
    return nil;
}
} // namespace chordengine::ui
