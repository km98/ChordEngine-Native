#include "EditorWidgets.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

using namespace chordengine::ui;
namespace st = chordengine::ui::style;

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
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(context);
    // Applied while drawing (not as the view's alphaValue), so offscreen
    // renders and every host show the same dimmed state.
    CGContextSetAlpha(context, self.widget->alpha());
    CGContextBeginTransparencyLayer(context, nullptr);
    self.widget->paint(context, self.bounds);
    CGContextEndTransparencyLayer(context);
    CGContextRestoreGState(context);
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
        if (NSCursor* cursor = self.widget->cursor())
            [self addCursorRect:self.bounds cursor:cursor];
}

- (CGPoint)local:(NSEvent*)event
{
    return NSPointToCGPoint([self convertPoint:event.locationInWindow fromView:nil]);
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
    self.widget->mouseMove(CGPointMake(-1, -1));
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (self.widget != nullptr)
        self.widget->mouseMove([self local:event]);
}

- (void)mouseDown:(NSEvent*)event
{
    if (self.widget == nullptr)
        return;
    if ((event.modifierFlags & NSEventModifierFlagControl) != 0)
    {
        self.widget->rightMouseDown([self local:event], event);
        return;
    }
    self.widget->pressed = true;
    self.widget->mouseDown([self local:event], event);
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (self.widget != nullptr)
        self.widget->mouseDrag([self local:event], event);
}

- (void)mouseUp:(NSEvent*)event
{
    if (self.widget == nullptr)
        return;
    self.widget->pressed = false;
    self.widget->mouseUp([self local:event], event);
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (self.widget != nullptr)
        self.widget->rightMouseDown([self local:event], event);
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
std::string noteName(int midiNote)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string(names[((midiNote % 12) + 12) % 12]) + std::to_string(midiNote / 12 - 1);
}

// ==================================================================== widget
Widget::Widget(std::string identifier) : identifier_(std::move(identifier))
{
    CEWidgetView* view = [[CEWidgetView alloc] initWithFrame:NSZeroRect];
    view.widget = this;
    view.identifier = [NSString stringWithUTF8String:identifier_.c_str()];
    view_ = view;
}

Widget::~Widget()
{
    static_cast<CEWidgetView*>(view_).widget = nullptr;
    [view_ removeFromSuperview];
}

void Widget::setBounds(CGRect bounds)
{
    view_.frame = NSRectFromCGRect(bounds);
    [view_.window invalidateCursorRectsForView:view_];
    repaint();
}

CGRect Widget::bounds() const
{
    return NSRectToCGRect(view_.frame);
}

CGRect Widget::localBounds() const
{
    return CGRectMake(0, 0, view_.frame.size.width, view_.frame.size.height);
}

void Widget::setVisible(bool visible)
{
    visible_ = visible;
    view_.hidden = !visible;
}

void Widget::setEnabled(bool enabled)
{
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    repaint();
}

void Widget::setAlpha(float alpha)
{
    if (alpha == alpha_)
        return;
    alpha_ = alpha;
    repaint();
}

void Widget::setTooltip(const std::string& tooltip)
{
    view_.toolTip = tooltip.empty() ? nil : [NSString stringWithUTF8String:tooltip.c_str()];
}

void Widget::repaint()
{
    [view_ setNeedsDisplay:YES];
}

// ===================================================================== label
Label::Label(std::string identifier, Mode mode) : Widget(std::move(identifier)), mode_(mode) {}

void Label::setText(const std::string& value)
{
    if (value == text_)
        return;
    text_ = value;
    repaint();
}

void Label::setColour(st::Colour colour)
{
    if (colour.argb == colour_.argb)
        return;
    colour_ = colour;
    repaint();
}

void Label::setFont(const st::Font& font)
{
    font_ = font;
    repaint();
}

void Label::setLive(bool live)
{
    if (live == live_)
        return;
    live_ = live;
    repaint();
}

void Label::paint(CGContextRef context, CGRect bounds)
{
    if (mode_ == Mode::infoRow)
        return paintInfoRow(context, bounds);
    if (mode_ == Mode::noteTokens)
        return paintNoteTokens(context, bounds);
    const int lines = std::max(1, static_cast<int>(bounds.size.height / std::max(1.0f, font_.height)));
    st::drawFitted(context, text_, bounds, font_, isEnabled() ? colour_ : colour_.withMultipliedAlpha(0.5f),
                   justification_, lines, minimumScale_);
}

