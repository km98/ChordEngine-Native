#pragma once

// =====================================================================
// The editor's controls: small C++ widgets, each backed by one AppKit view
// (CEWidgetView) that forwards drawing and mouse events. Drawing uses the
// 0.5 redesign's rules (EditorStyle.h); a widget scales with its own bounds,
// exactly as the 0.5 controls did.
// =====================================================================

#import <AppKit/AppKit.h>

#include "Core/ChordEngineCore.h"
#include "EditorStyle.h"
#include "Packs/PackStoreClient.h"
#include "Platform/ObjCNames.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

class ChordEngineAudioProcessor;

#define CEWidgetView CE_OBJC(CEWidgetView)
#define CEActionTarget CE_OBJC(CEActionTarget)

namespace chordengine::ui
{
class Widget;
}

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
namespace st = style;

class Widget
{
public:
    explicit Widget(std::string identifier);
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    NSView* view() const noexcept { return view_; }
    const std::string& identifier() const noexcept { return identifier_; }

    void setBounds(CGRect bounds);
    CGRect bounds() const;        // in the parent's coordinates
    CGRect localBounds() const;   // origin 0,0
    void setVisible(bool visible);
    bool isVisible() const noexcept { return visible_; }
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return enabled_; }
    void setAlpha(float alpha);
    float alpha() const noexcept { return alpha_; }
    void setTooltip(const std::string& tooltip);
    void repaint();

    // Mouse handling. `interactive` false makes the view transparent to clicks.
    virtual bool interactive() const { return true; }
    virtual void paint(CGContextRef context, CGRect bounds) = 0;
    virtual void mouseDown(CGPoint, NSEvent*) {}
    virtual void mouseDrag(CGPoint, NSEvent*) {}
    virtual void mouseUp(CGPoint, NSEvent*) {}
    virtual void rightMouseDown(CGPoint, NSEvent*) {}
    virtual void mouseMove(CGPoint) {}
    virtual NSCursor* cursor() const { return nil; }

    // Programmatic activation (tests, accessibility): what a click does.
    virtual void activate() {}

    bool hover = false;
    bool pressed = false;

protected:
    NSView* view_ = nil;

private:
    std::string identifier_;
    float alpha_ = 1.0f;
    bool visible_ = true;
    bool enabled_ = true;
};

// ---------------------------------------------------------------- label
class Label final : public Widget
{
public:
    enum class Mode { plain, infoRow, noteTokens };
    explicit Label(std::string identifier, Mode mode = Mode::plain);

    void setText(const std::string& text);
    const std::string& text() const noexcept { return text_; }
    void setColour(st::Colour colour);
    st::Colour colour() const noexcept { return colour_; }
    void setFont(const st::Font& font);
    const st::Font& font() const noexcept { return font_; }
    void setJustification(unsigned justification) { justification_ = justification; repaint(); }
    void setMinimumHorizontalScale(float scale) { minimumScale_ = scale; repaint(); }
    void setLive(bool live);
    bool interactive() const override { return false; }
    void paint(CGContextRef context, CGRect bounds) override;

private:
    void paintInfoRow(CGContextRef context, CGRect bounds);
    void paintNoteTokens(CGContextRef context, CGRect bounds);
    Mode mode_;
    std::string text_;
    st::Colour colour_ = st::text;
    st::Font font_ {};
    unsigned justification_ = st::centredLeft;
    float minimumScale_ = 0.0f;
    bool live_ = false;
};

// ---------------------------------------------------------------- buttons
class PillButton final : public Widget
{
public:
    PillButton(std::string identifier, std::string text, float cornerRadius, float designHeight,
               float designFontHeight);
    void setText(const std::string& text);
    const std::string& text() const noexcept { return text_; }
    void setPalette(st::Colour fill, st::Colour textColour, st::Colour outline = st::transparent,
                    st::Colour pressedFill = st::transparent);
    void setIndicator(st::Colour colour);
    void setToggleState(bool on) { toggle_ = on; }
    bool toggleState() const noexcept { return toggle_; }
    void setClickingTogglesState(bool toggles) { togglesOnClick_ = toggles; }
    std::function<void()> onClick;
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseDown(CGPoint, NSEvent*) override;
    void mouseUp(CGPoint, NSEvent*) override;
    void activate() override;
    NSCursor* cursor() const override { return [NSCursor pointingHandCursor]; }

private:
    std::string text_;
    float cornerRadius_;
    float designHeight_;
    float designFontHeight_;
    st::Colour fill_ = st::transparent, textColour_ = st::text, outline_ = st::transparent,
               pressedFill_ = st::transparent, indicator_ = st::transparent;
    bool toggle_ = false;
    bool togglesOnClick_ = false;
};

class StepButton final : public Widget
{
public:
    enum class Glyph { minus, plus, previous, next };
    StepButton(std::string identifier, Glyph glyph);
    std::function<void()> onClick;
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseDown(CGPoint, NSEvent*) override;
    void mouseUp(CGPoint, NSEvent*) override;
    void activate() override;
    NSCursor* cursor() const override { return [NSCursor pointingHandCursor]; }

private:
    Glyph glyph_;
};

