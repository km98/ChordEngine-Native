#include "PluginEditor.h"

#include "BinaryData.h"
#include "Core/ChordPresetSystem.h"
#include "Core/ScaleSystem.h"
#include "Licensing/LicensingServices.h"
#include "UI/ExternalFileDrag.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
// ---------------------------------------------------------------- palette
// 0.5.0 redesign (Docs/UI_REDESIGN_0.5.0.md): warm graphite surfaces in
// small tonal steps, ivory type in three strengths, and ONE restrained sand
// accent for selection, focus and the sounding chord. Surfaces are separated
// by tone first and by a hairline only where tone alone is not enough.
const juce::Colour background { 0xff0f0f11 };   // editor ground
const juce::Colour frame { 0xff141416 };        // content frame
const juce::Colour page { 0xff161618 };         // the open working page (V2)
const juce::Colour panel { 0xff1a1a1d };        // primary surface
const juce::Colour inset { 0xff131315 };        // wells inside a surface
const juce::Colour raised { 0xff232326 };       // controls at rest
const juce::Colour raisedHover { 0xff2a2a2e };  // controls under the mouse
const juce::Colour track { 0xff303035 };        // slider / segmented tracks
const juce::Colour hairline { 0xff2a2a2e };     // dividers between groups
const juce::Colour border { 0xff36363b };       // control outlines
const juce::Colour text { 0xffece7de };         // ivory body text
const juce::Colour textBright { 0xfff7f3ec };   // headline ivory
const juce::Colour muted { 0xffa6a096 };        // secondary text
const juce::Colour dim { 0xff7c776e };          // captions
const juce::Colour faint { 0xff5a5650 };        // footer, inactive hints
const juce::Colour accent { 0xffd8a55e };       // sand / amber accent
const juce::Colour accentSoft { 0xff3a3023 };   // accent-tinted surface
const juce::Colour onAccent { 0xff1b1610 };     // text on an accent fill
const juce::Colour terracotta { 0xffd06a50 };   // recording / trigger / error
const juce::Colour sage { 0xffa3c39a };         // confirmed / up to date
const juce::Colour pianoBed { 0xff121214 };
const juce::Colour whiteKey { 0xffe9e4da };
const juce::Colour whiteKeyShade { 0xffd9d3c7 };
const juce::Colour blackKey { 0xff18181b };
const juce::Colour blackKeyTop { 0xff26262a };
const juce::Colour chordWhiteKey { 0xffd8a55e };
const juce::Colour chordBlackKey { 0xffb9884a };
const juce::Colour triggerWhiteKey { 0xffd06a50 };
const juce::Colour triggerBlackKey { 0xffb4553e };
const juce::Colour whiteKeyLabel { 0xff8a8478 };
// Preview tint: native-only state (the reference highlights trigger and
// generated notes only). Documented in NATIVE_GUI_REFERENCE_INVENTORY.md.
const juce::Colour previewWhite { 0xffe6d3b2 };
const juce::Colour previewBlack { 0xff5a4a33 };

// The licensing and update models publish their semantic row colours as
// fixed ARGB values (their contract is unchanged). The editor maps each of
// those semantic values onto the redesign palette for display only; an
// unknown value is shown as published.
juce::Colour displayTone(std::uint32_t modelColour)
{
    switch (modelColour)
    {
        case 0xff7fd48a: return sage;        // active / up to date
        case 0xffe0b050: return accent;      // attention (inactive, offline)
        case 0xffe05a5a: return terracotta;  // expired / error / required
        case 0xff8a93a0: return muted;       // neutral / checking
        case 0xffe8ebef: return text;        // ordinary value
        default: return juce::Colour(modelColour);
    }
}

// Interface faces: the music-prod.com typefaces, Inter for text and
// Schibsted Grotesk for captions (both SIL OFL 1.1, Resources/Fonts). They are
// static instances cut from the variable fonts VYRE ships, embedded as binary
// data and loaded privately - nothing is installed system-wide. The serif
// display face and the script wordmark below stay as they are.
enum class UiFace { regular, semibold, caption };

juce::Typeface::Ptr uiTypeface(UiFace face)
{
    static const std::array<juce::Typeface::Ptr, 3> faces {{
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf,
                                                BinaryData::InterRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf,
                                                BinaryData::InterSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(BinaryData::SchibstedGroteskSemiBold_ttf,
                                                BinaryData::SchibstedGroteskSemiBold_ttfSize)
    }};
    return faces[static_cast<std::size_t>(face)];
}

juce::Font faceFont(UiFace face, float height, float kerning)
{
    auto font = juce::Font(juce::FontOptions(uiTypeface(face)).withHeight(height));
    return kerning != 0.0f ? font.withExtraKerningFactor(kerning) : font;
}

juce::Font uiFont(float height, bool bold = false, float kerning = 0.0f)
{
    return faceFont(bold ? UiFace::semibold : UiFace::regular, height, kerning);
}

// Captions: small letter-spaced capitals in the heading face, as the
// music-prod.com eyebrows are set.
juce::Font captionFont(float height, float kerning)
{
    return faceFont(UiFace::caption, height, kerning);
}

// Display face for the chord name and the INFO title: a book serif that
// ships with the operating system (Baskerville on macOS, Georgia on macOS and
// Windows), falling back to the platform's default serif. Nothing is bundled
// or downloaded; the choice is made once.
const juce::String& displayTypefaceName()
{
    static const juce::String name = []
    {
        const auto available = juce::Font::findAllTypefaceNames();
        for (const auto* candidate : { "Baskerville", "Georgia", "Times New Roman" })
            if (available.contains(candidate))
                return juce::String(candidate);
        return juce::Font::getDefaultSerifFontName();
    }();
    return name;
}

juce::Font displayFont(float height, float kerning = 0.0f)
{
    auto font = juce::Font(juce::FontOptions(displayTypefaceName(), height, juce::Font::plain));
    return kerning != 0.0f ? font.withExtraKerningFactor(kerning) : font;
}

// Script face for the "Chord Engine" header wordmark. Only faces that ship
// with the operating system are used - nothing is bundled or downloaded:
//   - Snell Roundhand (macOS, all supported versions): a formal, legible
//     roundhand, set in its Bold cut so the hairlines hold at 640x520;
//   - Gabriola (Windows 7 and later): the Windows calligraphic face;
//   - otherwise the italic of the display serif (Baskerville / Georgia
//     Italic), a classical fallback that is available on both platforms.
// The more ornamental script faces (Zapfino, Savoye) are deliberately not
// used: the wordmark must stay readable at every editor size.
struct WordmarkFace
{
    juce::String name;
    juce::String style;
    float sizeFactor = 1.0f; // optical size relative to the reference face
};

const WordmarkFace& wordmarkFace()
{
    static const WordmarkFace face = []
    {
        const auto available = juce::Font::findAllTypefaceNames();
        if (available.contains("Snell Roundhand"))
            return WordmarkFace { "Snell Roundhand", "Bold", 1.0f };
        if (available.contains("Gabriola"))
            return WordmarkFace { "Gabriola", "Regular", 1.12f };
        return WordmarkFace { displayTypefaceName(), "Italic", 0.82f };
    }();
    return face;
}

juce::Font wordmarkFont(float height)
{
    const auto& face = wordmarkFace();
    return juce::Font(juce::FontOptions(face.name, face.style, height * face.sizeFactor));
}

// Captions are small, bold, letter-spaced capitals in the caption tone.
constexpr float captionSize = 8.5f;
constexpr float captionKerning = 0.12f;

// The supplied Music-Prod artwork is white lettering on an opaque near-black
// ground. Re-deriving alpha from luminance keeps the exact letterforms and
// drops the ground, so the mark can be drawn in one ivory tone on any surface.
juce::Image makeMonochromeMark(const juce::Image& source, juce::Colour tone)
{
    if (!source.isValid())
        return {};

    juce::Image result(juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
    for (int y = 0; y < source.getHeight(); ++y)
        for (int x = 0; x < source.getWidth(); ++x)
        {
            const auto pixel = source.getPixelAt(x, y);
            const float luminance = (0.2126f * pixel.getFloatRed() + 0.7152f * pixel.getFloatGreen()
                                     + 0.0722f * pixel.getFloatBlue());
            const float alpha = juce::jlimit(0.0f, 1.0f, (luminance - 0.08f) / 0.84f)
                * pixel.getFloatAlpha();
            result.setPixelAt(x, y, tone.withAlpha(alpha));
        }
    return result;
}

// Test builds never reach the network: the store answers as if offline.
class OfflinePackStoreTransport final : public chordengine::packs::PackStoreTransport
{
public:
    void get(const juce::String&, std::size_t, Callback callback) override { reply(std::move(callback)); }
    void postJson(const juce::String&, const juce::String&, std::size_t, Callback callback) override
    {
        reply(std::move(callback));
    }

private:
    static void reply(Callback callback)
    {
        juce::MessageManager::callAsync([deliver = std::move(callback)] { deliver({}); });
    }
};

// The store origin: production (empty = the licensing origin) unless a
// developer points CHORDENGINE_PACK_STORE_ORIGIN at a LOCAL test server
// (Tools/LocalPackServer.py). Remote overrides are ignored.
juce::String packStoreOrigin()
{
    const auto value = juce::SystemStats::getEnvironmentVariable("CHORDENGINE_PACK_STORE_ORIGIN", {});
    const auto host = juce::URL(value).getDomain();
    if (value.startsWith("http://") && (host == "127.0.0.1" || host == "localhost")
        && chordengine::packs::isAllowedStoreUrl(value))
        return value.endsWithChar('/') ? value : value + "/";
    return {};
}

chordengine::core::CustomChordPreset toCustomPreset(const chordengine::packs::PackPreset& preset)
{
    chordengine::core::CustomChordPreset custom;
    custom.triggerMap = preset.triggerMap;
    custom.voicingStyle = chordengine::packs::toString(preset.voicing);
    return custom;
}

// The reference is a single-source version constant (CE_PRODUCT_VERSION);
// footer, INFO headline and INFO updates rows all derive from it.
// Every version surface of THIS editor shows the version of the binary that
// is actually running, taken from the build metadata (see
// chordengine::updates::currentBuildVersionString). The reference product's
// 0.4.0 string is not hardcoded anywhere in the UI and no development build
// pretends to be 0.4.0. The account bridge reports the same compiled version
// as its `plugin_version` telemetry field
// (chordengine::licensing::productVersion).
const juce::String productVersion { chordengine::updates::currentBuildVersionString };

const char* const noteNames[] { "C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B" };

juce::String pitchClassName(int pitchClass)
{
    return juce::String(noteNames[((pitchClass % 12) + 12) % 12]);
}

juce::String noteName(int midiNote)
{
    return pitchClassName(midiNote) + juce::String(midiNote / 12 - 1);
}

juce::String noteListText(const chordengine::core::NoteSet& notes)
{
    juce::String result;
    for (std::size_t index = 0; index < notes.size; ++index)
    {
        if (index != 0)
            result << " ";
        result << noteName(notes.notes[index]);
    }
    return result;
}

// Display-only mirror of the reference chord-name table (read-only reference
// script: ceChordSuffix). It classifies the interval set the Core already
// resolved, so the readout uses the product's chord terminology without
// computing any harmony here.
juce::String chordSuffix(const chordengine::core::ResolvedChord& chord)
{
    bool hasMinorThird = false;
    bool hasMajorThird = false;
    bool hasDiminishedFifth = false;
    bool hasAugmentedFifth = false;
    bool hasSus4 = false;
    bool hasSus2 = false;
    bool hasDiminishedSeventh = false;
    bool hasMajorSeventh = false;
    bool hasNinth = false;
    bool hasEleventh = false;
    bool hasThirteenth = false;
    bool hasSixth = false;

    for (std::size_t index = 0; index < chord.intervalCount; ++index)
    {
        const int interval = chord.intervals[index];
        const int pitchClass = interval % 12;
        if (pitchClass == 3) hasMinorThird = true;
        if (pitchClass == 4) hasMajorThird = true;
        if (pitchClass == 6) hasDiminishedFifth = true;
        if (pitchClass == 8) hasAugmentedFifth = true;
        if (pitchClass == 5) hasSus4 = true;
        if (pitchClass == 2) hasSus2 = true;
        if (interval == 10) hasDiminishedSeventh = true;
        if (interval == 11) hasMajorSeventh = true;
        if (interval == 14) hasNinth = true;
        if (interval == 17) hasEleventh = true;
        if (interval == 21) hasThirteenth = true;
    }

    if (!hasDiminishedSeventh && !hasMajorSeventh)
        for (std::size_t index = 0; index < chord.intervalCount; ++index)
            if (chord.intervals[index] % 12 == 9)
                hasSixth = true;

    if (hasMajorThird)
    {
        if (hasAugmentedFifth)
            return juce::String(hasMajorSeventh ? "maj7" : (hasDiminishedSeventh ? "7" : "")) + "#5";
        if (hasThirteenth)
            return hasMajorSeventh ? "maj13" : (hasDiminishedSeventh ? "13" : "add13");
        if (hasEleventh)
            return hasMajorSeventh ? "maj7#11" : (hasDiminishedSeventh ? "7#11" : "add#11");
        if (hasNinth)
            return hasMajorSeventh ? "maj9" : (hasDiminishedSeventh ? "9" : "add9");
        if (hasMajorSeventh)
            return "maj7";
        if (hasDiminishedSeventh)
            return "7";
        if (hasSixth)
            return "6";
        return {};
    }

    if (hasMinorThird)
    {
        if (hasDiminishedFifth)
            return hasDiminishedSeventh ? "m7b5" : "dim";
        if (hasThirteenth)
            return hasMajorSeventh ? "mMaj13" : (hasDiminishedSeventh ? "m13" : "madd13");
        if (hasNinth)
            return hasMajorSeventh ? "mMaj9" : (hasDiminishedSeventh ? "m9" : "madd9");
        if (hasEleventh)
            return hasMajorSeventh ? "mMaj11" : (hasDiminishedSeventh ? "m11" : "madd11");
        if (hasMajorSeventh)
            return "mMaj7";
        if (hasDiminishedSeventh)
            return "m7";
        if (hasSixth)
            return "m6";
        return "m";
    }

    if (hasSus4)
    {
        if (hasMajorSeventh)
            return "maj7sus4";
        if (hasDiminishedSeventh)
            return "7sus4";
        return "sus4";
    }

    if (hasSus2)
    {
        if (hasMajorSeventh)
            return "maj7sus2";
        if (hasDiminishedSeventh)
            return "7sus2";
        return "sus2";
    }

    if (hasDiminishedFifth)
        return "dim";
    if (hasAugmentedFifth)
        return "aug";
    return "5";
}

// Full persisted-field comparison. The timer re-syncs the controls whenever ANY
// of these changed, including values restored from host state.
bool sameConfiguration(const chordengine::core::CoreConfiguration& a,
                       const chordengine::core::CoreConfiguration& b) noexcept
{
    return a.keyIndex == b.keyIndex && a.scale == b.scale && a.preset == b.preset
        && a.velocityMode == b.velocityMode && a.fixedVelocity == b.fixedVelocity
        && a.transpose.target == b.transpose.target
        && a.transpose.octaveSteps == b.transpose.octaveSteps;
}

// Chord symbol for one trigger note, resolved by the Core itself
// (ChordPresetSystem::resolve) - the same resolution the engine voices.
juce::String chordSymbolFor(int triggerNote,
                            const chordengine::core::CoreConfiguration& configuration)
{
    const auto resolved = chordengine::core::ChordPresetSystem::resolve(
        configuration.preset, triggerNote, configuration.scale, configuration.keyIndex);
    if (!resolved || resolved->intervalCount == 0)
        return "No Chord";
    return pitchClassName(resolved->rootMidiNote) + chordSuffix(*resolved);
}
}

// ====================================================== chord wordmark ====
ChordEngineAudioProcessorEditor::ChordEngineWordmark::ChordEngineWordmark()
{
    setTitle("Chord Engine");
    setDescription("Chord Engine product wordmark");
    setInterceptsMouseClicks(false, false);
}

void ChordEngineAudioProcessorEditor::ChordEngineWordmark::paint(juce::Graphics& graphics)
{
    // The product name in a script face (wordmarkFace()), in ivory, drawn as
    // live glyphs. Script faces carry tall ascenders and deep descenders, so
    // the INK of the word - not the font box - is centred on the header's
    // centre line, which is also the centre line of the CHORD / INFO
    // navigation; the word starts exactly on the page's left edge.
    const float scale = static_cast<float>(getHeight()) / 40.0f;
    const auto font = wordmarkFont(27.0f * scale);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, "Chord Engine", 0.0f, 0.0f);
    const auto ink = glyphs.getBoundingBox(0, -1, true);
    if (ink.isEmpty())
        return;

    const float dx = -ink.getX();
    const float dy = static_cast<float>(getHeight()) * 0.5f - ink.getCentreY();
    graphics.setColour(text);
    glyphs.draw(graphics, juce::AffineTransform::translation(dx, dy));
}