void Label::paintInfoRow(CGContextRef context, CGRect bounds)
{
    // "Key  value" rows: the key in a fixed column in the quiet tone, the
    // value from one shared edge, wrapping inside its column.
    const auto separator = text_.find("  ");
    const float width = static_cast<float>(bounds.size.width);
    const bool hasKey = separator != std::string::npos && separator > 0;
    const float keyColumn = hasKey ? std::min(width * 0.4f, font_.height * 8.6f) : 0.0f;
    const float rowPitch = std::min(font_.height * 1.5f, static_cast<float>(bounds.size.height));
    const float topEdge = std::max(0.0f, (rowPitch - font_.height) * 0.5f);
    if (hasKey)
    {
        // The key keeps the row's face (0.5 set it "plain" on a fixed face,
        // which kept a semibold row's key semibold), in the quiet tone.
        const st::Font keyFont = font_;
        st::drawWrapped(context, text_.substr(0, separator), 0.0f, topEdge, keyColumn, keyFont, st::muted,
                        keyFont.height);
    }
    const auto value = hasKey ? text_.substr(separator + 2) : text_;
    st::drawWrapped(context, value, keyColumn, topEdge, std::max(1.0f, width - keyColumn), font_, colour_,
                    font_.height);
}

void Label::paintNoteTokens(CGContextRef context, CGRect bounds)
{
    // The chord's notes as a typeset sequence: each pitch class in a medium
    // weight with its octave as a small figure beside it; accent while live.
    const float height = static_cast<float>(bounds.size.height);
    const float scale = height / 26.0f;
    const auto pitchFont = st::uiFont(std::max(7.0f, 17.0f * scale), true, 0.01f);
    const auto octaveFont = st::uiFont(std::max(7.0f, 11.5f * scale), false, 0.02f);
    const float gap = 20.0f * scale;
    const float baseline = std::round(height * 0.5f + st::ascent(pitchFont) * 0.38f);
    float x = 0.0f;
    std::size_t start = 0;
    while (start < text_.size())
    {
        auto end = text_.find(' ', start);
        if (end == std::string::npos)
            end = text_.size();
        const auto note = text_.substr(start, end - start);
        start = end + 1;
        if (note.empty())
            continue;
        std::size_t split = note.size();
        while (split > 0 && (std::isdigit(static_cast<unsigned char>(note[split - 1])) || note[split - 1] == '-'))
            --split;
        const auto pitch = note.substr(0, split);
        const auto octave = note.substr(split);
        const float pitchWidth = st::stringWidth(pitchFont, pitch);
        const float octaveWidth = st::stringWidth(octaveFont, octave);
        if (x + pitchWidth + octaveWidth > static_cast<float>(bounds.size.width))
            break;
        st::drawLine(context, pitch, pitchFont, live_ ? st::accent : st::textBright, std::round(x), baseline);
        st::drawLine(context, octave, octaveFont, live_ ? st::accent.withAlpha(0.7f) : st::dim,
                     std::round(x + pitchWidth + 1.5f * scale), baseline);
        x += pitchWidth + octaveWidth + gap;
    }
}

// ================================================================ pill button
PillButton::PillButton(std::string identifier, std::string text, float cornerRadius, float designHeight,
                       float designFontHeight)
    : Widget(std::move(identifier)), text_(std::move(text)), cornerRadius_(cornerRadius),
      designHeight_(designHeight), designFontHeight_(designFontHeight)
{
}

void PillButton::setText(const std::string& text)
{
    if (text == text_)
        return;
    text_ = text;
    repaint();
}

void PillButton::setPalette(st::Colour fill, st::Colour textColour, st::Colour outline, st::Colour pressedFill)
{
    fill_ = fill;
    textColour_ = textColour;
    outline_ = outline;
    pressedFill_ = pressedFill;
    repaint();
}

void PillButton::setIndicator(st::Colour colour)
{
    indicator_ = colour;
    repaint();
}

void PillButton::mouseDown(CGPoint, NSEvent*) {}

void PillButton::mouseUp(CGPoint position, NSEvent*)
{
    if (CGRectContainsPoint(localBounds(), position))
        activate();
}

void PillButton::activate()
{
    if (!isEnabled())
        return;
    if (togglesOnClick_)
        toggle_ = !toggle_;
    if (onClick)
        onClick();
}

void PillButton::paint(CGContextRef context, CGRect area)
{
    const float height = static_cast<float>(area.size.height);
    const float scale = height / std::max(1.0f, designHeight_);
    const float radius = std::min(cornerRadius_ * scale, height * 0.5f);
    const bool enabled = isEnabled();

    auto fill = fill_;
    if (enabled && pressed)
        fill = pressedFill_.isTransparent() ? fill_.darker(0.15f) : pressedFill_;
    else if (enabled && hover)
        fill = fill_.isTransparent() ? st::white.withAlpha(0.04f) : fill_.brighter(0.07f);
    if (!fill.isTransparent())
        st::fillRoundedRect(context, area, radius, enabled ? fill : fill.withMultipliedAlpha(0.45f));
    if (!outline_.isTransparent())
        st::strokeRoundedRect(context, CGRectInset(area, 0.5, 0.5), radius, 1.0f,
                              enabled ? outline_ : outline_.withMultipliedAlpha(0.45f));

    const auto font = st::uiFont(std::max(6.0f, designFontHeight_ * scale), true, 0.08f);
    CGRect content = CGRectInset(area, std::round(4.0f * scale), 0);
    const auto colour = enabled ? textColour_ : textColour_.withMultipliedAlpha(0.5f);
    if (!indicator_.isTransparent())
    {
        const float dot = 6.0f * scale;
        const float gap = 7.0f * scale;
        const float textWidth = st::stringWidth(font, text_);
        const float unitWidth = std::min(static_cast<float>(content.size.width), dot + gap + textWidth);
        const float leftEdge = static_cast<float>(content.origin.x) + (static_cast<float>(content.size.width) - unitWidth) * 0.5f;
        st::fillEllipse(context, CGRectMake(leftEdge, CGRectGetMidY(area) - dot * 0.5f, dot, dot),
                        enabled ? indicator_ : indicator_.withMultipliedAlpha(0.5f));
        const CGRect textBox = CGRectMake(std::round(leftEdge + dot + gap), area.origin.y,
                                          std::round(unitWidth - dot - gap) + 2, area.size.height);
        st::drawFitted(context, text_, textBox, font, colour, st::centredLeft, 1, 0.85f);
        return;
    }
    st::drawFitted(context, text_, content, font, colour, st::centred, 1, 0.85f);
}

