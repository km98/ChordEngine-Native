#include "PluginEditor.h"

#include "BinaryData.h"
#include "Core/ChordPresetSystem.h"
#include "Core/ScaleSystem.h"
#include "Licensing/LicensingServices.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
// ---------------------------------------------------------------- palette
// Exact reference values (read-only reference script palette comment:
// base 0xFF0B0D10 / surface 0xFF14181E / raised 0xFF1C2028 / track
// 0xFF232830 / border 0xFF2A3140 / text bright 0xFFE8EBEF / mid 0xFF8A919C
// / dim 0xFF6A7280 / idle 0xFF5E6670 / accent blue 0xFF3E8BFF).
const juce::Colour background { 0xff0b0d10 };
const juce::Colour panel { 0xff14181e };
const juce::Colour raised { 0xff1c2028 };
const juce::Colour track { 0xff232830 };
const juce::Colour border { 0xff2a3140 };
const juce::Colour text { 0xffe8ebef };
const juce::Colour textBright { 0xfff2f3f5 };
const juce::Colour muted { 0xff8a919c };
const juce::Colour dim { 0xff6a7280 };
const juce::Colour idle { 0xff5e6670 };
const juce::Colour caption { 0xffaeb7c4 };
const juce::Colour accent { 0xff3e8bff };
const juce::Colour accentDim { 0xff273040 };
const juce::Colour coral { 0xffe6524d };
const juce::Colour cancelFill { 0xff6a7280 };
const juce::Colour stopFill { 0xffe05a5a };
const juce::Colour amber { 0xffe0b050 };
const juce::Colour footerText { 0xff4a515c };
const juce::Colour infoCopy { 0xff9aa3af };
const juce::Colour infoStatus { 0xff8a93a0 };
const juce::Colour chipFill { 0xff202834 };
const juce::Colour chipBorder { 0xff3a4553 };
const juce::Colour wellTop { 0xff181d24 };
const juce::Colour wellBottom { 0xff12161c };
const juce::Colour pianoBed { 0xff101318 };
const juce::Colour infoBed { 0xff10141a };
const juce::Colour whiteKey { 0xffe0e3e8 };
const juce::Colour whiteKeyEdge { 0xffb3b8bf };
const juce::Colour blackKey { 0xff121217 };
const juce::Colour chordWhiteKey { 0xff4f8fe0 };
const juce::Colour chordBlackKey { 0xff4073cc };
const juce::Colour whiteKeyLabel { 0xff6b7380 };
// Preview tint: native-only state (the reference highlights trigger and
// generated notes only). Documented in NATIVE_GUI_REFERENCE_INVENTORY.md.
const juce::Colour previewWhite { 0xffc3cfde };
const juce::Colour previewBlack { 0xff243040 };

// The reference is a single-source version constant (CE_PRODUCT_VERSION);
// footer, INFO headline and INFO updates rows all derive from it.
// Every version surface of THIS editor shows the version of the binary that
// is actually running, taken from the build metadata (see
// chordengine::updates::currentBuildVersionString). The reference product's
// 0.4.0 string is not hardcoded anywhere in the UI and no development build
// pretends to be 0.4.0. The licensing vocabulary keeps its own reference
// constant for the account bridge; that is a different concern.
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
void ChordEngineAudioProcessorEditor::ChordEngineWordmark::paint(juce::Graphics& graphics)
{
    // Reference geometry: 360x48 panel, 22 px letter cells, 14 px word gap,
    // CHORD bright / ENGINE dim, 2 px accent rule integrated underneath.
    const float scale = static_cast<float>(getWidth()) / 360.0f;
    const float cellWidth = 22.0f * scale;
    const float wordGap = 14.0f * scale;
    const float totalWidth = 5.0f * cellWidth + wordGap + 6.0f * cellWidth;
    const float startX = (static_cast<float>(getWidth()) - totalWidth) * 0.5f;
    const float textHeight = 40.0f * scale;

    const auto drawWord = [&graphics, cellWidth, textHeight](const char* letters, int count,
                                                             float x, juce::Colour colour,
                                                             float fontHeight)
    {
        graphics.setColour(colour);
        graphics.setFont(juce::Font(juce::FontOptions("Avenir Next", fontHeight, juce::Font::plain)));
        for (int index = 0; index < count; ++index)
            graphics.drawText(juce::String::charToString(letters[index]),
                              juce::Rectangle<float>(x + static_cast<float>(index) * cellWidth,
                                                     0.0f, cellWidth, textHeight),
                              juce::Justification::centred, false);
    };

    drawWord("CHORD", 5, startX, text, 19.0f * scale);
    drawWord("ENGINE", 6, startX + 5.0f * cellWidth + wordGap, muted, 19.0f * scale);

    graphics.setColour(accent);
    graphics.fillRect(startX, 43.0f * scale, totalWidth, 2.0f * scale);
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

void ChordEngineAudioProcessorEditor::PillButton::paintButton(juce::Graphics& graphics,
                                                              bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getHeight()) / juce::jmax(1.0f, designHeight_);
    const float radius = cornerRadius_ * scale;

    auto fill = fill_;
    if (down)
        fill = pressed_.getAlpha() == 0 ? fill_.darker(0.18f) : pressed_;
    else if (highlighted)
        fill = fill_.brighter(0.08f);

    if (!fill.isTransparent())
    {
        graphics.setColour(isEnabled() ? fill : fill.withMultipliedAlpha(0.45f));
        graphics.fillRoundedRectangle(area, radius);
    }

    if (outline_.getAlpha() > 0)
    {
        graphics.setColour(isEnabled() ? outline_ : outline_.withMultipliedAlpha(0.45f));
        graphics.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    }

    graphics.setColour(isEnabled() ? textColour_ : textColour_.withMultipliedAlpha(0.5f));
    graphics.setFont(juce::Font(juce::FontOptions(juce::jmax(6.0f, designFontHeight_ * scale))));
    graphics.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

// ======================================================== stepper button ===
ChordEngineAudioProcessorEditor::StepButton::StepButton(const juce::String& buttonName,
                                                        bool increments)
    : juce::Button(buttonName), increments_(increments)
{
    setClickingTogglesState(false);
}

void ChordEngineAudioProcessorEditor::StepButton::paintButton(juce::Graphics& graphics,
                                                              bool highlighted, bool down)
{
    juce::ignoreUnused(highlighted);

    const auto area = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getWidth()) / 28.0f;
    const float radius = 6.0f * scale;

    // Reference octave stepper: accent-outlined pill carrying a drawn bar
    // glyph (minus) or bar cross (plus) - no font glyph, so no encoding risk.
    graphics.setColour(down ? accent : accentDim);
    graphics.fillRoundedRectangle(area, radius);
    graphics.setColour(accent);
    graphics.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    graphics.setColour(juce::Colours::white);

    const float barLength = juce::jmax(4.0f, 14.0f * scale);
    const float barThickness = juce::jmax(1.0f, 2.0f * scale);
    const auto centre = area.getCentre();
    graphics.fillRect(juce::Rectangle<float>(centre.x - barLength * 0.5f,
                                             centre.y - barThickness * 0.5f,
                                             barLength, barThickness));
    if (increments_)
        graphics.fillRect(juce::Rectangle<float>(centre.x - barThickness * 0.5f,
                                                 centre.y - barLength * 0.5f,
                                                 barThickness, barLength));
}