// A stored take: number, note count and a DRAG cue; dragging starts an OS drag.
class TakeRow final : public Widget
{
public:
    explicit TakeRow(std::string identifier);
    void setTake(int takeNumber, int noteCount);
    std::function<bool(NSEvent*)> onDragStart;
    std::function<void()> onReveal;
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseDown(CGPoint, NSEvent*) override;
    void mouseDrag(CGPoint, NSEvent*) override;
    void mouseUp(CGPoint, NSEvent*) override;
    void rightMouseDown(CGPoint, NSEvent*) override;
    NSCursor* cursor() const override { return [NSCursor openHandCursor]; }
    void dragEnded() { pressed = false; dragStarted_ = false; repaint(); }

private:
    int takeNumber_ = 0;
    int noteCount_ = 0;
    CGPoint downAt_ {};
    bool dragStarted_ = false;
    CEActionTarget* menuTarget_ = nil;
};

// ---------------------------------------------------------------- selector
// A line of text with a rule under it; a click opens the item menu.
class ComboBox final : public Widget
{
public:
    explicit ComboBox(std::string identifier);
    void clear();
    void addItem(const std::string& text, int itemId);
    void addSectionHeading(const std::string& text);
    void addSeparator();
    void setSelectedId(int itemId);  // no notification
    int selectedId() const noexcept { return selectedId_; }
    void setText(const std::string& text); // shown when no item is selected
    std::string displayedText() const;
    void setEmphasis(bool emphasis) { emphasis_ = emphasis; repaint(); }
    void setFontHeight(float height) { fontHeight_ = height; repaint(); }
    bool selectItem(int itemId); // as a user choice: notifies
    std::vector<int> itemIds() const;
    std::function<void()> onChange;
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseDown(CGPoint, NSEvent*) override;
    NSCursor* cursor() const override { return [NSCursor pointingHandCursor]; }

private:
    struct Entry
    {
        enum class Kind { item, heading, separator } kind = Kind::item;
        std::string text;
        int itemId = 0;
    };
    std::vector<Entry> entries_;
    int selectedId_ = 0;
    std::string freeText_;
    bool emphasis_ = false;
    bool menuOpen_ = false;
    float fontHeight_ = 12.0f;
    CEActionTarget* menuTarget_ = nil;
};

// ---------------------------------------------------------------- slider
class Slider final : public Widget
{
public:
    explicit Slider(std::string identifier, double minimum, double maximum);
    void setValue(double value); // no notification
    double value() const noexcept { return value_; }
    bool setValueFromUser(double value); // notifies
    std::function<void()> onValueChange;
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseDown(CGPoint, NSEvent*) override;
    void mouseDrag(CGPoint, NSEvent*) override;
    void mouseUp(CGPoint, NSEvent*) override;

private:
    float thumbRadius() const;
    void setFromPosition(float x);
    double minimum_, maximum_, value_;
};

// The MIDI FX build's internal-audio status chip.
class StatusPill final : public Widget
{
public:
    explicit StatusPill(std::string identifier) : Widget(std::move(identifier)) {}
    void paint(CGContextRef context, CGRect bounds) override;
};

// The product wordmark in the system script face.
class Wordmark final : public Widget
{
public:
    Wordmark() : Widget("header-wordmark") {}
    bool interactive() const override { return false; }
    void paint(CGContextRef context, CGRect bounds) override;
};

// ---------------------------------------------------------------- piano
class Piano final : public Widget
{
public:
    explicit Piano(ChordEngineAudioProcessor& processor);
    ~Piano() override;
    void setPreviewNotes(const chordengine::core::NoteSet& notes);
    int noteAtPosition(CGPoint position) const;
    int heldNote() const noexcept { return heldNote_; }
    void pressNote(int note);
    void releaseHeldNote();
    void detach() { processor_ = nullptr; }
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseDown(CGPoint, NSEvent*) override;
    void mouseDrag(CGPoint, NSEvent*) override;
    void mouseUp(CGPoint, NSEvent*) override;
    NSCursor* cursor() const override { return [NSCursor pointingHandCursor]; }

private:
    CGRect keyboardBounds() const;
    static bool isWhiteKey(int note);
    int whiteKeyCount() const;
    int whiteKeysBefore(int note) const;
    float whiteKeyWidth() const;
    CGRect whiteKeyRect(int whiteIndex) const;
    CGRect blackKeyRect(int note) const;
    ChordEngineAudioProcessor* processor_;
    std::array<bool, 128> previewNotes_ {};
    int heldNote_ = -1;
};

// ---------------------------------------------------------------- packs
class PackList final : public Widget
{
public:
    static constexpr float designRowHeight = 64.0f;
    PackList() : Widget("packs-list") {}
    void setListings(std::vector<chordengine::packs::PackListing> listings, std::string busySlug);
    const std::vector<chordengine::packs::PackListing>& listings() const noexcept { return listings_; }
    void setScale(float scale);
    int rowHeight() const;
    static std::string actionText(const chordengine::packs::PackListing& listing, bool busy);
    CGRect actionBounds(int row) const;
    std::function<void(int)> onAction;
    void paint(CGContextRef context, CGRect bounds) override;
    void mouseMove(CGPoint position) override;
    void mouseUp(CGPoint position, NSEvent*) override;

private:
    int actionRowAt(CGPoint position) const;
    std::vector<chordengine::packs::PackListing> listings_;
    std::string busySlug_;
    float scale_ = 1.0f;
    int hoverRow_ = -1;
};

// "C#4"-style names.
std::string noteName(int midiNote);
} // namespace chordengine::ui