// ================================================================ step button
StepButton::StepButton(std::string identifier, Glyph glyph) : Widget(std::move(identifier)), glyph_(glyph) {}

void StepButton::mouseDown(CGPoint, NSEvent*) {}

void StepButton::mouseUp(CGPoint position, NSEvent*)
{
    if (CGRectContainsPoint(localBounds(), position))
        activate();
}

void StepButton::activate()
{
    if (isEnabled() && onClick)
        onClick();
}

void StepButton::paint(CGContextRef context, CGRect area)
{
    const float side = static_cast<float>(std::min(area.size.width, area.size.height));
    const CGRect circle = CGRectInset(CGRectMake(CGRectGetMidX(area) - side * 0.5f, CGRectGetMidY(area) - side * 0.5f,
                                                 side, side), 0.5, 0.5);
    const float scale = side / 28.0f;
    if (pressed || hover)
        st::fillEllipse(context, circle, pressed ? st::accentSoft : st::raisedHover);
    st::strokeEllipse(context, circle, 1.0f, pressed ? st::accent : hover ? st::border.brighter(0.3f) : st::border);

    const auto colour = isEnabled() ? (hover || pressed ? st::textBright : st::text) : st::dim.withAlpha(0.6f);
    const float cx = static_cast<float>(CGRectGetMidX(area));
    const float cy = static_cast<float>(CGRectGetMidY(area));
    const float barLength = std::max(4.0f, 9.0f * scale);
    const float barThickness = std::max(1.0f, 1.4f * scale);
    if (glyph_ == Glyph::minus || glyph_ == Glyph::plus)
    {
        st::fillRect(context, CGRectMake(cx - barLength * 0.5f, cy - barThickness * 0.5f, barLength, barThickness), colour);
        if (glyph_ == Glyph::plus)
            st::fillRect(context, CGRectMake(cx - barThickness * 0.5f, cy - barLength * 0.5f, barThickness, barLength), colour);
        return;
    }
    const float half = 3.6f * scale;
    const float direction = glyph_ == Glyph::next ? 1.0f : -1.0f;
    st::strokePolyline(context, { CGPointMake(cx - direction * half * 0.45f, cy - half),
                                  CGPointMake(cx + direction * half * 0.55f, cy),
                                  CGPointMake(cx - direction * half * 0.45f, cy + half) },
                       barThickness, colour);
}

// ================================================================== take row
TakeRow::TakeRow(std::string identifier) : Widget(std::move(identifier)) {}

void TakeRow::setTake(int takeNumber, int noteCount)
{
    if (takeNumber == takeNumber_ && noteCount == noteCount_)
        return;
    takeNumber_ = takeNumber;
    noteCount_ = noteCount;
    repaint();
}

void TakeRow::mouseDown(CGPoint position, NSEvent*)
{
    downAt_ = position;
    dragStarted_ = false;
}

void TakeRow::mouseDrag(CGPoint position, NSEvent* event)
{
    if (dragStarted_ || std::hypot(position.x - downAt_.x, position.y - downAt_.y) < 4.0)
        return;
    dragStarted_ = true;
    if (onDragStart && !onDragStart(event))
        dragStarted_ = false;
}

void TakeRow::mouseUp(CGPoint, NSEvent*)
{
    dragStarted_ = false;
}

void TakeRow::rightMouseDown(CGPoint position, NSEvent*)
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    auto reveal = onReveal;
    menuTarget_ = [[CEActionTarget alloc] initWithAction:[reveal](NSInteger) { if (reveal) reveal(); }];
    NSMenuItem* item = [menu addItemWithTitle:@"Show in Finder" action:@selector(invoke:) keyEquivalent:@""];
    item.target = menuTarget_;
    [menu popUpMenuPositioningItem:nil atLocation:NSPointFromCGPoint(position) inView:view_];
}

