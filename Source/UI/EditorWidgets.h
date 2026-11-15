#pragma once

// =====================================================================
// The editor's controls: small platform-neutral C++ widgets. Drawing uses the
// 0.5 redesign's rules (EditorStyle.h) through a Canvas; a widget scales with
// its own bounds, exactly as the 0.5 controls did.
//
// A widget's presence in the window system is its WidgetPeer, created by the
// platform back end when the widget is added to the editor (macOS: one
// AppKit view per widget, EditorWidgetsApple.mm; Windows: an entry in the
// editor window's widget list, EditorWin.cpp). Until then (and after the
// peer is gone) a widget simply keeps its state.
// =====================================================================

#include "Core/ChordEngineCore.h"
#include "EditorStyle.h"
#include "Packs/PackStoreClient.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ChordEngineAudioProcessor;

namespace chordengine::ui
{
namespace st = style;

// One mouse event, in the receiving widget's coordinates.
struct MouseEvent
{
    Point position;
    // The pointer in screen coordinates (points; only differences are used).
    Point screen;
    // The platform's own event (NSEvent* on macOS), for OS drags.
    const void* native = nullptr;
};

// A pop-up menu's entries.
struct MenuEntry
{
    enum class Kind { item, heading, separator } kind = Kind::item;
    std::string text;
    int itemId = 0;
    bool checked = false;
};

class Widget;

class WidgetPeer
{
public:
    virtual ~WidgetPeer() = default;
    // The widget's bounds, visibility, tooltip or cursor changed.
    virtual void boundsChanged() = 0;
    virtual void visibilityChanged() = 0;
    virtual void tooltipChanged() = 0;
    virtual void repaint() = 0;
    // Shows a menu (modal) with its top-left at `at` (widget coordinates) and
    // returns the chosen item id, or 0 when the menu was dismissed.
    virtual int showMenu(const std::vector<MenuEntry>& entries, Point at, float fontHeight) = 0;
};

class Widget
{
public:
    explicit Widget(std::string identifier);
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    const std::string& identifier() const noexcept { return identifier_; }

    void setPeer(std::unique_ptr<WidgetPeer> peer);
    WidgetPeer* peer() const noexcept { return peer_.get(); }

    void setBounds(Rect bounds);
    Rect bounds() const noexcept { return bounds_; }      // in the parent's coordinates
    Rect localBounds() const noexcept { return bounds_.local(); }
    void setVisible(bool visible);
    bool isVisible() const noexcept { return visible_; }
    void setEnabled(bool enabled);
    bool isEnabled() const noexcept { return enabled_; }
    void setAlpha(float alpha);
    float alpha() const noexcept { return alpha_; }
    void setTooltip(const std::string& tooltip);
    const std::string& tooltip() const noexcept { return tooltip_; }
    void repaint();

    // Mouse handling. `interactive` false makes the widget transparent to clicks.
    virtual bool interactive() const { return true; }
    virtual void paint(st::Canvas& canvas, Rect bounds) = 0;
    virtual void mouseDown(const MouseEvent&) {}
    virtual void mouseDrag(const MouseEvent&) {}
    virtual void mouseUp(const MouseEvent&) {}
    virtual void rightMouseDown(const MouseEvent&) {}
    virtual void mouseMove(Point) {}
    virtual Cursor cursor() const { return Cursor::arrow; }

    // Programmatic activation (tests, accessibility): what a click does.
    virtual void activate() {}

    bool hover = false;
    bool pressed = false;

protected:
    // Bounds or visibility changed (widgets with native children follow).
    virtual void geometryChanged() {}

private:
    std::string identifier_;
    std::unique_ptr<WidgetPeer> peer_;
    Rect bounds_ {};
    std::string tooltip_;
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
    void paint(st::Canvas& canvas, Rect bounds) override;

private:
    void paintInfoRow(st::Canvas& canvas, Rect bounds);
    void paintNoteTokens(st::Canvas& canvas, Rect bounds);
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
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseUp(const MouseEvent&) override;
    void activate() override;
    Cursor cursor() const override { return Cursor::pointingHand; }

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
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseUp(const MouseEvent&) override;
    void activate() override;
    Cursor cursor() const override { return Cursor::pointingHand; }

private:
    Glyph glyph_;
};

// A stored take: number, note count and a DRAG cue; dragging starts an OS drag.
class TakeRow final : public Widget
{
public:
    explicit TakeRow(std::string identifier);
    void setTake(int takeNumber, int noteCount);
    std::function<bool(const MouseEvent&)> onDragStart;
    std::function<void()> onReveal;
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    void rightMouseDown(const MouseEvent&) override;
    Cursor cursor() const override { return Cursor::openHand; }
    void dragEnded() { pressed = false; dragStarted_ = false; repaint(); }

private:
    int takeNumber_ = 0;
    int noteCount_ = 0;
    Point downAt_ {};
    bool dragStarted_ = false;
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
    // The menu as shown to the user (current choice checked).
    std::vector<MenuEntry> menuEntries() const;
    std::function<void()> onChange;
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseDown(const MouseEvent&) override;
    Cursor cursor() const override { return Cursor::pointingHand; }

private:
    std::vector<MenuEntry> entries_;
    int selectedId_ = 0;
    std::string freeText_;
    bool emphasis_ = false;
    bool menuOpen_ = false;
    float fontHeight_ = 12.0f;
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
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;

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
    void paint(st::Canvas& canvas, Rect bounds) override;
};

// The product wordmark in the system script face.
class Wordmark final : public Widget
{
public:
    Wordmark() : Widget("header-wordmark") {}
    bool interactive() const override { return false; }
    void paint(st::Canvas& canvas, Rect bounds) override;
};

// ---------------------------------------------------------------- piano
class Piano final : public Widget
{
public:
    explicit Piano(ChordEngineAudioProcessor& processor);
    ~Piano() override;
    void setPreviewNotes(const chordengine::core::NoteSet& notes);
    int noteAtPosition(Point position) const;
    int heldNote() const noexcept { return heldNote_; }
    void pressNote(int note);
    void releaseHeldNote();
    void detach() { processor_ = nullptr; }
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    Cursor cursor() const override { return Cursor::pointingHand; }
    // The centre of a key in widget coordinates ({-1,-1} when not shown).
    Point keyCentre(int note) const;

private:
    Rect keyboardBounds() const;
    static bool isWhiteKey(int note);
    int whiteKeyCount() const;
    int whiteKeysBefore(int note) const;
    float whiteKeyWidth() const;
    Rect whiteKeyRect(int whiteIndex) const;
    Rect blackKeyRect(int note) const;
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
    Rect actionBounds(int row) const;
    std::function<void(int)> onAction;
    void paint(st::Canvas& canvas, Rect bounds) override;
    void mouseMove(Point position) override;
    void mouseUp(const MouseEvent&) override;
    Cursor cursor() const override { return hoverRow_ >= 0 ? Cursor::pointingHand : Cursor::arrow; }

private:
    int actionRowAt(Point position) const;
    std::vector<chordengine::packs::PackListing> listings_;
    std::string busySlug_;
    float scale_ = 1.0f;
    int hoverRow_ = -1;
};

// "C#4"-style names.
std::string noteName(int midiNote);
} // namespace chordengine::ui
