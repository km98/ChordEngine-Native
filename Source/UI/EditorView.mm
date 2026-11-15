// =====================================================================
// The ChordEngine editor's AppKit back end: the root view, the EditorRoot
// that gives every widget its own flipped NSView, the licence key's
// NSTextField, the pack list's NSScrollView, and the AppKit-facing API in
// EditorHost.h. Everything else (controls, layout, logic) is the
// platform-neutral EditorController.
// =====================================================================
#import <AppKit/AppKit.h>

#include "EditorController.h"
#include "EditorHost.h"
#include "EditorStyleApple.h"
#include "EditorWidgetsApple.h"
#include "TakeDrag.h"

#include <algorithm>
#include <cmath>

#define CEEditorRootView CE_OBJC(CEEditorRootView)
#define CEKeyFieldDelegate CE_OBJC(CEKeyFieldDelegate)

namespace chordengine::ui
{
class AppleEditorRoot;
}

@interface CEEditorRootView : NSView
- (chordengine::ui::EditorController*)controller;
- (void)setRoot:(std::unique_ptr<chordengine::ui::AppleEditorRoot>)root
     controller:(std::unique_ptr<chordengine::ui::EditorController>)controller;
- (void)releaseController;
@end

@interface CEKeyFieldDelegate : NSObject <NSTextFieldDelegate>
@property (nonatomic, assign) chordengine::ui::Widget* owner;
@end

@implementation CEKeyFieldDelegate
- (void)controlTextDidBeginEditing:(NSNotification*)notification
{
    (void) notification;
    if (self.owner != nullptr)
        self.owner->repaint();
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    (void) notification;
    if (self.owner != nullptr)
        self.owner->repaint();
}
@end

namespace chordengine::ui
{
namespace
{
namespace st = style;

std::vector<std::pair<EditorHost*, __weak NSView*>>& openViews()
{
    static std::vector<std::pair<EditorHost*, __weak NSView*>> views;
    return views;
}

// The licence key: a borderless NSTextField inside the key widget's view.
class AppleTextField final : public TextFieldPeer
{
public:
    explicit AppleTextField(Widget& owner)
    {
        field_ = [[NSTextField alloc] initWithFrame:NSZeroRect];
        field_.bordered = NO;
        field_.bezeled = NO;
        field_.drawsBackground = NO;
        field_.focusRingType = NSFocusRingTypeNone;
        field_.textColor = [NSColor colorWithSRGBRed:st::textBright.r() / 255.0 green:st::textBright.g() / 255.0
                                                blue:st::textBright.b() / 255.0 alpha:1.0];
        field_.cell.scrollable = YES;
        field_.cell.wraps = NO;
        field_.identifier = @"license-key-text";
        delegate_ = [[CEKeyFieldDelegate alloc] init];
        delegate_.owner = &owner;
        field_.delegate = delegate_;
        [viewOf(owner) addSubview:field_];
    }

    ~AppleTextField() override
    {
        delegate_.owner = nullptr;
        field_.delegate = nil;
        [field_ removeFromSuperview];
    }

    std::string text() const override { return field_.stringValue.UTF8String ?: ""; }
    void setText(const std::string& value) override { field_.stringValue = [NSString stringWithUTF8String:value.c_str()]; }
    void setTooltip(const std::string& tooltip) override
    {
        field_.toolTip = tooltip.empty() ? nil : [NSString stringWithUTF8String:tooltip.c_str()];
    }

    void applyFont(float height) override
    {
        const st::Font font { st::Face::mono, height, 0.06f };
        field_.font = (__bridge NSFont*) st::ctFont(font);
        const auto placeholder = st::faint.brighter(0.2f);
        field_.placeholderAttributedString = [[NSAttributedString alloc]
            initWithString:@"PC-XXXX-XXXX-XXXX-XXXX"
                attributes:@{ NSFontAttributeName : field_.font,
                               NSKernAttributeName : @(0.06 * height),
                               NSForegroundColorAttributeName : [NSColor colorWithSRGBRed:placeholder.r() / 255.0
                                                                                    green:placeholder.g() / 255.0
                                                                                     blue:placeholder.b() / 255.0
                                                                                    alpha:1.0] }];
    }

    void layout(Rect area, float indent, bool) override
    {
        const CGFloat lineHeight = std::ceil(field_.intrinsicContentSize.height);
        field_.frame = NSMakeRect(indent, std::round((area.height - lineHeight) * 0.5),
                                  std::max<CGFloat>(1, area.width - indent * 2), lineHeight);
    }