// ============================================================ look and feel
ChordEngineAudioProcessorEditor::EditorLookAndFeel::EditorLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, dim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
    setColour(juce::PopupMenu::highlightedTextColourId, textBright);
    setColour(juce::TooltipWindow::backgroundColourId, panel);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, border);
    // Tooltips, alerts and anything else drawn with the default sans face.
    setDefaultSansSerifTypeface(uiTypeface(UiFace::regular));
}

juce::Font ChordEngineAudioProcessorEditor::EditorLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    const bool emphasis = static_cast<bool>(box.getProperties().getWithDefault("emphasis", false));
    return emphasis ? uiFont(fontHeight_ * 1.42f, true, -0.005f) : uiFont(fontHeight_ * 1.04f);
}

juce::Font ChordEngineAudioProcessorEditor::EditorLookAndFeel::getPopupMenuFont()
{
    return uiFont(fontHeight_);
}

void ChordEngineAudioProcessorEditor::EditorLookAndFeel::drawComboBox(
    juce::Graphics& graphics, int width, int height, bool isButtonDown, int, int, int, int,
    juce::ComboBox& box)
{
    // A selector is set like a line of text with a rule under it: no box.
    // The rule brightens under the mouse and turns sand with keyboard focus
    // or while the menu is open; the emphasised selector (the chord preset)
    // keeps a quiet sand rule at rest so it reads as the primary choice.
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                             static_cast<float>(height));
    const float scale = static_cast<float>(height) / 30.0f;
    const bool emphasis = static_cast<bool>(box.getProperties().getWithDefault("emphasis", false));
    const bool hover = box.isMouseOver(true);
    const bool focused = box.hasKeyboardFocus(true) || box.isPopupActive();

    if (hover || isButtonDown)
    {
        graphics.setColour(juce::Colours::white.withAlpha(isButtonDown ? 0.05f : 0.03f));
        graphics.fillRoundedRectangle(area, 4.0f * scale);
    }

    auto rule = hover ? border.brighter(0.35f) : border;
    if (emphasis)
        rule = accent.withAlpha(hover ? 0.75f : 0.5f);
    if (focused)
        rule = accent;
    const float ruleHeight = focused ? juce::jmax(1.0f, 1.5f * scale) : 1.0f;
    graphics.setColour(box.isEnabled() ? rule : rule.withMultipliedAlpha(0.5f));
    graphics.fillRect(area.withTop(area.getBottom() - ruleHeight));

    const float chevronWidth = 6.0f * scale;
    const float chevronHeight = 3.5f * scale;
    const float cx = area.getRight() - 8.0f * scale;
    const float cy = area.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath(cx - chevronWidth * 0.5f, cy - chevronHeight * 0.5f);
    chevron.lineTo(cx, cy + chevronHeight * 0.5f);
    chevron.lineTo(cx + chevronWidth * 0.5f, cy - chevronHeight * 0.5f);
    graphics.setColour(focused ? accent : hover ? text : dim);
    graphics.strokePath(chevron, juce::PathStrokeType(juce::jmax(1.0f, 1.3f * scale),
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
}

void ChordEngineAudioProcessorEditor::EditorLookAndFeel::positionComboBoxText(juce::ComboBox& box,
                                                                             juce::Label& label)
{
    // Text starts flush with the caption above it.
    const float scale = static_cast<float>(box.getHeight()) / 30.0f;
    const int right = juce::roundToInt(20.0f * scale);
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setBounds(0, 0, juce::jmax(1, box.getWidth() - right), box.getHeight());
    label.setFont(getComboBoxFont(box));
}

void ChordEngineAudioProcessorEditor::EditorLookAndFeel::drawPopupMenuBackground(
    juce::Graphics& graphics, int width, int height)
{
    graphics.fillAll(panel);
    graphics.setColour(border);
    graphics.drawRect(0, 0, width, height, 1);
}

int ChordEngineAudioProcessorEditor::EditorLookAndFeel::getSliderThumbRadius(juce::Slider& slider)
{
    return juce::jmax(4, juce::roundToInt(static_cast<float>(slider.getHeight()) * 0.2f));
}

void ChordEngineAudioProcessorEditor::EditorLookAndFeel::drawLinearSlider(
    juce::Graphics& graphics, int x, int y, int width, int height, float sliderPos, float, float,
    juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float scale = static_cast<float>(height) / 28.0f;
    const float trackHeight = juce::jmax(1.0f, 2.0f * scale);
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const auto trackArea = juce::Rectangle<float>(static_cast<float>(x), centreY - trackHeight * 0.5f,
                                                  static_cast<float>(width), trackHeight);

    graphics.setColour(track);
    graphics.fillRoundedRectangle(trackArea, trackHeight * 0.5f);
    graphics.setColour(accent);
    graphics.fillRoundedRectangle(trackArea.withRight(sliderPos), trackHeight * 0.5f);

    const float thumbRadius = static_cast<float>(getSliderThumbRadius(slider));
    const auto thumb = juce::Rectangle<float>(thumbRadius * 2.0f, thumbRadius * 2.0f)
                           .withCentre({ sliderPos, centreY });
    graphics.setColour(textBright);
    graphics.fillEllipse(thumb);
    graphics.setColour(slider.hasKeyboardFocus(false) ? accent : background.withAlpha(0.6f));
    graphics.drawEllipse(thumb.reduced(0.5f), 1.0f);
}

void ChordEngineAudioProcessorEditor::EditorLookAndFeel::fillTextEditorBackground(
    juce::Graphics& graphics, int width, int height, juce::TextEditor& editor)
{
    // The licence-key field: a faint inset with a rule underneath, the same
    // field language as the selectors.
    graphics.setColour(editor.hasKeyboardFocus(true) ? inset.brighter(0.05f) : inset);
    graphics.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                         static_cast<float>(height)),
                                  3.0f);
}

void ChordEngineAudioProcessorEditor::EditorLookAndFeel::drawTextEditorOutline(
    juce::Graphics& graphics, int width, int height, juce::TextEditor& editor)
{
    const bool focused = editor.hasKeyboardFocus(true);
    graphics.setColour(focused ? accent : editor.isMouseOver(true) ? border.brighter(0.35f) : border);
    const float ruleHeight = focused ? 1.5f : 1.0f;
    graphics.fillRect(juce::Rectangle<float>(0.0f, static_cast<float>(height) - ruleHeight,
                                             static_cast<float>(width), ruleHeight));
}

