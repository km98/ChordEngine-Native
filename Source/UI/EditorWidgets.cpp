// The editor's controls, platform-neutral (see EditorWidgets.h). The macOS
// peers are in EditorWidgetsApple.mm, the Windows ones in EditorWin.cpp.
#include "EditorWidgets.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace chordengine::ui;
namespace st = chordengine::ui::style;

namespace chordengine::ui
{
std::string noteName(int midiNote)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string(names[((midiNote % 12) + 12) % 12]) + std::to_string(midiNote / 12 - 1);
}

// ==================================================================== widget
Widget::Widget(std::string identifier) : identifier_(std::move(identifier)) {}

Widget::~Widget() = default;

void Widget::setPeer(std::unique_ptr<WidgetPeer> peer)
{
    peer_ = std::move(peer);
    if (peer_ != nullptr)
    {
        peer_->boundsChanged();
        peer_->visibilityChanged();
        peer_->tooltipChanged();
    }
}

void Widget::setBounds(Rect bounds)
{
    bounds_ = bounds;
    if (peer_ != nullptr)
        peer_->boundsChanged();
    geometryChanged();
    repaint();
}

void Widget::setVisible(bool visible)
{
    visible_ = visible;
    if (peer_ != nullptr)
        peer_->visibilityChanged();
    geometryChanged();
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
    tooltip_ = tooltip;
    if (peer_ != nullptr)
        peer_->tooltipChanged();
}