    bool hasFocus() const override { return field_.currentEditor != nil; }
    void focus() override { [field_.window makeFirstResponder:field_]; }

private:
    NSTextField* field_ = nil;
    CEKeyFieldDelegate* delegate_ = nil;
};

class AppleScrollArea final : public ScrollAreaPeer
{
public:
    AppleScrollArea(NSView* root, Widget& content)
    {
        view_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        view_.identifier = @"packs-viewport";
        view_.drawsBackground = NO;
        view_.hasVerticalScroller = YES;
        view_.autohidesScrollers = YES;
        view_.scrollerStyle = NSScrollerStyleOverlay;
        view_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        view_.documentView = attachAppleWidgetPeer(content);
        [root addSubview:view_];
    }
    ~AppleScrollArea() override { [view_ removeFromSuperview]; }

    void setFrame(Rect frame) override { view_.frame = NSRectFromCGRect(st::toCG(frame)); }
    Rect frame() const override { return st::fromCG(NSRectToCGRect(view_.frame)); }
    void setVisible(bool visible) override { view_.hidden = !visible; }
    bool isVisible() const override { return !view_.hidden; }
    float contentWidth() const override { return static_cast<float>(view_.contentSize.width); }

private:
    NSScrollView* view_ = nil;
};
} // namespace

class AppleEditorRoot final : public EditorRoot
{
public:
    explicit AppleEditorRoot(NSView* root) : root_(root) {}

    void addWidget(Widget& widget) override
    {
        NSView* root = root_;
        [root addSubview:attachAppleWidgetPeer(widget)];
    }

    std::unique_ptr<ScrollAreaPeer> createScrollArea(Widget& content) override
    {
        return std::make_unique<AppleScrollArea>(root_, content);
    }

    std::unique_ptr<TextFieldPeer> createTextField(Widget& owner) override
    {
        return std::make_unique<AppleTextField>(owner);
    }

    Size size() const override
    {
        NSView* root = root_;
        if (root == nil)
            return {};
        return { static_cast<float>(root.bounds.size.width), static_cast<float>(root.bounds.size.height) };
    }

    void setSize(int width, int height) override
    {
        NSView* root = root_;
        [root setFrameSize:NSMakeSize(width, height)];
    }

    void repaint() override
    {
        NSView* root = root_;
        [root setNeedsDisplay:YES];
    }

    bool startFileDrag(Widget& source, const MouseEvent& event, const std::filesystem::path& file,
                       std::function<void(int)> onEnded) override
    {
        return chordengine::ui::startFileDrag(viewOf(source), (__bridge NSEvent*) event.native, file, std::move(onEnded));
    }

    std::string processName() const override { return NSProcessInfo.processInfo.processName.UTF8String ?: ""; }

private:
    __weak NSView* root_;
};
} // namespace chordengine::ui

// =====================================================================
@implementation CEEditorRootView
{
    std::unique_ptr<chordengine::ui::AppleEditorRoot> root_;
    std::unique_ptr<chordengine::ui::EditorController> controller_;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)isOpaque
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void) event;
    return YES;
}

- (chordengine::ui::EditorController*)controller
{
    return controller_.get();
}

- (void)setRoot:(std::unique_ptr<chordengine::ui::AppleEditorRoot>)root
     controller:(std::unique_ptr<chordengine::ui::EditorController>)controller
{
    root_ = std::move(root);
    controller_ = std::move(controller);
}

- (void)releaseController
{
    if (controller_ != nullptr)
        controller_->detach();
}

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    if (controller_ != nullptr)
        controller_->resized();
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void) dirtyRect;
    if (controller_ != nullptr)
    {
        chordengine::ui::style::CGCanvas canvas(NSGraphicsContext.currentContext.CGContext);
        controller_->paint(canvas, chordengine::ui::style::fromCG(NSRectToCGRect(self.bounds)));
    }
}

- (void)dealloc
{
    controller_.reset();
    root_.reset();
}
@end