void TakeRow::paint(CGContextRef context, CGRect area)
{
    const float scale = static_cast<float>(area.size.height) / 20.0f;
    if (hover || pressed)
        st::fillRoundedRect(context, area, 3.0f * scale, pressed ? st::accentSoft : st::white.withAlpha(0.04f));
    st::fillRect(context, CGRectMake(area.origin.x, CGRectGetMaxY(area) - 1.0, area.size.width, 1.0), st::hairline);

    const auto label = st::uiFont(std::max(6.0f, 9.5f * scale), true, 0.08f);
    const auto quiet = st::uiFont(std::max(6.0f, 9.5f * scale), false, 0.04f);
    const float arrowWidth = 9.0f * scale;
    const float arrowX = static_cast<float>(CGRectGetMaxX(area)) - arrowWidth;
    const float cy = static_cast<float>(CGRectGetMidY(area));
    const float stroke = std::max(1.0f, 1.2f * scale);
    st::strokePolyline(context, { CGPointMake(arrowX, cy), CGPointMake(arrowX + arrowWidth, cy) }, stroke, st::accent);
    st::strokePolyline(context, { CGPointMake(arrowX + arrowWidth - 3.0f * scale, cy - 3.0f * scale),
                                  CGPointMake(arrowX + arrowWidth, cy),
                                  CGPointMake(arrowX + arrowWidth - 3.0f * scale, cy + 3.0f * scale) },
                       stroke, st::accent);
    st::drawFitted(context, "DRAG", CGRectMake(area.origin.x, area.origin.y, std::round(arrowX - 5.0f * scale) - area.origin.x,
                                               area.size.height),
                   label, st::accent, st::centredRight, 1, 1.0f);
    if (takeNumber_ > 0)
    {
        const auto takeText = "TAKE " + std::to_string(takeNumber_);
        st::drawFitted(context, takeText, area, label, st::text, st::centredLeft, 1, 1.0f);
        const float takeWidth = st::stringWidth(label, takeText);
        CGRect rest = area;
        rest.origin.x += std::round(takeWidth + 8.0f * scale);
        rest.size.width -= std::round(takeWidth + 8.0f * scale);
        st::drawFitted(context, std::to_string(noteCount_) + (noteCount_ == 1 ? " note" : " notes"), rest, quiet,
                       st::muted, st::centredLeft, 1, 1.0f);
    }
}

// =================================================================== combo
ComboBox::ComboBox(std::string identifier) : Widget(std::move(identifier)) {}

void ComboBox::clear()
{
    entries_.clear();
    selectedId_ = 0;
    freeText_.clear();
    repaint();
}

void ComboBox::addItem(const std::string& text, int itemId)
{
    entries_.push_back({ Entry::Kind::item, text, itemId });
}

void ComboBox::addSectionHeading(const std::string& text)
{
    entries_.push_back({ Entry::Kind::heading, text, 0 });
}

void ComboBox::addSeparator()
{
    entries_.push_back({ Entry::Kind::separator, {}, 0 });
}

void ComboBox::setSelectedId(int itemId)
{
    if (selectedId_ == itemId)
        return;
    selectedId_ = itemId;
    freeText_.clear();
    repaint();
}

void ComboBox::setText(const std::string& text)
{
    selectedId_ = 0;
    freeText_ = text;
    repaint();
}

std::string ComboBox::displayedText() const
{
    for (const auto& entry : entries_)
        if (entry.kind == Entry::Kind::item && entry.itemId == selectedId_)
            return entry.text;
    return freeText_;
}

std::vector<int> ComboBox::itemIds() const
{
    std::vector<int> ids;
    for (const auto& entry : entries_)
        if (entry.kind == Entry::Kind::item)
            ids.push_back(entry.itemId);
    return ids;
}

bool ComboBox::selectItem(int itemId)
{
    for (const auto& entry : entries_)
        if (entry.kind == Entry::Kind::item && entry.itemId == itemId)
        {
            const bool changed = selectedId_ != itemId;
            selectedId_ = itemId;
            freeText_.clear();
            repaint();
            if (changed && onChange)
                onChange();
            return true;
        }
    return false;
}

void ComboBox::mouseDown(CGPoint, NSEvent*)
{
    if (!isEnabled())
        return;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    menu.autoenablesItems = NO;
    menu.font = (__bridge NSFont*) st::ctFont(st::uiFont(std::max(14.0f, fontHeight_ * 1.15f)));
    menuTarget_ = [[CEActionTarget alloc] initWithAction:[this](NSInteger tag) { selectItem(static_cast<int>(tag)); }];
    for (const auto& entry : entries_)
    {
        if (entry.kind == Entry::Kind::separator)
        {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSString* title = [NSString stringWithUTF8String:entry.text.c_str()];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        if (entry.kind == Entry::Kind::heading)
        {
            item.enabled = NO;
            item.attributedTitle = [[NSAttributedString alloc]
                initWithString:title
                    attributes:@{ NSFontAttributeName : (__bridge NSFont*) st::ctFont(st::captionFont(11.0f, 0.12f)) }];
        }
        else
        {
            item.action = @selector(invoke:);
            item.target = menuTarget_;
            item.tag = entry.itemId;
            item.enabled = YES;
            item.state = entry.itemId == selectedId_ ? NSControlStateValueOn : NSControlStateValueOff;
        }
        [menu addItem:item];
    }
    menuOpen_ = true;
    repaint();
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, view_.bounds.size.height + 2) inView:view_];
    menuOpen_ = false;
    pressed = false;
    repaint();
}

