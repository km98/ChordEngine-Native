#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "Core/ChordEngineCore.h"
#include "Licensing/LicensingBoundary.h"
#include "Licensing/LicensingServices.h"
#include "Licensing/MusicProdAuthService.h"
#include "Packs/PackLibrary.h"
#include "Packs/PackStoreClient.h"
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
        std::unique_ptr<chordengine::updates::UpdateTransport> updateTransport = {},
        std::unique_ptr<chordengine::packs::PackStoreTransport> packTransport = {});
    ~ChordEngineAudioProcessorEditor() override;

    enum class Page { chord, packs, info };

    // Read-only view of the configuration the controls currently show. Used by
    // tests to prove the GUI re-synchronised every restored field.
    const chordengine::core::CoreConfiguration& displayedConfiguration() const noexcept
    {
        return displayedConfiguration_;
    }

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    // Recorder geometry in the 800x650 design space (editorial V2). The
    // recorder is an open column at the right of the chord identity block,
    // set off only by a hairline at x = 568 - no card. Its title line, state
    // line, three take rows (118..177) and the full-column action stack
    // inside the column; the action sits 3 px below the last take row, 4 px
    // clear of the column's bottom edge and 2 px in from each side, so it is
    // centred on the column axis (x = 674). These values are shared by paint(),
    // resized() and the layout test, so they can never drift apart.
    static constexpr int recorderCardX = 588;
    static constexpr int recorderCardY = 80;
    static constexpr int recorderCardWidth = 172;
    static constexpr int recorderCardHeight = 132;
    static constexpr int recorderActionWidth = 168;
    static constexpr int recorderActionHeight = 28;
    static constexpr int recorderActionY = 180;

    // The recorder card rectangle in editor coordinates, from the same mapping
    // paint() uses (so a layout check can never drift from the painted card).
    juce::Rectangle<int> recorderCardBounds() const noexcept
    {
        return design(recorderCardX, recorderCardY, recorderCardWidth, recorderCardHeight);
    }

    // The INFO page's regions, from the same mapping paint() and resized()
    // use. In the editorial layout they are open regions on the page (set
    // off by hairline rules, not cards): the content column that holds every
    // INFO control, and the help line at its foot. The lifetime-licence row
    // must stay inside the content region and clear of the help line, so a
    // layout check reads THESE rectangles rather than restating them.
    juce::Rectangle<int> infoBedBounds() const noexcept { return design(40, 72, 720, 506); }
    juce::Rectangle<int> infoHelpPanelBounds() const noexcept { return design(40, 532, 720, 42); }

    // The CHORD page's regions, from the same mapping: the chord identity
    // block (readout plus recorder column) and the secondary-control line.
    juce::Rectangle<int> chordHeroBounds() const noexcept { return design(40, 72, 720, 144); }
    juce::Rectangle<int> outputStripBounds() const noexcept { return design(40, 494, 720, 64); }

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
    // The product wordmark: "Chord Engine" set as live text in a script face
    // (see wordmarkFace() in PluginEditor.cpp), published to accessibility
    // clients under the same title.
    class ChordEngineWordmark final : public juce::Component
    {
    public:
        ChordEngineWordmark();
        void paint(juce::Graphics& graphics) override;
    };

    // One look and feel for every stock JUCE widget the editor uses (combo
    // boxes and their popup menus, the fixed-velocity slider), so selectors
    // and sliders share the redesign's surfaces, hover/focus rings and the
    // design-space font height. A combo box whose "emphasis" property is set
    // (the chord preset selector) carries the accent outline at rest.
    class EditorLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        EditorLookAndFeel();
        void setFontHeight(float height) { fontHeight_ = height; }
        juce::Font getComboBoxFont(juce::ComboBox& box) override;
        juce::Font getPopupMenuFont() override;
        void drawComboBox(juce::Graphics& graphics, int width, int height, bool isButtonDown,
                          int buttonX, int buttonY, int buttonW, int buttonH,
                          juce::ComboBox& box) override;
        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
        void drawPopupMenuBackground(juce::Graphics& graphics, int width, int height) override;
        int getSliderThumbRadius(juce::Slider& slider) override;
        void drawLinearSlider(juce::Graphics& graphics, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              juce::Slider::SliderStyle style, juce::Slider& slider) override;
        void fillTextEditorBackground(juce::Graphics& graphics, int width, int height,
                                      juce::TextEditor& editor) override;
        void drawTextEditorOutline(juce::Graphics& graphics, int width, int height,
                                   juce::TextEditor& editor) override;

    private:
        float fontHeight_ = 13.0f;
    };

    // Rounded button in the redesign's control language (navigation
    // segments, transpose targets, recorder action, INFO-page actions). Hover,
    // pressed, disabled and keyboard-focus states are all painted here, so
    // every button in the editor reacts the same way.
    class PillButton final : public juce::Button
    {
    public:
        PillButton(const juce::String& buttonText, float cornerRadius,
                   float designHeight, float designFontHeight);
        void setPalette(juce::Colour fill, juce::Colour textColour,
                        juce::Colour outlineColour = juce::Colour(),
                        juce::Colour pressedFill = juce::Colour());
        // A small status dot drawn before the label (transparent = none).
        void setIndicator(juce::Colour colour);
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

    private:
        float cornerRadius_ = 6.0f;
        float designHeight_ = 28.0f;
        float designFontHeight_ = 10.0f;
        juce::Colour fill_ {};
        juce::Colour textColour_ {};
        juce::Colour outline_ {};
        juce::Colour pressed_ {};
        juce::Colour indicator_ {};
    };

    // Square icon button whose glyph is drawn from bars and strokes (octave
    // minus / plus, previous / next preset), so no font glyph is involved
    // and no encoding can corrupt it.
    class StepButton final : public juce::Button
    {
    public:
        enum class Glyph { minus, plus, previous, next };
        StepButton(const juce::String& buttonName, Glyph glyph);
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;

    private:
        Glyph glyph_ = Glyph::minus;
    };

    // "Key  value" row on the INFO page. The licensing and update models
    // publish each row as one string with a two-space separator (for example
    // "User  Not signed in"); this label keeps that exact text as its value
    // and only PAINTS the key in the quiet secondary tone, wrapping a long
    // value onto a second line instead of squeezing it.
    class InfoRowLabel final : public juce::Label
    {
    public:
        void paint(juce::Graphics& graphics) override;
    };

    // The chord readout's note list ("D#3 G3 A#3 D4"): the label keeps the
    // plain text and paints each note as a token, accented while a chord is
    // actually sounding.
    class NoteTokensLabel final : public juce::Label
    {
    public:
        void setLive(bool live);
        void paint(juce::Graphics& graphics) override;

    private:
        bool live_ = false;
    };

    // Recorder take pill: two-line reference pill that is also the
    // external MIDI drag surface.
    class TakePillButton final : public juce::Button
    {
    public:
        explicit TakePillButton(const juce::String& buttonName);
        void setTake(int takeNumber, int noteCount);
        std::function<bool()> onDragStart;
        std::function<void()> onReveal; // right-click: "Show in Finder"
        void paintButton(juce::Graphics& graphics, bool highlighted, bool down) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;

    private:
        int takeNumber_ = 0;
        int noteCount_ = 0;
        bool dragStarted_ = false;
    };

    // The INTERNAL AUDIO chip of the MIDI FX AU, which owns no audio bus
    // (Logic Pro takes no audio from a MIDI FX slot). It is a plain
    // (non-button) status surface pointing at the ChordEngine Instrument AU;
    // it has no click handler and no ON/OFF state, so it can never fake a
    // toggle. Builds with an audio output show a real switch instead.
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

