#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>

#include "Core/ChordEngineCore.h"
#include "Licensing/LicensingBoundary.h"
#include "Licensing/LicensingServices.h"
#include "Licensing/MusicProdAuthService.h"
#include "PluginProcessor.h"
#include "Updates/UpdateService.h"
#include "Updates/UpdateTransport.h"

class ChordEngineAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    // An empty licensingStorageDir selects the production Music-Prod AppData
    // location (the same folder VYRE uses); tests pass a temporary directory
    // so they never read or write the user's real plugin state, and so the
    // update cache lands in that directory too.
    //
    // updateTransport is the test seam for the update check: production passes
    // nothing and the real HTTP transport is built, while a test injects a
    // deterministic transport so no automated run touches the live service.
    explicit ChordEngineAudioProcessorEditor(
        ChordEngineAudioProcessor& processor,
        juce::File licensingStorageDir = {},
        std::unique_ptr<chordengine::updates::UpdateTransport> updateTransport = {});
    ~ChordEngineAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    // Recorder geometry in the reference 800x650 design space. The reference
    // card surface (424, 80, 316 x 82) is too short to hold its own action
    // pill, which the reference nonetheless has to place 4 px BELOW it. The
    // native card is therefore extended to 114 px (bottom edge 194, still 2 px
    // inside the surrounding well at 196) and the action pill is placed in the
    // 34 px strip the card has below its last take row (which ends at 160):
    // 3 px above and 3 px below the pill. The pill is therefore fully INSIDE
    // its own card, never touching the card boundary, and horizontally centred
    // on the card axis (x = 582). These values are shared by paint(),
    // resized() and the layout test, so they can never drift apart.
    static constexpr int recorderCardX = 424;
    static constexpr int recorderCardY = 80;
    static constexpr int recorderCardWidth = 316;
    static constexpr int recorderCardHeight = 114;
    static constexpr int recorderActionWidth = 92;
    static constexpr int recorderActionHeight = 28;
    static constexpr int recorderActionY = 163;

    // The recorder card rectangle in editor coordinates, from the same mapping
    // paint() uses (so a layout check can never drift from the painted card).
    juce::Rectangle<int> recorderCardBounds() const noexcept
    {
        return design(recorderCardX, recorderCardY, recorderCardWidth, recorderCardHeight);
    }

    // The MIDI note under a point in EDITOR coordinates on the on-screen
    // keyboard, or -1. Shares the keyboard's single geometry implementation
    // with painting and mouse handling - there is no second hit-test copy.
    int pianoNoteAtEditorPoint(juce::Point<float> editorPoint) const noexcept
    {
        return piano_.noteAtPosition(piano_.getLocalPoint(this, editorPoint));
    }

    // The update client whose view the INFO page renders. Exposed so a test can
    // wait for an asynchronous check to settle; it is never null after
    // construction.
    chordengine::updates::UpdateService& updateService() const noexcept { return *updates_; }