void ComboBox::paint(CGContextRef context, CGRect area)
{
    // A selector is a line of text with a rule under it: no box.
    const float height = static_cast<float>(area.size.height);
    const float scale = height / 30.0f;
    const bool focused = menuOpen_;
    if (hover || pressed)
        st::fillRoundedRect(context, area, 4.0f * scale, st::white.withAlpha(pressed ? 0.05f : 0.03f));
    auto rule = hover ? st::border.brighter(0.35f) : st::border;
    if (emphasis_)
        rule = st::accent.withAlpha(hover ? 0.75f : 0.5f);
    if (focused)
        rule = st::accent;
    const float ruleHeight = focused ? std::max(1.0f, 1.5f * scale) : 1.0f;
    st::fillRect(context, CGRectMake(area.origin.x, CGRectGetMaxY(area) - ruleHeight, area.size.width, ruleHeight),
                 isEnabled() ? rule : rule.withMultipliedAlpha(0.5f));

    const float chevronWidth = 6.0f * scale;
    const float chevronHeight = 3.5f * scale;
    const float cx = static_cast<float>(CGRectGetMaxX(area)) - 8.0f * scale;
    const float cy = static_cast<float>(CGRectGetMidY(area));
    st::strokePolyline(context, { CGPointMake(cx - chevronWidth * 0.5f, cy - chevronHeight * 0.5f),
                                  CGPointMake(cx, cy + chevronHeight * 0.5f),
                                  CGPointMake(cx + chevronWidth * 0.5f, cy - chevronHeight * 0.5f) },
                       std::max(1.0f, 1.3f * scale), focused ? st::accent : hover ? st::text : st::dim);

    const auto font = emphasis_ ? st::uiFont(fontHeight_ * 1.42f, true, -0.005f) : st::uiFont(fontHeight_ * 1.04f);
    const CGRect textArea = CGRectMake(area.origin.x, area.origin.y,
                                       std::max(1.0, area.size.width - std::round(20.0f * scale)), area.size.height);
    const int lines = std::max(1, static_cast<int>(height / font.height));
    st::drawFitted(context, displayedText(), textArea, font, isEnabled() ? st::text : st::text.withMultipliedAlpha(0.5f),
                   st::centredLeft, lines, 0.0f);
}

// ==================================================================== slider
Slider::Slider(std::string identifier, double minimum, double maximum)
    : Widget(std::move(identifier)), minimum_(minimum), maximum_(maximum), value_(minimum)
{
}

float Slider::thumbRadius() const
{
    return std::max(4.0f, std::round(static_cast<float>(localBounds().size.height) * 0.2f));
}

void Slider::setValue(double value)
{
    value = std::clamp(std::round(value), minimum_, maximum_);
    if (value == value_)
        return;
    value_ = value;
    repaint();
}

bool Slider::setValueFromUser(double value)
{
    value = std::clamp(std::round(value), minimum_, maximum_);
    if (value == value_)
        return false;
    value_ = value;
    repaint();
    if (onValueChange)
        onValueChange();
    return true;
}

void Slider::setFromPosition(float x)
{
    const float indent = thumbRadius();
    const float width = std::max(1.0f, static_cast<float>(localBounds().size.width) - indent * 2.0f);
    const double proportion = std::clamp((x - indent) / width, 0.0f, 1.0f);
    setValueFromUser(minimum_ + proportion * (maximum_ - minimum_));
}

void Slider::mouseDown(CGPoint position, NSEvent*)
{
    if (isEnabled())
        setFromPosition(static_cast<float>(position.x));
}

void Slider::mouseDrag(CGPoint position, NSEvent*)
{
    if (isEnabled())
        setFromPosition(static_cast<float>(position.x));
}

void Slider::mouseUp(CGPoint, NSEvent*) {}