// ========================================================== pill button ====
ChordEngineAudioProcessorEditor::PillButton::PillButton(const juce::String& buttonText,
                                                        float cornerRadius,
                                                        float designHeight,
                                                        float designFontHeight)
    : juce::Button(buttonText), cornerRadius_(cornerRadius), designHeight_(designHeight),
      designFontHeight_(designFontHeight)
{
    setClickingTogglesState(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ChordEngineAudioProcessorEditor::PillButton::setPalette(juce::Colour fill,
                                                             juce::Colour textColour,
                                                             juce::Colour outlineColour,
                                                             juce::Colour pressedFill)
{
    fill_ = fill;
    textColour_ = textColour;
    outline_ = outlineColour;
    pressed_ = pressedFill;
    repaint();
}

void ChordEngineAudioProcessorEditor::PillButton::setIndicator(juce::Colour colour)
{
    indicator_ = colour;
    repaint();
}

void ChordEngineAudioProcessorEditor::PillButton::paintButton(juce::Graphics& graphics,
                                                              bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getHeight()) / juce::jmax(1.0f, designHeight_);
    const float radius = juce::jmin(cornerRadius_ * scale, area.getHeight() * 0.5f);
    const bool enabled = isEnabled();

    auto fill = fill_;
    if (enabled && down)
        fill = pressed_.getAlpha() == 0 ? fill_.darker(0.15f) : pressed_;
    else if (enabled && highlighted)
        fill = fill_.isTransparent() ? juce::Colours::white.withAlpha(0.04f) : fill_.brighter(0.07f);

    if (!fill.isTransparent())
    {
        graphics.setColour(enabled ? fill : fill.withMultipliedAlpha(0.45f));
        graphics.fillRoundedRectangle(area, radius);
    }

    if (outline_.getAlpha() > 0)
    {
        graphics.setColour(enabled ? outline_ : outline_.withMultipliedAlpha(0.45f));
        graphics.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    }

    // Keyboard focus: a sand ring just inside the edge, on every button.
    if (hasKeyboardFocus(false))
    {
        graphics.setColour(accent);
        graphics.drawRoundedRectangle(area.reduced(1.0f), juce::jmax(0.0f, radius - 0.5f), 1.5f);
    }

    const auto font = uiFont(juce::jmax(6.0f, designFontHeight_ * scale), true, 0.08f);
    auto content = getLocalBounds().reduced(juce::roundToInt(4.0f * scale), 0);

    // Optional status dot before the label (the recorder's arm / record
    // state), centred together with the text as one unit.
    if (indicator_.getAlpha() > 0)
    {
        const float dot = 6.0f * scale;
        const float gap = 7.0f * scale;
        const float textWidth = juce::GlyphArrangement::getStringWidth(font, getButtonText());
        const float unitWidth = juce::jmin(static_cast<float>(content.getWidth()), dot + gap + textWidth);
        const float left = static_cast<float>(content.getX())
            + (static_cast<float>(content.getWidth()) - unitWidth) * 0.5f;
        graphics.setColour(enabled ? indicator_ : indicator_.withMultipliedAlpha(0.5f));
        graphics.fillEllipse(left, area.getCentreY() - dot * 0.5f, dot, dot);
        content = content.withLeft(juce::roundToInt(left + dot + gap))
                      .withWidth(juce::roundToInt(unitWidth - dot - gap) + 2);
        graphics.setColour(enabled ? textColour_ : textColour_.withMultipliedAlpha(0.5f));
        graphics.setFont(font);
        graphics.drawFittedText(getButtonText(), content, juce::Justification::centredLeft, 1, 0.85f);
        return;
    }

    graphics.setColour(enabled ? textColour_ : textColour_.withMultipliedAlpha(0.5f));
    graphics.setFont(font);
    graphics.drawFittedText(getButtonText(), content, juce::Justification::centred, 1, 0.85f);
}

// ======================================================== stepper button ===
ChordEngineAudioProcessorEditor::StepButton::StepButton(const juce::String& buttonName,
                                                        Glyph glyph)
    : juce::Button(buttonName), glyph_(glyph)
{
    setClickingTogglesState(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ChordEngineAudioProcessorEditor::StepButton::paintButton(juce::Graphics& graphics,
                                                              bool highlighted, bool down)
{
    // A quiet round icon button: hairline circle at rest, a soft fill under
    // the mouse, sand when pressed or focused. The glyph is drawn.
    const auto area = getLocalBounds().toFloat();
    const float side = juce::jmin(area.getWidth(), area.getHeight());
    const auto circle = juce::Rectangle<float>(side, side).withCentre(area.getCentre()).reduced(0.5f);
    const float scale = side / 28.0f;
    const bool focused = hasKeyboardFocus(false);

    if (down || highlighted)
    {
        graphics.setColour(down ? accentSoft : raisedHover);
        graphics.fillEllipse(circle);
    }
    graphics.setColour(focused || down ? accent : highlighted ? border.brighter(0.3f) : border);
    graphics.drawEllipse(circle, focused ? 1.5f : 1.0f);

    graphics.setColour(isEnabled() ? (highlighted || down ? textBright : ::text) : dim.withAlpha(0.6f));
    const auto centre = area.getCentre();
    const float barLength = juce::jmax(4.0f, 9.0f * scale);
    const float barThickness = juce::jmax(1.0f, 1.4f * scale);

    if (glyph_ == Glyph::minus || glyph_ == Glyph::plus)
    {
        graphics.fillRect(juce::Rectangle<float>(centre.x - barLength * 0.5f,
                                                 centre.y - barThickness * 0.5f,
                                                 barLength, barThickness));
        if (glyph_ == Glyph::plus)
            graphics.fillRect(juce::Rectangle<float>(centre.x - barThickness * 0.5f,
                                                     centre.y - barLength * 0.5f,
                                                     barThickness, barLength));
        return;
    }

    const float half = 3.6f * scale;
    const float direction = glyph_ == Glyph::next ? 1.0f : -1.0f;
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - direction * half * 0.45f, centre.y - half);
    chevron.lineTo(centre.x + direction * half * 0.55f, centre.y);
    chevron.lineTo(centre.x - direction * half * 0.45f, centre.y + half);
    graphics.strokePath(chevron, juce::PathStrokeType(barThickness, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
}

// ===================================================== info row label =====
void ChordEngineAudioProcessorEditor::InfoRowLabel::paint(juce::Graphics& graphics)
{
    // "Key  value" rows: the key sits in a fixed column in the quiet tone and
    // the value starts on one shared edge, so a block of rows reads as a
    // small table. Long values wrap inside the value column. The label's
    // text stays exactly what the model publishes.
    const auto value = getText();
    const int separator = value.indexOf("  ");
    const auto font = getFont();
    const auto valueColour = findColour(juce::Label::textColourId);
    const float width = static_cast<float>(getWidth());
    const float keyColumn = separator > 0 ? juce::jmin(width * 0.4f, font.getHeight() * 8.6f) : 0.0f;

    const float rowPitch = juce::jmin(font.getHeight() * 1.5f, static_cast<float>(getHeight()));
    const auto drawBlock = [&graphics, rowPitch](const juce::AttributedString& block, float x, float w)
    {
        juce::TextLayout layout;
        layout.createLayout(block, juce::jmax(1.0f, w));
        const float firstLine = layout.getNumLines() > 0
            ? layout.getLine(0).getLineBoundsY().getLength()
            : 0.0f;
        const float top = juce::jmax(0.0f, (rowPitch - firstLine) * 0.5f);
        layout.draw(graphics, juce::Rectangle<float>(x, top, w, layout.getHeight()));
    };

    if (separator > 0)
    {
        juce::AttributedString key;
        key.append(value.substring(0, separator), font.withStyle(juce::Font::plain), muted);
        drawBlock(key, 0.0f, keyColumn);
    }

    juce::AttributedString row;
    row.setWordWrap(juce::AttributedString::byWord);
    row.append(separator > 0 ? value.substring(separator + 2) : value, font, valueColour);
    drawBlock(row, keyColumn, width - keyColumn);
}

// ===================================================== note tokens label ==
void ChordEngineAudioProcessorEditor::NoteTokensLabel::setLive(bool live)
{
    if (live_ == live)
        return;
    live_ = live;
    repaint();
}

void ChordEngineAudioProcessorEditor::NoteTokensLabel::paint(juce::Graphics& graphics)
{
    // The chord's notes as a typeset sequence, low to high: each pitch class
    // in a medium weight with its octave as a small figure beside it. While
    // a chord sounds the pitch classes take the accent.
    const auto notes = juce::StringArray::fromTokens(getText(), " ", {});
    const float height = static_cast<float>(getHeight());
    const float scale = height / 26.0f;
    const auto pitchFont = uiFont(juce::jmax(7.0f, 17.0f * scale), true, 0.01f);
    const auto octaveFont = uiFont(juce::jmax(7.0f, 11.5f * scale), false, 0.02f);
    const float gap = 20.0f * scale;
    const float baseline = height * 0.5f + pitchFont.getAscent() * 0.38f;
    float x = 0.0f;

    for (const auto& note : notes)
    {
        if (note.isEmpty())
            continue;
        int split = note.length();
        while (split > 0 && (juce::CharacterFunctions::isDigit(note[split - 1]) || note[split - 1] == '-'))
            --split;
        const auto pitch = note.substring(0, split);
        const auto octave = note.substring(split);

        const float pitchWidth = juce::GlyphArrangement::getStringWidth(pitchFont, pitch);
        const float octaveWidth = juce::GlyphArrangement::getStringWidth(octaveFont, octave);
        if (x + pitchWidth + octaveWidth > static_cast<float>(getWidth()))
            break;

        graphics.setColour(live_ ? accent : textBright);
        graphics.setFont(pitchFont);
        graphics.drawSingleLineText(pitch, juce::roundToInt(x), juce::roundToInt(baseline));
        graphics.setColour(live_ ? accent.withAlpha(0.7f) : dim);
        graphics.setFont(octaveFont);
        graphics.drawSingleLineText(octave, juce::roundToInt(x + pitchWidth + 1.5f * scale),
                                    juce::roundToInt(baseline));
        x += pitchWidth + octaveWidth + gap;
    }
}

// ======================================================== take pill =======
ChordEngineAudioProcessorEditor::TakePillButton::TakePillButton(const juce::String& buttonName)
    : juce::Button(buttonName)
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void ChordEngineAudioProcessorEditor::TakePillButton::setTake(int takeNumber, int noteCount)
{
    takeNumber_ = takeNumber;
    noteCount_ = noteCount;
    repaint();
}

void ChordEngineAudioProcessorEditor::TakePillButton::mouseDown(const juce::MouseEvent& event)
{
    dragStarted_ = false;
    if (event.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem("Show in Finder", [safe = juce::Component::SafePointer<TakePillButton>(this)]
        {
            if (safe != nullptr && safe->onReveal)
                safe->onReveal();
        });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
        return;
    }
    juce::Button::mouseDown(event);
}

void ChordEngineAudioProcessorEditor::TakePillButton::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragStarted_ && event.getDistanceFromDragStart() >= 4)
    {
        dragStarted_ = true;
        if (onDragStart)
            onDragStart();
    }
    juce::Button::mouseDrag(event);
}

void ChordEngineAudioProcessorEditor::TakePillButton::paintButton(juce::Graphics& graphics,
                                                                  bool highlighted, bool down)
{
    // A take is a line in a list, not a badge: number and note count on the
    // left, a sand DRAG cue with a drawn arrow on the right, and a hairline
    // under the row. The row lifts slightly under the mouse.
    const auto area = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getHeight()) / 20.0f;

    if (highlighted || down)
    {
        graphics.setColour(down ? accentSoft : juce::Colours::white.withAlpha(0.04f));
        graphics.fillRoundedRectangle(area, 3.0f * scale);
    }
    graphics.setColour(hairline);
    graphics.fillRect(area.withTop(area.getBottom() - 1.0f));

    auto content = getLocalBounds();
    const auto label = uiFont(juce::jmax(6.0f, 9.5f * scale), true, 0.08f);
    const auto quiet = uiFont(juce::jmax(6.0f, 9.5f * scale), false, 0.04f);

    // Drag cue: "DRAG" and a short arrow.
    const float arrowWidth = 9.0f * scale;
    const float arrowX = static_cast<float>(content.getRight()) - arrowWidth;
    const float cy = area.getCentreY();
    juce::Path arrow;
    arrow.startNewSubPath(arrowX, cy);
    arrow.lineTo(arrowX + arrowWidth, cy);
    arrow.startNewSubPath(arrowX + arrowWidth - 3.0f * scale, cy - 3.0f * scale);
    arrow.lineTo(arrowX + arrowWidth, cy);
    arrow.lineTo(arrowX + arrowWidth - 3.0f * scale, cy + 3.0f * scale);
    graphics.setColour(accent);
    graphics.strokePath(arrow, juce::PathStrokeType(juce::jmax(1.0f, 1.2f * scale),
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    graphics.setFont(label);
    graphics.drawText("DRAG", content.withRight(juce::roundToInt(arrowX - 5.0f * scale)),
                      juce::Justification::centredRight, false);

    if (takeNumber_ > 0)
    {
        const juce::String takeText = "TAKE " + juce::String(takeNumber_);
        graphics.setColour(::text);
        graphics.setFont(label);
        graphics.drawText(takeText, content, juce::Justification::centredLeft, false);
        const float takeWidth = juce::GlyphArrangement::getStringWidth(label, takeText);
        graphics.setColour(muted);
        graphics.setFont(quiet);
        graphics.drawText(juce::String(noteCount_) + (noteCount_ == 1 ? " note" : " notes"),
                          content.withTrimmedLeft(juce::roundToInt(takeWidth + 8.0f * scale)),
                          juce::Justification::centredLeft, false);
    }
}

// =================================================== internal audio pill ===
void ChordEngineAudioProcessorEditor::InternalAudioPill::paint(juce::Graphics& graphics)
{
    // Status chip in the DISABLED palette.
    //
    // Exact limitation: the reference's internal audio is its own SineSynth
    // ("Sine Wave Generator1") switched with setBypassed(), which requires an
    // audio output bus. This product is an AU `aumi` MIDI processor and a
    // VST3 MIDI effect - `isBusesLayoutSupported()` accepts no input and no
    // output bus at all - so there is no audio path to bypass. Enabling it
    // would change the plugin format and the host routing the product is
    // built on. The control is therefore published as disabled, never as a
    // fake ON/OFF toggle.
    const auto area = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getHeight()) / 28.0f;

    graphics.setColour(hairline.brighter(0.1f));
    graphics.drawRoundedRectangle(area.reduced(0.5f), 4.0f * scale, 1.0f);
    graphics.setColour(dim);
    graphics.setFont(uiFont(juce::jmax(6.0f, 9.0f * scale), true, 0.1f));
    // "N/A" (not "OFF"): the product cannot provide internal audio at all, so
    // the chip must not imply a state that could be switched. This surface is
    // not a Button and has no click handler.
    graphics.drawText("N/A", getLocalBounds(), juce::Justification::centred, false);
}

// ========================================================== piano =========
ChordEngineAudioProcessorEditor::PianoDisplay::PianoDisplay(ChordEngineAudioProcessor& processor)
    : processor_(processor)
{
    setComponentID("piano-display");
    setOpaque(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

ChordEngineAudioProcessorEditor::PianoDisplay::~PianoDisplay()
{
    // Never strand a note-on if the editor goes away mid-press.
    releaseHeldNote();
}

juce::Rectangle<float> ChordEngineAudioProcessorEditor::PianoDisplay::keyboardBounds() const noexcept
{
    // Editorial V2: the keys run edge to edge across the 720 px content
    // column with no surrounding bed; the top 6 px hold the felt rail.
    const auto scale = static_cast<float>(getWidth()) / 720.0f;
    return getLocalBounds().toFloat().withTrimmedTop(6.0f * scale);
}

bool ChordEngineAudioProcessorEditor::PianoDisplay::isWhiteKey(int note) noexcept
{
    switch (note % 12)
    {
        case 0: case 2: case 4: case 5: case 7: case 9: case 11: return true;
        default: return false;
    }
}

int ChordEngineAudioProcessorEditor::PianoDisplay::whiteKeyCount() const noexcept
{
    int count = 0;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote;
         note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
        if (isWhiteKey(note))
            ++count;
    return count;
}

int ChordEngineAudioProcessorEditor::PianoDisplay::whiteKeysBefore(int note) const noexcept
{
    int count = 0;
    for (int current = ChordEngineAudioProcessor::pianoFirstNote; current < note; ++current)
        if (isWhiteKey(current))
            ++count;
    return count;
}

float ChordEngineAudioProcessorEditor::PianoDisplay::whiteKeyWidth() const noexcept
{
    return keyboardBounds().getWidth() / static_cast<float>(juce::jmax(1, whiteKeyCount()));
}

juce::Rectangle<float> ChordEngineAudioProcessorEditor::PianoDisplay::whiteKeyRect(
    int whiteIndex) const noexcept
{
    const auto keys = keyboardBounds();
    const float width = whiteKeyWidth();
    return { keys.getX() + static_cast<float>(whiteIndex) * width, keys.getY(),
             width - 1.0f, keys.getHeight() };
}

juce::Rectangle<float> ChordEngineAudioProcessorEditor::PianoDisplay::blackKeyRect(
    int note) const noexcept
{
    const auto keys = keyboardBounds();
    const float width = whiteKeyWidth();
    const float blackWidth = width * 0.58f;
    const float blackOffset = width * 0.29f;
    const float blackHeight = keys.getHeight() * 0.62f;
    const float x = keys.getX() + static_cast<float>(whiteKeysBefore(note)) * width - blackOffset;
    return { x, keys.getY(), blackWidth, blackHeight };
}

int ChordEngineAudioProcessorEditor::PianoDisplay::noteAtPosition(
    juce::Point<float> position) const noexcept
{
    const auto keys = keyboardBounds();
    if (!keys.contains(position))
        return -1;

    // Black keys are painted on top of the white row, so they are hit first.
    float nearestBlack = 1.0e9f;
    int blackNote = -1;
    for (int note = ChordEngineAudioProcessor::pianoFirstNote;
         note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;

        const auto black = blackKeyRect(note);
        if (black.contains(position))
        {
            const float distance = std::abs(position.x - black.getCentreX());
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
    for (int note = ChordEngineAudioProcessor::pianoFirstNote;
         note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
    {
        if (!isWhiteKey(note))
            continue;

        if (whiteKeyRect(whiteIndex).contains(position))
            return note;
        ++whiteIndex;
    }

    return -1;
}

void ChordEngineAudioProcessorEditor::PianoDisplay::pressNote(int note) noexcept
{
    if (note == heldNote_)
        return;

    releaseHeldNote();
    if (note < ChordEngineAudioProcessor::pianoFirstNote
        || note > ChordEngineAudioProcessor::pianoLastNote)
        return;

    heldNote_ = note;
    processor_.pushUiNoteOn(ChordEngineAudioProcessor::uiPianoChannel, note,
                            ChordEngineAudioProcessor::uiPianoVelocity);
    repaint();
}

void ChordEngineAudioProcessorEditor::PianoDisplay::releaseHeldNote() noexcept
{
    if (heldNote_ < 0)
        return;

    processor_.pushUiNoteOff(ChordEngineAudioProcessor::uiPianoChannel, heldNote_);
    heldNote_ = -1;
    repaint();
}

void ChordEngineAudioProcessorEditor::PianoDisplay::mouseDown(const juce::MouseEvent& event)
{
    const int note = noteAtPosition(event.position);
    if (note >= 0)
        pressNote(note);
}

void ChordEngineAudioProcessorEditor::PianoDisplay::mouseDrag(const juce::MouseEvent& event)
{
    // Real keyboard behaviour: sliding onto another key releases the old note
    // and sounds the new one. Sliding off the keys releases without retrigger.
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

void ChordEngineAudioProcessorEditor::PianoDisplay::mouseUp(const juce::MouseEvent&)
{
    releaseHeldNote();
}

void ChordEngineAudioProcessorEditor::PianoDisplay::setPreviewNotes(
    const chordengine::core::NoteSet& notes)
{
    previewNotes_.fill(false);
    for (std::size_t index = 0; index < notes.size; ++index)
        previewNotes_[notes.notes[index]] = true;
    repaint();
}

void ChordEngineAudioProcessorEditor::PianoDisplay::paint(juce::Graphics& graphics)
{
    // Natural keys are warm ivory with a soft shade toward the front edge;
    // only C keys carry a label, so the octave structure reads at a glance.
    // Sounding chord notes are sand, the trigger note is terracotta, and the
    // idle C4 preview chord is a light sand tint.
    const float scale = static_cast<float>(getWidth()) / 720.0f;
    const auto area = getLocalBounds().toFloat();
    // No bed or frame: a thin felt rail sits on the page and the keys hang
    // from it, so the keyboard reads as part of the instrument rather than a
    // module in a box.
    graphics.setColour(pianoBed);
    graphics.fillRect(area.withHeight(keyboardBounds().getY()));
    graphics.setColour(accent.withAlpha(0.35f));
    graphics.fillRect(area.withHeight(1.0f));

    // One shared key geometry drives painting and hit testing (whiteKeyRect /
    // blackKeyRect), so the key that lights up is always the key that sounds.
    const float labelHeight = juce::jmax(8.0f, 11.0f * scale);
    const float keyRadius = 3.0f * scale;
    const auto labelFont = uiFont(juce::jmax(6.0f, 8.5f * scale), true);

    const auto stateOf = [this](int note)
    {
        const bool trigger = note == heldNote_
            || processor_.isAnyTriggerNoteDown(static_cast<std::uint8_t>(note))
            || processor_.isAnyTriggerNoteActive(static_cast<std::uint8_t>(note));
        const bool generated = processor_.isAnyOutputNoteActive(static_cast<std::uint8_t>(note));
        const bool preview = previewNotes_[static_cast<std::size_t>(note)];
        return std::array<bool, 3> { trigger, generated, preview };
    };

    int whiteIndex = 0;
    for (int note = processor_.pianoFirstNote; note <= processor_.pianoLastNote; ++note)
    {
        if (!isWhiteKey(note))
            continue;

        const auto key = whiteKeyRect(whiteIndex);
        const auto [trigger, generated, preview] = stateOf(note);
        const bool lit = trigger || generated;

        juce::Path shape;
        shape.addRoundedRectangle(key.getX(), key.getY(), key.getWidth(), key.getHeight(),
                                  keyRadius, keyRadius, false, false, true, true);
        if (lit)
        {
            graphics.setColour(trigger ? triggerWhiteKey : chordWhiteKey);
        }
        else
        {
            const auto top = preview ? previewWhite : whiteKey;
            graphics.setGradientFill(juce::ColourGradient(top, key.getX(), key.getY(),
                                                          preview ? previewWhite.darker(0.04f)
                                                                  : whiteKeyShade,
                                                          key.getX(), key.getBottom(), false));
        }
        graphics.fillPath(shape);

        if (note % 12 == 0)
        {
            graphics.setColour(lit ? onAccent : preview ? onAccent.withAlpha(0.7f) : whiteKeyLabel);
            graphics.setFont(labelFont);
            graphics.drawText(noteName(note),
                              juce::Rectangle<float>(key.getX(), key.getBottom() - labelHeight - 3.0f * scale,
                                                     key.getWidth(), labelHeight),
                              juce::Justification::centred, false);
        }
        ++whiteIndex;
    }

    // A thin shadow under the key bed's back rail gives the row depth.
    const auto keys = keyboardBounds();
    graphics.setColour(juce::Colours::black.withAlpha(0.18f));
    graphics.fillRect(keys.withHeight(2.0f * scale));

    for (int note = processor_.pianoFirstNote; note <= processor_.pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;

        const auto key = blackKeyRect(note);
        const auto [trigger, generated, preview] = stateOf(note);

        juce::Path shape;
        shape.addRoundedRectangle(key.getX(), key.getY(), key.getWidth(), key.getHeight(),
                                  keyRadius, keyRadius, false, false, true, true);
        if (trigger || generated)
            graphics.setColour(trigger ? triggerBlackKey : chordBlackKey);
        else
            graphics.setGradientFill(juce::ColourGradient(preview ? previewBlack.brighter(0.15f) : blackKeyTop,
                                                          key.getX(), key.getY(),
                                                          preview ? previewBlack : blackKey,
                                                          key.getX(), key.getBottom(), false));
        graphics.fillPath(shape);
    }
}

// ============================================================ editor ======
ChordEngineAudioProcessorEditor::ChordEngineAudioProcessorEditor(
    ChordEngineAudioProcessor& processorRef, juce::File licensingStorageDir,
    std::unique_ptr<chordengine::updates::UpdateTransport> updateTransport,
    std::unique_ptr<chordengine::packs::PackStoreTransport> packTransport)
    : juce::AudioProcessorEditor(processorRef), processor_(processorRef), piano_(processorRef),
      packTransport_(std::move(packTransport))
{
    setName("ChordEngine");
    setLookAndFeel(&lookAndFeel_);
    setResizable(true, false);
    setResizeLimits(ChordEngineAudioProcessor::minimumEditorWidth,
                    ChordEngineAudioProcessor::minimumEditorHeight,
                    ChordEngineAudioProcessor::maximumEditorWidth,
                    ChordEngineAudioProcessor::maximumEditorHeight);
    setSize(ChordEngineAudioProcessor::defaultEditorWidth,
            ChordEngineAudioProcessor::defaultEditorHeight);

    wordmark_.setComponentID("header-wordmark");
    addAndMakeVisible(wordmark_);

    // The REAL Music-Prod wordmark asset (260x86 RGBA) is compiled into the
    // plugin as binary data and drawn 1:1 into the reference slot
    // (270, 560, 260, 86), exactly like the reference logoPanel. No redraw,
    // no substitute text wordmark.

    navChord_.setComponentID("chord-button");
    navPacks_.setComponentID("packs-button");
    navInfo_.setComponentID("info-button");
    navChord_.onClick = [this] { setSelectedPage(Page::chord); };
    navPacks_.onClick = [this] { setSelectedPage(Page::packs); };
    navInfo_.onClick = [this] { setSelectedPage(Page::info); };
    addAndMakeVisible(navChord_);
    addAndMakeVisible(navPacks_);
    addAndMakeVisible(navInfo_);

    const auto addFieldLabel = [this](juce::Label& label, const juce::String& title,
                                      const juce::String& componentId)
    {
        configureLabel(label, title, captionSize, dim, true);
        label.setComponentID(componentId);
        addAndMakeVisible(label);
    };
    const auto addCombo = [this](juce::ComboBox& combo, const juce::String& componentId,
                                 float fontSize, bool bold)
    {
        configureComboBox(combo, fontSize, bold);
        combo.setComponentID(componentId);
        addAndMakeVisible(combo);
    };

    addFieldLabel(keyLabel_, "KEY", "key-label");
    addFieldLabel(scaleLabel_, "SCALE", "scale-label");
    addFieldLabel(presetLabel_, "CHORD PRESET", "chord-preset-label");
    addCombo(keySelector_, "key-selector", 11.0f, true);
    addCombo(scaleSelector_, "scale-selector", 11.0f, true);
    addCombo(presetSelector_, "chord-selector", 12.0f, true);

    for (int index = 0; index < static_cast<int>(chordengine::core::ScaleSystem::keyCount); ++index)
        keySelector_.addItem(juce::String(chordengine::core::ScaleSystem::keyName(index).data()),
                             index + 1);
    for (int index = 0; index < static_cast<int>(chordengine::core::ScaleSystem::scaleCount); ++index)
    {
        const auto* scale = chordengine::core::ScaleSystem::findByIndex(index);
        if (scale != nullptr)
            scaleSelector_.addItem(juce::String(scale->name.data()), index + 1);
    }
    // The menu (stock presets, then installed packs) is built once the pack
    // library and the account state exist - see rebuildPresetMenu().
    // The chord preset is the product's main choice: its selector carries the
    // accent outline at rest (see EditorLookAndFeel::drawComboBox).
    presetSelector_.getProperties().set("emphasis", true);

    presetPrevious_.setComponentID("preset-previous");
    presetNext_.setComponentID("preset-next");
    presetPrevious_.setTooltip("Previous chord preset");
    presetNext_.setTooltip("Next chord preset");
    presetPrevious_.onClick = [this] { stepPreset(-1); };
    presetNext_.onClick = [this] { stepPreset(1); };
    addAndMakeVisible(presetPrevious_);
    addAndMakeVisible(presetNext_);
    configureLabel(presetCounter_, {}, captionSize, dim, true, juce::Justification::centredRight);
    presetCounter_.setComponentID("preset-counter");
    addAndMakeVisible(presetCounter_);

    configureLabel(chordCaption_, {}, captionSize, dim, true);
    chordCaption_.setComponentID("chord-caption");
    configureLabel(chordName_, {}, 32.0f, text, true);
    chordName_.setComponentID("chord-name");
    configureLabel(chordNotes_, {}, 11.5f, text);
    chordNotes_.setComponentID("chord-notes");
    addAndMakeVisible(chordCaption_);
    addAndMakeVisible(chordName_);
    addAndMakeVisible(chordNotes_);

    configureLabel(transposeLabel_, "TRANSPOSE", captionSize, dim, true);
    transposeLabel_.setComponentID("transpose-label");
    configureLabel(octaveValue_, "0 OCT", 12.0f, text, true,
                   juce::Justification::centred);
    octaveValue_.setComponentID("transpose-octave");
    addAndMakeVisible(transposeLabel_);
    addAndMakeVisible(octaveValue_);

    octaveDown_.setComponentID("octave-down");
    octaveUp_.setComponentID("octave-up");
    octaveDown_.setTooltip("Transpose the generated chord down one octave");
    octaveUp_.setTooltip("Transpose the generated chord up one octave");
    octaveDown_.onClick = [this]
    {
        const auto current = processor_.configurationSnapshot().transpose.octaveSteps;
        processor_.setTransposeOctave(juce::jmax(-2, current - 1));
        updateStateDisplay();
    };
    octaveUp_.onClick = [this]
    {
        const auto current = processor_.configurationSnapshot().transpose.octaveSteps;
        processor_.setTransposeOctave(juce::jmin(2, current + 1));
        updateStateDisplay();
    };
    addAndMakeVisible(octaveDown_);
    addAndMakeVisible(octaveUp_);

    const std::array<std::pair<PillButton*, chordengine::core::TransposeTarget>, 3> targets {{
        { &transposeWhole_, chordengine::core::TransposeTarget::whole },
        { &transposeLowest_, chordengine::core::TransposeTarget::lowest },
        { &transposeHighest_, chordengine::core::TransposeTarget::highest } }};
    for (const auto& target : targets)
    {
        target.first->setComponentID(target.first == &transposeWhole_ ? "transpose-whole"
                                     : target.first == &transposeLowest_ ? "transpose-lowest"
                                                                        : "transpose-highest");
        target.first->onClick = [this, value = target.second]
        {
            processor_.setTransposeTarget(value);
            updateStateDisplay();
        };
        addAndMakeVisible(*target.first);
    }

    configureLabel(velocityModeLabel_, "VELOCITY MODE", captionSize, dim, true);
    velocityModeLabel_.setComponentID("velocity-mode-label");
    addAndMakeVisible(velocityModeLabel_);
    addCombo(velocityModeSelector_, "velocity-mode-selector", 12.0f, true);
    velocityModeSelector_.addItem("Dynamic", 1);
    velocityModeSelector_.addItem("Maximum", 2);
    velocityModeSelector_.addItem("Fixed", 3);

    configureLabel(fixedVelocityLabel_, "FIXED VELOCITY", captionSize, dim, true);
    fixedVelocityLabel_.setComponentID("fixed-velocity-label");
    addAndMakeVisible(fixedVelocityLabel_);

    fixedVelocitySlider_.setComponentID("fixed-velocity-slider");
    fixedVelocitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    fixedVelocitySlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fixedVelocitySlider_.setRange(1.0, 127.0, 1.0);
    fixedVelocitySlider_.setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(fixedVelocitySlider_);

    configureLabel(fixedVelocityValue_, "100", 12.0f, text, true, juce::Justification::centredRight);
    fixedVelocityValue_.setComponentID("fixed-velocity-value");
    addAndMakeVisible(fixedVelocityValue_);

    addAndMakeVisible(piano_);

    configureLabel(recorderStatus_, "MIDI RECORDER", captionSize, dim, true);
    recorderStatus_.setComponentID("recorder-status");
    configureLabel(recorderCount_, "WAITING FOR ARM", captionSize, dim, true);
    recorderCount_.setComponentID("recorder-count");
    configureLabel(recorderHint_, "Arm, then play. Drag each finished take into your DAW.",
                   10.0f, muted.darker(0.15f), false, juce::Justification::topLeft);
    recorderHint_.setComponentID("recorder-hint");
    // Wrap instead of squeezing the glyphs when the line nearly fits.
    recorderHint_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(recorderStatus_);
    addAndMakeVisible(recorderCount_);
    // Takes are stored as played; the chosen grid applies whenever a take is
    // dragged out, so another grid can be picked and the take dragged again.
    configureComboBox(recordQuantizeSelector_, 10.5f, false);
    recordQuantizeSelector_.setComponentID("record-quantize-selector");
    recordQuantizeSelector_.addSectionHeading("QUANTIZE TAKES");
    {
        const std::array<const char*, chordengine::midi::takeQuantizeCount> quantizeNames {
            "No quantize", "1/6 step", "1/4 step", "1/3 step", "1/2 step", "Step",
            "1/6 beat", "1/4 beat", "1/3 beat", "1/2 beat", "Beat", "Bar" };
        for (std::size_t index = 0; index < quantizeNames.size(); ++index)
        {
            recordQuantizeSelector_.addItem(quantizeNames[index], static_cast<int>(index) + 1);
            if (index == 0 || index == 5 || index == 10)
                recordQuantizeSelector_.addSeparator();
        }
    }
    recordQuantizeSelector_.setTooltip("Quantize dragged-out takes to the host grid. A step is a 1/16 note; "
                                       "note lengths are kept.");
    recordQuantizeSelector_.onChange = [this]
    {
        const auto id = recordQuantizeSelector_.getSelectedId();
        if (id >= 1 && id <= chordengine::midi::takeQuantizeCount)
            processor_.setRecordQuantize(static_cast<ChordEngineAudioProcessor::RecordQuantize>(id - 1));
    };
    addAndMakeVisible(recordQuantizeSelector_);
    addAndMakeVisible(recorderHint_);

    recorderAction_.setComponentID("recorder-action");
    // With FOLLOW DAW RECORD on, the recorder also follows the host's own
    // record button (see ChordEngineAudioProcessor::followHostRecording).
    // Logic Pro can in addition record the generated notes onto the track.
    recorderAction_.setTooltip(juce::PluginHostType().isLogic()
        ? "Arm a take by hand, then play. With FOLLOW DAW RECORD on, pressing Record in Logic Pro "
          "arms it for you. To record the chords straight into the region instead, open this MIDI FX "
          "slot's menu and choose \"Record MIDI to Track Here\"."
        : "Arm a take by hand, then play. With FOLLOW DAW RECORD on, pressing Record in your DAW "
          "arms it for you. Drag finished takes into your arrangement.");
    recorderAction_.setPalette(accent, onAccent);
    recorderAction_.onClick = [this]
    {
        switch (processor_.recorderState())
        {
            case 0: processor_.armRecorder(); break;
            case 1: processor_.cancelRecorder(); break;
            case 2: processor_.stopRecorder(); break;
            default: break;
        }
        updateRecorderDisplay();
    };
    addAndMakeVisible(recorderAction_);

    followHostRecord_.setComponentID("follow-host-record");
    followHostRecord_.setClickingTogglesState(true);
    followHostRecord_.setTooltip("When on, ChordEngine starts a take when you press Record in your DAW "
                                 "and stores it when the DAW stops. A take armed by hand is left to you.");
    followHostRecord_.onClick = [this]
    {
        processor_.setFollowHostRecord(followHostRecord_.getToggleState());
        updateRecorderDisplay();
    };
    addAndMakeVisible(followHostRecord_);

    for (std::size_t index = 0; index < recorderTakes_.size(); ++index)
    {
        auto& pill = recorderTakes_[index];
        pill.setTooltip("Drag this completed MIDI take into your DAW (right-click: Show in Finder).");
        pill.setEnabled(false);
        pill.setVisible(false);
        pill.onDragStart = [this, index] { return startRecorderTakeDrag(index); };
        pill.onReveal = [this, index] { revealRecorderTake(index); };
        addAndMakeVisible(pill);
    }

    configureLabel(internalAudioLabel_, "INTERNAL AUDIO", captionSize, dim, true);
    internalAudioLabel_.setComponentID("internal-audio-label");
    // The MIDI FX AU has no audio bus, so its caption points at the
    // instrument variant instead of implying a switch it cannot have.
    configureLabel(internalAudioSubLabel_, "MIDI FX - load ChordEngine Instrument for sound", 9.0f,
                   faint.brighter(0.3f));
    internalAudioSubLabel_.setComponentID("internal-audio-sub-label");
    // Wrap onto two lines at every size rather than squeezing the glyphs
    // onto one line where the text nearly fits (the largest editor size).
    internalAudioSubLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(internalAudioLabel_);
    addChildComponent(internalAudioSubLabel_);
    internalAudioPill_.setComponentID("internal-audio-status");
    internalAudioPill_.setTooltip("This is the MIDI FX version of ChordEngine: Logic Pro's MIDI FX slot "
                                  "carries no audio buses, so it drives the track's instrument. To hear "
                                  "the built-in synth, piano or bass, insert \"ChordEngine Instrument\" "
                                  "as the track's instrument instead.");
    addChildComponent(internalAudioPill_);

    internalAudioToggle_.setComponentID("internal-audio-toggle");
    internalAudioToggle_.setClickingTogglesState(true);
    internalAudioToggle_.setTooltip("Play the generated chords through ChordEngine's built-in sound, "
                                    "so no other instrument is needed. The MIDI output is unchanged.");
    internalAudioToggle_.onClick = [this]
    {
        processor_.setInternalAudioEnabled(internalAudioToggle_.getToggleState());
        updateInternalAudioDisplay();
    };
    addChildComponent(internalAudioToggle_);
    configureComboBox(internalSoundSelector_, 10.5f, true);
    internalSoundSelector_.setComponentID("internal-sound-selector");
    internalSoundSelector_.addItem("Synth", 1);
    internalSoundSelector_.addItem("Piano", 2);
    internalSoundSelector_.addItem("Bass", 3);
    internalSoundSelector_.setTooltip("Built-in sound: Synth, Piano or Bass (Bass plays the lowest chord note)");
    internalSoundSelector_.onChange = [this]
    {
        const auto id = internalSoundSelector_.getSelectedId();
        if (id >= 1 && id <= 3)
            processor_.setInternalSound(static_cast<ChordEngineAudioProcessor::InternalSound>(id - 1));
        updateInternalAudioDisplay();
    };
    addChildComponent(internalSoundSelector_);
    updateInternalAudioDisplay();

    configureLabel(infoTitle_, "Chord Engine", 26.0f, textBright, false);
    infoTitle_.setComponentID("info-title");
    configureLabel(infoVersion_, "Version v" + productVersion, 11.5f, muted);
    infoVersion_.setComponentID("info-version");
    configureLabel(accountHeader_, "ACCOUNT", captionSize, dim, true);
    accountHeader_.setComponentID("info-account-header");
    configureLabel(infoUser_, "User  Not signed in", 12.0f, text);
    infoUser_.setComponentID("info-user");
    configureLabel(infoPlus_, "Music-Prod+  NOT ACTIVE", 12.0f, accent, true);
    infoPlus_.setComponentID("info-plus");
    configureLabel(infoLicense_, "License  Not verified", 12.0f, text);
    infoLicense_.setComponentID("info-license");
    // The lifetime-licence route. It is the SECOND, independent way to the
    // full version: a permanent ChordEngine key needs no Music-Prod+
    // subscription, and the two routes never affect each other.
    configureLabel(licenseKeyLabel_, "LIFETIME LICENCE KEY", captionSize, dim, true);
    licenseKeyLabel_.setComponentID("license-key-label");
    licenseKeyEditor_.setComponentID("license-key-editor");
    licenseKeyEditor_.setTextToShowWhenEmpty("PC-XXXX-XXXX-XXXX-XXXX", faint.brighter(0.2f));
    licenseKeyEditor_.setColour(juce::TextEditor::backgroundColourId, inset);
    licenseKeyEditor_.setColour(juce::TextEditor::outlineColourId, border);
    licenseKeyEditor_.setColour(juce::TextEditor::focusedOutlineColourId, accent);
    licenseKeyEditor_.setColour(juce::TextEditor::textColourId, textBright);
    licenseKeyEditor_.setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.3f));
    licenseKeyEditor_.setColour(juce::CaretComponent::caretColourId, accent);
    licenseKeyEditor_.setJustification(juce::Justification::centredLeft);
    licenseKeyEditor_.setTooltip("Enter the ChordEngine licence key from your purchase email. "
                                 "It is activated on this computer and needs no Music-Prod+ "
                                 "subscription. The key is never stored on this computer.");
    activateLicense_.setComponentID("license-activate");
    // Secondary action with full-strength label and a sand outline, so it
    // reads as the field's own button even while SIGN IN is the primary.
    activateLicense_.setPalette(juce::Colours::transparentBlack, accent, accent.withAlpha(0.8f), accentSoft);
    activateLicense_.setTooltip("Activate this ChordEngine licence key on this computer.");
    activateLicense_.onClick = [this]
    {
        if (licensing_ == nullptr)
            return;

        // The key is handed over and then cleared from the field: the server
        // binds it to the account and stores only its hash, so there is
        // nothing useful (and nothing safe) in keeping it on screen.
        licensing_->activateLicenseKey(licenseKeyEditor_.getText());
        licenseKeyEditor_.clear();
        updateInfoDisplay();
    };
    configureLabel(updatesHeader_, "UPDATES", captionSize, dim, true);
    updatesHeader_.setComponentID("info-updates-header");
    configureLabel(infoCurrentVersion_, "Current version  " + productVersion, 12.0f, text);
    infoCurrentVersion_.setComponentID("info-current-version");
    configureLabel(infoLatestVersion_, "Latest version  -", 12.0f, text);
    infoLatestVersion_.setComponentID("info-latest-version");
    configureLabel(infoStatus_, "Status  -", 11.5f, muted);
    infoStatus_.setComponentID("info-update-status");
    configureLabel(trialHeader_, "TRIAL", captionSize, dim, true);
    trialHeader_.setComponentID("info-trial-header");
    configureLabel(trialStatus_, "Trial access  -", 12.0f, text);
    trialStatus_.setComponentID("info-trial-status");
    configureLabel(helpTitle_, "HELP & FEEDBACK", captionSize, dim, true);
    helpTitle_.setComponentID("info-help-title");
    configureLabel(helpCopy_, "Need help or want to report a problem with ChordEngine?", 11.0f,
                   muted);
    helpCopy_.setComponentID("info-help-copy");
    for (auto* component : { static_cast<juce::Component*>(&infoTitle_),
                             static_cast<juce::Component*>(&infoVersion_),
                             static_cast<juce::Component*>(&accountHeader_),
                             static_cast<juce::Component*>(&infoUser_),
                             static_cast<juce::Component*>(&infoPlus_),
                             static_cast<juce::Component*>(&infoLicense_),
                             static_cast<juce::Component*>(&licenseKeyLabel_),
                             static_cast<juce::Component*>(&licenseKeyEditor_),
                             static_cast<juce::Component*>(&activateLicense_),
                             static_cast<juce::Component*>(&updatesHeader_),
                             static_cast<juce::Component*>(&infoCurrentVersion_),
                             static_cast<juce::Component*>(&infoLatestVersion_),
                             static_cast<juce::Component*>(&infoStatus_),
                             static_cast<juce::Component*>(&trialHeader_),
                             static_cast<juce::Component*>(&trialStatus_),
                             static_cast<juce::Component*>(&helpTitle_),
                             static_cast<juce::Component*>(&helpCopy_) })
        addAndMakeVisible(*component);

    // The three INFO-page controls of the REAL Music-Prod account bridge.
    // Each one is bound to the reference action - nothing here is a
    // placeholder.
    checkUpdates_.setComponentID("check-updates");
    checkUpdates_.setPalette(juce::Colours::transparentBlack, text, border, raised);
    checkUpdates_.setTooltip("Ask the Music-Prod release service whether a newer ChordEngine "
                             "build has been published. This check is anonymous and needs no sign-in.");
    checkUpdates_.onClick = [this]
    {
        // One real check on the update service. The INFO rows are derived from
        // the service's view, so nothing is faked here and a success never
        // opens Music-Prod Studio.
        if (updates_ != nullptr)
            updates_->checkNow();
        updateInfoDisplay();
    };
    addAndMakeVisible(checkUpdates_);

    openStudio_.setComponentID("open-music-prod-studio");
    openStudio_.setPalette(juce::Colours::transparentBlack, text, border, raised);
    openStudio_.setTooltip("Open Music-Prod Studio through its registered musicprodstudio:// URL scheme.");
    openStudio_.onClick = [this] { openStudio(); };
    addAndMakeVisible(openStudio_);

    signIn_.setComponentID("sign-in");
    signIn_.setPalette(accent, onAccent, juce::Colour(), accent.darker(0.2f));
    signIn_.setTooltip("Sign in to Music-Prod with the existing device-link flow.");
    signIn_.onClick = [this] { licensing_->signInButtonClicked(); updateInfoDisplay(); };
    addAndMakeVisible(signIn_);

    // SIGN OUT shares the SIGN IN slot: exactly one of the pair is ever
    // visible (reference ceRefreshAuthHint).
    signOut_.setComponentID("sign-out");
    signOut_.setPalette(juce::Colours::transparentBlack, text, border, raised);
    signOut_.setTooltip("Sign out of Music-Prod and revoke this plugin token.");
    signOut_.onClick = [this] { licensing_->signOutButtonClicked(); updateInfoDisplay(); };
    addAndMakeVisible(signOut_);

    // Device-link instruction line + the result of the last account action.
    // Only the PUBLIC user code and static copy ever appear here.
    configureLabel(infoAuthHint_, {}, 11.0f, muted, false, juce::Justification::topLeft);
    infoAuthHint_.setComponentID("info-auth-hint");
    addAndMakeVisible(infoAuthHint_);

    // Expiry gate (reference ACCESS EXPIRED modal).
    configureLabel(gateTitle_, chordengine::licensing::expiryModalTitle, 20.0f, textBright,
                   true, juce::Justification::centred);
    gateTitle_.setComponentID("trial-gate-title");
    configureLabel(gateBody_, {}, 12.0f, muted, false, juce::Justification::centredTop);
    gateBody_.setComponentID("trial-gate-body");
    addAndMakeVisible(gateTitle_);
    addAndMakeVisible(gateBody_);

    gateStudio_.setComponentID("trial-gate-studio");
    gateStudio_.setPalette(accent, onAccent, juce::Colour(), accent.darker(0.2f));
    gateStudio_.onClick = [this] { openStudio(); };
    addAndMakeVisible(gateStudio_);

    gateSignIn_.setComponentID("trial-gate-sign-in");
    gateSignIn_.setPalette(juce::Colours::transparentBlack, text, border, raised);
    gateSignIn_.onClick = [this] { licensing_->signInButtonClicked(); updateInfoDisplay(); };
    addAndMakeVisible(gateSignIn_);

    helpButton_.setComponentID("help-feedback");
    helpButton_.setPalette(juce::Colours::transparentBlack, text, border, raised);
    helpButton_.setTooltip("Open the Music-Prod ChordEngine Help & Feedback page in your browser.");
    helpButton_.onClick = []
    {
        juce::URL(ChordEngineAudioProcessor::helpFeedbackUrl).launchInDefaultBrowser();
    };
    addAndMakeVisible(helpButton_);

    configureLabel(footer_, "Chord Engine v" + productVersion, 9.0f, faint, false,
                   juce::Justification::centredRight);
    footer_.setComponentID("footer");
    addAndMakeVisible(footer_);

    // Type scale (editorial V2): captions 8.5 bold letter-spaced; body and
    // values 11-12; the preset name 17; the INFO title 26 and the chord name
    // 54 in the serif display face.
    textSpecs_ = {{
        { &keyLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &scaleLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &presetLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &presetCounter_, captionSize, true, captionKerning, TextRole::caption },
        { &chordCaption_, captionSize, true, captionKerning, TextRole::caption },
        { &chordName_, 54.0f, false, -0.01f, TextRole::display },
        { &chordNotes_, 11.5f, false },
        { &transposeLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &octaveValue_, 12.0f, true, 0.04f },
        { &velocityModeLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &fixedVelocityLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &fixedVelocityValue_, 12.0f, true },
        { &recorderStatus_, captionSize, true, captionKerning, TextRole::caption },
        { &recorderCount_, captionSize, true, captionKerning, TextRole::caption },
        { &recorderHint_, 10.0f, false, 0.01f },
        { &internalAudioLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &internalAudioSubLabel_, 9.0f, false },
        { &infoTitle_, 26.0f, false, 0.0f, TextRole::display },
        { &packsTitle_, 26.0f, false, 0.0f, TextRole::display },
        { &packsSubtitle_, 11.5f, false },
        { &packsStatus_, 11.0f, false },
        { &infoVersion_, 11.5f, false },
        { &accountHeader_, captionSize, true, captionKerning, TextRole::caption },
        { &infoUser_, 12.0f, false },
        { &infoPlus_, 12.0f, true },
        { &infoLicense_, 12.0f, false },
        { &licenseKeyLabel_, captionSize, true, captionKerning, TextRole::caption },
        { &updatesHeader_, captionSize, true, captionKerning, TextRole::caption },
        { &infoCurrentVersion_, 12.0f, false },
        { &infoLatestVersion_, 12.0f, false },
        { &infoStatus_, 11.5f, false },
        { &infoAuthHint_, 11.0f, false },
        { &trialHeader_, captionSize, true, captionKerning, TextRole::caption },
        { &trialStatus_, 12.0f, false },
        { &helpTitle_, captionSize, true, captionKerning, TextRole::caption },
        { &helpCopy_, 11.5f, false },
        { &footer_, 9.0f, false, 0.04f },
        { &gateTitle_, 13.0f, true, 0.3f },
        { &gateBody_, 12.0f, false },
    }};

    keySelector_.onChange = [this]
    {
        processor_.setKeyIndex(keySelector_.getSelectedId() - 1);
        updateStateDisplay();
    };
    scaleSelector_.onChange = [this]
    {
        processor_.setScale(static_cast<chordengine::core::ScaleId>(scaleSelector_.getSelectedId() - 1));
        updateStateDisplay();
    };
    presetSelector_.onChange = [this]
    {
        selectPresetEntry(presetSelector_.getSelectedId() - 1);
    };
    velocityModeSelector_.onChange = [this]
    {
        processor_.setVelocityMode(static_cast<chordengine::core::VelocityMode>(
            velocityModeSelector_.getSelectedId() - 1));
        updateVelocityEnabled();
        updateStateDisplay();
    };
    fixedVelocitySlider_.onValueChange = [this]
    {
        const auto value = static_cast<int>(fixedVelocitySlider_.getValue());
        processor_.setFixedVelocity(value);
        fixedVelocityValue_.setText(juce::String(value), juce::dontSendNotification);
    };

    updateStateDisplay();
    updateVelocityEnabled();
    setSelectedPage(false);
    updateRecorderDisplay();

    // ---- real Music-Prod logo asset --------------------------------
    logoImage_ = makeMonochromeMark(juce::ImageFileFormat::loadFrom(BinaryData::MusicProdLogo_png,
                                                                   BinaryData::MusicProdLogo_pngSize),
                                    muted);
    jassert(logoImage_.isValid());

    // ---- Music-Prod state folder (reference ceInitialize) -----------
    // The account token and the update cache live next to the other
    // Music-Prod plugin state, in the same AppData location the reference and
    // VYRE use.
    const auto storageDir = licensingStorageDir != juce::File()
        ? licensingStorageDir
        : [] {
              auto appData = juce::File::getSpecialLocation(
                  juce::File::userApplicationDataDirectory);
              if (appData.getFileName() == "Library")
                  appData = appData.getChildFile("Application Support");
              return appData.getChildFile("Music-Prod")
                            .getChildFile("ChordEngine");
          }();
    const auto authFile = storageDir.getChildFile("auth.json");
    takeExportDir_ = storageDir.getChildFile("Takes");

    licensing_ = std::make_unique<chordengine::licensing::LicensingBoundary>(
        authService_, updateService_, authFile);
    licensing_->setGateSink([this](bool gateOpen) { processor_.setLicensingGateOpen(gateOpen); });
    licensing_->initialise();
    licensing_->setInfoPageVisible(false);

    // ---- Preset Packs: the installed library, the store client and the
    // PACKS page. The catalog is fetched the first time the page opens.
    packLibrary_ = std::make_unique<chordengine::packs::PackLibrary>(storageDir.getChildFile("Packs"));
    packLibrary_->scan();
    if (packTransport_ == nullptr)
    {
       #if CHORDENGINE_UPDATE_OFFLINE_TRANSPORT
        packTransport_ = std::make_unique<OfflinePackStoreTransport>();
       #else
        packTransport_ = std::make_unique<chordengine::packs::HttpPackStoreTransport>();
       #endif
    }
    packStore_ = std::make_unique<chordengine::packs::PackStoreClient>(
        *packTransport_, *packLibrary_, packStoreOrigin());

    configureLabel(packsTitle_, "Preset Packs", 26.0f, textBright, false);
    packsTitle_.setComponentID("packs-title");
    configureLabel(packsSubtitle_, {}, 11.5f, muted);
    packsSubtitle_.setComponentID("packs-subtitle");
    configureLabel(packsStatus_, {}, 11.0f, muted, false, juce::Justification::centredRight);
    packsStatus_.setComponentID("packs-status");
    packsRefresh_.setComponentID("packs-refresh");
    packsRefresh_.setPalette(juce::Colours::transparentBlack, text, border, raised);
    packsRefresh_.setTooltip("Reload the Music-Prod pack catalog");
    packsRefresh_.onClick = [this] { refreshPackCatalog(); };
    packsSignIn_.setComponentID("packs-sign-in");
    packsSignIn_.setPalette(accent, onAccent);
    packsSignIn_.setTooltip("Sign in with your Music-Prod account to download packs");
    packsSignIn_.onClick = [this]
    {
        licensing_->signInButtonClicked();
        updatePacksDisplay(true);
    };
    packList_.setComponentID("packs-list");
    packList_.onAction = [this](int index) { packActionClicked(index); };
    packsViewport_.setComponentID("packs-viewport");
    packsViewport_.setViewedComponent(&packList_, false);
    packsViewport_.setScrollBarsShown(true, false);
    packsViewport_.setScrollBarThickness(6);
    for (auto* component : std::initializer_list<juce::Component*> {
             &packsTitle_, &packsSubtitle_, &packsStatus_, &packsRefresh_, &packsSignIn_,
             &packsViewport_ })
        addChildComponent(component);
    rebuildPresetMenu();
    updatePacksDisplay(true);
    lastLicensingTickMs_ = juce::Time::currentTimeMillis();
    // The gate body is derived state, never a component default: seed it from
    // the same model the modal reads so the reference wording is always shown.
    gateBody_.setText(chordengine::licensing::expiryModalBody(licensing_->snapshot()),
                      juce::dontSendNotification);

    // ---- the real, anonymous update client --------------------------
    // It is NOT part of LicensingBoundary: the check needs no account state and
    // licensing never reads update state. Every request runs on the service's
    // own single background worker and the view is published back on the
    // message thread, so neither the audio thread nor the GUI thread is ever
    // blocked by the network.
    updates_ = std::make_unique<chordengine::updates::UpdateService>(
        // The version THIS binary was built as, from the build metadata of this
        // translation unit - never a hardcoded product version.
        chordengine::updates::currentBuildVersionString,
        storageDir.getChildFile(chordengine::updates::UpdateCache::fileName),
        updateTransport != nullptr ? std::move(updateTransport)
                                   : chordengine::updates::makeDefaultUpdateTransport());
    updates_->onViewChanged = [safeThis = juce::Component::SafePointer<
                                   ChordEngineAudioProcessorEditor>(this)]
    {
        if (safeThis != nullptr)
            safeThis->updateInfoDisplay();
    };

    updateInfoDisplay();

    // On editor open: publish the cached answer at once (it is marked as
    // cached) and then refresh it exactly ONCE. Nothing here can loop: the
    // automatic check happens once per service instance and only a 503 is
    // retried, a single time.
    updates_->ensureStartupCheck();
    updateInfoDisplay();

    // setSize() above ran before the type scale and the selector emphasis
    // existed, and a host that opens the editor at its default size never
    // resizes it, so lay the editor out once more with everything in place.
    resized();

    startTimerHz(20);
}

ChordEngineAudioProcessorEditor::~ChordEngineAudioProcessorEditor()
{
    stopTimer();

    // Stop the update client before any surface it can notify goes away: the
    // callback is cleared first and then the service (and its worker thread) is
    // destroyed, so no late reply can reach a half-destroyed editor.
    if (updates_ != nullptr)
    {
        updates_->onViewChanged = nullptr;
        updates_.reset();
    }

    // Release any note the on-screen keyboard is still holding.
    piano_.releaseHeldNote();
    for (auto& pill : recorderTakes_)
    {
        pill.onDragStart = nullptr;
        pill.onReveal = nullptr;
    }
    for (auto* combo : { &keySelector_, &scaleSelector_, &presetSelector_, &velocityModeSelector_ })
        combo->setLookAndFeel(nullptr);
    fixedVelocitySlider_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void ChordEngineAudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& value,
                                                     float fontSize, juce::Colour colour,
                                                     bool bold,
                                                     juce::Justification justification)
{
    label.setText(value, juce::dontSendNotification);
    label.setJustificationType(justification);
    label.setFont(uiFont(fontSize, bold));
    label.setColour(juce::Label::textColourId, colour);
    // No inner padding: every caption, value and heading starts exactly on
    // the column edge it is laid out on, so text and rules align.
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setInterceptsMouseClicks(false, false);
}

void ChordEngineAudioProcessorEditor::configureComboBox(juce::ComboBox& combo, float, bool)
{
    combo.setLookAndFeel(&lookAndFeel_);
    combo.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    combo.setColour(juce::ComboBox::backgroundColourId, raised);
    combo.setColour(juce::ComboBox::textColourId, text);
    combo.setColour(juce::ComboBox::outlineColourId, border);
    combo.setColour(juce::ComboBox::focusedOutlineColourId, accent);
    combo.setColour(juce::ComboBox::arrowColourId, accent);
    combo.setColour(juce::ComboBox::buttonColourId, raised);
    combo.setColour(juce::PopupMenu::backgroundColourId, panel);
    combo.setColour(juce::PopupMenu::textColourId, text);
    combo.setColour(juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
    combo.setColour(juce::PopupMenu::highlightedTextColourId, textBright);
}

void ChordEngineAudioProcessorEditor::updateDesignMapping() noexcept
{
    const float width = static_cast<float>(juce::jmax(1, getWidth()));
    const float height = static_cast<float>(juce::jmax(1, getHeight()));
    designScale_ = juce::jmin(width / static_cast<float>(ChordEngineAudioProcessor::defaultEditorWidth),
                              height / static_cast<float>(ChordEngineAudioProcessor::defaultEditorHeight));
    designScale_ = juce::jmax(0.1f, designScale_);
    designOffsetX_ = juce::roundToInt((width - static_cast<float>(ChordEngineAudioProcessor::defaultEditorWidth) * designScale_) * 0.5f);
    designOffsetY_ = juce::roundToInt((height - static_cast<float>(ChordEngineAudioProcessor::defaultEditorHeight) * designScale_) * 0.5f);
}

juce::Rectangle<int> ChordEngineAudioProcessorEditor::design(int x, int y, int width,
                                                             int height) const noexcept
{
    const auto mapX = [this](int value) { return designOffsetX_ + juce::roundToInt(static_cast<float>(value) * designScale_); };
    const auto mapY = [this](int value) { return designOffsetY_ + juce::roundToInt(static_cast<float>(value) * designScale_); };
    const auto mapSize = [this](int value) { return juce::jmax(1, juce::roundToInt(static_cast<float>(value) * designScale_)); };
    return { mapX(x), mapY(y), mapSize(width), mapSize(height) };
}

float ChordEngineAudioProcessorEditor::textScale() const noexcept
{
    // Text scales more gently than the layout: at the 640x520 minimum the
    // type is a little larger relative to its space (legibility first), and
    // at 1280x1040 a little smaller (the extra room becomes air around it).
    return std::pow(designScale_, 0.85f);
}

void ChordEngineAudioProcessorEditor::applyTypography()
{
    // The display role grows slightly faster than the layout, so the chord
    // name gains presence on a large editor and steps back on a small one.
    const float displayScale = std::pow(designScale_, 1.08f);
    for (const auto& spec : textSpecs_)
    {
        if (spec.label == nullptr)
            continue;
        if (spec.role == TextRole::display)
            spec.label->setFont(displayFont(juce::jmax(8.0f, spec.designFontSize * displayScale),
                                            spec.kerning));
        else if (spec.role == TextRole::caption)
            spec.label->setFont(captionFont(juce::jmax(6.0f, spec.designFontSize * textScale()),
                                            spec.kerning));
        else
            spec.label->setFont(uiFont(juce::jmax(6.0f, spec.designFontSize * textScale()),
                                       spec.bold, spec.kerning));
    }

    // The licence-key field is a TextEditor rather than a Label, so it
    // carries its own font on the same scale as its label. Keys are set in
    // the monospaced face so each group lines up as typed.
    licenseKeyEditor_.applyFontToAllText(juce::Font(juce::FontOptions(
        juce::Font::getDefaultMonospacedFontName(), juce::jmax(8.0f, 12.0f * textScale()),
        juce::Font::plain)).withExtraKerningFactor(0.06f));
    licenseKeyEditor_.setIndents(juce::roundToInt(10.0f * designScale_), 0);

    lookAndFeel_.setFontHeight(juce::jmax(8.0f, 12.0f * textScale()));
    for (auto* combo : { &keySelector_, &scaleSelector_, &presetSelector_, &velocityModeSelector_ })
    {
        combo->lookAndFeelChanged();
        combo->resized();
    }
}

void ChordEngineAudioProcessorEditor::paintSurface(juce::Graphics& graphics,
                                                  juce::Rectangle<int> bounds, juce::Colour fill,
                                                  float cornerRadius, juce::Colour outline) const
{
    const auto area = bounds.toFloat();
    const float radius = cornerRadius * designScale_;
    graphics.setColour(fill);
    graphics.fillRoundedRectangle(area, radius);
    if (outline.getAlpha() > 0)
    {
        graphics.setColour(outline);
        graphics.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    }
}

void ChordEngineAudioProcessorEditor::paintHairline(juce::Graphics& graphics,
                                                   juce::Rectangle<int> bounds) const
{
    graphics.setColour(hairline);
    if (bounds.getWidth() >= bounds.getHeight())
        graphics.fillRect(bounds.toFloat().withHeight(1.0f));
    else
        graphics.fillRect(bounds.toFloat().withWidth(1.0f));
}

void ChordEngineAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    // Editorial V2: one open graphite page. No outer frame, no hero panel,
    // no cards - structure comes from type, alignment and a few hairline
    // rules. The near-black ground only shows outside the design canvas.
    graphics.fillAll(background);
    const auto canvas = design(0, 0, ChordEngineAudioProcessor::defaultEditorWidth,
                               ChordEngineAudioProcessor::defaultEditorHeight);
    graphics.setColour(page);
    graphics.fillRect(canvas);

    // Header rule, and the active page marked by a short sand underline
    // beneath its navigation label.
    paintHairline(graphics, design(40, 60, 720, 1));
    const auto& activeNav = infoPageVisible_ ? navInfo_ : (packsPageVisible_ ? navPacks_ : navChord_);
    const auto navBounds = activeNav.getBounds().toFloat();
    graphics.setColour(accent);
    graphics.fillRect(juce::Rectangle<float>(navBounds.getX() + 10.0f * designScale_,
                                             navBounds.getBottom() + 1.0f * designScale_,
                                             navBounds.getWidth() - 20.0f * designScale_,
                                             juce::jmax(1.0f, 1.5f * designScale_)));

    if (!gateVisible_)
    {
        if (infoPageVisible_)
            paintInfoPage(graphics);
        else if (packsPageVisible_)
            paintHairline(graphics, design(40, 146, 720, 1));
        else
            paintChordPage(graphics);
    }

    drawLogo(graphics);

    // Expiry gate: fully opaque backdrop so no page content can show through
    // (reference trialGatePanel + trialGateCardPanel). Typography only - the
    // title, a short terracotta rule, the explanation and the two routes.
    if (gateVisible_)
    {
        graphics.setColour(page);
        graphics.fillRect(canvas.withTrimmedTop(design(0, 0, 1, 64).getHeight()));
        graphics.setColour(terracotta);
        graphics.fillRect(design(380, 282, 40, 2).toFloat().withHeight(juce::jmax(1.0f, 1.5f * designScale_)));
    }
}

void ChordEngineAudioProcessorEditor::paintChordPage(juce::Graphics& graphics) const
{
    // The recorder column is set off from the chord identity by one rule.
    paintHairline(graphics, design(568, 86, 1, 120));

    // Secondary controls: a rule above the line and quiet rules between the
    // three groups (transpose | velocity | internal audio).
    paintHairline(graphics, outputStripBounds().withHeight(1));
    paintHairline(graphics, design(360, 512, 1, 40));
    paintHairline(graphics, design(628, 512, 1, 40));

    // Transpose mode: the active target is marked with the same sand
    // underline the navigation uses.
    const auto mode = displayedConfiguration_.transpose.target;
    const PillButton* activeTarget = mode == chordengine::core::TransposeTarget::lowest
        ? &transposeLowest_
        : mode == chordengine::core::TransposeTarget::highest ? &transposeHighest_
                                                               : &transposeWhole_;
    const auto target = activeTarget->getBounds().toFloat();
    graphics.setColour(accent);
    graphics.fillRect(juce::Rectangle<float>(target.getX() + 8.0f * designScale_,
                                             target.getBottom() - 3.0f * designScale_,
                                             target.getWidth() - 16.0f * designScale_,
                                             juce::jmax(1.0f, 1.5f * designScale_)));

    // The octave value sits on a faint inset between its two steppers.
    paintSurface(graphics, octaveValue_.getBounds(), inset, 4.0f);
}

void ChordEngineAudioProcessorEditor::paintInfoPage(juce::Graphics& graphics) const
{
    // Rules between the identity and each section; the section names sit in
    // the left column, their content in the right.
    for (const int y : { 146, 284, 390, 524 })
        paintHairline(graphics, design(40, y, 720, 1));
}

void ChordEngineAudioProcessorEditor::drawLogo(juce::Graphics& graphics) const
{
    if (!logoImage_.isValid() || gateVisible_)
        return;

    // The Music-Prod mark signs the foot of the page at the asset's own
    // aspect ratio (260 x 86), as a quiet ivory mark with no backing slab.
    graphics.setOpacity(0.7f);
    graphics.drawImage(logoImage_, design(40, 600, 72, 24).toFloat(),
                       juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    graphics.setOpacity(1.0f);
}

void ChordEngineAudioProcessorEditor::resized()
{
    updateDesignMapping();
    applyTypography();
    repaint();

    // ---- header: quiet wordmark, text navigation ------------------------
    // Centred on y = 36, the navigation's centre line; tall enough for the
    // script's ascenders and descenders.
    wordmark_.setBounds(design(40, 16, 240, 40));
    navChord_.setBounds(design(588, 24, 56, 24));
    navPacks_.setBounds(design(648, 24, 56, 24));
    navInfo_.setBounds(design(708, 24, 52, 24));

    // PACKS page: identity block like INFO, then the scrolling pack list.
    packsTitle_.setBounds(design(40, 80, 440, 36));
    packsSubtitle_.setBounds(design(40, 118, 520, 18));
    packsRefresh_.setBounds(design(660, 84, 100, 28));
    packsSignIn_.setBounds(design(552, 84, 100, 28));
    packsStatus_.setBounds(design(400, 118, 360, 18));
    packsViewport_.setBounds(design(40, 158, 720, 420));
    packList_.setScale(designScale_);
    updatePacksDisplay(true);

    // ---- chord identity: caption, display name, note sequence -----------
    chordCaption_.setBounds(design(40, 86, 440, 12));
    chordName_.setBounds(design(40, 100, 516, 64));
    chordNotes_.setBounds(design(40, 172, 512, 26));

    // ---- recorder column (right of the identity, no card) ----------------
    // Title line, then the state on its own full-width line (so the longest
    // state, ARMED - PLAY TO RECORD, is never clipped), then the takes.
    recorderStatus_.setBounds(design(588, 84, 84, 12));
    recordQuantizeSelector_.setBounds(design(676, 78, 84, 22));
    recorderCount_.setBounds(design(588, 100, 172, 12));
    recorderTakes_[0].setBounds(design(588, 118, 172, 19));
    recorderTakes_[1].setBounds(design(588, 138, 172, 19));
    recorderTakes_[2].setBounds(design(588, 158, 172, 19));
    recorderHint_.setBounds(design(588, 120, 172, 48));
    recorderAction_.setBounds(design(recorderCardX + (recorderCardWidth - recorderActionWidth) / 2,
                                     recorderActionY, recorderActionWidth, recorderActionHeight));
    followHostRecord_.setBounds(design(recorderCardX + (recorderCardWidth - recorderActionWidth) / 2,
                                       recorderActionY + recorderActionHeight + 4, recorderActionWidth, 20));

    // ---- musical configuration: preset (primary), key, scale -------------
    presetLabel_.setBounds(design(40, 236, 160, 12));
    presetCounter_.setBounds(design(296, 236, 80, 12));
    presetSelector_.setBounds(design(40, 252, 264, 32));
    presetPrevious_.setBounds(design(316, 254, 28, 28));
    presetNext_.setBounds(design(348, 254, 28, 28));
    keyLabel_.setBounds(design(424, 236, 96, 12));
    keySelector_.setBounds(design(424, 252, 96, 32));
    scaleLabel_.setBounds(design(552, 236, 208, 12));
    scaleSelector_.setBounds(design(552, 252, 208, 32));

    // ---- keyboard: full content width, hanging from its felt rail --------
    piano_.setBounds(design(40, 302, 720, 180));

    // ---- secondary controls: one open line under a rule ------------------
    transposeLabel_.setBounds(design(40, 508, 160, 12));
    octaveDown_.setBounds(design(40, 526, 28, 28));
    octaveValue_.setBounds(design(72, 526, 52, 28));
    octaveUp_.setBounds(design(128, 526, 28, 28));
    transposeWhole_.setBounds(design(170, 526, 58, 28));
    transposeLowest_.setBounds(design(230, 526, 58, 28));
    transposeHighest_.setBounds(design(290, 526, 60, 28));

    velocityModeLabel_.setBounds(design(376, 508, 104, 12));
    velocityModeSelector_.setBounds(design(376, 524, 100, 30));
    fixedVelocityLabel_.setBounds(design(492, 508, 124, 12));
    fixedVelocitySlider_.setBounds(design(488, 526, 96, 28));
    fixedVelocityValue_.setBounds(design(586, 526, 30, 28));

    internalAudioLabel_.setBounds(design(644, 508, 116, 12));
    internalAudioPill_.setBounds(design(644, 526, 40, 28));
    internalAudioSubLabel_.setBounds(design(692, 524, 68, 32));
    internalAudioToggle_.setBounds(design(644, 526, 40, 28));
    internalSoundSelector_.setBounds(design(692, 524, 68, 32));

    // ---- INFO: identity, then labelled sections in two columns -----------
    // Section names sit in a 136 px label column (x 40); their content
    // starts on one shared edge at x 192.
    infoTitle_.setBounds(design(40, 78, 360, 40));
    infoVersion_.setBounds(design(40, 118, 360, 16));

    accountHeader_.setBounds(design(40, 164, 136, 12));
    infoUser_.setBounds(design(192, 158, 420, 20));
    infoPlus_.setBounds(design(192, 180, 420, 20));
    infoLicense_.setBounds(design(192, 202, 568, 32));
    signIn_.setBounds(design(192, 242, 104, 28));
    signOut_.setBounds(design(192, 242, 104, 28));
    openStudio_.setBounds(design(304, 242, 196, 28));

    // The lifetime-licence route has its own section: the key field and
    // ACTIVATE share one line, and the result line below has room for a
    // full sentence (an activation error such as a full house of device
    // slots is a sentence, not a word).
    licenseKeyLabel_.setBounds(design(40, 302, 136, 26));
    licenseKeyEditor_.setBounds(design(192, 298, 300, 30));
    activateLicense_.setBounds(design(504, 298, 112, 30));
    infoAuthHint_.setBounds(design(192, 336, 568, 44));

    updatesHeader_.setBounds(design(40, 408, 136, 12));
    infoCurrentVersion_.setBounds(design(192, 402, 340, 20));
    infoLatestVersion_.setBounds(design(192, 424, 340, 20));
    infoStatus_.setBounds(design(192, 446, 380, 30));
    checkUpdates_.setBounds(design(600, 402, 160, 28));
    trialHeader_.setBounds(design(40, 488, 136, 12));
    trialStatus_.setBounds(design(192, 482, 568, 30));

    // Help is one line at the foot of the content (infoHelpPanelBounds).
    helpTitle_.setBounds(design(40, 547, 136, 12));
    helpCopy_.setBounds(design(192, 543, 380, 20));
    helpButton_.setBounds(design(580, 539, 180, 28));

    footer_.setBounds(design(600, 604, 160, 16));

    // ---- expiry gate ------------------------------------------------------
    gateTitle_.setBounds(design(200, 248, 400, 24));
    gateBody_.setBounds(design(200, 296, 400, 60));
    gateStudio_.setBounds(design(290, 372, 220, 30));
    gateSignIn_.setBounds(design(290, 410, 220, 30));
}

void ChordEngineAudioProcessorEditor::updateStateDisplay()
{
    const auto configuration = processor_.configurationSnapshot();
    displayedConfiguration_ = configuration;
    keySelector_.setSelectedId(configuration.keyIndex + 1, juce::dontSendNotification);
    scaleSelector_.setSelectedId(static_cast<int>(configuration.scale) + 1,
                                 juce::dontSendNotification);
    const auto packPreset = processor_.packPreset();
    displayedPackKey_ = packPreset.has_value() ? packPreset->packSlug + "/" + packPreset->presetName
                                               : juce::String();
    const auto entry = currentPresetEntry();
    if (entry >= 0)
        presetSelector_.setSelectedId(entry + 1, juce::dontSendNotification);
    else if (packPreset.has_value())
        // A pack preset restored from the project whose pack is not usable on
        // this machine: it still plays (the project carries its chord map).
        presetSelector_.setText(packPreset->presetName, juce::dontSendNotification);
    velocityModeSelector_.setSelectedId(static_cast<int>(configuration.velocityMode) + 1,
                                        juce::dontSendNotification);

    const auto target = configuration.transpose.target;
    // Transpose mode is set as three words; the active one is ivory and
    // carries the sand underline painted by paintChordPage(), the others
    // are quiet. No segmented track.
    const auto paintTarget = [](PillButton& button, bool active)
    {
        button.setPalette(juce::Colours::transparentBlack, active ? textBright : dim);
    };
    paintTarget(transposeWhole_, target == chordengine::core::TransposeTarget::whole);
    paintTarget(transposeLowest_, target == chordengine::core::TransposeTarget::lowest);
    paintTarget(transposeHighest_, target == chordengine::core::TransposeTarget::highest);

    const auto octaveSteps = configuration.transpose.octaveSteps;
    octaveValue_.setText(juce::String(octaveSteps > 0 ? "+" + juce::String(octaveSteps)
                                                      : juce::String(octaveSteps)) + " OCT",
                         juce::dontSendNotification);

    fixedVelocitySlider_.setValue(configuration.fixedVelocity, juce::dontSendNotification);
    fixedVelocityValue_.setText(juce::String(configuration.fixedVelocity),
                                juce::dontSendNotification);

    const auto presetPosition = entry + 1;
    const auto presetTotal = static_cast<int>(presetEntries_.size());
    presetCounter_.setText(juce::String(presetPosition).paddedLeft('0', 2) + " / "
                               + juce::String(presetTotal).paddedLeft('0', 2),
                           juce::dontSendNotification);

    piano_.setPreviewNotes(processor_.previewChordForTrigger(60));
    updateVelocityEnabled();
    updateChordReadout();
    // The transpose-mode underline is painted by the editor itself.
    repaint();
}

void ChordEngineAudioProcessorEditor::stepPreset(int direction)
{
    // Browse in menu order (stock, then installed packs) and wrap at either
    // end, through the same path the selector uses.
    const auto count = static_cast<int>(presetEntries_.size());
    if (count <= 0)
        return;
    const auto current = juce::jmax(0, currentPresetEntry());
    selectPresetEntry(((current + direction) % count + count) % count);
}

void ChordEngineAudioProcessorEditor::updateVelocityEnabled()
{
    const auto fixed = processor_.configurationSnapshot().velocityMode
        == chordengine::core::VelocityMode::fixed;
    fixedVelocitySlider_.setEnabled(fixed);
    fixedVelocityValue_.setEnabled(fixed);
    // A disabled JUCE slider keeps its full colouring, so a control that looks
    // live but ignores the mouse is exactly the "static prototype" problem.
    // Dimming the whole card's interactive half makes the disabled state
    // unmistakable: in Dynamic/Maximum the slider is inert AND visibly inert.
    fixedVelocitySlider_.setAlpha(fixed ? 1.0f : 0.35f);
    fixedVelocityValue_.setAlpha(fixed ? 1.0f : 0.45f);
    fixedVelocitySlider_.setTooltip(fixed
        ? "Drag or click to set the fixed output velocity (1-127)."
        : "Inactive: choose Velocity Mode = Fixed to enable the fixed velocity control.");
}

void ChordEngineAudioProcessorEditor::updateChordReadout()
{
    const auto configuration = processor_.configurationSnapshot();
    const auto* scale = chordengine::core::ScaleSystem::find(configuration.scale);
    const auto* preset = chordengine::core::ChordPresetSystem::findByIndex(
        static_cast<int>(configuration.preset));
    if (scale == nullptr || preset == nullptr)
        return;

    int trigger = -1;
    for (int note = chordengine::core::ChordPresetSystem::triggerLow;
         note <= chordengine::core::ChordPresetSystem::triggerHigh; ++note)
    {
        if (processor_.isAnyTriggerNoteDown(static_cast<std::uint8_t>(note))
            || processor_.isAnyTriggerNoteActive(static_cast<std::uint8_t>(note)))
        {
            trigger = note;
            break;
        }
    }

    if (trigger >= 0)
    {
        // Live chord: the notes the engine is actually sounding, with the
        // reference chord symbol for the resolved trigger note.
        juce::String notes;
        for (int note = ChordEngineAudioProcessor::pianoFirstNote;
             note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
        {
            if (!processor_.isAnyOutputNoteActive(static_cast<std::uint8_t>(note)))
                continue;
            if (notes.isNotEmpty())
                notes << " ";
            notes << noteName(note);
        }

        if (notes.isEmpty())
            notes = noteListText(processor_.previewChordForTrigger(trigger));

        chordCaption_.setText("PLAYING - TRIGGER " + noteName(trigger), juce::dontSendNotification);
        chordCaption_.setColour(juce::Label::textColourId, accent);
        chordName_.setText(chordSymbolFor(trigger, configuration), juce::dontSendNotification);
        chordName_.setColour(juce::Label::textColourId, accent);
        chordNotes_.setText(notes, juce::dontSendNotification);
        chordNotes_.setLive(true);
    }
    else
    {
        // Idle: the reference context line, with the Core's C4 preview chord
        // (the note the readout has always previewed) in the note slot.
        // Editorial V2: the idle headline is the C4 preview chord's own
        // symbol, resolved by the Core exactly like the live readout, so the
        // composition never changes shape between preview and playing - only
        // its colour and caption do. The key, scale and preset it depends on
        // are shown directly below in the configuration line.
        chordName_.setText(chordSymbolFor(60, configuration), juce::dontSendNotification);
        chordName_.setColour(juce::Label::textColourId, textBright);
        chordCaption_.setText("PREVIEW - TRIGGER C4", juce::dontSendNotification);
        chordCaption_.setColour(juce::Label::textColourId, dim);
        chordNotes_.setText(noteListText(processor_.previewChordForTrigger(60)),
                            juce::dontSendNotification);
        chordNotes_.setLive(false);
    }
}

void ChordEngineAudioProcessorEditor::updateRecorderDisplay()
{
    const auto state = processor_.recorderState();
    const auto count = processor_.recorderTakeCount();

    juce::String status;
    juce::Colour statusColour = dim;
    if (state == 1)
    {
        status = "ARMED - PLAY TO RECORD";
        statusColour = accent;
    }
    else if (state == 2)
    {
        status = "RECORDING";
        statusColour = terracotta;
    }
    else if (count == 0)
    {
        status = "WAITING FOR ARM";
    }
    else
    {
        status = juce::String(static_cast<int>(count))
            + (count == 1 ? " TAKE STORED" : " TAKES STORED");
        statusColour = sage;
    }

    if (processor_.isRecorderOverflowed())
    {
        status = "BUFFER LIMIT REACHED";
        statusColour = terracotta;
    }
    recorderCount_.setText(status, juce::dontSendNotification);
    recorderCount_.setColour(juce::Label::textColourId, statusColour);
    // The host can restore another grid with a project; follow the processor.
    const auto quantizeId = static_cast<int>(processor_.recordQuantize()) + 1;
    if (recordQuantizeSelector_.getSelectedId() != quantizeId)
        recordQuantizeSelector_.setSelectedId(quantizeId, juce::dontSendNotification);
    const bool follow = processor_.followHostRecord();
    if (displayedFollowHostRecord_ != (follow ? 1 : 0))
    {
        displayedFollowHostRecord_ = follow ? 1 : 0;
        followHostRecord_.setToggleState(follow, juce::dontSendNotification);
        followHostRecord_.setButtonText(follow ? "FOLLOW DAW RECORD: ON" : "FOLLOW DAW RECORD: OFF");
        if (follow)
            followHostRecord_.setPalette(accentSoft, accent, accent, accentSoft.darker(0.1f));
        else
            followHostRecord_.setPalette(juce::Colours::transparentBlack, dim, border, raised);
        recorderHint_.setText(follow ? "Arm, or press Record in your DAW. Play, then drag each take out."
                                     : "Arm, then play. Drag each finished take into your DAW.",
                              juce::dontSendNotification);
    }

    if (state == 1)
    {
        recorderAction_.setButtonText("CANCEL");
        recorderAction_.setPalette(juce::Colours::transparentBlack, text, accent, accentSoft);
        recorderAction_.setIndicator(accent);
    }
    else if (state == 2)
    {
        recorderAction_.setButtonText("STOP TAKE");
        recorderAction_.setPalette(terracotta, textBright, juce::Colour(), terracotta.darker(0.2f));
        recorderAction_.setIndicator(textBright);
    }
    else
    {
        recorderAction_.setButtonText("RECORD ARM");
        recorderAction_.setPalette(accent, onAccent, juce::Colour(), accent.darker(0.2f));
        recorderAction_.setIndicator(terracotta);
    }

    const auto displayed = juce::jmin<std::uint8_t>(count,
        static_cast<std::uint8_t>(recorderTakes_.size()));
    recorderHint_.setVisible(!infoPageVisible_ && !packsPageVisible_ && !gateVisible_ && displayed == 0);
    for (std::size_t index = 0; index < recorderTakes_.size(); ++index)
    {
        auto& pill = recorderTakes_[index];
        const bool available = index < displayed;
        pill.setVisible(!infoPageVisible_ && !packsPageVisible_ && !gateVisible_ && available);
        pill.setEnabled(available);
        if (available)
        {
            pill.setTake(static_cast<int>(index + 1),
                         processor_.recorderTakeNoteCount(static_cast<std::uint8_t>(index)));
        }
    }
}

void ChordEngineAudioProcessorEditor::updateNavigation()
{
    // Text navigation: the current page is ivory with the sand underline
    // painted by the editor; the other page is quiet.
    const auto paintNavigation = [](PillButton& button, bool active)
    {
        button.setPalette(juce::Colours::transparentBlack, active ? textBright : dim);
    };
    paintNavigation(navChord_, !infoPageVisible_ && !packsPageVisible_);
    paintNavigation(navPacks_, packsPageVisible_);
    paintNavigation(navInfo_, infoPageVisible_);
}

juce::File ChordEngineAudioProcessorEditor::writeRecorderTakeFile(std::size_t index)
{
    if (index >= recorderTakes_.size() || !takeExportDir_.createDirectory())
        return {};

    // Takes older than a week are pruned here, the only place that adds them.
    const auto now = juce::Time::getCurrentTime();
    for (const auto& old : takeExportDir_.findChildFiles(juce::File::findFiles, false, "*.mid"))
        if (now - old.getLastModificationTime() > juce::RelativeTime::days(7))
            old.deleteFile();

    // A plain, unique name: hosts name the imported region after the file.
    auto destination = takeExportDir_.getNonexistentChildFile(
        "ChordEngine Take " + juce::String(static_cast<int>(index + 1))
            + now.formatted(" %Y%m%d-%H%M%S"),
        ".mid", false);
    if (!processor_.exportRecorderTake(static_cast<std::uint8_t>(index), destination))
    {
        destination.deleteFile();
        return {};
    }
    return destination;
}

bool ChordEngineAudioProcessorEditor::startRecorderTakeDrag(std::size_t index)
{
    // The file must outlive the drag. Logic Pro and FL Studio read a dropped
    // file after the drag session has ended, so deleting it when the drag
    // callback fires (as earlier builds did) left them nothing to import.
    const auto destination = writeRecorderTakeFile(index);
    if (destination == juce::File())
        return false;

    // Each drag's outcome is appended to Takes/drag-log.txt (the operation the
    // drop target performed; 0 = refused), so a failed drop in a host can be
    // diagnosed from the file alone. No account or musical data is written.
    const auto log = takeExportDir_.getChildFile("drag-log.txt");
    const auto host = juce::String(juce::PluginHostType().getHostDescription());
    const auto name = destination.getFileName();
    return chordengine::ui::startExternalFileDrag(destination, *this, [log, host, name](int operation)
    {
        if (log.getSize() > 64 * 1024)
            log.deleteFile();
        log.appendText(juce::Time::getCurrentTime().toISO8601(true) + "  host=" + host
                       + "  operation=" + juce::String(operation)
                       + (operation == 0 ? " (refused or cancelled)" : "") + "  " + name + "\n");
    });
}

void ChordEngineAudioProcessorEditor::revealRecorderTake(std::size_t index)
{
    // Fallback for any host that will not accept a drop from a plug-in window:
    // the take is shown in Finder, from where it can be dragged in.
    const auto file = writeRecorderTakeFile(index);
    if (file != juce::File())
        file.revealToUser();
}

void ChordEngineAudioProcessorEditor::setSelectedPage(Page selected)
{
    infoPageVisible_ = selected == Page::info;
    packsPageVisible_ = selected == Page::packs;
    const bool showChord = selected == Page::chord;

    // While the expiry gate owns the screen, NO normal surface may stay
    // visible: the reference hides every normal component explicitly and
    // raises the gate last, so nothing can render or be clicked above it.
    const bool gate = gateVisible_;

    const std::array<juce::Component*, 33> chordComponents {{
        &keyLabel_, &keySelector_, &scaleLabel_, &scaleSelector_, &presetLabel_, &presetSelector_,
        &presetPrevious_, &presetNext_, &presetCounter_, &chordCaption_,
        &chordName_, &chordNotes_, &transposeLabel_, &octaveValue_, &octaveDown_, &octaveUp_,
        &transposeWhole_, &transposeLowest_, &transposeHighest_, &velocityModeLabel_,
        &velocityModeSelector_, &fixedVelocityLabel_, &fixedVelocitySlider_, &fixedVelocityValue_,
        &piano_, &recorderStatus_, &recorderCount_, &recorderAction_,
        &recorderTakes_[0], &recorderTakes_[1], &recorderTakes_[2], &recordQuantizeSelector_,
        &followHostRecord_ }};
    for (auto* component : chordComponents)
        component->setVisible(showChord && !gate);

    // Leaving the CHORD page (or raising the gate) must never strand a held
    // note-on: the key is no longer visible, so it is released explicitly.
    if (!showChord || gate)
        piano_.releaseHeldNote();

    // Reference: INTERNAL AUDIO is owned by the CHORD page. Builds with an
    // audio output show the switch and sound; the MIDI FX shows its chip.
    const bool audioBuild = processor_.hasAudioOutput();
    internalAudioLabel_.setVisible(showChord && !gate);
    internalAudioSubLabel_.setVisible(showChord && !gate && !audioBuild);
    internalAudioPill_.setVisible(showChord && !gate && !audioBuild);
    internalAudioToggle_.setVisible(showChord && !gate && audioBuild);
    internalSoundSelector_.setVisible(showChord && !gate && audioBuild);

    const std::array<juce::Component*, 23> infoComponents {{
        &infoTitle_, &infoVersion_, &accountHeader_, &infoUser_, &infoPlus_, &infoLicense_,
        &licenseKeyLabel_, &licenseKeyEditor_, &activateLicense_,
        &updatesHeader_, &infoCurrentVersion_, &infoLatestVersion_, &infoStatus_,
        &checkUpdates_, &openStudio_, &signIn_, &signOut_, &infoAuthHint_,
        &trialHeader_, &trialStatus_,
        &helpTitle_, &helpCopy_, &helpButton_ }};
    for (auto* component : infoComponents)
        component->setVisible(infoPageVisible_ && !gate);

    for (auto* component : std::initializer_list<juce::Component*> {
             &packsTitle_, &packsSubtitle_, &packsStatus_, &packsRefresh_, &packsViewport_ })
        component->setVisible(packsPageVisible_ && !gate);
    if (packsPageVisible_ && !gate && !packCatalogRequested_)
        refreshPackCatalog();
    updatePacksDisplay(true);

    // The expiry gate owns the whole screen while it is up.
    for (auto* component : { static_cast<juce::Component*>(&gateTitle_),
                             static_cast<juce::Component*>(&gateBody_),
                             static_cast<juce::Component*>(&gateStudio_),
                             static_cast<juce::Component*>(&gateSignIn_) })
        component->setVisible(gate);

    // Shared chrome (background, wordmark, logo asset, footer and both
    // navigation buttons) stays visible on either page - and above the gate,
    // exactly as the reference keeps its navigation clickable.
    updateNavigation();
    updateInfoDisplay();
    updateRecorderDisplay();
    repaint();
}

chordengine::updates::UpdateView ChordEngineAudioProcessorEditor::updateView() const
{
    if (updates_ != nullptr)
        return updates_->view();

    chordengine::updates::UpdateView idle;
    idle.currentVersion = chordengine::updates::currentBuildVersionString;
    return idle;
}

void ChordEngineAudioProcessorEditor::updateInfoDisplay()
{
    if (licensing_ == nullptr)
        return;

    const auto& snapshot = licensing_->snapshot();

    infoVersion_.setText("Version v" + juce::String(chordengine::updates::currentBuildVersionString),
                         juce::dontSendNotification);
    infoCurrentVersion_.setText(
        chordengine::updates::currentVersionLine(updateView()),
        juce::dontSendNotification);
    infoUser_.setText(chordengine::licensing::userLineText(snapshot), juce::dontSendNotification);
    infoPlus_.setText(chordengine::licensing::plusLineText(snapshot), juce::dontSendNotification);
    infoPlus_.setColour(juce::Label::textColourId,
                        displayTone(chordengine::licensing::plusLineColour(snapshot)));
    infoLicense_.setText("License  " + chordengine::licensing::licenseLineText(snapshot),
                         juce::dontSendNotification);

    // The lifetime-licence control reflects the real state: it is disabled
    // only while an activation is genuinely in flight, and its label never
    // claims a result the server has not given.
    activateLicense_.setButtonText(chordengine::licensing::activationButtonText(snapshot));
    activateLicense_.setEnabled(!snapshot.activationPending);

    // The update rows are the real client's view: an honest state, never a
    // fabricated version and never a placeholder dash once an answer exists.
    const auto update = updateView();
    infoLatestVersion_.setText(chordengine::updates::latestVersionLine(update),
                               juce::dontSendNotification);
    infoStatus_.setText(chordengine::updates::statusLine(update), juce::dontSendNotification);
    infoStatus_.setColour(juce::Label::textColourId,
                          displayTone(chordengine::updates::statusColour(update)));

    trialStatus_.setText(chordengine::licensing::trialStatusText(snapshot),
                         juce::dontSendNotification);
    trialStatus_.setColour(juce::Label::textColourId,
                           displayTone(chordengine::licensing::trialStatusColour(snapshot)));

    // The account pair shares one slot and one derived flag: exactly one of
    // SIGN IN / SIGN OUT is ever visible (reference ceRefreshAuthHint).
    const bool signedIn = chordengine::licensing::isSignedIn(snapshot);
    signIn_.setVisible(infoPageVisible_ && !signedIn && !gateVisible_);
    signOut_.setVisible(infoPageVisible_ && signedIn && !gateVisible_);
    signIn_.setButtonText(chordengine::licensing::signButtonText(snapshot));

    infoAuthHint_.setText(snapshot.message, juce::dontSendNotification);

    // The device-code flow asked the browser to open the approval page.
    juce::String verificationUrl;
    if (licensing_->consumeVerificationUrl(verificationUrl))
        juce::URL(verificationUrl).launchInDefaultBrowser();
}

void ChordEngineAudioProcessorEditor::updateGateDisplay()
{
    if (licensing_ == nullptr)
        return;

    const bool active = licensing_->expiredGateActive();
    if (active == gateVisible_)
        return;

    gateVisible_ = active;
    gateBody_.setText(chordengine::licensing::expiryModalBody(licensing_->snapshot()),
                      juce::dontSendNotification);

    if (gateVisible_)
    {
        // Full-screen exclusive state: every normal surface goes away while
        // the gate is up (reference ceHideAllNormalUI + ceShowExpiredModal).
        setSelectedPage(false);
    }
    else
    {
        // Restore: re-derive the whole page from scratch.
        setSelectedPage(infoPageVisible_);
    }

    repaint();
}

void ChordEngineAudioProcessorEditor::openStudio()
{
    // Reference ceOpenStudio(): the EXISTING registered URL scheme of
    // Music-Prod Studio. No bundle id is guessed and no path is hardcoded.
    // A missing application fails quietly - the reference does not fake
    // success and neither does this.
    juce::URL(juce::String(chordengine::licensing::studioUrl)).launchInDefaultBrowser();
}

void ChordEngineAudioProcessorEditor::driveLicensingTick(double nowMs)
{
    if (licensing_ == nullptr)
        return;

    if (nowMs - lastLicensingTickMs_ < licensingTickMs)
        return;

    lastLicensingTickMs_ = nowMs;

    // The trial usage clock starts only after the first generated chord
    // (reference onNoteOn sets trialStarted).
    if (processor_.consumePlaybackActivity())
        licensing_->notePlaybackStarted();

    licensing_->tick();
    updateInfoDisplay();
    updateGateDisplay();
}

void ChordEngineAudioProcessorEditor::timerCallback()
{
    driveLicensingTick(juce::Time::currentTimeMillis());

    const auto configuration = processor_.configurationSnapshot();
    if (!sameConfiguration(displayedConfiguration_, configuration))
        updateStateDisplay();

    updateRecorderDisplay();
    updateChordReadout();
    updateInternalAudioDisplay();
    rebuildPresetMenu();
    const auto packPreset = processor_.packPreset();
    const auto packKey = packPreset.has_value() ? packPreset->packSlug + "/" + packPreset->presetName
                                                : juce::String();
    if (packKey != displayedPackKey_)
        updateStateDisplay();
    if (packsPageVisible_)
        updatePacksDisplay();
    piano_.repaint();
}

void ChordEngineAudioProcessorEditor::updateInternalAudioDisplay()
{
    // The processor is the source of truth (the host state can change it).
    const bool enabled = processor_.internalAudioEnabled();
    const auto sound = static_cast<int>(processor_.internalSound());
    if (enabled == displayedInternalAudio_ && sound == displayedInternalSound_)
        return;
    displayedInternalAudio_ = enabled;
    displayedInternalSound_ = sound;
    internalAudioToggle_.setToggleState(enabled, juce::dontSendNotification);
    internalAudioToggle_.setButtonText(enabled ? "ON" : "OFF");
    if (enabled)
        internalAudioToggle_.setPalette(accent, background, accent, accent.darker(0.15f));
    else
        internalAudioToggle_.setPalette(juce::Colours::transparentBlack, dim, border, raised);
    internalSoundSelector_.setSelectedId(sound + 1, juce::dontSendNotification);
    internalSoundSelector_.setEnabled(true);
}

// ============================================================== Preset Packs

bool ChordEngineAudioProcessorEditor::packMemberActive() const
{
    // Downloaded packs are a Music-Prod+ benefit: an active (cached, verified)
    // membership on a signed-in account. The store enforces the same rule.
    if (licensing_ == nullptr)
        return false;
    const auto& snapshot = licensing_->snapshot();
    return chordengine::licensing::isSignedIn(snapshot) && snapshot.cached.subscribed;
}

void ChordEngineAudioProcessorEditor::rebuildPresetMenu()
{
    if (packLibrary_ == nullptr)
        return;
    const auto usable = packLibrary_->usablePacks(packMemberActive());
    juce::String key;
    for (const auto* pack : usable)
        if (!pack->isStock)
            key << pack->slug << '@' << pack->version << ';';
    if (key == presetMenuKey_ && !presetEntries_.empty())
        return;
    presetMenuKey_ = key;

    presetEntries_.clear();
    presetSelector_.clear(juce::dontSendNotification);
    for (int index = 0; index < static_cast<int>(chordengine::core::ChordPresetSystem::count()); ++index)
    {
        const auto* preset = chordengine::core::ChordPresetSystem::findByIndex(index);
        if (preset == nullptr)
            continue;
        presetEntries_.push_back({ index, {}, -1 });
        presetSelector_.addItem(juce::String(preset->name.data()), static_cast<int>(presetEntries_.size()));
    }
    for (const auto* pack : usable)
    {
        if (pack->isStock)
            continue;
        presetSelector_.addSectionHeading(pack->name);
        for (int index = 0; index < static_cast<int>(pack->presets.size()); ++index)
        {
            presetEntries_.push_back({ -1, pack->slug, index });
            presetSelector_.addItem(pack->presets[static_cast<std::size_t>(index)].name,
                                    static_cast<int>(presetEntries_.size()));
        }
    }
    updateStateDisplay();
}

int ChordEngineAudioProcessorEditor::currentPresetEntry() const
{
    const auto packPreset = processor_.packPreset();
    const auto stock = static_cast<int>(processor_.configurationSnapshot().preset);
    for (std::size_t index = 0; index < presetEntries_.size(); ++index)
    {
        const auto& entry = presetEntries_[index];
        if (!packPreset.has_value() && entry.stockIndex == stock)
            return static_cast<int>(index);
        if (packPreset.has_value() && entry.stockIndex < 0 && entry.packSlug == packPreset->packSlug)
        {
            const auto* pack = packLibrary_ != nullptr ? packLibrary_->find(entry.packSlug) : nullptr;
            if (pack != nullptr && entry.presetIndex < static_cast<int>(pack->presets.size())
                && pack->presets[static_cast<std::size_t>(entry.presetIndex)].name == packPreset->presetName)
                return static_cast<int>(index);
        }
    }
    return -1;
}

void ChordEngineAudioProcessorEditor::selectPresetEntry(int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= static_cast<int>(presetEntries_.size()))
        return;
    const auto& entry = presetEntries_[static_cast<std::size_t>(entryIndex)];
    if (entry.stockIndex >= 0)
    {
        processor_.clearPackPreset();
        processor_.setChordPreset(static_cast<chordengine::core::ChordPresetId>(entry.stockIndex));
    }
    else if (const auto* pack = packLibrary_->find(entry.packSlug);
             pack != nullptr && entry.presetIndex < static_cast<int>(pack->presets.size()))
    {
        const auto& preset = pack->presets[static_cast<std::size_t>(entry.presetIndex)];
        ChordEngineAudioProcessor::PackPreset selection;
        selection.packSlug = pack->slug;
        selection.packName = pack->name;
        selection.presetName = preset.name;
        selection.preset = toCustomPreset(preset);
        processor_.setPackPreset(selection);
    }
    updateStateDisplay();
}

void ChordEngineAudioProcessorEditor::refreshPackCatalog()
{
    if (packStore_ == nullptr || packCatalogLoading_)
        return;
    packCatalogRequested_ = true;
    packCatalogLoading_ = true;
    packMessage_ = "Loading the pack catalog...";
    packLibrary_->scan();
    updatePacksDisplay(true);
    juce::Component::SafePointer<ChordEngineAudioProcessorEditor> safe(this);
    packStore_->fetchCatalog([safe](chordengine::packs::PackStoreClient::CatalogResult result)
    {
        if (safe == nullptr)
            return;
        safe->packCatalogLoading_ = false;
        if (result.ok)
        {
            safe->packCatalog_ = std::move(result.catalog);
            safe->packMessage_ = {};
        }
        else
        {
            safe->packMessage_ = "Store unavailable - showing installed packs";
        }
        safe->updatePacksDisplay(true);
    });
}

void ChordEngineAudioProcessorEditor::updatePacksDisplay(bool force)
{
    if (packLibrary_ == nullptr || licensing_ == nullptr)
        return;
    const auto& snapshot = licensing_->snapshot();
    chordengine::packs::StoreAccess access;
    access.signedIn = chordengine::licensing::isSignedIn(snapshot);
    access.subscribed = snapshot.cached.subscribed;
    auto listings = chordengine::packs::mergeListings(packCatalog_.has_value() ? &*packCatalog_ : nullptr,
                                                      *packLibrary_, access);

    juce::String key;
    key << (access.signedIn ? "1" : "0") << (access.subscribed ? "1" : "0") << packMessage_
        << packDownloadingSlug_ << (int) snapshot.phase;
    for (const auto& listing : listings)
        key << listing.slug << static_cast<int>(listing.status) << listing.statusText;
    if (!force && key == packListingKey_)
        return;
    packListingKey_ = key;

    if (!access.signedIn)
        packsSubtitle_.setText(snapshot.phase == chordengine::licensing::Phase::linking
                                   ? "Finish signing in in your browser."
                                   : "Sign in with Music-Prod+ to download every pack.",
                               juce::dontSendNotification);
    else if (!access.subscribed)
        packsSubtitle_.setText("Packs are included with an active Music-Prod+ membership.",
                               juce::dontSendNotification);
    else
        packsSubtitle_.setText("Your Music-Prod+ membership includes every pack. "
                               "Installed presets appear in the CHORD PRESET menu.",
                               juce::dontSendNotification);
    packsStatus_.setText(packMessage_, juce::dontSendNotification);
    packsSignIn_.setVisible(packsPageVisible_ && !gateVisible_ && !access.signedIn);
    packsSignIn_.setButtonText(chordengine::licensing::signButtonText(snapshot));

    packList_.setListings(std::move(listings), packDownloadingSlug_);
    const auto width = packsViewport_.getMaximumVisibleWidth();
    packList_.setSize(juce::jmax(1, width),
                      juce::jmax(1, packList_.rowHeight() * static_cast<int>(packList_.listings().size())));
    rebuildPresetMenu();
}

void ChordEngineAudioProcessorEditor::packActionClicked(int index)
{
    const auto& listings = packList_.listings();
    if (index < 0 || index >= static_cast<int>(listings.size()) || packDownloadingSlug_.isNotEmpty())
        return;
    const auto listing = listings[static_cast<std::size_t>(index)];
    using chordengine::packs::PackStatus;
    switch (listing.status)
    {
        case PackStatus::signInRequired:
            licensing_->signInButtonClicked();
            updatePacksDisplay(true);
            return;
        case PackStatus::requiresMembership:
            juce::URL("https://music-prod.com/plus").launchInDefaultBrowser();
            return;
        case PackStatus::builtIn:
        case PackStatus::installed:
            setSelectedPage(Page::chord);
            return;
        case PackStatus::available:
        case PackStatus::updateAvailable:
            break;
    }
    if (!listing.canDownload)
        return;

    packDownloadingSlug_ = listing.slug;
    packMessage_ = "Downloading " + listing.name + "...";
    updatePacksDisplay(true);
    juce::Component::SafePointer<ChordEngineAudioProcessorEditor> safe(this);
    packStore_->download(licensing_->storeToken(), listing.slug,
                         [safe](chordengine::packs::PackStoreClient::DownloadResult result)
    {
        if (safe == nullptr)
            return;
        safe->packDownloadingSlug_ = {};
        safe->packMessage_ = result.message;
        if (result.outcome == chordengine::packs::PackStoreClient::DownloadOutcome::membershipRequired
            && result.unlockUrl.isNotEmpty())
            safe->packMessage_ = "Music-Prod+ required - see music-prod.com";
        safe->packLibrary_->scan();
        safe->updatePacksDisplay(true);
    });
}

// ---------------------------------------------------------- PackListView

void ChordEngineAudioProcessorEditor::PackListView::setListings(
    std::vector<chordengine::packs::PackListing> listings, juce::String busySlug)
{
    listings_ = std::move(listings);
    busySlug_ = std::move(busySlug);
    hoverRow_ = -1;
    repaint();
}

void ChordEngineAudioProcessorEditor::PackListView::setScale(float scale)
{
    scale_ = juce::jmax(0.1f, scale);
    repaint();
}

int ChordEngineAudioProcessorEditor::PackListView::rowHeight() const noexcept
{
    return juce::roundToInt(designRowHeight * scale_);
}

juce::String ChordEngineAudioProcessorEditor::PackListView::actionText(
    const chordengine::packs::PackListing& listing, bool busy)
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

juce::Rectangle<int> ChordEngineAudioProcessorEditor::PackListView::actionBounds(int row) const
{
    const auto height = rowHeight();
    const auto width = juce::roundToInt(140.0f * scale_);
    const auto buttonHeight = juce::roundToInt(28.0f * scale_);
    return { getWidth() - width - juce::roundToInt(8.0f * scale_),
             row * height + (height - buttonHeight) / 2, width, buttonHeight };
}

int ChordEngineAudioProcessorEditor::PackListView::actionRowAt(juce::Point<int> position) const
{
    for (int row = 0; row < static_cast<int>(listings_.size()); ++row)
        if (actionBounds(row).contains(position))
            return row;
    return -1;
}

void ChordEngineAudioProcessorEditor::PackListView::paint(juce::Graphics& graphics)
{
    using chordengine::packs::PackStatus;
    const auto height = rowHeight();
    for (int row = 0; row < static_cast<int>(listings_.size()); ++row)
    {
        const auto& listing = listings_[static_cast<std::size_t>(row)];
        const auto area = juce::Rectangle<int>(0, row * height, getWidth(), height);
        graphics.setColour(hairline);
        graphics.fillRect(area.withTop(area.getBottom() - 1));

        // A pack tile: the pack's initials on a quiet plate (sand once owned).
        const auto tileSize = juce::roundToInt(40.0f * scale_);
        const auto tile = juce::Rectangle<int>(0, area.getY() + (height - tileSize) / 2, tileSize, tileSize)
                              .toFloat();
        const bool owned = listing.status == PackStatus::builtIn || listing.status == PackStatus::installed
            || listing.status == PackStatus::updateAvailable;
        graphics.setColour(owned ? accent.withAlpha(0.18f) : raised);
        graphics.fillRoundedRectangle(tile, 6.0f * scale_);
        graphics.setColour(owned ? accent : dim);
        juce::String initials;
        for (const auto& word : juce::StringArray::fromTokens(listing.name, " ", {}))
            if (word.isNotEmpty() && initials.length() < 2 && word != "ChordEngine")
                initials << word.substring(0, 1).toUpperCase();
        graphics.setFont(captionFont(13.0f * scale_, 0.04f));
        graphics.drawText(initials, tile, juce::Justification::centred, false);

        const auto textX = juce::roundToInt(56.0f * scale_);
        const auto textWidth = actionBounds(row).getX() - textX - juce::roundToInt(12.0f * scale_);
        graphics.setColour(textBright);
        graphics.setFont(uiFont(14.0f * scale_, true));
        graphics.drawText(listing.name, textX, area.getY() + juce::roundToInt(13.0f * scale_), textWidth,
                          juce::roundToInt(18.0f * scale_), juce::Justification::centredLeft, true);
        const bool usableStatus = owned || listing.status == PackStatus::available;
        graphics.setColour(usableStatus ? muted : accent);
        graphics.setFont(uiFont(11.5f * scale_));
        const auto detail = listing.description.isNotEmpty() ? listing.statusText + "  -  " + listing.description
                                                             : listing.statusText;
        graphics.drawText(detail, textX, area.getY() + juce::roundToInt(34.0f * scale_), textWidth,
                          juce::roundToInt(16.0f * scale_), juce::Justification::centredLeft, true);

        const bool busy = listing.slug == busySlug_;
        const auto action = actionBounds(row).toFloat();
        const bool primary = listing.status == PackStatus::available
            || listing.status == PackStatus::updateAvailable || listing.status == PackStatus::signInRequired;
        const bool quiet = listing.status == PackStatus::builtIn || listing.status == PackStatus::installed;
        const bool hover = row == hoverRow_ && !busy;
        if (primary && !busy)
        {
            graphics.setColour(hover ? accent.brighter(0.1f) : accent);
            graphics.fillRoundedRectangle(action, 4.0f * scale_);
            graphics.setColour(onAccent);
        }
        else
        {
            graphics.setColour(hover ? raisedHover : juce::Colours::transparentBlack);
            graphics.fillRoundedRectangle(action, 4.0f * scale_);
            graphics.setColour(quiet ? hairline.brighter(0.2f) : border);
            graphics.drawRoundedRectangle(action.reduced(0.5f), 4.0f * scale_, 1.0f);
            graphics.setColour(quiet ? dim : text);
        }
        graphics.setFont(uiFont(9.5f * scale_, true, 0.08f));
        graphics.drawText(actionText(listing, busy), action, juce::Justification::centred, false);
    }
}

void ChordEngineAudioProcessorEditor::PackListView::mouseMove(const juce::MouseEvent& event)
{
    const auto row = actionRowAt(event.getPosition());
    if (row != hoverRow_)
    {
        hoverRow_ = row;
        setMouseCursor(row >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ChordEngineAudioProcessorEditor::PackListView::mouseExit(const juce::MouseEvent&)
{
    hoverRow_ = -1;
    repaint();
}

void ChordEngineAudioProcessorEditor::PackListView::mouseUp(const juce::MouseEvent& event)
{
    const auto row = actionRowAt(event.getPosition());
    if (row >= 0 && onAction)
        onAction(row);
}