namespace chordengine::ui
{
namespace
{
CEEditorRootView* rootOf(NSView* view)
{
    return [view isKindOfClass:[CEEditorRootView class]] ? static_cast<CEEditorRootView*>(view) : nil;
}

EditorController* controllerOf(NSView* view)
{
    CEEditorRootView* root = rootOf(view);
    return root != nil ? [root controller] : nullptr;
}

Widget* widgetOf(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr ? controller->find(identifier) : nullptr;
}
} // namespace

NSView* createEditorViewWithOptions(EditorHost& host, int width, int height, EditorOptions options)
{
    CEEditorRootView* root = [[CEEditorRootView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    root.identifier = @"chordengine-editor";
    root.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    auto editorRoot = std::make_unique<AppleEditorRoot>(root);
    auto controller = std::make_unique<EditorController>(host, *editorRoot, std::move(options));
    [root setRoot:std::move(editorRoot) controller:std::move(controller)];
    openViews().emplace_back(&host, root);
    return root;
}

NSView* createEditorView(EditorHost& host, int width, int height)
{
    return createEditorViewWithOptions(host, width, height, EditorOptions {});
}

void detachEditorView(NSView* view)
{
    if (CEEditorRootView* root = rootOf(view))
        [root releaseController];
    auto& views = openViews();
    views.erase(std::remove_if(views.begin(), views.end(),
                               [view](const auto& entry) { NSView* open = entry.second; return open == nil || open == view; }),
                views.end());
}

void detachAllEditorViews(EditorHost* host)
{
    auto& views = openViews();
    for (auto& [owner, weakView] : views)
        if (owner == host)
            if (CEEditorRootView* root = rootOf(weakView))
                [root releaseController];
    views.erase(std::remove_if(views.begin(), views.end(),
                               [host](const auto& entry) { NSView* open = entry.second; return open == nil || entry.first == host; }),
                views.end());
}

// ---------------------------------------------------------------- access
bool editorHasControl(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && controller->hasControl(identifier);
}

bool clickEditorControl(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && controller->clickControl(identifier);
}

bool selectEditorItem(NSView* editor, const std::string& identifier, int itemId)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && controller->selectItem(identifier, itemId);
}

std::vector<int> editorItemIds(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr ? controller->itemIds(identifier) : std::vector<int> {};
}

int editorSelectedItem(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr ? controller->selectedItem(identifier) : -1;
}

std::string editorControlText(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr ? controller->controlText(identifier) : std::string {};
}

bool isEditorControlVisible(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    if (widget == nullptr)
        return false;
    for (NSView* view = viewOf(*widget); view != nil && view != editor; view = view.superview)
        if (view.hidden)
            return false;
    return true;
}

bool isEditorControlEnabled(NSView* editor, const std::string& identifier)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && controller->isControlEnabled(identifier);
}

CGRect editorControlBounds(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    NSView* view = widget != nullptr ? viewOf(*widget) : nil;
    if (view == nil)
        return CGRectNull;
    return NSRectToCGRect([view convertRect:view.bounds toView:editor]);
}

bool setEditorSliderValue(NSView* editor, const std::string& identifier, double value)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && controller->setSliderValue(identifier, value);
}

bool setEditorFieldText(NSView* editor, const std::string& identifier, const std::string& text)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && controller->setFieldText(identifier, text);
}

void refreshEditor(NSView* editor)
{
    if (auto* controller = controllerOf(editor))
        controller->timerCallback();
}

int pressEditorPiano(NSView* editor, CGPoint point)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr ? controller->pressPiano(st::fromCG(point)) : -1;
}

void releaseEditorPiano(NSView* editor)
{
    if (auto* controller = controllerOf(editor))
        controller->releasePiano();
}

CGPoint editorPianoKeyCentre(NSView* editor, int note)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr ? st::toCG(controller->pianoKeyCentre(note)) : CGPointZero;
}

std::filesystem::path writeEditorTakeFile(NSView* editor, int index)
{
    auto* controller = controllerOf(editor);
    return controller != nullptr && index >= 0 ? controller->writeRecorderTakeFile(static_cast<std::size_t>(index))
                                               : std::filesystem::path {};
}

void resizeEditorTo(NSView* editor, int width)
{
    if (auto* controller = controllerOf(editor))
        controller->resizeTo(static_cast<float>(width));
}

CGImageRef renderEditorImage(NSView* editor)
{
    [editor layoutSubtreeIfNeeded];
    // One pixel per point, as the 0.5 snapshots were rendered.
    const NSSize size = editor.bounds.size;
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:static_cast<NSInteger>(size.width)
                                                                    pixelsHigh:static_cast<NSInteger>(size.height)
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:0
                                                                  bitsPerPixel:0];
    rep.size = size;
    [editor cacheDisplayInRect:editor.bounds toBitmapImageRep:rep];
    CGImageRef image = rep.CGImage;
    return image != nullptr ? CGImageRetain(image) : nullptr;
}
} // namespace chordengine::ui