void Slider::paint(CGContextRef context, CGRect area)
{
    const float indent = thumbRadius();
    const float x = indent;
    const float width = std::max(1.0f, static_cast<float>(area.size.width) - indent * 2.0f);
    const float height = static_cast<float>(area.size.height);
    const float scale = height / 28.0f;
    const float trackHeight = std::max(1.0f, 2.0f * scale);
    const float centreY = height * 0.5f;
    const float position = x + static_cast<float>((value_ - minimum_) / (maximum_ - minimum_)) * width;
    const CGRect trackArea = CGRectMake(x, centreY - trackHeight * 0.5f, width, trackHeight);
    st::fillRoundedRect(context, trackArea, trackHeight * 0.5f, st::track);
    st::fillRoundedRect(context, CGRectMake(x, trackArea.origin.y, position - x, trackHeight), trackHeight * 0.5f, st::accent);
    const float radius = thumbRadius();
    const CGRect thumb = CGRectMake(position - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
    st::fillEllipse(context, thumb, st::textBright);
    st::strokeEllipse(context, CGRectInset(thumb, 0.5, 0.5), 1.0f, st::background.withAlpha(0.6f));
}

// ============================================================= status pill
void StatusPill::paint(CGContextRef context, CGRect area)
{
    // "N/A": the MIDI FX build cannot sound; this is a status, not a switch.
    const float scale = static_cast<float>(area.size.height) / 28.0f;
    st::strokeRoundedRect(context, CGRectInset(area, 0.5, 0.5), 4.0f * scale, 1.0f, st::hairline.brighter(0.1f));
    st::drawFitted(context, "N/A", area, st::uiFont(std::max(6.0f, 9.0f * scale), true, 0.1f), st::dim, st::centred, 1, 1.0f);
}

// ================================================================= wordmark
void Wordmark::paint(CGContextRef context, CGRect area)
{
    // The ink of the word is centred on the header's centre line; it starts
    // exactly on the page's left edge.
    const float scale = static_cast<float>(area.size.height) / 40.0f;
    st::Font font { st::Face::wordmark, 27.0f * scale, 0.0f };
    CFStringRef string = CFSTR("Chord Engine");
    const void* keys[] = { kCTFontAttributeName };
    const void* values[] = { st::ctFont(font) };
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(nullptr, string, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    const CGRect ink = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
    CFRelease(line);
    CFRelease(attributed);
    CFRelease(attributes);
    if (CGRectIsEmpty(ink))
        return;
    const float x = static_cast<float>(-ink.origin.x);
    const float baseline = static_cast<float>(area.size.height) * 0.5f + static_cast<float>(ink.origin.y + ink.size.height * 0.5);
    st::drawLine(context, "Chord Engine", font, st::text, x, baseline);
}

// ==================================================================== piano
Piano::Piano(ChordEngineAudioProcessor& processor) : Widget("piano-display"), processor_(&processor) {}

Piano::~Piano()
{
    releaseHeldNote();
}

CGRect Piano::keyboardBounds() const
{
    // The keys run edge to edge; the top 6 px (design) hold the felt rail.
    const CGRect local = localBounds();
    const float scale = static_cast<float>(local.size.width) / 720.0f;
    return CGRectMake(0, 6.0f * scale, local.size.width, local.size.height - 6.0f * scale);
}

bool Piano::isWhiteKey(int note)
{
    switch (note % 12)
    {
        case 0: case 2: case 4: case 5: case 7: case 9: case 11: return true;
        default: return false;
    }
}

int Piano::whiteKeyCount() const
{
    int count = 0;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
        count += isWhiteKey(note) ? 1 : 0;
    return count;
}

int Piano::whiteKeysBefore(int note) const
{
    int count = 0;
    for (int current = ChordEngineAudioProcessor::pianoFirstNote; current < note; ++current)
        count += isWhiteKey(current) ? 1 : 0;
    return count;
}

float Piano::whiteKeyWidth() const
{
    return static_cast<float>(keyboardBounds().size.width) / static_cast<float>(std::max(1, whiteKeyCount()));
}

CGRect Piano::whiteKeyRect(int whiteIndex) const
{
    const CGRect keys = keyboardBounds();
    const float width = whiteKeyWidth();
    return CGRectMake(keys.origin.x + whiteIndex * width, keys.origin.y, width - 1.0f, keys.size.height);
}

CGRect Piano::blackKeyRect(int note) const
{
    const CGRect keys = keyboardBounds();
    const float width = whiteKeyWidth();
    const float x = static_cast<float>(keys.origin.x) + whiteKeysBefore(note) * width - width * 0.29f;
    return CGRectMake(x, keys.origin.y, width * 0.58f, keys.size.height * 0.62f);
}

int Piano::noteAtPosition(CGPoint position) const
{
    if (!CGRectContainsPoint(keyboardBounds(), position))
        return -1;
    float nearestBlack = 1.0e9f;
    int blackNote = -1;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;
        const CGRect black = blackKeyRect(note);
        if (CGRectContainsPoint(black, position))
        {
            const float distance = std::abs(static_cast<float>(position.x - CGRectGetMidX(black)));
            if (distance < nearestBlack)
            {
                nearestBlack = distance;
                blackNote = note;
            }
        }
    }
    if (blackNote >= 0)
        return blackNote;
    int whiteIndex = 0;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (!isWhiteKey(note))
            continue;
        if (CGRectContainsPoint(whiteKeyRect(whiteIndex), position))
            return note;
        ++whiteIndex;
    }
    return -1;
}

void Piano::pressNote(int note)
{
    if (note == heldNote_)
        return;
    releaseHeldNote();
    if (processor_ == nullptr || note < ChordEngineAudioProcessor::pianoFirstNote
        || note > ChordEngineAudioProcessor::pianoLastNote)
        return;
    heldNote_ = note;
    processor_->pushUiNoteOn(ChordEngineAudioProcessor::uiPianoChannel, note, ChordEngineAudioProcessor::uiPianoVelocity);
    repaint();
}