// ======================================================== take pill =======
ChordEngineAudioProcessorEditor::TakePillButton::TakePillButton(const juce::String& buttonName)
    : juce::Button(buttonName)
{
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
    juce::ignoreUnused(highlighted);

    const auto area = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getHeight()) / 20.0f;

    // Reference drag pill: solid accent fill (darker while the drag is
    // pending), two centred lines - identity plus the drag cue.
    graphics.setColour(down ? juce::Colour(0xff2a6fdb) : accent);
    graphics.fillRoundedRectangle(area, 9.0f * scale);

    if (takeNumber_ > 0)
    {
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::Font(juce::FontOptions("Arial", juce::jmax(6.0f, 10.0f * scale), juce::Font::plain)));
        graphics.drawText("TAKE " + juce::String(takeNumber_) + " - " + juce::String(noteCount_)
                              + " NOTES",
                          getLocalBounds().removeFromTop(getHeight() / 2 + 1),
                          juce::Justification::centred, false);

        graphics.setColour(juce::Colours::white.withAlpha(0.8f));
        graphics.setFont(juce::Font(juce::FontOptions("Arial", juce::jmax(6.0f, 9.0f * scale), juce::Font::plain)));
        graphics.drawText("DRAG MIDI", getLocalBounds().removeFromBottom(getHeight() / 2 + 1),
                          juce::Justification::centred, false);
    }
    else
    {
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::Font(juce::FontOptions("Arial", juce::jmax(6.0f, 10.0f * scale), juce::Font::plain)));
        graphics.drawText("DRAG MIDI", getLocalBounds(), juce::Justification::centred, false);
    }
}

// =================================================== internal audio pill ===
void ChordEngineAudioProcessorEditor::InternalAudioPill::paint(juce::Graphics& graphics)
{
    // Reference pill geometry (56x32, radius 8) with the reference OFF
    // wording, rendered in the DISABLED palette.
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
    const float scale = static_cast<float>(getHeight()) / 32.0f;

    graphics.setColour(raised);
    graphics.fillRoundedRectangle(area, 8.0f * scale);
    graphics.setColour(border);
    graphics.drawRoundedRectangle(area.reduced(0.5f), 8.0f * scale, 1.0f);
    graphics.setColour(idle.withAlpha(0.75f));
    graphics.setFont(juce::Font(juce::FontOptions("Arial", juce::jmax(6.0f, 11.0f * scale), juce::Font::plain)));
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
    const auto scale = static_cast<float>(getWidth()) / 720.0f;
    const auto area = getLocalBounds().toFloat();
    return area.withTrimmedLeft(12.0f * scale).withTrimmedRight(12.0f * scale)
               .withTrimmedTop(8.0f * scale).withTrimmedBottom(8.0f * scale);
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
    // Reference piano bed: 720x216 panel, 12 px side / 8 px vertical margins,
    // white-key label on C keys only.
    const float scale = static_cast<float>(getWidth()) / 720.0f;
    const auto area = getLocalBounds().toFloat();
    graphics.setColour(pianoBed);
    graphics.fillRoundedRectangle(area, 10.0f * scale);
    graphics.setColour(border);
    graphics.drawRoundedRectangle(area.reduced(0.5f), 10.0f * scale, 1.0f);

    // One shared key geometry drives painting and hit testing (whiteKeyRect /
    // blackKeyRect), so the key that lights up is always the key that sounds.
    const float labelHeight = juce::jmax(8.0f, 10.0f * scale);

    int whiteIndex = 0;
    for (int note = processor_.pianoFirstNote; note <= processor_.pianoLastNote; ++note)
    {
        if (!isWhiteKey(note))
            continue;

        auto key = whiteKeyRect(whiteIndex);
        const bool trigger = note == heldNote_
            || processor_.isAnyTriggerNoteDown(static_cast<std::uint8_t>(note))
            || processor_.isAnyTriggerNoteActive(static_cast<std::uint8_t>(note));
        const bool generated = processor_.isAnyOutputNoteActive(static_cast<std::uint8_t>(note));
        const bool preview = previewNotes_[static_cast<std::size_t>(note)];

        graphics.setColour(trigger ? coral : generated ? chordWhiteKey
                                                    : preview ? previewWhite : whiteKey);
        graphics.fillRect(key);
        if (!trigger && !generated)
        {
            graphics.setColour(whiteKeyEdge);
            graphics.drawRect(key, 1.0f);
            graphics.setColour(preview ? juce::Colours::white : whiteKeyLabel);
            graphics.setFont(juce::Font(juce::FontOptions("Arial", juce::jmax(6.0f, 8.0f * scale), juce::Font::plain)));
            graphics.drawText(noteName(note),
                              juce::Rectangle<float>(key.getX() + 1.0f, key.getBottom() - 12.0f * scale,
                                                     key.getWidth() - 2.0f, labelHeight),
                              juce::Justification::topLeft, false);
        }
        else if (note % 12 == 0)
        {
            graphics.setColour(juce::Colours::white);
            graphics.setFont(juce::Font(juce::FontOptions("Arial", juce::jmax(6.0f, 8.0f * scale), juce::Font::plain)));
            graphics.drawText(noteName(note),
                              juce::Rectangle<float>(key.getX() + 1.0f, key.getBottom() - 12.0f * scale,
                                                     key.getWidth() - 2.0f, labelHeight),
                              juce::Justification::topLeft, false);
        }
        ++whiteIndex;
    }

    for (int note = processor_.pianoFirstNote; note <= processor_.pianoLastNote; ++note)
    {
        if (isWhiteKey(note))
            continue;

        auto key = blackKeyRect(note);
        const bool trigger = note == heldNote_
            || processor_.isAnyTriggerNoteDown(static_cast<std::uint8_t>(note))
            || processor_.isAnyTriggerNoteActive(static_cast<std::uint8_t>(note));
        const bool generated = processor_.isAnyOutputNoteActive(static_cast<std::uint8_t>(note));
        const bool preview = previewNotes_[static_cast<std::size_t>(note)];

        graphics.setColour(trigger ? coral : generated ? chordBlackKey
                                                    : preview ? previewBlack : blackKey);
        graphics.fillRect(key);
    }
}