void Widget::repaint()
{
    if (peer_ != nullptr)
        peer_->repaint();
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

void Label::paint(st::Canvas& context, Rect bounds)
{
    if (mode_ == Mode::infoRow)
        return paintInfoRow(context, bounds);
    if (mode_ == Mode::noteTokens)
        return paintNoteTokens(context, bounds);
    const int lines = std::max(1, static_cast<int>(bounds.height / std::max(1.0f, font_.height)));
    st::drawFitted(context, text_, bounds, font_, isEnabled() ? colour_ : colour_.withMultipliedAlpha(0.5f),
                   justification_, lines, minimumScale_);
}

void Label::paintInfoRow(st::Canvas& context, Rect bounds)
{
    // "Key  value" rows: the key in a fixed column in the quiet tone, the
    // value from one shared edge, wrapping inside its column.
    const auto separator = text_.find("  ");
    const float width = bounds.width;
    const bool hasKey = separator != std::string::npos && separator > 0;
    const float keyColumn = hasKey ? std::min(width * 0.4f, font_.height * 8.6f) : 0.0f;
    const float rowPitch = std::min(font_.height * 1.5f, bounds.height);
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

void Label::paintNoteTokens(st::Canvas& context, Rect bounds)
{
    // The chord's notes as a typeset sequence: each pitch class in a medium
    // weight with its octave as a small figure beside it; accent while live.
    const float height = bounds.height;
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
        if (x + pitchWidth + octaveWidth > bounds.width)
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

void PillButton::mouseUp(const MouseEvent& event)
{
    if (localBounds().contains(event.position))
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

void PillButton::paint(st::Canvas& context, Rect area)
{
    const float height = area.height;
    const float scale = height / std::max(1.0f, designHeight_);
    const float radius = std::min(cornerRadius_ * scale, height * 0.5f);
    const bool enabled = isEnabled();

    auto fill = fill_;
    if (enabled && pressed)
        fill = pressedFill_.isTransparent() ? fill_.darker(0.15f) : pressedFill_;
    else if (enabled && hover)
        fill = fill_.isTransparent() ? st::white.withAlpha(0.04f) : fill_.brighter(0.07f);
    if (!fill.isTransparent())
        context.fillRoundedRect(area, radius, enabled ? fill : fill.withMultipliedAlpha(0.45f));
    if (!outline_.isTransparent())
        context.strokeRoundedRect(area.inset(0.5f, 0.5f), radius, 1.0f,
                              enabled ? outline_ : outline_.withMultipliedAlpha(0.45f));

    const auto font = st::uiFont(std::max(6.0f, designFontHeight_ * scale), true, 0.08f);
    Rect content = area.inset(std::round(4.0f * scale), 0.0f);
    const auto colour = enabled ? textColour_ : textColour_.withMultipliedAlpha(0.5f);
    if (!indicator_.isTransparent())
    {
        const float dot = 6.0f * scale;
        const float gap = 7.0f * scale;
        const float textWidth = st::stringWidth(font, text_);
        const float unitWidth = std::min(content.width, dot + gap + textWidth);
        const float leftEdge = content.x + (content.width - unitWidth) * 0.5f;
        context.fillEllipse(Rect(leftEdge, area.midY() - dot * 0.5f, dot, dot),
                        enabled ? indicator_ : indicator_.withMultipliedAlpha(0.5f));
        const Rect textBox = Rect(std::round(leftEdge + dot + gap), area.y,
                                          std::round(unitWidth - dot - gap) + 2, area.height);
        st::drawFitted(context, text_, textBox, font, colour, st::centredLeft, 1, 0.85f);
        return;
    }
    st::drawFitted(context, text_, content, font, colour, st::centred, 1, 0.85f);
}

// ================================================================ step button
StepButton::StepButton(std::string identifier, Glyph glyph) : Widget(std::move(identifier)), glyph_(glyph) {}

void StepButton::mouseUp(const MouseEvent& event)
{
    if (localBounds().contains(event.position))
        activate();
}

void StepButton::activate()
{
    if (isEnabled() && onClick)
        onClick();
}

void StepButton::paint(st::Canvas& context, Rect area)
{
    const float side = std::min(area.width, area.height);
    const Rect circle = Rect(area.midX() - side * 0.5f, area.midY() - side * 0.5f, side, side).inset(0.5f, 0.5f);
    const float scale = side / 28.0f;
    if (pressed || hover)
        context.fillEllipse(circle, pressed ? st::accentSoft : st::raisedHover);
    context.strokeEllipse(circle, 1.0f, pressed ? st::accent : hover ? st::border.brighter(0.3f) : st::border);

    const auto colour = isEnabled() ? (hover || pressed ? st::textBright : st::text) : st::dim.withAlpha(0.6f);
    const float cx = area.midX();
    const float cy = area.midY();
    const float barLength = std::max(4.0f, 9.0f * scale);
    const float barThickness = std::max(1.0f, 1.4f * scale);
    if (glyph_ == Glyph::minus || glyph_ == Glyph::plus)
    {
        context.fillRect(Rect(cx - barLength * 0.5f, cy - barThickness * 0.5f, barLength, barThickness), colour);
        if (glyph_ == Glyph::plus)
            context.fillRect(Rect(cx - barThickness * 0.5f, cy - barLength * 0.5f, barThickness, barLength), colour);
        return;
    }
    const float half = 3.6f * scale;
    const float direction = glyph_ == Glyph::next ? 1.0f : -1.0f;
    context.strokePolyline({ Point(cx - direction * half * 0.45f, cy - half),
                                  Point(cx + direction * half * 0.55f, cy),
                                  Point(cx - direction * half * 0.45f, cy + half) },
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

void TakeRow::mouseDown(const MouseEvent& event)
{
    downAt_ = event.position;
    dragStarted_ = false;
}

void TakeRow::mouseDrag(const MouseEvent& event)
{
    const Point position = event.position;
    if (dragStarted_ || std::hypot(position.x - downAt_.x, position.y - downAt_.y) < 4.0f)
        return;
    dragStarted_ = true;
    if (onDragStart && !onDragStart(event))
        dragStarted_ = false;
}

void TakeRow::mouseUp(const MouseEvent&)
{
    dragStarted_ = false;
}

void TakeRow::rightMouseDown(const MouseEvent& event)
{
    if (peer() == nullptr)
        return;
#ifdef _WIN32
    const char* title = "Show in Explorer";
#else
    const char* title = "Show in Finder";
#endif
    auto reveal = onReveal;
    if (peer()->showMenu({ { MenuEntry::Kind::item, title, 1, false } }, event.position, 0.0f) == 1 && reveal)
        reveal();
}

void TakeRow::paint(st::Canvas& context, Rect area)
{
    const float scale = area.height / 20.0f;
    if (hover || pressed)
        context.fillRoundedRect(area, 3.0f * scale, pressed ? st::accentSoft : st::white.withAlpha(0.04f));
    context.fillRect(Rect(area.x, area.maxY() - 1.0f, area.width, 1.0f), st::hairline);

    const auto label = st::uiFont(std::max(6.0f, 9.5f * scale), true, 0.08f);
    const auto quiet = st::uiFont(std::max(6.0f, 9.5f * scale), false, 0.04f);
    const float arrowWidth = 9.0f * scale;
    const float arrowX = area.maxX() - arrowWidth;
    const float cy = area.midY();
    const float stroke = std::max(1.0f, 1.2f * scale);
    context.strokePolyline({ Point(arrowX, cy), Point(arrowX + arrowWidth, cy) }, stroke, st::accent);
    context.strokePolyline({ Point(arrowX + arrowWidth - 3.0f * scale, cy - 3.0f * scale),
                                  Point(arrowX + arrowWidth, cy),
                                  Point(arrowX + arrowWidth - 3.0f * scale, cy + 3.0f * scale) },
                       stroke, st::accent);
    st::drawFitted(context, "DRAG", Rect(area.x, area.y, std::round(arrowX - 5.0f * scale) - area.x,
                                               area.height),
                   label, st::accent, st::centredRight, 1, 1.0f);
    if (takeNumber_ > 0)
    {
        const auto takeText = "TAKE " + std::to_string(takeNumber_);
        st::drawFitted(context, takeText, area, label, st::text, st::centredLeft, 1, 1.0f);
        const float takeWidth = st::stringWidth(label, takeText);
        Rect rest = area;
        rest.x += std::round(takeWidth + 8.0f * scale);
        rest.width -= std::round(takeWidth + 8.0f * scale);
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
    entries_.push_back({ MenuEntry::Kind::item, text, itemId, false });
}

void ComboBox::addSectionHeading(const std::string& text)
{
    entries_.push_back({ MenuEntry::Kind::heading, text, 0, false });
}

void ComboBox::addSeparator()
{
    entries_.push_back({ MenuEntry::Kind::separator, {}, 0, false });
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
        if (entry.kind == MenuEntry::Kind::item && entry.itemId == selectedId_)
            return entry.text;
    return freeText_;
}

std::vector<int> ComboBox::itemIds() const
{
    std::vector<int> ids;
    for (const auto& entry : entries_)
        if (entry.kind == MenuEntry::Kind::item)
            ids.push_back(entry.itemId);
    return ids;
}

bool ComboBox::selectItem(int itemId)
{
    for (const auto& entry : entries_)
        if (entry.kind == MenuEntry::Kind::item && entry.itemId == itemId)
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

std::vector<MenuEntry> ComboBox::menuEntries() const
{
    auto entries = entries_;
    for (auto& entry : entries)
        entry.checked = entry.kind == MenuEntry::Kind::item && entry.itemId == selectedId_;
    return entries;
}

void ComboBox::mouseDown(const MouseEvent&)
{
    if (!isEnabled() || peer() == nullptr)
        return;
    menuOpen_ = true;
    repaint();
    const int chosen = peer()->showMenu(menuEntries(), Point(0.0f, localBounds().height + 2.0f),
                                        std::max(14.0f, fontHeight_ * 1.15f));
    menuOpen_ = false;
    pressed = false;
    repaint();
    if (chosen != 0)
        selectItem(chosen);
}

void ComboBox::paint(st::Canvas& context, Rect area)
{
    // A selector is a line of text with a rule under it: no box.
    const float height = area.height;
    const float scale = height / 30.0f;
    const bool focused = menuOpen_;
    if (hover || pressed)
        context.fillRoundedRect(area, 4.0f * scale, st::white.withAlpha(pressed ? 0.05f : 0.03f));
    auto rule = hover ? st::border.brighter(0.35f) : st::border;
    if (emphasis_)
        rule = st::accent.withAlpha(hover ? 0.75f : 0.5f);
    if (focused)
        rule = st::accent;
    const float ruleHeight = focused ? std::max(1.0f, 1.5f * scale) : 1.0f;
    context.fillRect(Rect(area.x, area.maxY() - ruleHeight, area.width, ruleHeight),
                 isEnabled() ? rule : rule.withMultipliedAlpha(0.5f));

    const float chevronWidth = 6.0f * scale;
    const float chevronHeight = 3.5f * scale;
    const float cx = area.maxX() - 8.0f * scale;
    const float cy = area.midY();
    context.strokePolyline({ Point(cx - chevronWidth * 0.5f, cy - chevronHeight * 0.5f),
                                  Point(cx, cy + chevronHeight * 0.5f),
                                  Point(cx + chevronWidth * 0.5f, cy - chevronHeight * 0.5f) },
                       std::max(1.0f, 1.3f * scale), focused ? st::accent : hover ? st::text : st::dim);

    const auto font = emphasis_ ? st::uiFont(fontHeight_ * 1.42f, true, -0.005f) : st::uiFont(fontHeight_ * 1.04f);
    const Rect textArea = Rect(area.x, area.y,
                                       std::max(1.0f, area.width - std::round(20.0f * scale)), area.height);
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
    return std::max(4.0f, std::round(localBounds().height * 0.2f));
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
    const float width = std::max(1.0f, localBounds().width - indent * 2.0f);
    const double proportion = std::clamp((x - indent) / width, 0.0f, 1.0f);
    setValueFromUser(minimum_ + proportion * (maximum_ - minimum_));
}

void Slider::mouseDown(const MouseEvent& event)
{
    if (isEnabled())
        setFromPosition(event.position.x);
}

void Slider::mouseDrag(const MouseEvent& event)
{
    if (isEnabled())
        setFromPosition(event.position.x);
}

void Slider::paint(st::Canvas& context, Rect area)
{
    const float indent = thumbRadius();
    const float x = indent;
    const float width = std::max(1.0f, area.width - indent * 2.0f);
    const float height = area.height;
    const float scale = height / 28.0f;
    const float trackHeight = std::max(1.0f, 2.0f * scale);
    const float centreY = height * 0.5f;
    const float position = x + static_cast<float>((value_ - minimum_) / (maximum_ - minimum_)) * width;
    const Rect trackArea = Rect(x, centreY - trackHeight * 0.5f, width, trackHeight);
    context.fillRoundedRect(trackArea, trackHeight * 0.5f, st::track);
    context.fillRoundedRect(Rect(x, trackArea.y, position - x, trackHeight), trackHeight * 0.5f, st::accent);
    const float radius = thumbRadius();
    const Rect thumb = Rect(position - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
    context.fillEllipse(thumb, st::textBright);
    context.strokeEllipse(thumb.inset(0.5f, 0.5f), 1.0f, st::background.withAlpha(0.6f));
}

// ============================================================= status pill
void StatusPill::paint(st::Canvas& context, Rect area)
{
    // "N/A": the MIDI FX build cannot sound; this is a status, not a switch.
    const float scale = area.height / 28.0f;
    context.strokeRoundedRect(area.inset(0.5f, 0.5f), 4.0f * scale, 1.0f, st::hairline.brighter(0.1f));
    st::drawFitted(context, "N/A", area, st::uiFont(std::max(6.0f, 9.0f * scale), true, 0.1f), st::dim, st::centred, 1, 1.0f);
}

// ================================================================= wordmark
void Wordmark::paint(st::Canvas& context, Rect area)
{
    // The ink of the word is centred on the header's centre line; it starts
    // exactly on the page's left edge.
    const float scale = area.height / 40.0f;
    st::Font font { st::Face::wordmark, 27.0f * scale, 0.0f };
    const Rect ink = st::inkBounds(font, "Chord Engine");
    if (ink.isEmpty())
        return;
    const float x = -ink.x;
    const float baseline = area.height * 0.5f + (ink.y + ink.height * 0.5f);
    st::drawLine(context, "Chord Engine", font, st::text, x, baseline);
}

// ==================================================================== piano
Piano::Piano(ChordEngineAudioProcessor& processor) : Widget("piano-display"), processor_(&processor) {}

Piano::~Piano()
{
    releaseHeldNote();
}

Rect Piano::keyboardBounds() const
{
    // The keys run edge to edge; the top 6 px (design) hold the felt rail.
    const Rect local = localBounds();
    const float scale = local.width / 720.0f;
    return Rect(0.0f, 6.0f * scale, local.width, local.height - 6.0f * scale);
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
    return keyboardBounds().width / static_cast<float>(std::max(1, whiteKeyCount()));
}

Rect Piano::whiteKeyRect(int whiteIndex) const
{
    const Rect keys = keyboardBounds();
    const float width = whiteKeyWidth();
    return Rect(keys.x + whiteIndex * width, keys.y, width - 1.0f, keys.height);
}

Rect Piano::blackKeyRect(int note) const
{
    const Rect keys = keyboardBounds();
    const float width = whiteKeyWidth();
    const float x = static_cast<float>(keys.x) + whiteKeysBefore(note) * width - width * 0.29f;
    return Rect(x, keys.y, width * 0.58f, keys.height * 0.62f);
}

int Piano::noteAtPosition(Point position) const
{
    if (!keyboardBounds().contains(position))
        return -1;
    float nearestBlack = 1.0e9f;
    int blackNote = -1;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;
        const Rect black = blackKeyRect(note);
        if (black.contains(position))
        {
            const float distance = std::abs(position.x - black.midX());
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
        if (whiteKeyRect(whiteIndex).contains(position))
            return note;
        ++whiteIndex;
    }
    return -1;
}

Point Piano::keyCentre(int note) const
{
    // Scan the lower part (white keys) and the upper part (black keys).
    const Rect bounds = localBounds();
    for (const float y : { bounds.height * 0.85f, bounds.height * 0.3f })
        for (float x = 0.5f; x < bounds.width; x += 1.0f)
            if (noteAtPosition({ x, y }) == note)
            {
                float end = x;
                while (end + 1.0f < bounds.width && noteAtPosition({ end + 1.0f, y }) == note)
                    end += 1.0f;
                return { (x + end) * 0.5f, y };
            }
    return { -1.0f, -1.0f };
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

void Piano::mouseDown(const MouseEvent& event)
{
    const int note = noteAtPosition(event.position);
    if (note >= 0)
        pressNote(note);
}

void Piano::mouseDrag(const MouseEvent& event)
{
    // Sliding onto another key releases the old note and sounds the new one;
    // sliding off the keys releases without a retrigger.
    if (heldNote_ < 0)
        return;
    const int note = noteAtPosition(event.position);
    if (note == heldNote_)
        return;
    if (note >= 0)
        pressNote(note);
    else
        releaseHeldNote();
}

void Piano::mouseUp(const MouseEvent&)
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

void Piano::paint(st::Canvas& context, Rect area)
{
    const float scale = area.width / 720.0f;
    const Rect keys = keyboardBounds();
    context.fillRect(Rect(0.0f, 0.0f, area.width, keys.y), st::pianoBed);
    context.fillRect(Rect(0.0f, 0.0f, area.width, 1.0f), st::accent.withAlpha(0.35f));

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
        const Rect key = whiteKeyRect(whiteIndex);
        const auto [trigger, generated, preview] = stateOf(note);
        const bool lit = trigger || generated;
        if (lit)
        {
            const auto colour = trigger ? st::triggerWhiteKey : st::chordWhiteKey;
            context.fillBottomRoundedGradient(key, keyRadius, colour, colour);
        }
        else
            context.fillBottomRoundedGradient(key, keyRadius, preview ? st::previewWhite : st::whiteKey,
                                              preview ? st::previewWhite.darker(0.04f) : st::whiteKeyShade);
        if (note % 12 == 0)
            st::drawFitted(context, noteName(note),
                           Rect(key.x, key.maxY() - labelHeight - 3.0f * scale, key.width, labelHeight),
                           labelFont, lit ? st::onAccent : preview ? st::onAccent.withAlpha(0.7f) : st::whiteKeyLabel,
                           st::centred, 1, 1.0f);
        ++whiteIndex;
    }

    context.fillRect(Rect(keys.x, keys.y, keys.width, 2.0f * scale), st::black.withAlpha(0.18f));

    for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;
        const Rect key = blackKeyRect(note);
        const auto [trigger, generated, preview] = stateOf(note);
        if (trigger || generated)
        {
            const auto colour = trigger ? st::triggerBlackKey : st::chordBlackKey;
            context.fillBottomRoundedGradient(key, keyRadius, colour, colour);
        }
        else
            context.fillBottomRoundedGradient(key, keyRadius, preview ? st::previewBlack.brighter(0.15f) : st::blackKeyTop,
                                              preview ? st::previewBlack : st::blackKey);
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

Rect PackList::actionBounds(int row) const
{
    const int height = rowHeight();
    const float width = std::round(140.0f * scale_);
    const float buttonHeight = std::round(28.0f * scale_);
    return Rect(localBounds().width - width - std::round(8.0f * scale_),
                      row * height + std::floor((height - buttonHeight) / 2.0f), width, buttonHeight);
}

int PackList::actionRowAt(Point position) const
{
    for (int row = 0; row < static_cast<int>(listings_.size()); ++row)
        if (actionBounds(row).contains(position))
            return row;
    return -1;
}

void PackList::mouseMove(Point position)
{
    const int row = actionRowAt(position);
    if (row != hoverRow_)
    {
        hoverRow_ = row;
        repaint();
    }
}

void PackList::mouseUp(const MouseEvent& event)
{
    const int row = actionRowAt(event.position);
    if (row >= 0 && onAction)
        onAction(row);
}

void PackList::paint(st::Canvas& context, Rect area)
{
    using chordengine::packs::PackStatus;
    const int height = rowHeight();
    const float width = area.width;
    for (int row = 0; row < static_cast<int>(listings_.size()); ++row)
    {
        const auto& listing = listings_[static_cast<std::size_t>(row)];
        const float y = static_cast<float>(row * height);
        context.fillRect(Rect(0.0f, y + static_cast<float>(height) - 1.0f, width, 1.0f), st::hairline);

        const float tileSize = std::round(40.0f * scale_);
        const Rect tile = Rect(0.0f, y + std::floor((static_cast<float>(height) - tileSize) / 2.0f), tileSize, tileSize);
        const bool owned = listing.status == PackStatus::builtIn || listing.status == PackStatus::installed
            || listing.status == PackStatus::updateAvailable;
        context.fillRoundedRect(tile, 6.0f * scale_, owned ? st::accent.withAlpha(0.18f) : st::raised);
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

        const Rect action = actionBounds(row);
        const float textX = std::round(56.0f * scale_);
        const float textWidth = action.x - textX - std::round(12.0f * scale_);
        st::drawFitted(context, listing.name, Rect(textX, y + std::round(13.0f * scale_), textWidth, std::round(18.0f * scale_)),
                       st::uiFont(14.0f * scale_, true), st::textBright, st::centredLeft, 1, 1.0f);
        const bool usableStatus = owned || listing.status == PackStatus::available;
        const auto detail = !listing.description.empty() ? listing.statusText + "  -  " + listing.description : listing.statusText;
        st::drawFitted(context, detail, Rect(textX, y + std::round(34.0f * scale_), textWidth, std::round(16.0f * scale_)),
                       st::uiFont(11.5f * scale_), usableStatus ? st::muted : st::accent, st::centredLeft, 1, 1.0f);

        const bool busy = listing.slug == busySlug_;
        const bool primary = listing.status == PackStatus::available || listing.status == PackStatus::updateAvailable
            || listing.status == PackStatus::signInRequired;
        const bool quiet = listing.status == PackStatus::builtIn || listing.status == PackStatus::installed;
        const bool hot = row == hoverRow_ && !busy;
        st::Colour labelColour = st::text;
        if (primary && !busy)
        {
            context.fillRoundedRect(action, 4.0f * scale_, hot ? st::accent.brighter(0.1f) : st::accent);
            labelColour = st::onAccent;
        }
        else
        {
            if (hot)
                context.fillRoundedRect(action, 4.0f * scale_, st::raisedHover);
            context.strokeRoundedRect(action.inset(0.5f, 0.5f), 4.0f * scale_, 1.0f,
                                  quiet ? st::hairline.brighter(0.2f) : st::border);
            labelColour = quiet ? st::dim : st::text;
        }
        st::drawFitted(context, actionText(listing, busy), action, st::uiFont(9.5f * scale_, true, 0.08f), labelColour,
                       st::centred, 1, 1.0f);
    }
}
} // namespace chordengine::ui