void Piano::releaseHeldNote()
{
    if (heldNote_ < 0)
        return;
    if (processor_ != nullptr)
        processor_->pushUiNoteOff(ChordEngineAudioProcessor::uiPianoChannel, heldNote_);
    heldNote_ = -1;
    repaint();
}

void Piano::mouseDown(CGPoint position, NSEvent*)
{
    const int note = noteAtPosition(position);
    if (note >= 0)
        pressNote(note);
}

void Piano::mouseDrag(CGPoint position, NSEvent*)
{
    // Sliding onto another key releases the old note and sounds the new one;
    // sliding off the keys releases without a retrigger.
    if (heldNote_ < 0)
        return;
    const int note = noteAtPosition(position);
    if (note == heldNote_)
        return;
    if (note >= 0)
        pressNote(note);
    else
        releaseHeldNote();
}

void Piano::mouseUp(CGPoint, NSEvent*)
{
    releaseHeldNote();
}

void Piano::setPreviewNotes(const chordengine::core::NoteSet& notes)
{
    previewNotes_.fill(false);
    for (std::size_t index = 0; index < notes.size; ++index)
        previewNotes_[notes.notes[index]] = true;
    repaint();
}

void Piano::paint(CGContextRef context, CGRect area)
{
    const float scale = static_cast<float>(area.size.width) / 720.0f;
    const CGRect keys = keyboardBounds();
    st::fillRect(context, CGRectMake(0, 0, area.size.width, keys.origin.y), st::pianoBed);
    st::fillRect(context, CGRectMake(0, 0, area.size.width, 1.0), st::accent.withAlpha(0.35f));

    const float labelHeight = std::max(8.0f, 11.0f * scale);
    const float keyRadius = 3.0f * scale;
    const auto labelFont = st::uiFont(std::max(6.0f, 8.5f * scale), true);
    const auto stateOf = [this](int note)
    {
        if (processor_ == nullptr)
            return std::array<bool, 3> { note == heldNote_, false, previewNotes_[static_cast<std::size_t>(note)] };
        const auto n = static_cast<std::uint8_t>(note);
        const bool trigger = note == heldNote_ || processor_->isAnyTriggerNoteDown(n) || processor_->isAnyTriggerNoteActive(n);
        const bool generated = processor_->isAnyOutputNoteActive(n);
        return std::array<bool, 3> { trigger, generated, previewNotes_[static_cast<std::size_t>(note)] };
    };

    int whiteIndex = 0;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (!isWhiteKey(note))
            continue;
        const CGRect key = whiteKeyRect(whiteIndex);
        const auto [trigger, generated, preview] = stateOf(note);
        const bool lit = trigger || generated;
        CGPathRef shape = st::createBottomRoundedPath(key, keyRadius);
        if (lit)
        {
            const auto colour = trigger ? st::triggerWhiteKey : st::chordWhiteKey;
            st::fillPathVerticalGradient(context, shape, key, colour, colour);
        }
        else
            st::fillPathVerticalGradient(context, shape, key, preview ? st::previewWhite : st::whiteKey,
                                         preview ? st::previewWhite.darker(0.04f) : st::whiteKeyShade);
        CGPathRelease(shape);
        if (note % 12 == 0)
            st::drawFitted(context, noteName(note),
                           CGRectMake(key.origin.x, CGRectGetMaxY(key) - labelHeight - 3.0f * scale, key.size.width, labelHeight),
                           labelFont, lit ? st::onAccent : preview ? st::onAccent.withAlpha(0.7f) : st::whiteKeyLabel,
                           st::centred, 1, 1.0f);
        ++whiteIndex;
    }

    st::fillRect(context, CGRectMake(keys.origin.x, keys.origin.y, keys.size.width, 2.0f * scale), st::black.withAlpha(0.18f));

    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;
        const CGRect key = blackKeyRect(note);
        const auto [trigger, generated, preview] = stateOf(note);
        CGPathRef shape = st::createBottomRoundedPath(key, keyRadius);
        if (trigger || generated)
        {
            const auto colour = trigger ? st::triggerBlackKey : st::chordBlackKey;
            st::fillPathVerticalGradient(context, shape, key, colour, colour);
        }
        else
            st::fillPathVerticalGradient(context, shape, key, preview ? st::previewBlack.brighter(0.15f) : st::blackKeyTop,
                                         preview ? st::previewBlack : st::blackKey);
        CGPathRelease(shape);
    }
}

// ================================================================== packs
void PackList::setListings(std::vector<chordengine::packs::PackListing> listings, std::string busySlug)
{
    listings_ = std::move(listings);
    busySlug_ = std::move(busySlug);
    hoverRow_ = -1;
    repaint();
}

void PackList::setScale(float scale)
{
    scale_ = std::max(0.1f, scale);
    repaint();
}

int PackList::rowHeight() const
{
    return static_cast<int>(std::lround(designRowHeight * scale_));
}