// ============================================================ editor ======
ChordEngineAudioProcessorEditor::ChordEngineAudioProcessorEditor(
    ChordEngineAudioProcessor& processorRef, juce::File licensingStorageDir,
    std::unique_ptr<chordengine::updates::UpdateTransport> updateTransport)
    : juce::AudioProcessorEditor(processorRef), processor_(processorRef), piano_(processorRef)
{
    setName("ChordEngine");
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
    navInfo_.setComponentID("info-button");
    navChord_.onClick = [this] { setSelectedPage(false); };
    navInfo_.onClick = [this] { setSelectedPage(true); };
    addAndMakeVisible(navChord_);
    addAndMakeVisible(navInfo_);

    const auto addFieldLabel = [this](juce::Label& label, const juce::String& title,
                                      const juce::String& componentId)
    {
        configureLabel(label, title, 8.0f, dim, true);
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
    for (int index = 0; index < static_cast<int>(chordengine::core::ChordPresetSystem::count()); ++index)
    {
        const auto* preset = chordengine::core::ChordPresetSystem::findByIndex(index);
        if (preset != nullptr)
            presetSelector_.addItem(juce::String(preset->name.data()), index + 1);
    }

    configureLabel(chordName_, {}, 26.0f, idle, true);
    chordName_.setComponentID("chord-name");
    configureLabel(chordNotes_, {}, 11.0f, muted);
    chordNotes_.setComponentID("chord-notes");
    addAndMakeVisible(chordName_);
    addAndMakeVisible(chordNotes_);

    configureLabel(transposeLabel_, "TRANSPOSE", 10.0f, caption, true);
    transposeLabel_.setComponentID("transpose-label");
    configureLabel(octaveValue_, "0 OCT", 14.0f, juce::Colours::white, true,
                   juce::Justification::centred);
    octaveValue_.setComponentID("transpose-octave");
    addAndMakeVisible(transposeLabel_);
    addAndMakeVisible(octaveValue_);

    octaveDown_.setComponentID("octave-down");
    octaveUp_.setComponentID("octave-up");
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

    configureLabel(velocityModeLabel_, "VELOCITY MODE", 10.0f, caption, true);
    velocityModeLabel_.setComponentID("velocity-mode-label");
    addAndMakeVisible(velocityModeLabel_);
    addCombo(velocityModeSelector_, "velocity-mode-selector", 12.0f, true);
    velocityModeSelector_.addItem("Dynamic", 1);
    velocityModeSelector_.addItem("Maximum", 2);
    velocityModeSelector_.addItem("Fixed", 3);

    configureLabel(fixedVelocityLabel_, "FIXED VELOCITY", 10.0f, caption, true);
    fixedVelocityLabel_.setComponentID("fixed-velocity-label");
    addAndMakeVisible(fixedVelocityLabel_);

    fixedVelocitySlider_.setComponentID("fixed-velocity-slider");
    fixedVelocitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    fixedVelocitySlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fixedVelocitySlider_.setRange(1.0, 127.0, 1.0);
    fixedVelocitySlider_.setColour(juce::Slider::trackColourId, accent);
    fixedVelocitySlider_.setColour(juce::Slider::backgroundColourId, track);
    fixedVelocitySlider_.setColour(juce::Slider::thumbColourId, text);
    addAndMakeVisible(fixedVelocitySlider_);

    configureLabel(fixedVelocityValue_, "100", 14.0f, text, true, juce::Justification::centred);
    fixedVelocityValue_.setComponentID("fixed-velocity-value");
    addAndMakeVisible(fixedVelocityValue_);

    addAndMakeVisible(piano_);

    configureLabel(recorderStatus_, "MIDI RECORDER", 10.0f, text);
    recorderStatus_.setComponentID("recorder-status");
    configureLabel(recorderCount_, "WAITING FOR ARM", 9.0f, dim, false,
                   juce::Justification::centredRight);
    recorderCount_.setComponentID("recorder-count");
    addAndMakeVisible(recorderStatus_);
    addAndMakeVisible(recorderCount_);

    recorderAction_.setComponentID("recorder-action");
    recorderAction_.setPalette(accent, juce::Colours::white);
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

    for (std::size_t index = 0; index < recorderTakes_.size(); ++index)
    {
        auto& pill = recorderTakes_[index];
        pill.setTooltip("Drag this completed MIDI take into your DAW.");
        pill.setEnabled(false);
        pill.setVisible(false);
        pill.onDragStart = [this, index] { return startRecorderTakeDrag(index); };
        addAndMakeVisible(pill);
    }

    configureLabel(internalAudioLabel_, "INTERNAL AUDIO", 10.0f, caption, true);
    internalAudioLabel_.setComponentID("internal-audio-label");
    // The reference sub-caption describes what the switch does. Here the
    // switch does not exist as a capability, so the caption states that
    // plainly instead of implying a feature the build cannot provide.
    // The product owns no audio bus, so the reference's SineSynth switch has
    // nowhere to route audio. The caption states that plainly instead of
    // implying a feature the build cannot provide.
    configureLabel(internalAudioSubLabel_, "MIDI effect - no audio bus", 8.0f, idle);
    internalAudioSubLabel_.setComponentID("internal-audio-sub-label");
    addAndMakeVisible(internalAudioLabel_);
    addAndMakeVisible(internalAudioSubLabel_);
    internalAudioPill_.setComponentID("internal-audio-status");
    internalAudioPill_.setTooltip("Internal audio is unavailable: it needs an audio output bus, and "
                                  "ChordEngine is a pure MIDI effect (AU aumi / VST3 MIDI effect) with no "
                                  "audio buses. Routing to a downstream instrument is the supported path.");
    addAndMakeVisible(internalAudioPill_);

    configureLabel(infoTitle_, "CHORD ENGINE", 20.0f, textBright, true);
    infoTitle_.setComponentID("info-title");
    configureLabel(infoVersion_, "Version v" + productVersion, 12.0f, infoStatus);
    infoVersion_.setComponentID("info-version");
    configureLabel(accountHeader_, "ACCOUNT", 9.0f, dim, true);
    accountHeader_.setComponentID("info-account-header");
    configureLabel(infoUser_, "User  Not signed in", 12.0f, text);
    infoUser_.setComponentID("info-user");
    configureLabel(infoPlus_, "Music-Prod+  NOT ACTIVE", 12.0f, amber, true);
    infoPlus_.setComponentID("info-plus");
    configureLabel(infoLicense_, "License  Not verified", 12.0f, text);
    infoLicense_.setComponentID("info-license");
    configureLabel(updatesHeader_, "UPDATES", 9.0f, dim, true);
    updatesHeader_.setComponentID("info-updates-header");
    configureLabel(infoCurrentVersion_, "Current version  " + productVersion, 12.0f, text);
    infoCurrentVersion_.setComponentID("info-current-version");
    configureLabel(infoLatestVersion_, "Latest version  -", 12.0f, text);
    infoLatestVersion_.setComponentID("info-latest-version");
    configureLabel(infoStatus_, "Status  -", 12.0f, infoStatus);
    infoStatus_.setComponentID("info-update-status");
    configureLabel(trialHeader_, "TRIAL", 9.0f, dim, true);
    trialHeader_.setComponentID("info-trial-header");
    configureLabel(trialStatus_, "Trial access  -", 12.0f, text);
    trialStatus_.setComponentID("info-trial-status");
    configureLabel(helpTitle_, "HELP & FEEDBACK", 10.0f, textBright, true);
    helpTitle_.setComponentID("info-help-title");
    configureLabel(helpCopy_, "Need help or want to report a problem with ChordEngine?", 10.0f,
                   infoCopy);
    helpCopy_.setComponentID("info-help-copy");
    for (auto* component : { static_cast<juce::Component*>(&infoTitle_),
                             static_cast<juce::Component*>(&infoVersion_),
                             static_cast<juce::Component*>(&accountHeader_),
                             static_cast<juce::Component*>(&infoUser_),
                             static_cast<juce::Component*>(&infoPlus_),
                             static_cast<juce::Component*>(&infoLicense_),
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
    checkUpdates_.setPalette(raised, textBright, border, track);
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
    openStudio_.setPalette(raised, textBright, border, track);
    openStudio_.setTooltip("Open Music-Prod Studio through its registered musicprodstudio:// URL scheme.");
    openStudio_.onClick = [this] { openStudio(); };
    addAndMakeVisible(openStudio_);

    signIn_.setComponentID("sign-in");
    signIn_.setPalette(accent, juce::Colours::white, accent, accent.withAlpha(0.75f));
    signIn_.setTooltip("Sign in to Music-Prod with the existing device-link flow.");
    signIn_.onClick = [this] { licensing_->signInButtonClicked(); updateInfoDisplay(); };
    addAndMakeVisible(signIn_);

    // SIGN OUT shares the SIGN IN slot: exactly one of the pair is ever
    // visible (reference ceRefreshAuthHint).
    signOut_.setComponentID("sign-out");
    signOut_.setPalette(raised, textBright, border, track);
    signOut_.setTooltip("Sign out of Music-Prod and revoke this plugin token.");
    signOut_.onClick = [this] { licensing_->signOutButtonClicked(); updateInfoDisplay(); };
    addAndMakeVisible(signOut_);

    // Device-link instruction line + the result of the last account action.
    // Only the PUBLIC user code and static copy ever appear here.
    configureLabel(infoAuthHint_, {}, 11.0f, infoStatus, false);
    infoAuthHint_.setComponentID("info-auth-hint");
    addAndMakeVisible(infoAuthHint_);

    // Expiry gate (reference ACCESS EXPIRED modal).
    configureLabel(gateTitle_, chordengine::licensing::expiryModalTitle, 20.0f, textBright,
                   true, juce::Justification::centred);
    gateTitle_.setComponentID("trial-gate-title");
    configureLabel(gateBody_, {}, 12.0f, infoCopy, false, juce::Justification::centredTop);
    gateBody_.setComponentID("trial-gate-body");
    addAndMakeVisible(gateTitle_);
    addAndMakeVisible(gateBody_);

    gateStudio_.setComponentID("trial-gate-studio");
    gateStudio_.setPalette(raised, textBright, accent, accent.withAlpha(0.35f));
    gateStudio_.onClick = [this] { openStudio(); };
    addAndMakeVisible(gateStudio_);

    gateSignIn_.setComponentID("trial-gate-sign-in");
    gateSignIn_.setPalette(raised, textBright, accent, accent.withAlpha(0.35f));
    gateSignIn_.onClick = [this] { licensing_->signInButtonClicked(); updateInfoDisplay(); };
    addAndMakeVisible(gateSignIn_);

    helpButton_.setComponentID("help-feedback");
    helpButton_.setPalette(raised, textBright, accent.withAlpha(0.6f), accent.withAlpha(0.35f));
    helpButton_.setTooltip("Open the Music-Prod ChordEngine Help & Feedback page in your browser.");
    helpButton_.onClick = []
    {
        juce::URL(ChordEngineAudioProcessor::helpFeedbackUrl).launchInDefaultBrowser();
    };
    addAndMakeVisible(helpButton_);

    configureLabel(footer_, "Chord Engine v" + productVersion, 9.0f, footerText, false,
                   juce::Justification::centredRight);
    footer_.setComponentID("footer");
    addAndMakeVisible(footer_);

    textSpecs_ = {{
        { &keyLabel_, 8.0f, true },
        { &scaleLabel_, 8.0f, true },
        { &presetLabel_, 8.0f, true },
        { &chordName_, 26.0f, true },
        { &chordNotes_, 11.0f, false },
        { &transposeLabel_, 10.0f, true },
        { &octaveValue_, 14.0f, true },
        { &velocityModeLabel_, 10.0f, true },
        { &fixedVelocityLabel_, 10.0f, true },
        { &fixedVelocityValue_, 14.0f, true },
        { &recorderStatus_, 10.0f, false },
        { &recorderCount_, 9.0f, false },
        { &internalAudioLabel_, 10.0f, true },
        { &internalAudioSubLabel_, 8.0f, false },
        { &infoTitle_, 20.0f, true },
        { &infoVersion_, 12.0f, false },
        { &accountHeader_, 9.0f, true },
        { &infoUser_, 12.0f, false },
        { &infoPlus_, 12.0f, true },
        { &infoLicense_, 12.0f, false },
        { &updatesHeader_, 9.0f, true },
        { &infoCurrentVersion_, 12.0f, false },
        { &infoLatestVersion_, 12.0f, false },
        { &infoStatus_, 12.0f, false },
        { &infoAuthHint_, 11.0f, false },
        { &trialHeader_, 9.0f, true },
        { &trialStatus_, 12.0f, false },
        { &helpTitle_, 10.0f, true },
        { &helpCopy_, 10.0f, false },
        { &footer_, 9.0f, false },
        { &gateTitle_, 20.0f, true },
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
        processor_.setChordPreset(static_cast<chordengine::core::ChordPresetId>(
            presetSelector_.getSelectedId() - 1));
        updateStateDisplay();
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
    logoImage_ = juce::ImageFileFormat::loadFrom(BinaryData::MusicProdLogo_png,
                                                BinaryData::MusicProdLogo_pngSize);
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

    licensing_ = std::make_unique<chordengine::licensing::LicensingBoundary>(
        authService_, updateService_, authFile);
    licensing_->setGateSink([this](bool gateOpen) { processor_.setLicensingGateOpen(gateOpen); });
    licensing_->initialise();
    licensing_->setInfoPageVisible(false);
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
        pill.onDragStart = nullptr;
    for (auto* combo : { &keySelector_, &scaleSelector_, &presetSelector_, &velocityModeSelector_ })
        combo->setLookAndFeel(nullptr);
}

void ChordEngineAudioProcessorEditor::configureLabel(juce::Label& label, const juce::String& value,
                                                     float fontSize, juce::Colour colour,
                                                     bool bold,
                                                     juce::Justification justification)
{
    label.setText(value, juce::dontSendNotification);
    label.setJustificationType(justification);
    label.setFont(juce::Font(juce::FontOptions(fontSize, bold ? juce::Font::bold
                                                              : juce::Font::plain)));
    label.setColour(juce::Label::textColourId, colour);
    label.setInterceptsMouseClicks(false, false);
}

void ChordEngineAudioProcessorEditor::configureComboBox(juce::ComboBox& combo, float, bool)
{
    combo.setLookAndFeel(&comboLookAndFeel_);
    combo.setColour(juce::ComboBox::backgroundColourId, panel);
    combo.setColour(juce::ComboBox::textColourId, text);
    combo.setColour(juce::ComboBox::outlineColourId, border);
    combo.setColour(juce::ComboBox::arrowColourId, accent);
    combo.setColour(juce::ComboBox::buttonColourId, raised);
    combo.setColour(juce::PopupMenu::backgroundColourId, raised);
    combo.setColour(juce::PopupMenu::textColourId, text);
    combo.setColour(juce::PopupMenu::highlightedBackgroundColourId, track);
    combo.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
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

void ChordEngineAudioProcessorEditor::applyTypography()
{
    for (const auto& spec : textSpecs_)
        if (spec.label != nullptr)
            spec.label->setFont(juce::Font(juce::FontOptions(
                juce::jmax(6.0f, spec.designFontSize * designScale_),
                spec.bold ? juce::Font::bold : juce::Font::plain)));

    comboLookAndFeel_.setFontHeight(juce::jmax(8.0f, 12.0f * designScale_));
    for (auto* combo : { &keySelector_, &scaleSelector_, &presetSelector_, &velocityModeSelector_ })
    {
        combo->lookAndFeelChanged();
        combo->resized();
    }
}

void ChordEngineAudioProcessorEditor::paintCard(juce::Graphics& graphics,
                                               juce::Rectangle<int> bounds, juce::Colour fill,
                                               float cornerRadius, bool accentRing) const
{
    const auto area = bounds.toFloat();
    const float radius = cornerRadius * designScale_;
    graphics.setColour(fill);
    graphics.fillRoundedRectangle(area, radius);
    graphics.setColour(border);
    graphics.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    if (accentRing)
    {
        graphics.setColour(accent.withAlpha(0.10f));
        graphics.drawRoundedRectangle(area.expanded(designScale_) , radius + designScale_, 1.0f);
    }
}

void ChordEngineAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(background);

    const auto canvas = design(0, 0, ChordEngineAudioProcessor::defaultEditorWidth,
                               ChordEngineAudioProcessor::defaultEditorHeight).toFloat();
    const auto frame = canvas.reduced(4.0f * designScale_);
    graphics.setGradientFill(juce::ColourGradient(juce::Colour(0xff0e1116), frame.getCentreX(),
                                                 frame.getY(), juce::Colour(0xff0a0c0f),
                                                 frame.getCentreX(), frame.getBottom(), false));
    graphics.fillRoundedRectangle(frame, 10.0f * designScale_);
    graphics.setColour(border);
    graphics.drawRoundedRectangle(frame, 10.0f * designScale_, 1.0f);
    graphics.setColour(accent.withAlpha(0.10f));
    graphics.drawRoundedRectangle(canvas.reduced(3.0f * designScale_), 11.0f * designScale_, 1.0f);
    graphics.setColour(accent.withAlpha(0.05f));
    graphics.drawRoundedRectangle(canvas.reduced(2.0f * designScale_), 12.0f * designScale_, 1.0f);

    if (!infoPageVisible_)
    {
        // Chord display well: charcoal surface with vertical depth.
        const auto well = design(40, 78, 720, 118).toFloat();
        graphics.setGradientFill(juce::ColourGradient(wellTop, well.getCentreX(), well.getY(),
                                                     wellBottom, well.getCentreX(),
                                                     well.getBottom(), false));
        graphics.fillRoundedRectangle(well, 10.0f * designScale_);
        graphics.setColour(border);
        graphics.drawRoundedRectangle(well.reduced(0.5f), 10.0f * designScale_, 1.0f);
        graphics.setColour(juce::Colour(0x14232a33));
        graphics.fillRect(well.getX() + 8.0f * designScale_, well.getY() + designScale_,
                          well.getWidth() - 16.0f * designScale_, designScale_);

        // Recorder card. The reference card surface (424, 80, 316 x 82) is too
        // short to contain its own action pill, which the reference nonetheless
        // places 4 px BELOW the card so the pill ends up inside the surrounding
        // well rather than inside the card. The native card is extended to
        // height 114 (bottom 194, still 2 px inside the well at 196) and the
        // action pill is placed in the strip below the last take row, fully
        // INSIDE its own card and horizontally centred on the card axis
        // (x = 582). Geometry lives in the class constants so paint(),
        // resized() and the layout test can never disagree.
        paintCard(graphics, design(recorderCardX, recorderCardY, recorderCardWidth,
                                   recorderCardHeight), panel, 10.0f, true);  // recorder
        paintCard(graphics, design(40, 478, 348, 78), panel, 10.0f, true);   // velocity mode
        paintCard(graphics, design(408, 478, 352, 78), panel, 10.0f, true);  // fixed velocity
        paintCard(graphics, design(560, 556, 200, 64), panel, 10.0f, true);  // internal audio

        // Octave readout chip.
        paintCard(graphics, design(148, 144, 64, 28), chipFill, 6.0f, false);
        graphics.setColour(chipBorder);
        graphics.drawRoundedRectangle(design(148, 144, 64, 28).toFloat().reduced(0.5f),
                                      6.0f * designScale_, 1.0f);

        // Music-Prod wordmark, in the reference's exact slot and paint order
        // (logoPanel is created before the INFO card, so on the INFO page the
        // card covers it exactly as the reference does).
        drawLogo(graphics);
    }
    else
    {
        drawLogo(graphics);
        paintCard(graphics, design(40, 46, 720, 560), infoBed, 10.0f, false);
        paintCard(graphics, design(64, 508, 560, 98), panel, 10.0f, false);
    }

    // Expiry gate: fully opaque backdrop so no page content can show through
    // (reference trialGatePanel + trialGateCardPanel).
    if (gateVisible_)
    {
        const auto canvasArea = design(0, 0, ChordEngineAudioProcessor::defaultEditorWidth,
                                      ChordEngineAudioProcessor::defaultEditorHeight);
        graphics.setColour(juce::Colour(0xff07090c));
        graphics.fillRect(canvasArea);

        const auto card = design(180, 220, 440, 210).toFloat();
        graphics.setColour(panel);
        graphics.fillRoundedRectangle(card, 12.0f * designScale_);
        graphics.setColour(border);
        graphics.drawRoundedRectangle(card.reduced(0.5f), 12.0f * designScale_, 1.0f);
        graphics.setColour(accent.withAlpha(0.10f));
        graphics.drawRoundedRectangle(card.expanded(designScale_), 13.0f * designScale_, 1.0f);
    }
}

void ChordEngineAudioProcessorEditor::drawLogo(juce::Graphics& graphics) const
{
    if (!logoImage_.isValid())
        return;

    // Reference logoPanel: (270, 560) 260 x 86, image drawn 1:1 into the
    // panel, native aspect ratio preserved.
    graphics.drawImage(logoImage_, design(270, 560, 260, 86).toFloat());
}

void ChordEngineAudioProcessorEditor::resized()
{
    updateDesignMapping();
    applyTypography();
    repaint();

    wordmark_.setBounds(design(200, 8, 360, 48));
    navChord_.setBounds(design(528, 8, 112, 26));
    navInfo_.setBounds(design(648, 8, 112, 26));

    keyLabel_.setBounds(design(188, 200, 92, 12));
    keySelector_.setBounds(design(188, 212, 92, 26));
    scaleLabel_.setBounds(design(304, 200, 92, 12));
    scaleSelector_.setBounds(design(304, 212, 92, 26));
    presetLabel_.setBounds(design(420, 200, 192, 11));
    presetSelector_.setBounds(design(420, 212, 192, 26));

    chordName_.setBounds(design(56, 86, 330, 36));
    chordNotes_.setBounds(design(56, 126, 330, 16));
    transposeLabel_.setBounds(design(56, 154, 84, 20));
    octaveValue_.setBounds(design(148, 144, 64, 28));
    octaveDown_.setBounds(design(216, 144, 28, 28));
    octaveUp_.setBounds(design(248, 144, 28, 28));
    transposeWhole_.setBounds(design(56, 176, 80, 18));
    transposeLowest_.setBounds(design(140, 176, 80, 18));
    transposeHighest_.setBounds(design(224, 176, 80, 18));

    recorderStatus_.setBounds(design(438, 82, 110, 13));
    recorderCount_.setBounds(design(548, 82, 178, 13));
    recorderTakes_[0].setBounds(design(438, 98, 288, 20));
    recorderTakes_[1].setBounds(design(438, 119, 288, 20));
    recorderTakes_[2].setBounds(design(438, 140, 288, 20));
    // Centred horizontally inside the recorder card, with an even 3 px of
    // clearance above (below the last take row) and below (inside the card's
    // bottom edge).
    recorderAction_.setBounds(design(recorderCardX + (recorderCardWidth - recorderActionWidth) / 2,
                                     recorderActionY, recorderActionWidth, recorderActionHeight));

    piano_.setBounds(design(40, 244, 720, 216));

    velocityModeLabel_.setBounds(design(56, 490, 160, 16));
    velocityModeSelector_.setBounds(design(56, 510, 168, 34));
    fixedVelocityLabel_.setBounds(design(432, 490, 200, 16));
    fixedVelocitySlider_.setBounds(design(432, 510, 240, 34));
    fixedVelocityValue_.setBounds(design(688, 510, 56, 34));

    internalAudioLabel_.setBounds(design(574, 564, 130, 14));
    internalAudioSubLabel_.setBounds(design(574, 580, 122, 12));
    internalAudioPill_.setBounds(design(676, 566, 56, 32));

    infoTitle_.setBounds(design(64, 74, 400, 30));
    infoVersion_.setBounds(design(64, 108, 400, 18));
    accountHeader_.setBounds(design(64, 160, 400, 14));
    infoUser_.setBounds(design(64, 184, 560, 18));
    infoPlus_.setBounds(design(64, 208, 560, 18));
    infoLicense_.setBounds(design(64, 232, 560, 18));
    updatesHeader_.setBounds(design(64, 280, 400, 14));
    infoCurrentVersion_.setBounds(design(64, 304, 560, 18));
    infoLatestVersion_.setBounds(design(64, 328, 560, 18));
    infoStatus_.setBounds(design(64, 352, 560, 18));
    checkUpdates_.setBounds(design(64, 384, 170, 26));
    openStudio_.setBounds(design(246, 384, 200, 26));
    signIn_.setBounds(design(456, 384, 112, 26));
    signOut_.setBounds(design(456, 384, 112, 26));
    infoAuthHint_.setBounds(design(64, 416, 440, 18));
    trialHeader_.setBounds(design(64, 448, 400, 14));
    trialStatus_.setBounds(design(64, 472, 560, 18));
    helpTitle_.setBounds(design(80, 520, 300, 18));
    helpCopy_.setBounds(design(80, 543, 520, 18));
    helpButton_.setBounds(design(80, 568, 220, 28));
    footer_.setBounds(design(620, 626, 160, 16));

    // Expiry gate (reference trialGateCardPanel geometry).
    gateTitle_.setBounds(design(200, 252, 400, 30));
    gateBody_.setBounds(design(200, 294, 400, 56));
    gateStudio_.setBounds(design(290, 380, 220, 30));
    gateSignIn_.setBounds(design(290, 416, 220, 30));
}

void ChordEngineAudioProcessorEditor::updateStateDisplay()
{
    const auto configuration = processor_.configurationSnapshot();
    displayedConfiguration_ = configuration;
    keySelector_.setSelectedId(configuration.keyIndex + 1, juce::dontSendNotification);
    scaleSelector_.setSelectedId(static_cast<int>(configuration.scale) + 1,
                                 juce::dontSendNotification);
    presetSelector_.setSelectedId(static_cast<int>(configuration.preset) + 1,
                                  juce::dontSendNotification);
    velocityModeSelector_.setSelectedId(static_cast<int>(configuration.velocityMode) + 1,
                                        juce::dontSendNotification);

    const auto target = configuration.transpose.target;
    const auto paintTarget = [](PillButton& button, bool active)
    {
        if (active)
            button.setPalette(accent, juce::Colours::white);
        else
            button.setPalette(accentDim, caption);
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

    piano_.setPreviewNotes(processor_.previewChordForTrigger(60));
    updateVelocityEnabled();
    updateChordReadout();
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

        chordName_.setText(chordSymbolFor(trigger, configuration), juce::dontSendNotification);
        chordName_.setColour(juce::Label::textColourId, textBright);
        chordNotes_.setText("[" + notes + "]", juce::dontSendNotification);
    }
    else
    {
        // Idle: the reference context line, with the Core's C4 preview chord
        // (the note the readout has always previewed) in the note slot.
        chordName_.setText(juce::String(chordengine::core::ScaleSystem::keyName(
                               configuration.keyIndex).data())
                               + " " + juce::String(scale->name.data()) + " - "
                               + juce::String(preset->name.data()),
                           juce::dontSendNotification);
        chordName_.setColour(juce::Label::textColourId, idle);
        chordNotes_.setText("[" + noteListText(processor_.previewChordForTrigger(60)) + "]",
                            juce::dontSendNotification);
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
    }
    else if (state == 2)
    {
        status = "RECORDING";
        statusColour = stopFill;
    }
    else if (count == 0)
    {
        status = "WAITING FOR ARM";
    }
    else
    {
        status = juce::String(static_cast<int>(count))
            + (count == 1 ? " TAKE STORED" : " TAKES STORED");
        statusColour = accent;
    }

    if (processor_.isRecorderOverflowed())
    {
        status = "BUFFER LIMIT REACHED";
        statusColour = stopFill;
    }
    recorderCount_.setText(status, juce::dontSendNotification);
    recorderCount_.setColour(juce::Label::textColourId, statusColour);

    if (state == 1)
    {
        recorderAction_.setButtonText("CANCEL");
        recorderAction_.setPalette(cancelFill, text);
    }
    else if (state == 2)
    {
        recorderAction_.setButtonText("STOP TAKE");
        recorderAction_.setPalette(stopFill, text);
    }
    else
    {
        recorderAction_.setButtonText("RECORD ARM");
        recorderAction_.setPalette(accent, juce::Colours::white);
    }

    const auto displayed = juce::jmin<std::uint8_t>(count,
        static_cast<std::uint8_t>(recorderTakes_.size()));
    for (std::size_t index = 0; index < recorderTakes_.size(); ++index)
    {
        auto& pill = recorderTakes_[index];
        const bool available = index < displayed;
        pill.setVisible(!infoPageVisible_ && !gateVisible_ && available);
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
    const auto paintNavigation = [](PillButton& button, bool active)
    {
        if (active)
            button.setPalette(track, juce::Colours::white, accent.withAlpha(0.45f));
        else
            button.setPalette(raised, muted, juce::Colour());
    };
    paintNavigation(navChord_, !infoPageVisible_);
    paintNavigation(navInfo_, infoPageVisible_);
}

bool ChordEngineAudioProcessorEditor::startRecorderTakeDrag(std::size_t index)
{
    if (index >= recorderTakes_.size())
        return false;

    auto destination = juce::File::createTempFile(
        "ChordEngine_Take_" + juce::String(static_cast<int>(index + 1)) + ".mid");
    destination.deleteFile();
    if (!processor_.exportRecorderTake(static_cast<std::uint8_t>(index), destination))
        return false;

    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        juce::StringArray(destination.getFullPathName()), false, this,
        [destination] { juce::MessageManager::callAsync([destination] { destination.deleteFile(); }); });
    if (!started)
        destination.deleteFile();
    return started;
}

void ChordEngineAudioProcessorEditor::setSelectedPage(bool showInfo)
{
    infoPageVisible_ = showInfo;
    const bool showChord = !infoPageVisible_;

    // While the expiry gate owns the screen, NO normal surface may stay
    // visible: the reference hides every normal component explicitly and
    // raises the gate last, so nothing can render or be clicked above it.
    const bool gate = gateVisible_;

    const std::array<juce::Component*, 27> chordComponents {{
        &keyLabel_, &keySelector_, &scaleLabel_, &scaleSelector_, &presetLabel_, &presetSelector_,
        &chordName_, &chordNotes_, &transposeLabel_, &octaveValue_, &octaveDown_, &octaveUp_,
        &transposeWhole_, &transposeLowest_, &transposeHighest_, &velocityModeLabel_,
        &velocityModeSelector_, &fixedVelocityLabel_, &fixedVelocitySlider_, &fixedVelocityValue_,
        &piano_, &recorderStatus_, &recorderCount_, &recorderAction_,
        &recorderTakes_[0], &recorderTakes_[1], &recorderTakes_[2] }};
    for (auto* component : chordComponents)
        component->setVisible(showChord && !gate);

    // Leaving the CHORD page (or raising the gate) must never strand a held
    // note-on: the key is no longer visible, so it is released explicitly.
    if (!showChord || gate)
        piano_.releaseHeldNote();

    // Reference: INTERNAL AUDIO is owned by the CHORD page.
    internalAudioLabel_.setVisible(showChord && !gate);
    internalAudioSubLabel_.setVisible(showChord && !gate);
    internalAudioPill_.setVisible(showChord && !gate);

    const std::array<juce::Component*, 20> infoComponents {{
        &infoTitle_, &infoVersion_, &accountHeader_, &infoUser_, &infoPlus_, &infoLicense_,
        &updatesHeader_, &infoCurrentVersion_, &infoLatestVersion_, &infoStatus_,
        &checkUpdates_, &openStudio_, &signIn_, &signOut_, &infoAuthHint_,
        &trialHeader_, &trialStatus_,
        &helpTitle_, &helpCopy_, &helpButton_ }};
    for (auto* component : infoComponents)
        component->setVisible(infoPageVisible_ && !gate);

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
                        juce::Colour(chordengine::licensing::plusLineColour(snapshot)));
    infoLicense_.setText("License  " + chordengine::licensing::licenseLineText(snapshot),
                         juce::dontSendNotification);

    // The update rows are the real client's view: an honest state, never a
    // fabricated version and never a placeholder dash once an answer exists.
    const auto update = updateView();
    infoLatestVersion_.setText(chordengine::updates::latestVersionLine(update),
                               juce::dontSendNotification);
    infoStatus_.setText(chordengine::updates::statusLine(update), juce::dontSendNotification);
    infoStatus_.setColour(juce::Label::textColourId,
                          juce::Colour(chordengine::updates::statusColour(update)));

    trialStatus_.setText(chordengine::licensing::trialStatusText(snapshot),
                         juce::dontSendNotification);
    trialStatus_.setColour(juce::Label::textColourId,
                           juce::Colour(chordengine::licensing::trialStatusColour(snapshot)));

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
    piano_.repaint();
}