private:
    // ------------------------------------------------ design-space mapping
    // Every child bound and painted card is expressed in the reference
    // product's 800x650 design space and mapped through one uniform
    // scale + centring offset, so the layout stays faithful at any size
    // instead of drifting with magic coordinates.
    juce::Rectangle<int> design(int x, int y, int width, int height) const noexcept;
    void updateDesignMapping() noexcept;
    float designScale_ = 1.0f;
    int designOffsetX_ = 0;
    int designOffsetY_ = 0;

    // ------------------------------------- reference visual-language surfaces
    class ChordEngineWordmark final : public juce::Component
    {
    public:
        void paint(juce::Graphics& graphics) override;
    };

    // Combo boxes take their text font from the look and feel, so one shared
    // instance carries the design-space font height into the reference-sized
    // selectors.
    class ComboLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void setFontHeight(float height) { fontHeight_ = height; }
        juce::Font getComboBoxFont(juce::ComboBox&) override
        {
            return juce::Font(juce::FontOptions(fontHeight_));
        }

    private:
        float fontHeight_ = 13.0f;
    };

    // Rounded pill in the reference control language (nav, transpose
    // targets, recorder action, INFO-page buttons).
    class PillButton final : public juce::Button
    {
    public:
        PillButton(const juce::String& buttonText, float cornerRadius,
                   float designHeight, float designFontHeight);
        void setPalette(juce::Colour fill, juce::Colour textColour,
                        juce::Colour outlineColour = juce::Colour(),
                        juce::Colour pressedFill = juce::Colour());
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

    private:
        float cornerRadius_ = 6.0f;
        float designHeight_ = 28.0f;
        float designFontHeight_ = 10.0f;
        juce::Colour fill_ {};
        juce::Colour textColour_ {};
        juce::Colour outline_ {};
        juce::Colour pressed_ {};
    };

    // Octave stepper: the reference draws a bar glyph (minus) or bar
    // cross (plus) inside an accent-outlined pill, so no font glyph is
    // involved and no encoding can corrupt it.
    class StepButton final : public juce::Button
    {
    public:
        StepButton(const juce::String& buttonName, bool increments);
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

    private:
        bool increments_ = false;
    };

    // Recorder take pill: two-line reference pill that is also the
    // external MIDI drag surface.
    class TakePillButton final : public juce::Button
    {
    public:
        explicit TakePillButton(const juce::String& buttonName);
        void setTake(int takeNumber, int noteCount);
        std::function<bool()> onDragStart;
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;

    private:
        int takeNumber_ = 0;
        int noteCount_ = 0;
        bool dragStarted_ = false;
    };

    // INTERNAL AUDIO is genuinely unavailable in this plugin architecture:
    // the reference drives its own SineSynth through setBypassed(), which
    // needs an audio output bus, and this product is an AU aumi / VST3 MIDI
    // effect with NO audio buses. Adding one would change the plugin format
    // and the host routing the product is built on, so the card keeps the
    // reference caption and geometry but its chip is a plain (non-button)
    // status surface that publishes the capability as unavailable. It has no
    // click handler and no ON/OFF state, so it can never fake a toggle.
    class InternalAudioPill final : public juce::Component,
                                   public juce::SettableTooltipClient
    {
    public:
        void paint(juce::Graphics& graphics) override;
    };

    // The piano is a real input surface, not a readout: pressing a key pushes a
    // raw MIDI note into the processor (see pushUiNoteOn), so a mouse key
    // travels the identical path as an external keyboard. Painting and hit
    // testing share blackKeyRect/whiteKeyRect, so the key that lights up and
    // the key that sounds can never disagree.
    class PianoDisplay final : public juce::Component
    {
    public:
        explicit PianoDisplay(ChordEngineAudioProcessor& processor);
        ~PianoDisplay() override;
        void paint(juce::Graphics& graphics) override;
        void setPreviewNotes(const chordengine::core::NoteSet& notes);

        // Pure geometry shared by painting, hit testing and tests. Returns -1
        // when the point is not on a key.
        int noteAtPosition(juce::Point<float> position) const noexcept;
        int heldNote() const noexcept { return heldNote_; }
        // Releases the held note (mouse-up, editor teardown, page change).
        void releaseHeldNote() noexcept;

        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        juce::Rectangle<float> keyboardBounds() const noexcept;
        static bool isWhiteKey(int note) noexcept;
        int whiteKeyCount() const noexcept;
        int whiteKeysBefore(int note) const noexcept;
        float whiteKeyWidth() const noexcept;
        juce::Rectangle<float> whiteKeyRect(int whiteIndex) const noexcept;
        juce::Rectangle<float> blackKeyRect(int note) const noexcept;
        void pressNote(int note) noexcept;

        ChordEngineAudioProcessor& processor_;
        std::array<bool, 128> previewNotes_ {};
        int heldNote_ = -1;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoDisplay)
    };

    // Design-space typography: every text surface keeps its reference font
    // size at 800x650 and scales with the same uniform factor as the layout.
    struct TextSpec
    {
        juce::Label* label = nullptr;
        float designFontSize = 12.0f;
        bool bold = false;
    };
    static constexpr std::size_t textSpecCount = 33;
    std::array<TextSpec, textSpecCount> textSpecs_ {};
    ComboLookAndFeel comboLookAndFeel_;
    void applyTypography();

    // Licensing reference geometry (read-only reference script).
    static constexpr double licensingTickMs = 100.0; // Synth.startTimer(0.1)

    // ------------------------------------------------------------------ state
    void timerCallback() override;
    void updateStateDisplay();
    void updateVelocityEnabled();
    void updateChordReadout();
    void updateRecorderDisplay();
    void updateNavigation();
    // The state the INFO update rows render. Before the update client exists
    // (early construction) or before it has answered, this is the idle view.
    chordengine::updates::UpdateView updateView() const;
    void updateInfoDisplay();
    void updateGateDisplay();
    void driveLicensingTick(double nowMs);
    void openStudio();
    bool startRecorderTakeDrag(std::size_t index);
    void setSelectedPage(bool showInfo);
    void paintCard(juce::Graphics& graphics, juce::Rectangle<int> bounds,
                   juce::Colour fill, float cornerRadius, bool accentRing) const;
    void drawLogo(juce::Graphics& graphics) const;
    static void configureLabel(juce::Label& label, const juce::String& text,
                               float fontSize, juce::Colour colour, bool bold = false,
                               juce::Justification justification = juce::Justification::centredLeft);
    void configureComboBox(juce::ComboBox& combo, float fontSize, bool bold);

    ChordEngineAudioProcessor& processor_;

    // Real Music-Prod wordmark asset (260x86 RGBA), the exact reference file:
    // Resources/MusicProdLogo.png, MD5 9f221d8f5555684d91474bba301a43d9.
    juce::Image logoImage_;

    ChordEngineWordmark wordmark_;
    PillButton navChord_ { "CHORD", 6.0f, 26.0f, 10.0f };
    PillButton navInfo_ { "INFO", 6.0f, 26.0f, 10.0f };

    juce::Label keyLabel_;
    juce::ComboBox keySelector_;
    juce::Label scaleLabel_;
    juce::ComboBox scaleSelector_;
    juce::Label presetLabel_;
    juce::ComboBox presetSelector_;
    juce::Label chordName_;
    juce::Label chordNotes_;
    juce::Label transposeLabel_;
    juce::Label octaveValue_;
    StepButton octaveDown_ { "octave-down", false };
    StepButton octaveUp_ { "octave-up", true };
    PillButton transposeWhole_ { "WHOLE", 5.0f, 18.0f, 9.0f };
    PillButton transposeLowest_ { "LOWEST", 5.0f, 18.0f, 9.0f };
    PillButton transposeHighest_ { "HIGHEST", 5.0f, 18.0f, 9.0f };

    juce::Label velocityModeLabel_;
    juce::ComboBox velocityModeSelector_;
    juce::Label fixedVelocityLabel_;
    juce::Slider fixedVelocitySlider_;
    juce::Label fixedVelocityValue_;

    PianoDisplay piano_;

    juce::Label recorderStatus_;
    juce::Label recorderCount_;
    PillButton recorderAction_ { "RECORD ARM", 8.0f, 28.0f, 12.0f };
    std::array<TakePillButton, 3> recorderTakes_ { TakePillButton { "recorder-take-1" },
                                                   TakePillButton { "recorder-take-2" },
                                                   TakePillButton { "recorder-take-3" } };

    juce::Label internalAudioLabel_;
    juce::Label internalAudioSubLabel_;
    InternalAudioPill internalAudioPill_;

    juce::Label infoTitle_;
    juce::Label infoVersion_;
    juce::Label accountHeader_;
    juce::Label infoUser_;
    juce::Label infoPlus_;
    juce::Label infoLicense_;
    juce::Label updatesHeader_;
    juce::Label infoCurrentVersion_;
    juce::Label infoLatestVersion_;
    juce::Label infoStatus_;
    PillButton checkUpdates_ { "CHECK FOR UPDATES", 6.0f, 26.0f, 10.0f };
    PillButton openStudio_ { "OPEN MUSIC-PROD STUDIO", 6.0f, 26.0f, 10.0f };
    PillButton signIn_ { "SIGN IN", 6.0f, 26.0f, 10.0f };
    PillButton signOut_ { "SIGN OUT", 6.0f, 26.0f, 10.0f };
    juce::Label infoAuthHint_;
    juce::Label trialHeader_;
    juce::Label trialStatus_;
    juce::Label helpTitle_;
    juce::Label helpCopy_;
    PillButton helpButton_ { "OPEN HELP & FEEDBACK", 6.0f, 28.0f, 10.0f };
    juce::Label footer_;

    // Expiry gate (reference trialGatePanel / trialGateCardPanel).
    juce::Label gateTitle_;
    juce::Label gateBody_;
    PillButton gateStudio_ { "OPEN MUSIC-PROD STUDIO", 6.0f, 30.0f, 10.0f };
    PillButton gateSignIn_ { "SIGN IN", 6.0f, 30.0f, 10.0f };

    chordengine::licensing::MusicProdAuthService authService_;
    chordengine::licensing::MusicProdUpdateService updateService_;
    // The real, anonymous update client. It is deliberately separate from the
    // licensing boundary below: the INFO update rows read THIS view, the check
    // needs no account state, and nothing here writes licensing state.
    std::unique_ptr<chordengine::updates::UpdateService> updates_;
    std::unique_ptr<chordengine::licensing::LicensingBoundary> licensing_;
    double lastLicensingTickMs_ = 0.0;

    bool infoPageVisible_ = false;
    bool gateVisible_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordEngineAudioProcessorEditor)
};