std::string PackList::actionText(const chordengine::packs::PackListing& listing, bool busy)
{
    using chordengine::packs::PackStatus;
    if (busy)
        return "DOWNLOADING";
    switch (listing.status)
    {
        case PackStatus::builtIn: return "BUILT IN";
        case PackStatus::installed: return "INSTALLED";
        case PackStatus::updateAvailable: return "UPDATE";
        case PackStatus::available: return "DOWNLOAD";
        case PackStatus::requiresMembership: return "GET MUSIC-PROD+";
        case PackStatus::signInRequired: return "SIGN IN";
    }
    return {};
}

CGRect PackList::actionBounds(int row) const
{
    const int height = rowHeight();
    const float width = std::round(140.0f * scale_);
    const float buttonHeight = std::round(28.0f * scale_);
    return CGRectMake(localBounds().size.width - width - std::round(8.0f * scale_),
                      row * height + std::floor((height - buttonHeight) / 2.0f), width, buttonHeight);
}

int PackList::actionRowAt(CGPoint position) const
{
    for (int row = 0; row < static_cast<int>(listings_.size()); ++row)
        if (CGRectContainsPoint(actionBounds(row), position))
            return row;
    return -1;
}

void PackList::mouseMove(CGPoint position)
{
    const int row = actionRowAt(position);
    if (row != hoverRow_)
    {
        hoverRow_ = row;
        repaint();
    }
}

void PackList::mouseUp(CGPoint position, NSEvent*)
{
    const int row = actionRowAt(position);
    if (row >= 0 && onAction)
        onAction(row);
}

void PackList::paint(CGContextRef context, CGRect area)
{
    using chordengine::packs::PackStatus;
    const int height = rowHeight();
    const float width = static_cast<float>(area.size.width);
    for (int row = 0; row < static_cast<int>(listings_.size()); ++row)
    {
        const auto& listing = listings_[static_cast<std::size_t>(row)];
        const float y = static_cast<float>(row * height);
        st::fillRect(context, CGRectMake(0, y + height - 1, width, 1), st::hairline);

        const float tileSize = std::round(40.0f * scale_);
        const CGRect tile = CGRectMake(0, y + std::floor((height - tileSize) / 2.0f), tileSize, tileSize);
        const bool owned = listing.status == PackStatus::builtIn || listing.status == PackStatus::installed
            || listing.status == PackStatus::updateAvailable;
        st::fillRoundedRect(context, tile, 6.0f * scale_, owned ? st::accent.withAlpha(0.18f) : st::raised);
        std::string initials;
        std::size_t start = 0;
        while (start < listing.name.size() && initials.size() < 2)
        {
            auto end = listing.name.find(' ', start);
            if (end == std::string::npos)
                end = listing.name.size();
            const auto word = listing.name.substr(start, end - start);
            if (!word.empty() && word != "ChordEngine")
                initials += static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
            start = end + 1;
        }
        st::drawFitted(context, initials, tile, st::captionFont(13.0f * scale_, 0.04f), owned ? st::accent : st::dim,
                       st::centred, 1, 1.0f);

        const CGRect action = actionBounds(row);
        const float textX = std::round(56.0f * scale_);
        const float textWidth = static_cast<float>(action.origin.x) - textX - std::round(12.0f * scale_);
        st::drawFitted(context, listing.name, CGRectMake(textX, y + std::round(13.0f * scale_), textWidth, std::round(18.0f * scale_)),
                       st::uiFont(14.0f * scale_, true), st::textBright, st::centredLeft, 1, 1.0f);
        const bool usableStatus = owned || listing.status == PackStatus::available;
        const auto detail = !listing.description.empty() ? listing.statusText + "  -  " + listing.description : listing.statusText;
        st::drawFitted(context, detail, CGRectMake(textX, y + std::round(34.0f * scale_), textWidth, std::round(16.0f * scale_)),
                       st::uiFont(11.5f * scale_), usableStatus ? st::muted : st::accent, st::centredLeft, 1, 1.0f);

        const bool busy = listing.slug == busySlug_;
        const bool primary = listing.status == PackStatus::available || listing.status == PackStatus::updateAvailable
            || listing.status == PackStatus::signInRequired;
        const bool quiet = listing.status == PackStatus::builtIn || listing.status == PackStatus::installed;
        const bool hot = row == hoverRow_ && !busy;
        st::Colour labelColour = st::text;
        if (primary && !busy)
        {
            st::fillRoundedRect(context, action, 4.0f * scale_, hot ? st::accent.brighter(0.1f) : st::accent);
            labelColour = st::onAccent;
        }
        else
        {
            if (hot)
                st::fillRoundedRect(context, action, 4.0f * scale_, st::raisedHover);
            st::strokeRoundedRect(context, CGRectInset(action, 0.5, 0.5), 4.0f * scale_, 1.0f,
                                  quiet ? st::hairline.brighter(0.2f) : st::border);
            labelColour = quiet ? st::dim : st::text;
        }
        st::drawFitted(context, actionText(listing, busy), action, st::uiFont(9.5f * scale_, true, 0.08f), labelColour,
                       st::centred, 1, 1.0f);
    }
}
} // namespace chordengine::ui