public:
    // The PACKS page list: one row per pack (built in, installed, in the
    // store), each with its status and one action. Rows are painted, not
    // child components, so the list scrolls inside a Viewport as one surface.
    // Public so the functional tests can read the rows it shows.
    class PackListView final : public juce::Component
    {
    public:
        static constexpr float designRowHeight = 64.0f;
        void setListings(std::vector<chordengine::packs::PackListing> listings,
                         juce::String busySlug);
        void setScale(float scale);
        int rowHeight() const noexcept;
        const std::vector<chordengine::packs::PackListing>& listings() const noexcept { return listings_; }
        // The action label a row shows ("DOWNLOAD", "INSTALLED", ...).
        static juce::String actionText(const chordengine::packs::PackListing& listing, bool busy);
        std::function<void(int)> onAction;
        void paint(juce::Graphics& graphics) override;
        void mouseMove(const juce::MouseEvent& event) override;
        void mouseExit(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        juce::Rectangle<int> actionBounds(int row) const;
        int actionRowAt(juce::Point<int> position) const;
        std::vector<chordengine::packs::PackListing> listings_;
        juce::String busySlug_;
        float scale_ = 1.0f;
        int hoverRow_ = -1;
    };

private:

    // Design-space typography: every text surface keeps its reference font
    // size at 800x650 and scales with the same uniform factor as the layout.
    // Captions (KEY, SCALE, ACCOUNT, ...) carry extra letter spacing; the
    // display role (the chord name and the INFO title) uses the serif display
    // face and scales with the layout, while text roles scale more gently so
    // small editors stay legible and large ones gain air (see textScale()).
    enum class TextRole { text, caption, display };
    struct TextSpec
    {
        juce::Label* label = nullptr;
        float designFontSize = 12.0f;
        bool bold = false;
        float kerning = 0.0f;
        TextRole role = TextRole::text;
    };
    float textScale() const noexcept;
    static constexpr std::size_t textSpecCount = 39;
    std::array<TextSpec, textSpecCount> textSpecs_ {};
    EditorLookAndFeel lookAndFeel_;
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
    void revealRecorderTake(std::size_t index);
    // Writes take `index` to a uniquely named, persistent .mid file in
    // takeExportDir_ and returns it, or an empty File on failure.
    juce::File writeRecorderTakeFile(std::size_t index);
    void setSelectedPage(Page page);
    void setSelectedPage(bool showInfo) { setSelectedPage(showInfo ? Page::info : Page::chord); }
    void stepPreset(int direction);
    void paintSurface(juce::Graphics& graphics, juce::Rectangle<int> bounds,
                      juce::Colour fill, float cornerRadius,
                      juce::Colour outline = juce::Colour()) const;
    void paintHairline(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintChordPage(juce::Graphics& graphics) const;
    void paintInfoPage(juce::Graphics& graphics) const;
    void drawLogo(juce::Graphics& graphics) const;
    static void configureLabel(juce::Label& label, const juce::String& text,
                               float fontSize, juce::Colour colour, bool bold = false,
                               juce::Justification justification = juce::Justification::centredLeft);
    void configureComboBox(juce::ComboBox& combo, float fontSize, bool bold);

    ChordEngineAudioProcessor& processor_;

    // Real Music-Prod wordmark asset (260x86 RGBA), the exact reference file:
    // Resources/MusicProdLogo.png, MD5 9f221d8f5555684d91474bba301a43d9. The
    // file itself is unchanged; at load time its white-on-black artwork is
    // turned into an ivory mark on a transparent ground (see
    // makeMonochromeMark) so it sits on the graphite chrome without a black
    // slab behind it.
    juce::Image logoImage_;

    ChordEngineWordmark wordmark_;
    PillButton navChord_ { "CHORD", 4.0f, 24.0f, 9.5f };
    PillButton navPacks_ { "PACKS", 4.0f, 24.0f, 9.5f };
    PillButton navInfo_ { "INFO", 4.0f, 24.0f, 9.5f };

    juce::Label keyLabel_;
    juce::ComboBox keySelector_;
    juce::Label scaleLabel_;
    juce::ComboBox scaleSelector_;
    juce::Label presetLabel_;
    juce::ComboBox presetSelector_;
    // Preset browsing: step through the chord presets in catalogue order
    // (wrapping), with the position shown above the selector.
    StepButton presetPrevious_ { "preset-previous", StepButton::Glyph::previous };
    StepButton presetNext_ { "preset-next", StepButton::Glyph::next };
    juce::Label presetCounter_;
    juce::Label chordCaption_;
    juce::Label chordName_;
    NoteTokensLabel chordNotes_;
    juce::Label transposeLabel_;
    juce::Label octaveValue_;
    StepButton octaveDown_ { "octave-down", StepButton::Glyph::minus };
    StepButton octaveUp_ { "octave-up", StepButton::Glyph::plus };
    PillButton transposeWhole_ { "WHOLE", 4.0f, 28.0f, 9.0f };
    PillButton transposeLowest_ { "LOWEST", 4.0f, 28.0f, 9.0f };
    PillButton transposeHighest_ { "HIGHEST", 4.0f, 28.0f, 9.0f };

    juce::Label velocityModeLabel_;
    juce::ComboBox velocityModeSelector_;
    juce::Label fixedVelocityLabel_;
    juce::Slider fixedVelocitySlider_;
    juce::Label fixedVelocityValue_;

    PianoDisplay piano_;

    juce::Label recorderStatus_;
    juce::Label recorderCount_;
    // Shown in the recorder well while it holds no take, so the empty well
    // explains the workflow instead of reading as unfinished.
    juce::Label recorderHint_;
    // Export quantize grid for recorded takes (FL Studio's list).
    juce::ComboBox recordQuantizeSelector_;
    PillButton recorderAction_ { "RECORD ARM", 4.0f, 28.0f, 9.5f };
    // FOLLOW DAW RECORD switch under RECORD ARM (off by default).
    PillButton followHostRecord_ { "FOLLOW DAW RECORD: OFF", 4.0f, 20.0f, 8.5f };
    int displayedFollowHostRecord_ = -1;
    std::array<TakePillButton, 3> recorderTakes_ { TakePillButton { "recorder-take-1" },
                                                   TakePillButton { "recorder-take-2" },
                                                   TakePillButton { "recorder-take-3" } };

    juce::Label internalAudioLabel_;
    juce::Label internalAudioSubLabel_;
    InternalAudioPill internalAudioPill_;
    // Audio-output builds (VST3, ChordEngine Instrument AU) only.
    PillButton internalAudioToggle_ { "OFF", 4.0f, 28.0f, 9.0f };
    juce::ComboBox internalSoundSelector_;
    bool displayedInternalAudio_ = false;
    int displayedInternalSound_ = -1;
    void updateInternalAudioDisplay();

    juce::Label infoTitle_;
    juce::Label infoVersion_;
    juce::Label accountHeader_;
    InfoRowLabel infoUser_;
    InfoRowLabel infoPlus_;
    InfoRowLabel infoLicense_;
    // The lifetime-licence route: the customer's key, and the one button that
    // activates it on THIS computer. It sits directly under the ACCOUNT block
    // because it is an account-level route, and it is never presented as a
    // substitute for Music-Prod+ - either route grants the full version on
    // its own.
    juce::Label licenseKeyLabel_;
    juce::TextEditor licenseKeyEditor_;
    PillButton activateLicense_ { "ACTIVATE", 4.0f, 30.0f, 9.5f };
    juce::Label updatesHeader_;
    InfoRowLabel infoCurrentVersion_;
    InfoRowLabel infoLatestVersion_;
    InfoRowLabel infoStatus_;
    PillButton checkUpdates_ { "CHECK FOR UPDATES", 4.0f, 28.0f, 9.5f };
    PillButton openStudio_ { "OPEN MUSIC-PROD STUDIO", 4.0f, 28.0f, 9.5f };
    PillButton signIn_ { "SIGN IN", 4.0f, 28.0f, 9.5f };
    PillButton signOut_ { "SIGN OUT", 4.0f, 28.0f, 9.5f };
    juce::Label infoAuthHint_;
    juce::Label trialHeader_;
    InfoRowLabel trialStatus_;
    juce::Label helpTitle_;
    juce::Label helpCopy_;
    PillButton helpButton_ { "OPEN HELP & FEEDBACK", 4.0f, 28.0f, 9.5f };
    juce::Label footer_;
    // Dragged takes are written here and kept (see startRecorderTakeDrag).
    juce::File takeExportDir_;

    // Expiry gate (reference trialGatePanel / trialGateCardPanel).
    juce::Label gateTitle_;
    juce::Label gateBody_;
    PillButton gateStudio_ { "OPEN MUSIC-PROD STUDIO", 4.0f, 30.0f, 9.5f };
    PillButton gateSignIn_ { "SIGN IN", 4.0f, 30.0f, 9.5f };

    chordengine::licensing::MusicProdAuthService authService_;
    chordengine::licensing::MusicProdUpdateService updateService_;
    // The real, anonymous update client. It is deliberately separate from the
    // licensing boundary below: the INFO update rows read THIS view, the check
    // needs no account state, and nothing here writes licensing state.
    std::unique_ptr<chordengine::updates::UpdateService> updates_;
    std::unique_ptr<chordengine::licensing::LicensingBoundary> licensing_;
    double lastLicensingTickMs_ = 0.0;
    // The configuration the controls currently show. The timer compares it with
    // the processor so restored or externally changed settings reach the GUI.
    chordengine::core::CoreConfiguration displayedConfiguration_ {};

    bool infoPageVisible_ = false;
    bool packsPageVisible_ = false;
    bool gateVisible_ = false;

    // ---- Preset Packs ------------------------------------------------
    // The chord-preset menu lists the 12 stock presets, then the presets of
    // every installed pack the account may use (Music-Prod+). Item id - 1
    // indexes presetEntries_.
    struct PresetEntry
    {
        int stockIndex = -1;    // >= 0 for a stock preset
        juce::String packSlug;  // otherwise the pack and preset
        int presetIndex = -1;
    };
    std::vector<PresetEntry> presetEntries_;
    juce::String presetMenuKey_;     // what the menu was last built from
    juce::String displayedPackKey_;  // the pack preset the controls show
    bool packMemberActive() const;
    void rebuildPresetMenu();
    int currentPresetEntry() const;
    void selectPresetEntry(int entryIndex);

    juce::Label packsTitle_;
    juce::Label packsSubtitle_;
    juce::Label packsStatus_;
    PillButton packsRefresh_ { "REFRESH", 4.0f, 28.0f, 9.5f };
    PillButton packsSignIn_ { "SIGN IN", 4.0f, 28.0f, 9.5f };
    juce::Viewport packsViewport_;
    PackListView packList_;
    std::unique_ptr<chordengine::packs::PackLibrary> packLibrary_;
    std::unique_ptr<chordengine::packs::PackStoreTransport> packTransport_;
    std::unique_ptr<chordengine::packs::PackStoreClient> packStore_;
    std::optional<chordengine::packs::Catalog> packCatalog_;
    bool packCatalogLoading_ = false;
    bool packCatalogRequested_ = false;
    juce::String packMessage_;
    juce::String packDownloadingSlug_;
    juce::String packListingKey_;
    void refreshPackCatalog();
    void updatePacksDisplay(bool force = false);
    void packActionClicked(int index);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordEngineAudioProcessorEditor)
};
