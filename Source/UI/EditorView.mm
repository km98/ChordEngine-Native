// =====================================================================
// The ChordEngine editor (AppKit): the 0.5.0 redesign, rebuilt on plain
// AppKit views. One root view lays every control out in the 800 x 650
// design space and scales it with the window (640 x 520 .. 1280 x 1040).
// The processor is the source of truth: the editor shows it and calls it;
// licensing and the trial run in the processor whether or not this view
// exists.
// =====================================================================
#import <AppKit/AppKit.h>

#include "EditorHost.h"
#include "EditorStyle.h"
#include "EditorWidgets.h"
#include "TakeDrag.h"

#include "Core/ChordPresetSystem.h"
#include "Core/ScaleSystem.h"
#include "Licensing/LicensingBoundary.h"
#include "Packs/PackLibrary.h"
#include "Packs/PackStoreClient.h"
#include "Platform/Platform.h"
#include "PluginProcessor.h"
#include "Updates/UpdateService.h"
#include "Updates/UpdateTransport.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>

#define CEEditorRootView CE_OBJC(CEEditorRootView)
#define CEKeyFieldDelegate CE_OBJC(CEKeyFieldDelegate)

namespace chordengine::ui
{
class EditorController;
}

@interface CEEditorRootView : NSView
- (chordengine::ui::EditorController*)controller;
- (void)setController:(std::unique_ptr<chordengine::ui::EditorController>)controller;
- (void)releaseController;
@end

namespace chordengine::ui
{
namespace
{
namespace st = style;
using chordengine::core::ChordPresetSystem;
using chordengine::core::ScaleSystem;

// ------------------------------------------------------------------ helpers
std::string pitchClassName(int pitchClass)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

std::string noteListText(const chordengine::core::NoteSet& notes)
{
    std::string result;
    for (std::size_t index = 0; index < notes.size; ++index)
        result += (index != 0 ? " " : "") + noteName(notes.notes[index]);
    return result;
}

// Display-only mirror of the reference chord-name table: it classifies the
// interval set the Core already resolved.
std::string chordSuffix(const chordengine::core::ResolvedChord& chord)
{
    bool minorThird = false, majorThird = false, diminishedFifth = false, augmentedFifth = false;
    bool sus4 = false, sus2 = false, flatSeventh = false, majorSeventh = false;
    bool ninth = false, eleventh = false, thirteenth = false, sixth = false;
    for (std::size_t index = 0; index < chord.intervalCount; ++index)
    {
        const int interval = chord.intervals[index];
        const int pitchClass = interval % 12;
        minorThird |= pitchClass == 3;
        majorThird |= pitchClass == 4;
        diminishedFifth |= pitchClass == 6;
        augmentedFifth |= pitchClass == 8;
        sus4 |= pitchClass == 5;
        sus2 |= pitchClass == 2;
        flatSeventh |= interval == 10;
        majorSeventh |= interval == 11;
        ninth |= interval == 14;
        eleventh |= interval == 17;
        thirteenth |= interval == 21;
    }
    if (!flatSeventh && !majorSeventh)
        for (std::size_t index = 0; index < chord.intervalCount; ++index)
            sixth |= chord.intervals[index] % 12 == 9;

    if (majorThird)
    {
        if (augmentedFifth)
            return std::string(majorSeventh ? "maj7" : (flatSeventh ? "7" : "")) + "#5";
        if (thirteenth) return majorSeventh ? "maj13" : (flatSeventh ? "13" : "add13");
        if (eleventh) return majorSeventh ? "maj7#11" : (flatSeventh ? "7#11" : "add#11");
        if (ninth) return majorSeventh ? "maj9" : (flatSeventh ? "9" : "add9");
        if (majorSeventh) return "maj7";
        if (flatSeventh) return "7";
        if (sixth) return "6";
        return {};
    }
    if (minorThird)
    {
        if (diminishedFifth) return flatSeventh ? "m7b5" : "dim";
        if (thirteenth) return majorSeventh ? "mMaj13" : (flatSeventh ? "m13" : "madd13");
        if (ninth) return majorSeventh ? "mMaj9" : (flatSeventh ? "m9" : "madd9");
        if (eleventh) return majorSeventh ? "mMaj11" : (flatSeventh ? "m11" : "madd11");
        if (majorSeventh) return "mMaj7";
        if (flatSeventh) return "m7";
        if (sixth) return "m6";
        return "m";
    }
    if (sus4) return majorSeventh ? "maj7sus4" : (flatSeventh ? "7sus4" : "sus4");
    if (sus2) return majorSeventh ? "maj7sus2" : (flatSeventh ? "7sus2" : "sus2");
    if (diminishedFifth) return "dim";
    if (augmentedFifth) return "aug";
    return "5";
}

bool sameConfiguration(const chordengine::core::CoreConfiguration& a, const chordengine::core::CoreConfiguration& b)
{
    return a.keyIndex == b.keyIndex && a.scale == b.scale && a.preset == b.preset && a.velocityMode == b.velocityMode
        && a.fixedVelocity == b.fixedVelocity && a.transpose.target == b.transpose.target
        && a.transpose.octaveSteps == b.transpose.octaveSteps;
}

std::string chordSymbolFor(int triggerNote, const chordengine::core::CoreConfiguration& configuration)
{
    const auto resolved = ChordPresetSystem::resolve(configuration.preset, triggerNote, configuration.scale,
                                                     configuration.keyIndex);
    if (!resolved || resolved->intervalCount == 0)
        return "No Chord";
    return pitchClassName(resolved->rootMidiNote) + chordSuffix(*resolved);
}

chordengine::core::CustomChordPreset toCustomPreset(const chordengine::packs::PackPreset& preset)
{
    chordengine::core::CustomChordPreset custom;
    custom.triggerMap = preset.triggerMap;
    custom.voicingStyle = chordengine::packs::toString(preset.voicing);
    return custom;
}

std::string padTwo(int value)
{
    return value < 10 && value >= 0 ? "0" + std::to_string(value) : std::to_string(value);
}

// Test builds never reach the network: the store answers as if offline.
class OfflinePackStoreTransport final : public chordengine::packs::PackStoreTransport
{
public:
    void get(const std::string&, std::size_t, Callback callback) override { reply(std::move(callback)); }
    void postJson(const std::string&, const std::string&, std::size_t, Callback callback) override
    {
        reply(std::move(callback));
    }

private:
    static void reply(Callback callback)
    {
        platform::callOnMainThread([deliver = std::move(callback)] { deliver({}); });
    }
};

// The store origin: production unless a developer points
// CHORDENGINE_PACK_STORE_ORIGIN at a LOCAL test server.
std::string packStoreOrigin()
{
    const char* value = std::getenv("CHORDENGINE_PACK_STORE_ORIGIN");
    if (value == nullptr)
        return {};
    std::string origin(value);
    const bool local = origin.rfind("http://127.0.0.1", 0) == 0 || origin.rfind("http://localhost", 0) == 0;
    if (!local || !chordengine::packs::isAllowedStoreUrl(origin))
        return {};
    return origin.back() == '/' ? origin : origin + "/";
}

// --------------------------------------------------------------- key field
class LicenseKeyField;
} // namespace
} // namespace chordengine::ui

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
// The lifetime-licence key field: a faint inset with a rule underneath.
class LicenseKeyField final : public Widget
{
public:
    LicenseKeyField() : Widget("license-key-editor")
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
        delegate_.owner = this;
        field_.delegate = delegate_;
        [view_ addSubview:field_];
    }
    ~LicenseKeyField() override { delegate_.owner = nullptr; }

    std::string text() const { return field_.stringValue.UTF8String ?: ""; }
    void setText(const std::string& value) { field_.stringValue = [NSString stringWithUTF8String:value.c_str()]; }
    void clear() { field_.stringValue = @""; }
    void setTooltip(const std::string& tooltip)
    {
        Widget::setTooltip(tooltip);
        field_.toolTip = view_.toolTip;
    }

    void applyFont(float height, float indent)
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
        indent_ = indent;
        layoutField();
    }

    void layoutField()
    {
        const CGRect local = localBounds();
        const CGFloat lineHeight = std::ceil(field_.intrinsicContentSize.height);
        field_.frame = NSMakeRect(indent_, std::round((local.size.height - lineHeight) * 0.5),
                                  std::max<CGFloat>(1, local.size.width - indent_ * 2), lineHeight);
    }

    void paint(CGContextRef context, CGRect area) override
    {
        const bool focused = field_.currentEditor != nil;
        st::fillRoundedRect(context, area, 3.0f, focused ? st::inset.brighter(0.05f) : st::inset);
        const float rule = focused ? 1.5f : 1.0f;
        st::fillRect(context, CGRectMake(0, area.size.height - rule, area.size.width, rule),
                     focused ? st::accent : hover ? st::border.brighter(0.35f) : st::border);
    }

    void mouseDown(CGPoint, NSEvent*) override { [field_.window makeFirstResponder:field_]; }
    NSCursor* cursor() const override { return [NSCursor IBeamCursor]; }

private:
    NSTextField* field_ = nil;
    CEKeyFieldDelegate* delegate_ = nil;
    float indent_ = 10.0f;
};

// The bottom-right resize grip (the host window follows the editor's size).
class ResizeGrip final : public Widget
{
public:
    ResizeGrip() : Widget("resize-grip") {}
    std::function<void(float)> onDrag; // proposed width
    void paint(CGContextRef context, CGRect area) override
    {
        const float s = static_cast<float>(area.size.width) / 14.0f;
        const auto colour = (hover || pressed ? st::muted : st::faint).withAlpha(0.6f);
        for (int line = 0; line < 3; ++line)
        {
            const float offset = (3.0f + 4.0f * line) * s;
            st::strokePolyline(context, { CGPointMake(area.size.width - offset, area.size.height - 1.5f * s),
                                          CGPointMake(area.size.width - 1.5f * s, area.size.height - offset) },
                               1.0f, colour);
        }
    }
    void mouseDown(CGPoint, NSEvent* event) override
    {
        startScreenX_ = NSEvent.mouseLocation.x;
        startWidth_ = static_cast<float>(view_.superview.bounds.size.width);
        (void) event;
    }
    void mouseDrag(CGPoint, NSEvent*) override
    {
        if (onDrag)
            onDrag(startWidth_ + static_cast<float>(NSEvent.mouseLocation.x - startScreenX_));
    }
    NSCursor* cursor() const override { return [NSCursor crosshairCursor]; }

private:
    CGFloat startScreenX_ = 0;
    float startWidth_ = 800.0f;
};

struct PresetEntry
{
    int stockIndex = -1;
    std::string packSlug;
    int presetIndex = -1;
};

struct TextSpec
{
    Label* label = nullptr;
    float size = 12.0f;
    bool bold = false;
    float kerning = 0.0f;
    enum class Role { text, caption, display } role = Role::text;
};

std::vector<std::pair<EditorHost*, __weak NSView*>>& openViews()
{
    static std::vector<std::pair<EditorHost*, __weak NSView*>> views;
    return views;
}
} // namespace

// =====================================================================
class EditorController
{
public:
    enum class Page { chord, packs, info };

    EditorController(EditorHost& host, NSView* root, EditorOptions options);
    ~EditorController();

    void detach();
    bool attached() const noexcept { return host_ != nullptr; }
    void paint(CGContextRef context, CGRect bounds);
    void resized();
    void timerCallback();
    void resizeTo(float proposedWidth);

    Widget* find(const std::string& identifier) const;
    Piano& piano() { return *piano_; }
    std::filesystem::path writeRecorderTakeFile(std::size_t index);
    void setFieldText(const std::string& identifier, const std::string& value);

private:
    template <typename T, typename... Args>
    T* make(Args&&... args)
    {
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = widget.get();
        [root_ addSubview:raw->view()];
        widgets_.push_back(std::move(widget));
        return raw;
    }
    Label* label(const std::string& id, const std::string& text, st::Colour colour, unsigned justification = st::centredLeft,
                 Label::Mode mode = Label::Mode::plain);
    PillButton* pill(const std::string& id, const std::string& text, float radius, float height, float fontHeight);

    CGRect design(int x, int y, int width, int height) const;
    float textScale() const { return std::pow(scale_, 0.85f); }
    void applyTypography();
    void layout();

    ChordEngineAudioProcessor& processor() const { return host_->processor(); }
    void updateStateDisplay();
    void updateVelocityEnabled();
    void updateChordReadout();
    void updateRecorderDisplay();
    void updateNavigation();
    void updateInfoDisplay();
    void updateGateDisplay();
    void updateInternalAudioDisplay();
    void setSelectedPage(Page page);
    void stepPreset(int direction);
    void openStudio();
    bool startRecorderTakeDrag(std::size_t index, NSEvent* event);
    void revealRecorderTake(std::size_t index);
    chordengine::updates::UpdateView updateView() const;

    bool packMemberActive() const;
    void rebuildPresetMenu();
    int currentPresetEntry() const;
    void selectPresetEntry(int entryIndex);
    void refreshPackCatalog();
    void updatePacksDisplay(bool force = false);
    void packActionClicked(int index);

    EditorHost* host_;
    __weak NSView* root_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    platform::RepeatingTimer timer_;
    platform::LifetimeToken lifetime_;

    float scale_ = 1.0f;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;

    Wordmark* wordmark_ {};
    PillButton *navChord_ {}, *navPacks_ {}, *navInfo_ {};
    Label *keyLabel_ {}, *scaleLabel_ {}, *presetLabel_ {}, *presetCounter_ {};
    ComboBox *keySelector_ {}, *scaleSelector_ {}, *presetSelector_ {};
    StepButton *presetPrevious_ {}, *presetNext_ {};
    Label *chordCaption_ {}, *chordName_ {}, *chordNotes_ {};
    Label *transposeLabel_ {}, *octaveValue_ {};
    StepButton *octaveDown_ {}, *octaveUp_ {};
    PillButton *transposeWhole_ {}, *transposeLowest_ {}, *transposeHighest_ {};
    Label *velocityModeLabel_ {}, *fixedVelocityLabel_ {}, *fixedVelocityValue_ {};
    ComboBox* velocityModeSelector_ {};
    Slider* fixedVelocitySlider_ {};
    Piano* piano_ {};
    Label *recorderStatus_ {}, *recorderCount_ {}, *recorderHint_ {};
    ComboBox* recordQuantizeSelector_ {};
    PillButton *recorderAction_ {}, *followHostRecord_ {};
    std::array<TakeRow*, 3> recorderTakes_ {};
    int displayedFollowHostRecord_ = -1;
    Label *internalAudioLabel_ {}, *internalAudioSubLabel_ {};
    StatusPill* internalAudioPill_ {};
    PillButton* internalAudioToggle_ {};
    ComboBox* internalSoundSelector_ {};
    int displayedInternalAudio_ = -1;
    int displayedInternalSound_ = -1;

    Label *infoTitle_ {}, *infoVersion_ {}, *accountHeader_ {}, *infoUser_ {}, *infoPlus_ {}, *infoLicense_ {};
    Label* licenseKeyLabel_ {};
    LicenseKeyField* licenseKeyEditor_ {};
    PillButton* activateLicense_ {};
    Label *updatesHeader_ {}, *infoCurrentVersion_ {}, *infoLatestVersion_ {}, *infoStatus_ {};
    PillButton *checkUpdates_ {}, *openStudio_ {}, *signIn_ {}, *signOut_ {};
    Label *infoAuthHint_ {}, *trialHeader_ {}, *trialStatus_ {}, *helpTitle_ {}, *helpCopy_ {};
    PillButton* helpButton_ {};
    Label* footer_ {};
    Label *gateTitle_ {}, *gateBody_ {};
    PillButton *gateStudio_ {}, *gateSignIn_ {};

    Label *packsTitle_ {}, *packsSubtitle_ {}, *packsStatus_ {};
    PillButton *packsRefresh_ {}, *packsSignIn_ {};
    NSScrollView* packsViewport_ = nil;
    std::unique_ptr<PackList> packList_;
    ResizeGrip* grip_ {};

    std::vector<TextSpec> textSpecs_;
    std::filesystem::path takeExportDir_;
    chordengine::licensing::LicensingBoundary* licensing_ = nullptr;
    std::unique_ptr<chordengine::updates::UpdateService> updates_;
    chordengine::core::CoreConfiguration displayedConfiguration_ {};
    Page page_ = Page::chord;
    bool gateVisible_ = false;

    std::vector<PresetEntry> presetEntries_;
    std::string presetMenuKey_;
    std::string displayedPackKey_;
    bool presetMenuBuilt_ = false;

    std::unique_ptr<chordengine::packs::PackLibrary> packLibrary_;
    std::unique_ptr<chordengine::packs::PackStoreTransport> packTransport_;
    std::unique_ptr<chordengine::packs::PackStoreClient> packStore_;
    std::optional<chordengine::packs::Catalog> packCatalog_;
    bool packCatalogLoading_ = false;
    bool packCatalogRequested_ = false;
    std::string packMessage_;
    std::string packDownloadingSlug_;
    std::string packListingKey_;
};

EditorController::EditorController(EditorHost& host, NSView* root, EditorOptions options)
    : host_(&host), root_(root)
{
    auto& proc = host.processor();
    wordmark_ = make<Wordmark>();

    navChord_ = pill("chord-button", "CHORD", 4.0f, 24.0f, 9.5f);
    navPacks_ = pill("packs-button", "PACKS", 4.0f, 24.0f, 9.5f);
    navInfo_ = pill("info-button", "INFO", 4.0f, 24.0f, 9.5f);
    navChord_->onClick = [this] { setSelectedPage(Page::chord); };
    navPacks_->onClick = [this] { setSelectedPage(Page::packs); };
    navInfo_->onClick = [this] { setSelectedPage(Page::info); };

    keyLabel_ = label("key-label", "KEY", st::dim);
    scaleLabel_ = label("scale-label", "SCALE", st::dim);
    presetLabel_ = label("chord-preset-label", "CHORD PRESET", st::dim);
    keySelector_ = make<ComboBox>("key-selector");
    scaleSelector_ = make<ComboBox>("scale-selector");
    presetSelector_ = make<ComboBox>("chord-selector");
    for (int index = 0; index < static_cast<int>(ScaleSystem::keyCount); ++index)
        keySelector_->addItem(std::string(ScaleSystem::keyName(index)), index + 1);
    for (int index = 0; index < static_cast<int>(ScaleSystem::scaleCount); ++index)
        if (const auto* scale = ScaleSystem::findByIndex(index))
            scaleSelector_->addItem(std::string(scale->name), index + 1);
    presetSelector_->setEmphasis(true);

    presetPrevious_ = make<StepButton>("preset-previous", StepButton::Glyph::previous);
    presetNext_ = make<StepButton>("preset-next", StepButton::Glyph::next);
    presetPrevious_->setTooltip("Previous chord preset");
    presetNext_->setTooltip("Next chord preset");
    presetPrevious_->onClick = [this] { stepPreset(-1); };
    presetNext_->onClick = [this] { stepPreset(1); };
    presetCounter_ = label("preset-counter", "", st::dim, st::centredRight);

    chordCaption_ = label("chord-caption", "", st::dim);
    chordName_ = label("chord-name", "", st::text);
    chordNotes_ = label("chord-notes", "", st::text, st::centredLeft, Label::Mode::noteTokens);

    transposeLabel_ = label("transpose-label", "TRANSPOSE", st::dim);
    octaveValue_ = label("transpose-octave", "0 OCT", st::text, st::centred);
    octaveDown_ = make<StepButton>("octave-down", StepButton::Glyph::minus);
    octaveUp_ = make<StepButton>("octave-up", StepButton::Glyph::plus);
    octaveDown_->setTooltip("Transpose the generated chord down one octave");
    octaveUp_->setTooltip("Transpose the generated chord up one octave");
    octaveDown_->onClick = [this]
    {
        processor().setTransposeOctave(std::max(-2, processor().configurationSnapshot().transpose.octaveSteps - 1));
        updateStateDisplay();
    };
    octaveUp_->onClick = [this]
    {
        processor().setTransposeOctave(std::min(2, processor().configurationSnapshot().transpose.octaveSteps + 1));
        updateStateDisplay();
    };
    transposeWhole_ = pill("transpose-whole", "WHOLE", 4.0f, 28.0f, 9.0f);
    transposeLowest_ = pill("transpose-lowest", "LOWEST", 4.0f, 28.0f, 9.0f);
    transposeHighest_ = pill("transpose-highest", "HIGHEST", 4.0f, 28.0f, 9.0f);
    const std::array<std::pair<PillButton*, chordengine::core::TransposeTarget>, 3> targets { {
        { transposeWhole_, chordengine::core::TransposeTarget::whole },
        { transposeLowest_, chordengine::core::TransposeTarget::lowest },
        { transposeHighest_, chordengine::core::TransposeTarget::highest } } };
    for (const auto& [button, value] : targets)
        button->onClick = [this, value = value]
        {
            processor().setTransposeTarget(value);
            updateStateDisplay();
        };

    velocityModeLabel_ = label("velocity-mode-label", "VELOCITY MODE", st::dim);
    velocityModeSelector_ = make<ComboBox>("velocity-mode-selector");
    velocityModeSelector_->addItem("Dynamic", 1);
    velocityModeSelector_->addItem("Maximum", 2);
    velocityModeSelector_->addItem("Fixed", 3);
    fixedVelocityLabel_ = label("fixed-velocity-label", "FIXED VELOCITY", st::dim);
    fixedVelocitySlider_ = make<Slider>("fixed-velocity-slider", 1.0, 127.0);
    fixedVelocityValue_ = label("fixed-velocity-value", "100", st::text, st::centredRight);

    piano_ = make<Piano>(proc);

    recorderStatus_ = label("recorder-status", "MIDI RECORDER", st::dim);
    recorderCount_ = label("recorder-count", "WAITING FOR ARM", st::dim);
    recorderHint_ = label("recorder-hint", "Arm, then play. Drag each finished take into your DAW.", st::muted.darker(0.15f),
                          st::topLeft);
    recorderHint_->setMinimumHorizontalScale(1.0f);
    recordQuantizeSelector_ = make<ComboBox>("record-quantize-selector");
    recordQuantizeSelector_->addSectionHeading("QUANTIZE TAKES");
    {
        const std::array<const char*, chordengine::midi::takeQuantizeCount> names {
            "No quantize", "1/6 step", "1/4 step", "1/3 step", "1/2 step", "Step",
            "1/6 beat", "1/4 beat", "1/3 beat", "1/2 beat", "Beat", "Bar" };
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            recordQuantizeSelector_->addItem(names[index], static_cast<int>(index) + 1);
            if (index == 0 || index == 5 || index == 10)
                recordQuantizeSelector_->addSeparator();
        }
    }
    recordQuantizeSelector_->setTooltip("Quantize dragged-out takes to the host grid. A step is a 1/16 note; "
                                        "note lengths are kept.");
    recordQuantizeSelector_->onChange = [this]
    {
        const int id = recordQuantizeSelector_->selectedId();
        if (id >= 1 && id <= chordengine::midi::takeQuantizeCount)
            processor().setRecordQuantize(static_cast<ChordEngineAudioProcessor::RecordQuantize>(id - 1));
    };

    recorderAction_ = pill("recorder-action", "RECORD ARM", 4.0f, 28.0f, 9.5f);
    recorderAction_->setTooltip(host.hostName() == "Logic"
        ? "Arm a take by hand, then play. With FOLLOW DAW RECORD on, pressing Record in Logic Pro "
          "arms it for you. To record the chords straight into the region instead, open this MIDI FX "
          "slot's menu and choose \"Record MIDI to Track Here\"."
        : "Arm a take by hand, then play. With FOLLOW DAW RECORD on, pressing Record in your DAW "
          "arms it for you. Drag finished takes into your arrangement.");
    recorderAction_->setPalette(st::accent, st::onAccent);
    recorderAction_->onClick = [this]
    {
        switch (processor().recorderState())
        {
            case 0: processor().armRecorder(); break;
            case 1: processor().cancelRecorder(); break;
            case 2: processor().stopRecorder(); break;
            default: break;
        }
        updateRecorderDisplay();
    };
    followHostRecord_ = pill("follow-host-record", "FOLLOW DAW RECORD: OFF", 4.0f, 20.0f, 8.5f);
    followHostRecord_->setClickingTogglesState(true);
    followHostRecord_->setTooltip("When on, ChordEngine starts a take when you press Record in your DAW "
                                  "and stores it when the DAW stops. A take armed by hand is left to you.");
    followHostRecord_->onClick = [this]
    {
        processor().setFollowHostRecord(followHostRecord_->toggleState());
        updateRecorderDisplay();
    };
    for (std::size_t index = 0; index < recorderTakes_.size(); ++index)
    {
        auto* row = make<TakeRow>("recorder-take-" + std::to_string(index + 1));
        row->setTooltip("Drag this completed MIDI take into your DAW (right-click: Show in Finder).");
        row->setEnabled(false);
        row->setVisible(false);
        row->onDragStart = [this, index](NSEvent* event) { return startRecorderTakeDrag(index, event); };
        row->onReveal = [this, index] { revealRecorderTake(index); };
        recorderTakes_[index] = row;
    }

    internalAudioLabel_ = label("internal-audio-label", "INTERNAL AUDIO", st::dim);
    internalAudioSubLabel_ = label("internal-audio-sub-label", "MIDI FX - load ChordEngine Instrument for sound",
                                   st::faint.brighter(0.3f));
    internalAudioSubLabel_->setMinimumHorizontalScale(1.0f);
    internalAudioPill_ = make<StatusPill>("internal-audio-status");
    internalAudioPill_->setTooltip("This is the MIDI FX version of ChordEngine: Logic Pro's MIDI FX slot "
                                   "carries no audio buses, so it drives the track's instrument. To hear "
                                   "the built-in synth, piano or bass, insert \"ChordEngine Instrument\" "
                                   "as the track's instrument instead.");
    internalAudioToggle_ = pill("internal-audio-toggle", "OFF", 4.0f, 28.0f, 9.0f);
    internalAudioToggle_->setClickingTogglesState(true);
    internalAudioToggle_->setTooltip("Play the generated chords through ChordEngine's built-in sound, "
                                     "so no other instrument is needed. The MIDI output is unchanged.");
    internalAudioToggle_->onClick = [this]
    {
        processor().setInternalAudioEnabled(internalAudioToggle_->toggleState());
        updateInternalAudioDisplay();
    };
    internalSoundSelector_ = make<ComboBox>("internal-sound-selector");
    internalSoundSelector_->addItem("Synth", 1);
    internalSoundSelector_->addItem("Piano", 2);
    internalSoundSelector_->addItem("Bass", 3);
    internalSoundSelector_->setTooltip("Built-in sound: Synth, Piano or Bass (Bass plays the lowest chord note)");
    internalSoundSelector_->onChange = [this]
    {
        const int id = internalSoundSelector_->selectedId();
        if (id >= 1 && id <= 3)
            processor().setInternalSound(static_cast<ChordEngineAudioProcessor::InternalSound>(id - 1));
        updateInternalAudioDisplay();
    };

    const std::string version = chordengine::updates::currentBuildVersionString;
    infoTitle_ = label("info-title", "Chord Engine", st::textBright);
    infoVersion_ = label("info-version", "Version v" + version, st::muted);
    accountHeader_ = label("info-account-header", "ACCOUNT", st::dim);
    infoUser_ = label("info-user", "User  Not signed in", st::text, st::centredLeft, Label::Mode::infoRow);
    infoPlus_ = label("info-plus", "Music-Prod+  NOT ACTIVE", st::accent, st::centredLeft, Label::Mode::infoRow);
    infoLicense_ = label("info-license", "License  Not verified", st::text, st::centredLeft, Label::Mode::infoRow);
    licenseKeyLabel_ = label("license-key-label", "LIFETIME LICENCE KEY", st::dim);
    licenseKeyEditor_ = make<LicenseKeyField>();
    licenseKeyEditor_->setTooltip("Enter the ChordEngine licence key from your purchase email. "
                                  "It is activated on this computer and needs no Music-Prod+ "
                                  "subscription. The key is never stored on this computer.");
    activateLicense_ = pill("license-activate", "ACTIVATE", 4.0f, 30.0f, 9.5f);
    activateLicense_->setPalette(st::transparent, st::accent, st::accent.withAlpha(0.8f), st::accentSoft);
    activateLicense_->setTooltip("Activate this ChordEngine licence key on this computer.");
    activateLicense_->onClick = [this]
    {
        if (licensing_ == nullptr)
            return;
        // The key is handed over and cleared from the field: the server binds
        // it to the account; nothing useful or safe remains in keeping it.
        licensing_->activateLicenseKey(licenseKeyEditor_->text());
        licenseKeyEditor_->clear();
        updateInfoDisplay();
    };
    updatesHeader_ = label("info-updates-header", "UPDATES", st::dim);
    infoCurrentVersion_ = label("info-current-version", "Current version  " + version, st::text, st::centredLeft,
                                Label::Mode::infoRow);
    infoLatestVersion_ = label("info-latest-version", "Latest version  -", st::text, st::centredLeft, Label::Mode::infoRow);
    infoStatus_ = label("info-update-status", "Status  -", st::muted, st::centredLeft, Label::Mode::infoRow);
    trialHeader_ = label("info-trial-header", "TRIAL", st::dim);
    trialStatus_ = label("info-trial-status", "Trial access  -", st::text, st::centredLeft, Label::Mode::infoRow);
    helpTitle_ = label("info-help-title", "HELP & FEEDBACK", st::dim);
    helpCopy_ = label("info-help-copy", "Need help or want to report a problem with ChordEngine?", st::muted);

    checkUpdates_ = pill("check-updates", "CHECK FOR UPDATES", 4.0f, 28.0f, 9.5f);
    checkUpdates_->setPalette(st::transparent, st::text, st::border, st::raised);
    checkUpdates_->setTooltip("Ask the Music-Prod release service whether a newer ChordEngine "
                              "build has been published. This check is anonymous and needs no sign-in.");
    checkUpdates_->onClick = [this]
    {
        if (updates_ != nullptr)
            updates_->checkNow();
        updateInfoDisplay();
    };
    openStudio_ = pill("open-music-prod-studio", "OPEN MUSIC-PROD STUDIO", 4.0f, 28.0f, 9.5f);
    openStudio_->setPalette(st::transparent, st::text, st::border, st::raised);
    openStudio_->setTooltip("Open Music-Prod Studio through its registered musicprodstudio:// URL scheme.");
    openStudio_->onClick = [this] { openStudio(); };
    signIn_ = pill("sign-in", "SIGN IN", 4.0f, 28.0f, 9.5f);
    signIn_->setPalette(st::accent, st::onAccent, st::transparent, st::accent.darker(0.2f));
    signIn_->setTooltip("Sign in to Music-Prod with the existing device-link flow.");
    signIn_->onClick = [this]
    {
        if (licensing_ != nullptr)
            licensing_->signInButtonClicked();
        updateInfoDisplay();
    };
    signOut_ = pill("sign-out", "SIGN OUT", 4.0f, 28.0f, 9.5f);
    signOut_->setPalette(st::transparent, st::text, st::border, st::raised);
    signOut_->setTooltip("Sign out of Music-Prod and revoke this plugin token.");
    signOut_->onClick = [this]
    {
        if (licensing_ != nullptr)
            licensing_->signOutButtonClicked();
        updateInfoDisplay();
    };
    infoAuthHint_ = label("info-auth-hint", "", st::muted, st::topLeft);

    gateTitle_ = label("trial-gate-title", chordengine::licensing::expiryModalTitle, st::textBright, st::centred);
    gateBody_ = label("trial-gate-body", "", st::muted, st::centredTop);
    gateStudio_ = pill("trial-gate-studio", "OPEN MUSIC-PROD STUDIO", 4.0f, 30.0f, 9.5f);
    gateStudio_->setPalette(st::accent, st::onAccent, st::transparent, st::accent.darker(0.2f));
    gateStudio_->onClick = [this] { openStudio(); };
    gateSignIn_ = pill("trial-gate-sign-in", "SIGN IN", 4.0f, 30.0f, 9.5f);
    gateSignIn_->setPalette(st::transparent, st::text, st::border, st::raised);
    gateSignIn_->onClick = [this]
    {
        if (licensing_ != nullptr)
            licensing_->signInButtonClicked();
        updateInfoDisplay();
    };

    helpButton_ = pill("help-feedback", "OPEN HELP & FEEDBACK", 4.0f, 28.0f, 9.5f);
    helpButton_->setPalette(st::transparent, st::text, st::border, st::raised);
    helpButton_->setTooltip("Open the Music-Prod ChordEngine Help & Feedback page in your browser.");
    helpButton_->onClick = [] { platform::openUrl(ChordEngineAudioProcessor::helpFeedbackUrl); };

    footer_ = label("footer", "Chord Engine v" + version, st::faint, st::centredRight);

    using Role = TextSpec::Role;
    const float caption = st::captionSize;
    const float kerning = st::captionKerning;
    textSpecs_ = {
        { keyLabel_, caption, true, kerning, Role::caption }, { scaleLabel_, caption, true, kerning, Role::caption },
        { presetLabel_, caption, true, kerning, Role::caption }, { presetCounter_, caption, true, kerning, Role::caption },
        { chordCaption_, caption, true, kerning, Role::caption }, { chordName_, 54.0f, false, -0.01f, Role::display },
        { chordNotes_, 11.5f, false }, { transposeLabel_, caption, true, kerning, Role::caption },
        { octaveValue_, 12.0f, true, 0.04f }, { velocityModeLabel_, caption, true, kerning, Role::caption },
        { fixedVelocityLabel_, caption, true, kerning, Role::caption }, { fixedVelocityValue_, 12.0f, true },
        { recorderStatus_, caption, true, kerning, Role::caption }, { recorderCount_, caption, true, kerning, Role::caption },
        { recorderHint_, 10.0f, false, 0.01f }, { internalAudioLabel_, caption, true, kerning, Role::caption },
        { internalAudioSubLabel_, 9.0f, false }, { infoTitle_, 26.0f, false, 0.0f, Role::display },
        { infoVersion_, 11.5f, false }, { accountHeader_, caption, true, kerning, Role::caption },
        { infoUser_, 12.0f, false }, { infoPlus_, 12.0f, true }, { infoLicense_, 12.0f, false },
        { licenseKeyLabel_, caption, true, kerning, Role::caption }, { updatesHeader_, caption, true, kerning, Role::caption },
        { infoCurrentVersion_, 12.0f, false }, { infoLatestVersion_, 12.0f, false }, { infoStatus_, 11.5f, false },
        { infoAuthHint_, 11.0f, false }, { trialHeader_, caption, true, kerning, Role::caption },
        { trialStatus_, 12.0f, false }, { helpTitle_, caption, true, kerning, Role::caption },
        { helpCopy_, 11.5f, false }, { footer_, 9.0f, false, 0.04f }, { gateTitle_, 13.0f, true, 0.3f },
        { gateBody_, 12.0f, false },
    };

    keySelector_->onChange = [this]
    {
        processor().setKeyIndex(keySelector_->selectedId() - 1);
        updateStateDisplay();
    };
    scaleSelector_->onChange = [this]
    {
        processor().setScale(static_cast<chordengine::core::ScaleId>(scaleSelector_->selectedId() - 1));
        updateStateDisplay();
    };
    presetSelector_->onChange = [this] { selectPresetEntry(presetSelector_->selectedId() - 1); };
    velocityModeSelector_->onChange = [this]
    {
        processor().setVelocityMode(static_cast<chordengine::core::VelocityMode>(velocityModeSelector_->selectedId() - 1));
        updateVelocityEnabled();
        updateStateDisplay();
    };
    fixedVelocitySlider_->onValueChange = [this]
    {
        const int value = static_cast<int>(fixedVelocitySlider_->value());
        processor().setFixedVelocity(value);
        fixedVelocityValue_->setText(std::to_string(value));
    };

    // ---- storage, licensing, packs, updates ---------------------------------
    const auto storage = options.storageDirectory.empty() ? ChordEngineAudioProcessor::defaultLicensingStorageDir()
                                                          : options.storageDirectory;
    takeExportDir_ = storage / "Takes";
    licensing_ = &proc.licensing().boundary();
    proc.licensing().editorOpened();
    licensing_->setInfoPageVisible(false);

    packLibrary_ = std::make_unique<chordengine::packs::PackLibrary>(storage / "Packs");
    packLibrary_->scan();
    packTransport_ = std::move(options.packTransport);
    if (packTransport_ == nullptr)
    {
#if defined(CHORDENGINE_UPDATE_OFFLINE_TRANSPORT) && CHORDENGINE_UPDATE_OFFLINE_TRANSPORT
        packTransport_ = std::make_unique<OfflinePackStoreTransport>();
#else
        packTransport_ = std::make_unique<chordengine::packs::HttpPackStoreTransport>();
#endif
    }
    packStore_ = std::make_unique<chordengine::packs::PackStoreClient>(*packTransport_, *packLibrary_, packStoreOrigin());

    packsTitle_ = label("packs-title", "Preset Packs", st::textBright);
    packsSubtitle_ = label("packs-subtitle", "", st::muted);
    packsStatus_ = label("packs-status", "", st::muted, st::centredRight);
    packsRefresh_ = pill("packs-refresh", "REFRESH", 4.0f, 28.0f, 9.5f);
    packsRefresh_->setPalette(st::transparent, st::text, st::border, st::raised);
    packsRefresh_->setTooltip("Reload the Music-Prod pack catalog");
    packsRefresh_->onClick = [this] { refreshPackCatalog(); };
    packsSignIn_ = pill("packs-sign-in", "SIGN IN", 4.0f, 28.0f, 9.5f);
    packsSignIn_->setPalette(st::accent, st::onAccent);
    packsSignIn_->setTooltip("Sign in with your Music-Prod account to download packs");
    packsSignIn_->onClick = [this]
    {
        if (licensing_ != nullptr)
            licensing_->signInButtonClicked();
        updatePacksDisplay(true);
    };
    textSpecs_.push_back({ packsTitle_, 26.0f, false, 0.0f, Role::display });
    textSpecs_.push_back({ packsSubtitle_, 11.5f, false });
    textSpecs_.push_back({ packsStatus_, 11.0f, false });

    packList_ = std::make_unique<PackList>();
    packList_->onAction = [this](int index) { packActionClicked(index); };
    packsViewport_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    packsViewport_.identifier = @"packs-viewport";
    packsViewport_.drawsBackground = NO;
    packsViewport_.hasVerticalScroller = YES;
    packsViewport_.autohidesScrollers = YES;
    packsViewport_.scrollerStyle = NSScrollerStyleOverlay;
    packsViewport_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    packsViewport_.documentView = packList_->view();
    [root_ addSubview:packsViewport_];

    grip_ = make<ResizeGrip>();
    grip_->onDrag = [this](float width) { resizeTo(width); };

    updates_ = std::make_unique<chordengine::updates::UpdateService>(
        chordengine::updates::currentBuildVersionString, storage / chordengine::updates::UpdateCache::fileName,
        options.updateTransport != nullptr ? std::move(options.updateTransport)
                                           : chordengine::updates::makeDefaultUpdateTransport());
    const auto weak = lifetime_.weak();
    updates_->onViewChanged = [this, weak]
    {
        if (platform::LifetimeToken::alive(weak) && attached())
            updateInfoDisplay();
    };

    resized();
    updateStateDisplay();
    updateVelocityEnabled();
    rebuildPresetMenu();
    setSelectedPage(Page::chord);
    updatePacksDisplay(true);
    gateBody_->setText(chordengine::licensing::expiryModalBody(licensing_->snapshot()));
    updates_->ensureStartupCheck();
    updateInfoDisplay();
    updateGateDisplay();

    timer_.start(50, [this, weak]
    {
        if (platform::LifetimeToken::alive(weak))
            timerCallback();
    });
}

EditorController::~EditorController()
{
    detach();
}

void EditorController::detach()
{
    // Everything that reaches the processor or the host stops here: the view
    // may outlive the plug-in instance (a host disposing the AU with its
    // window open).
    timer_.stop();
    if (updates_ != nullptr)
        updates_->onViewChanged = nullptr;
    if (host_ != nullptr && piano_ != nullptr)
        piano_->releaseHeldNote();
    if (piano_ != nullptr)
        piano_->detach();
    // No control may reach the processor or the host any more.
    for (auto& widget : widgets_)
    {
        if (auto* button = dynamic_cast<PillButton*>(widget.get()))
            button->onClick = nullptr;
        else if (auto* step = dynamic_cast<StepButton*>(widget.get()))
            step->onClick = nullptr;
        else if (auto* combo = dynamic_cast<ComboBox*>(widget.get()))
            combo->onChange = nullptr;
        else if (auto* slider = dynamic_cast<Slider*>(widget.get()))
            slider->onValueChange = nullptr;
        else if (auto* row = dynamic_cast<TakeRow*>(widget.get()))
        {
            row->onDragStart = nullptr;
            row->onReveal = nullptr;
        }
        else if (auto* grip = dynamic_cast<ResizeGrip*>(widget.get()))
            grip->onDrag = nullptr;
    }
    if (packList_ != nullptr)
        packList_->onAction = nullptr;
    host_ = nullptr;
    licensing_ = nullptr;
}

Label* EditorController::label(const std::string& id, const std::string& text, st::Colour colour,
                               unsigned justification, Label::Mode mode)
{
    auto* result = make<Label>(id, mode);
    result->setText(text);
    result->setColour(colour);
    result->setJustification(justification);
    return result;
}

PillButton* EditorController::pill(const std::string& id, const std::string& text, float radius, float height,
                                   float fontHeight)
{
    return make<PillButton>(id, text, radius, height, fontHeight);
}

Widget* EditorController::find(const std::string& identifier) const
{
    for (const auto& widget : widgets_)
        if (widget->identifier() == identifier)
            return widget.get();
    if (packList_ != nullptr && identifier == packList_->identifier())
        return packList_.get();
    return nullptr;
}

CGRect EditorController::design(int x, int y, int width, int height) const
{
    const auto mapX = [this](int value) { return offsetX_ + std::round(static_cast<float>(value) * scale_); };
    const auto mapY = [this](int value) { return offsetY_ + std::round(static_cast<float>(value) * scale_); };
    const auto mapSize = [this](int value) { return std::max(1.0f, std::round(static_cast<float>(value) * scale_)); };
    return CGRectMake(mapX(x), mapY(y), mapSize(width), mapSize(height));
}

void EditorController::applyTypography()
{
    const float displayScale = std::pow(scale_, 1.08f);
    for (const auto& spec : textSpecs_)
    {
        if (spec.label == nullptr)
            continue;
        if (spec.role == TextSpec::Role::display)
            spec.label->setFont(st::displayFont(std::max(8.0f, spec.size * displayScale), spec.kerning));
        else if (spec.role == TextSpec::Role::caption)
            spec.label->setFont(st::captionFont(std::max(6.0f, spec.size * textScale()), spec.kerning));
        else
            spec.label->setFont(st::uiFont(std::max(6.0f, spec.size * textScale()), spec.bold, spec.kerning));
    }
    licenseKeyEditor_->applyFont(std::max(8.0f, 12.0f * textScale()), std::round(10.0f * scale_));
    const float comboFont = std::max(8.0f, 12.0f * textScale());
    for (auto* combo : { keySelector_, scaleSelector_, presetSelector_, velocityModeSelector_, recordQuantizeSelector_,
                         internalSoundSelector_ })
        combo->setFontHeight(comboFont);
}

void EditorController::resized()
{
    NSView* root = root_;
    if (root == nil)
        return;
    const float width = std::max(1.0f, static_cast<float>(root.bounds.size.width));
    const float height = std::max(1.0f, static_cast<float>(root.bounds.size.height));
    scale_ = std::max(0.1f, std::min(width / editorDesignWidth, height / editorDesignHeight));
    offsetX_ = std::round((width - editorDesignWidth * scale_) * 0.5f);
    offsetY_ = std::round((height - editorDesignHeight * scale_) * 0.5f);
    applyTypography();
    layout();
    [root setNeedsDisplay:YES];
}

void EditorController::layout()
{
    wordmark_->setBounds(design(40, 16, 240, 40));
    navChord_->setBounds(design(588, 24, 56, 24));
    navPacks_->setBounds(design(648, 24, 56, 24));
    navInfo_->setBounds(design(708, 24, 52, 24));

    packsTitle_->setBounds(design(40, 80, 440, 36));
    packsSubtitle_->setBounds(design(40, 118, 520, 18));
    packsRefresh_->setBounds(design(660, 84, 100, 28));
    packsSignIn_->setBounds(design(552, 84, 100, 28));
    packsStatus_->setBounds(design(400, 118, 360, 18));
    packsViewport_.frame = NSRectFromCGRect(design(40, 158, 720, 420));
    packList_->setScale(scale_);

    chordCaption_->setBounds(design(40, 86, 440, 12));
    chordName_->setBounds(design(40, 100, 516, 64));
    chordNotes_->setBounds(design(40, 172, 512, 26));

    recorderStatus_->setBounds(design(588, 84, 84, 12));
    recordQuantizeSelector_->setBounds(design(676, 78, 84, 22));
    recorderCount_->setBounds(design(588, 100, 172, 12));
    recorderTakes_[0]->setBounds(design(588, 118, 172, 19));
    recorderTakes_[1]->setBounds(design(588, 138, 172, 19));
    recorderTakes_[2]->setBounds(design(588, 158, 172, 19));
    recorderHint_->setBounds(design(588, 120, 172, 48));
    recorderAction_->setBounds(design(590, 180, 168, 28));
    followHostRecord_->setBounds(design(590, 212, 168, 20));

    presetLabel_->setBounds(design(40, 236, 160, 12));
    presetCounter_->setBounds(design(296, 236, 80, 12));
    presetSelector_->setBounds(design(40, 252, 264, 32));
    presetPrevious_->setBounds(design(316, 254, 28, 28));
    presetNext_->setBounds(design(348, 254, 28, 28));
    keyLabel_->setBounds(design(424, 236, 96, 12));
    keySelector_->setBounds(design(424, 252, 96, 32));
    scaleLabel_->setBounds(design(552, 236, 208, 12));
    scaleSelector_->setBounds(design(552, 252, 208, 32));

    piano_->setBounds(design(40, 302, 720, 180));

    transposeLabel_->setBounds(design(40, 508, 160, 12));
    octaveDown_->setBounds(design(40, 526, 28, 28));
    octaveValue_->setBounds(design(72, 526, 52, 28));
    octaveUp_->setBounds(design(128, 526, 28, 28));
    transposeWhole_->setBounds(design(170, 526, 58, 28));
    transposeLowest_->setBounds(design(230, 526, 58, 28));
    transposeHighest_->setBounds(design(290, 526, 60, 28));
    velocityModeLabel_->setBounds(design(376, 508, 104, 12));
    velocityModeSelector_->setBounds(design(376, 524, 100, 30));
    fixedVelocityLabel_->setBounds(design(492, 508, 124, 12));
    fixedVelocitySlider_->setBounds(design(488, 526, 96, 28));
    fixedVelocityValue_->setBounds(design(586, 526, 30, 28));
    internalAudioLabel_->setBounds(design(644, 508, 116, 12));
    internalAudioPill_->setBounds(design(644, 526, 40, 28));
    internalAudioSubLabel_->setBounds(design(692, 524, 68, 32));
    internalAudioToggle_->setBounds(design(644, 526, 40, 28));
    internalSoundSelector_->setBounds(design(692, 524, 68, 32));

    infoTitle_->setBounds(design(40, 78, 360, 40));
    infoVersion_->setBounds(design(40, 118, 360, 16));
    accountHeader_->setBounds(design(40, 164, 136, 12));
    infoUser_->setBounds(design(192, 158, 420, 20));
    infoPlus_->setBounds(design(192, 180, 420, 20));
    infoLicense_->setBounds(design(192, 202, 568, 32));
    signIn_->setBounds(design(192, 242, 104, 28));
    signOut_->setBounds(design(192, 242, 104, 28));
    openStudio_->setBounds(design(304, 242, 196, 28));
    licenseKeyLabel_->setBounds(design(40, 302, 136, 26));
    licenseKeyEditor_->setBounds(design(192, 298, 300, 30));
    licenseKeyEditor_->layoutField();
    activateLicense_->setBounds(design(504, 298, 112, 30));
    infoAuthHint_->setBounds(design(192, 336, 568, 44));
    updatesHeader_->setBounds(design(40, 408, 136, 12));
    infoCurrentVersion_->setBounds(design(192, 402, 340, 20));
    infoLatestVersion_->setBounds(design(192, 424, 340, 20));
    infoStatus_->setBounds(design(192, 446, 380, 30));
    checkUpdates_->setBounds(design(600, 402, 160, 28));
    trialHeader_->setBounds(design(40, 488, 136, 12));
    trialStatus_->setBounds(design(192, 482, 568, 30));
    helpTitle_->setBounds(design(40, 547, 136, 12));
    helpCopy_->setBounds(design(192, 543, 380, 20));
    helpButton_->setBounds(design(580, 539, 180, 28));
    footer_->setBounds(design(600, 604, 160, 16));

    gateTitle_->setBounds(design(200, 248, 400, 24));
    gateBody_->setBounds(design(200, 296, 400, 60));
    gateStudio_->setBounds(design(290, 372, 220, 30));
    gateSignIn_->setBounds(design(290, 410, 220, 30));

    NSView* root = root_;
    const float gripSize = std::round(14.0f * scale_);
    grip_->setBounds(CGRectMake(root.bounds.size.width - gripSize, root.bounds.size.height - gripSize, gripSize, gripSize));
    updatePacksDisplay(true);
}

void EditorController::paint(CGContextRef context, CGRect bounds)
{
    // One open graphite page; the near-black ground shows only outside the
    // design canvas. Structure comes from type, alignment and hairlines.
    st::fillRect(context, bounds, st::background);
    const CGRect canvas = design(0, 0, editorDesignWidth, editorDesignHeight);
    st::fillRect(context, canvas, st::page);
    const auto hairline = [&](CGRect rect)
    {
        if (rect.size.width >= rect.size.height)
            rect.size.height = 1;
        else
            rect.size.width = 1;
        st::fillRect(context, rect, st::hairline);
    };
    hairline(design(40, 60, 720, 1));
    const auto* activeNav = page_ == Page::info ? navInfo_ : page_ == Page::packs ? navPacks_ : navChord_;
    const CGRect nav = activeNav->bounds();
    st::fillRect(context, CGRectMake(nav.origin.x + 10.0f * scale_, CGRectGetMaxY(nav) + 1.0f * scale_,
                                     nav.size.width - 20.0f * scale_, std::max(1.0f, 1.5f * scale_)),
                 st::accent);

    if (!gateVisible_)
    {
        if (page_ == Page::info)
            for (const int y : { 146, 284, 390, 524 })
                hairline(design(40, y, 720, 1));
        else if (page_ == Page::packs)
            hairline(design(40, 146, 720, 1));
        else
        {
            hairline(design(568, 86, 1, 120));
            hairline(design(40, 494, 720, 1));
            hairline(design(360, 512, 1, 40));
            hairline(design(628, 512, 1, 40));
            const auto target = displayedConfiguration_.transpose.target;
            const auto* active = target == chordengine::core::TransposeTarget::lowest ? transposeLowest_
                : target == chordengine::core::TransposeTarget::highest ? transposeHighest_ : transposeWhole_;
            const CGRect underline = active->bounds();
            st::fillRect(context, CGRectMake(underline.origin.x + 8.0f * scale_, CGRectGetMaxY(underline) - 3.0f * scale_,
                                             underline.size.width - 16.0f * scale_, std::max(1.0f, 1.5f * scale_)),
                         st::accent);
            st::fillRoundedRect(context, octaveValue_->bounds(), 4.0f * scale_, st::inset);
        }
    }

    if (!gateVisible_)
        if (CGImageRef mark = st::musicProdMark(st::muted))
        {
            // The Music-Prod mark at its own aspect ratio, quiet ivory, no slab.
            const CGRect slot = design(40, 600, 72, 24);
            const float aspect = static_cast<float>(CGImageGetWidth(mark)) / static_cast<float>(CGImageGetHeight(mark));
            float w = static_cast<float>(slot.size.width);
            float h = w / aspect;
            if (h > slot.size.height)
            {
                h = static_cast<float>(slot.size.height);
                w = h * aspect;
            }
            const CGRect target = CGRectMake(slot.origin.x, CGRectGetMidY(slot) - h * 0.5f, w, h);
            CGContextSaveGState(context);
            CGContextSetAlpha(context, 0.7);
            CGContextTranslateCTM(context, target.origin.x, CGRectGetMaxY(target));
            CGContextScaleCTM(context, 1.0, -1.0);
            CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
            CGContextDrawImage(context, CGRectMake(0, 0, target.size.width, target.size.height), mark);
            CGContextRestoreGState(context);
        }

    if (gateVisible_)
    {
        CGRect cover = canvas;
        const float headerHeight = static_cast<float>(design(0, 0, 1, 64).size.height);
        cover.origin.y += headerHeight;
        cover.size.height -= headerHeight;
        st::fillRect(context, cover, st::page);
        CGRect rule = design(380, 282, 40, 2);
        rule.size.height = std::max(1.0f, 1.5f * scale_);
        st::fillRect(context, rule, st::terracotta);
    }
}

void EditorController::resizeTo(float proposedWidth)
{
    int width = 0, height = 0;
    editorSizeForWidth(static_cast<int>(std::lround(proposedWidth)), width, height);
    if (host_ != nullptr && host_->requestEditorSize(width, height))
        return;
    NSView* root = root_;
    [root setFrameSize:NSMakeSize(width, height)];
}

// ---------------------------------------------------------------- display
void EditorController::updateStateDisplay()
{
    if (!attached())
        return;
    const auto configuration = processor().configurationSnapshot();
    displayedConfiguration_ = configuration;
    keySelector_->setSelectedId(configuration.keyIndex + 1);
    scaleSelector_->setSelectedId(static_cast<int>(configuration.scale) + 1);
    const auto packPreset = processor().packPreset();
    displayedPackKey_ = packPreset.has_value() ? packPreset->packSlug + "/" + packPreset->presetName : std::string();
    const int entry = currentPresetEntry();
    if (entry >= 0)
        presetSelector_->setSelectedId(entry + 1);
    else if (packPreset.has_value())
        presetSelector_->setText(packPreset->presetName);
    velocityModeSelector_->setSelectedId(static_cast<int>(configuration.velocityMode) + 1);

    const auto target = configuration.transpose.target;
    transposeWhole_->setPalette(st::transparent, target == chordengine::core::TransposeTarget::whole ? st::textBright : st::dim);
    transposeLowest_->setPalette(st::transparent, target == chordengine::core::TransposeTarget::lowest ? st::textBright : st::dim);
    transposeHighest_->setPalette(st::transparent, target == chordengine::core::TransposeTarget::highest ? st::textBright : st::dim);

    const int octave = configuration.transpose.octaveSteps;
    octaveValue_->setText((octave > 0 ? "+" + std::to_string(octave) : std::to_string(octave)) + " OCT");
    fixedVelocitySlider_->setValue(configuration.fixedVelocity);
    fixedVelocityValue_->setText(std::to_string(configuration.fixedVelocity));
    presetCounter_->setText(padTwo(entry + 1) + " / " + padTwo(static_cast<int>(presetEntries_.size())));
    piano_->setPreviewNotes(processor().previewChordForTrigger(60));
    updateVelocityEnabled();
    updateChordReadout();
    NSView* root = root_;
    [root setNeedsDisplay:YES];
}

void EditorController::stepPreset(int direction)
{
    const int count = static_cast<int>(presetEntries_.size());
    if (count <= 0)
        return;
    const int current = std::max(0, currentPresetEntry());
    selectPresetEntry(((current + direction) % count + count) % count);
}

void EditorController::updateVelocityEnabled()
{
    if (!attached())
        return;
    const bool fixed = processor().configurationSnapshot().velocityMode == chordengine::core::VelocityMode::fixed;
    fixedVelocitySlider_->setEnabled(fixed);
    fixedVelocityValue_->setEnabled(fixed);
    fixedVelocitySlider_->setAlpha(fixed ? 1.0f : 0.35f);
    fixedVelocityValue_->setAlpha(fixed ? 1.0f : 0.45f);
    fixedVelocitySlider_->setTooltip(fixed ? "Drag or click to set the fixed output velocity (1-127)."
                                           : "Inactive: choose Velocity Mode = Fixed to enable the fixed velocity control.");
}

void EditorController::updateChordReadout()
{
    if (!attached())
        return;
    auto& proc = processor();
    const auto configuration = proc.configurationSnapshot();
    if (ScaleSystem::find(configuration.scale) == nullptr
        || ChordPresetSystem::findByIndex(static_cast<int>(configuration.preset)) == nullptr)
        return;
    int trigger = -1;
    for (int note = ChordPresetSystem::triggerLow; note <= ChordPresetSystem::triggerHigh; ++note)
        if (proc.isAnyTriggerNoteDown(static_cast<std::uint8_t>(note)) || proc.isAnyTriggerNoteActive(static_cast<std::uint8_t>(note)))
        {
            trigger = note;
            break;
        }
    if (trigger >= 0)
    {
        std::string notes;
        for (int note = ChordEngineAudioProcessor::pianoFirstNote; note <= ChordEngineAudioProcessor::pianoLastNote; ++note)
            if (proc.isAnyOutputNoteActive(static_cast<std::uint8_t>(note)))
                notes += (notes.empty() ? "" : " ") + noteName(note);
        if (notes.empty())
            notes = noteListText(proc.previewChordForTrigger(trigger));
        chordCaption_->setText("PLAYING - TRIGGER " + noteName(trigger));
        chordCaption_->setColour(st::accent);
        chordName_->setText(chordSymbolFor(trigger, configuration));
        chordName_->setColour(st::accent);
        chordNotes_->setText(notes);
        chordNotes_->setLive(true);
    }
    else
    {
        chordName_->setText(chordSymbolFor(60, configuration));
        chordName_->setColour(st::textBright);
        chordCaption_->setText("PREVIEW - TRIGGER C4");
        chordCaption_->setColour(st::dim);
        chordNotes_->setText(noteListText(proc.previewChordForTrigger(60)));
        chordNotes_->setLive(false);
    }
}

void EditorController::updateRecorderDisplay()
{
    if (!attached())
        return;
    auto& proc = processor();
    const auto state = proc.recorderState();
    const auto count = proc.recorderTakeCount();
    std::string status;
    st::Colour statusColour = st::dim;
    if (state == 1)
    {
        status = "ARMED - PLAY TO RECORD";
        statusColour = st::accent;
    }
    else if (state == 2)
    {
        status = "RECORDING";
        statusColour = st::terracotta;
    }
    else if (count == 0)
        status = "WAITING FOR ARM";
    else
    {
        status = std::to_string(static_cast<int>(count)) + (count == 1 ? " TAKE STORED" : " TAKES STORED");
        statusColour = st::sage;
    }
    if (proc.isRecorderOverflowed())
    {
        status = "BUFFER LIMIT REACHED";
        statusColour = st::terracotta;
    }
    recorderCount_->setText(status);
    recorderCount_->setColour(statusColour);
    const int quantizeId = static_cast<int>(proc.recordQuantize()) + 1;
    recordQuantizeSelector_->setSelectedId(quantizeId);
    const bool follow = proc.followHostRecord();
    if (displayedFollowHostRecord_ != (follow ? 1 : 0))
    {
        displayedFollowHostRecord_ = follow ? 1 : 0;
        followHostRecord_->setToggleState(follow);
        followHostRecord_->setText(follow ? "FOLLOW DAW RECORD: ON" : "FOLLOW DAW RECORD: OFF");
        if (follow)
            followHostRecord_->setPalette(st::accentSoft, st::accent, st::accent, st::accentSoft.darker(0.1f));
        else
            followHostRecord_->setPalette(st::transparent, st::dim, st::border, st::raised);
        recorderHint_->setText(follow ? "Arm, or press Record in your DAW. Play, then drag each take out."
                                      : "Arm, then play. Drag each finished take into your DAW.");
    }
    if (state == 1)
    {
        recorderAction_->setText("CANCEL");
        recorderAction_->setPalette(st::transparent, st::text, st::accent, st::accentSoft);
        recorderAction_->setIndicator(st::accent);
    }
    else if (state == 2)
    {
        recorderAction_->setText("STOP TAKE");
        recorderAction_->setPalette(st::terracotta, st::textBright, st::transparent, st::terracotta.darker(0.2f));
        recorderAction_->setIndicator(st::textBright);
    }
    else
    {
        recorderAction_->setText("RECORD ARM");
        recorderAction_->setPalette(st::accent, st::onAccent, st::transparent, st::accent.darker(0.2f));
        recorderAction_->setIndicator(st::terracotta);
    }
    const bool chordPage = page_ == Page::chord && !gateVisible_;
    const int displayed = std::min<int>(count, static_cast<int>(recorderTakes_.size()));
    recorderHint_->setVisible(chordPage && displayed == 0);
    for (std::size_t index = 0; index < recorderTakes_.size(); ++index)
    {
        auto* row = recorderTakes_[index];
        const bool available = static_cast<int>(index) < displayed;
        row->setVisible(chordPage && available);
        row->setEnabled(available);
        if (available)
            row->setTake(static_cast<int>(index + 1), proc.recorderTakeNoteCount(static_cast<std::uint8_t>(index)));
    }
}

void EditorController::updateNavigation()
{
    navChord_->setPalette(st::transparent, page_ == Page::chord ? st::textBright : st::dim);
    navPacks_->setPalette(st::transparent, page_ == Page::packs ? st::textBright : st::dim);
    navInfo_->setPalette(st::transparent, page_ == Page::info ? st::textBright : st::dim);
}

chordengine::updates::UpdateView EditorController::updateView() const
{
    if (updates_ != nullptr)
        return updates_->view();
    chordengine::updates::UpdateView idle;
    idle.currentVersion = chordengine::updates::currentBuildVersionString;
    return idle;
}

void EditorController::updateInfoDisplay()
{
    if (licensing_ == nullptr)
        return;
    const auto& snapshot = licensing_->snapshot();
    infoVersion_->setText("Version v" + std::string(chordengine::updates::currentBuildVersionString));
    const auto update = updateView();
    infoCurrentVersion_->setText(chordengine::updates::currentVersionLine(update));
    infoUser_->setText(chordengine::licensing::userLineText(snapshot));
    infoPlus_->setText(chordengine::licensing::plusLineText(snapshot));
    infoPlus_->setColour(st::displayTone(chordengine::licensing::plusLineColour(snapshot)));
    infoLicense_->setText("License  " + chordengine::licensing::licenseLineText(snapshot));
    activateLicense_->setText(chordengine::licensing::activationButtonText(snapshot));
    activateLicense_->setEnabled(!snapshot.activationPending);
    infoLatestVersion_->setText(chordengine::updates::latestVersionLine(update));
    infoStatus_->setText(chordengine::updates::statusLine(update));
    infoStatus_->setColour(st::displayTone(chordengine::updates::statusColour(update)));
    trialStatus_->setText(chordengine::licensing::trialStatusText(snapshot));
    trialStatus_->setColour(st::displayTone(chordengine::licensing::trialStatusColour(snapshot)));
    const bool signedIn = chordengine::licensing::isSignedIn(snapshot);
    signIn_->setVisible(page_ == Page::info && !signedIn && !gateVisible_);
    signOut_->setVisible(page_ == Page::info && signedIn && !gateVisible_);
    signIn_->setText(chordengine::licensing::signButtonText(snapshot));
    infoAuthHint_->setText(snapshot.message);
    std::string verificationUrl;
    if (licensing_->consumeVerificationUrl(verificationUrl))
        platform::openUrl(verificationUrl);
}

void EditorController::updateGateDisplay()
{
    if (licensing_ == nullptr)
        return;
    const bool active = licensing_->expiredGateActive();
    if (active == gateVisible_)
        return;
    gateVisible_ = active;
    gateBody_->setText(chordengine::licensing::expiryModalBody(licensing_->snapshot()));
    setSelectedPage(gateVisible_ ? Page::chord : page_);
}

void EditorController::updateInternalAudioDisplay()
{
    if (!attached())
        return;
    const int enabled = processor().internalAudioEnabled() ? 1 : 0;
    const int sound = static_cast<int>(processor().internalSound());
    if (enabled == displayedInternalAudio_ && sound == displayedInternalSound_)
        return;
    displayedInternalAudio_ = enabled;
    displayedInternalSound_ = sound;
    internalAudioToggle_->setToggleState(enabled != 0);
    internalAudioToggle_->setText(enabled != 0 ? "ON" : "OFF");
    if (enabled != 0)
        internalAudioToggle_->setPalette(st::accent, st::background, st::accent, st::accent.darker(0.15f));
    else
        internalAudioToggle_->setPalette(st::transparent, st::dim, st::border, st::raised);
    internalSoundSelector_->setSelectedId(sound + 1);
}

void EditorController::setSelectedPage(Page selected)
{
    if (!attached())
        return;
    page_ = selected;
    const bool showChord = selected == Page::chord;
    const bool gate = gateVisible_;
    for (Widget* widget : std::initializer_list<Widget*> {
             keyLabel_, keySelector_, scaleLabel_, scaleSelector_, presetLabel_, presetSelector_, presetPrevious_,
             presetNext_, presetCounter_, chordCaption_, chordName_, chordNotes_, transposeLabel_, octaveValue_,
             octaveDown_, octaveUp_, transposeWhole_, transposeLowest_, transposeHighest_, velocityModeLabel_,
             velocityModeSelector_, fixedVelocityLabel_, fixedVelocitySlider_, fixedVelocityValue_, piano_,
             recorderStatus_, recorderCount_, recorderAction_, recorderTakes_[0], recorderTakes_[1], recorderTakes_[2],
             recordQuantizeSelector_, followHostRecord_ })
        widget->setVisible(showChord && !gate);
    if (!showChord || gate)
        piano_->releaseHeldNote();

    const bool audioBuild = processor().hasAudioOutput();
    internalAudioLabel_->setVisible(showChord && !gate);
    internalAudioSubLabel_->setVisible(showChord && !gate && !audioBuild);
    internalAudioPill_->setVisible(showChord && !gate && !audioBuild);
    internalAudioToggle_->setVisible(showChord && !gate && audioBuild);
    internalSoundSelector_->setVisible(showChord && !gate && audioBuild);

    const bool info = selected == Page::info && !gate;
    for (Widget* widget : std::initializer_list<Widget*> {
             infoTitle_, infoVersion_, accountHeader_, infoUser_, infoPlus_, infoLicense_, licenseKeyLabel_,
             licenseKeyEditor_, activateLicense_, updatesHeader_, infoCurrentVersion_, infoLatestVersion_, infoStatus_,
             checkUpdates_, openStudio_, signIn_, signOut_, infoAuthHint_, trialHeader_, trialStatus_, helpTitle_,
             helpCopy_, helpButton_ })
        widget->setVisible(info);

    const bool packs = selected == Page::packs && !gate;
    for (Widget* widget : std::initializer_list<Widget*> { packsTitle_, packsSubtitle_, packsStatus_, packsRefresh_ })
        widget->setVisible(packs);
    packsViewport_.hidden = !packs;
    if (packs && !packCatalogRequested_)
        refreshPackCatalog();
    updatePacksDisplay(true);

    for (Widget* widget : std::initializer_list<Widget*> { gateTitle_, gateBody_, gateStudio_, gateSignIn_ })
        widget->setVisible(gate);

    updateNavigation();
    updateInfoDisplay();
    updateRecorderDisplay();
    NSView* root = root_;
    [root setNeedsDisplay:YES];
}

void EditorController::openStudio()
{
    platform::openUrl(chordengine::licensing::studioUrl);
}

void EditorController::timerCallback()
{
    if (!attached())
        return;
    updateInfoDisplay();
    updateGateDisplay();
    const auto configuration = processor().configurationSnapshot();
    if (!sameConfiguration(displayedConfiguration_, configuration))
        updateStateDisplay();
    updateRecorderDisplay();
    updateChordReadout();
    updateInternalAudioDisplay();
    rebuildPresetMenu();
    const auto packPreset = processor().packPreset();
    const auto packKey = packPreset.has_value() ? packPreset->packSlug + "/" + packPreset->presetName : std::string();
    if (packKey != displayedPackKey_)
        updateStateDisplay();
    if (page_ == Page::packs)
        updatePacksDisplay();
    piano_->repaint();
}

// --------------------------------------------------------------- takes
std::filesystem::path EditorController::writeRecorderTakeFile(std::size_t index)
{
    std::error_code error;
    if (!attached() || index >= recorderTakes_.size())
        return {};
    std::filesystem::create_directories(takeExportDir_, error);
    if (!std::filesystem::is_directory(takeExportDir_, error))
        return {};
    // Takes older than a week are pruned here, the only place that adds them.
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const auto& entry : std::filesystem::directory_iterator(takeExportDir_, error))
        if (entry.path().extension() == ".mid" && now - entry.last_write_time(error) > std::chrono::hours(24 * 7))
            std::filesystem::remove(entry.path(), error);

    // A plain, unique name: hosts name the imported region after the file.
    const std::time_t clock = std::time(nullptr);
    std::tm local {};
    localtime_r(&clock, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), " %Y%m%d-%H%M%S", &local);
    const auto base = "ChordEngine Take " + std::to_string(index + 1) + stamp;
    auto destination = takeExportDir_ / (base + ".mid");
    for (int suffix = 2; std::filesystem::exists(destination, error); ++suffix)
        destination = takeExportDir_ / (base + " (" + std::to_string(suffix) + ").mid");
    if (!processor().exportRecorderTake(static_cast<std::uint8_t>(index), destination))
    {
        std::filesystem::remove(destination, error);
        return {};
    }
    return destination;
}

bool EditorController::startRecorderTakeDrag(std::size_t index, NSEvent* event)
{
    // The file outlives the drag: Logic Pro and FL Studio read a dropped file
    // after the drag session has ended.
    const auto destination = writeRecorderTakeFile(index);
    if (destination.empty())
        return false;
    // Each drag's outcome is appended to Takes/drag-log.txt (0 = refused).
    const auto log = takeExportDir_ / "drag-log.txt";
    const auto hostName = host_->hostName().empty() ? std::string(NSProcessInfo.processInfo.processName.UTF8String)
                                                    : host_->hostName();
    const auto name = destination.filename().string();
    auto* row = recorderTakes_[index];
    const auto weak = lifetime_.weak();
    return startFileDrag(row->view(), event, destination, [log, hostName, name, row, weak](int operation)
    {
        if (platform::LifetimeToken::alive(weak))
            row->dragEnded();
        std::error_code error;
        if (std::filesystem::file_size(log, error) > 64 * 1024)
            std::filesystem::remove(log, error);
        std::ofstream out(log, std::ios::app);
        const std::time_t clock = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&clock));
        out << stamp << "  host=" << hostName << "  operation=" << operation
            << (operation == 0 ? " (refused or cancelled)" : "") << "  " << name << "\n";
    });
}

void EditorController::revealRecorderTake(std::size_t index)
{
    // Fallback for a host that will not accept a drop from a plug-in window.
    const auto file = writeRecorderTakeFile(index);
    if (!file.empty())
        platform::revealInFileManager(file);
}

// --------------------------------------------------------------- packs
bool EditorController::packMemberActive() const
{
    if (licensing_ == nullptr)
        return false;
    const auto& snapshot = licensing_->snapshot();
    return chordengine::licensing::isSignedIn(snapshot) && snapshot.cached.subscribed;
}

void EditorController::rebuildPresetMenu()
{
    if (packLibrary_ == nullptr || !attached())
        return;
    const auto usable = packLibrary_->usablePacks(packMemberActive());
    std::string key;
    for (const auto* pack : usable)
        if (!pack->isStock)
            key += pack->slug + '@' + pack->version + ';';
    if (presetMenuBuilt_ && key == presetMenuKey_)
        return;
    presetMenuBuilt_ = true;
    presetMenuKey_ = key;
    presetEntries_.clear();
    presetSelector_->clear();
    for (int index = 0; index < static_cast<int>(ChordPresetSystem::count()); ++index)
        if (const auto* preset = ChordPresetSystem::findByIndex(index))
        {
            presetEntries_.push_back({ index, {}, -1 });
            presetSelector_->addItem(std::string(preset->name), static_cast<int>(presetEntries_.size()));
        }
    for (const auto* pack : usable)
    {
        if (pack->isStock)
            continue;
        presetSelector_->addSectionHeading(pack->name);
        for (int index = 0; index < static_cast<int>(pack->presets.size()); ++index)
        {
            presetEntries_.push_back({ -1, pack->slug, index });
            presetSelector_->addItem(pack->presets[static_cast<std::size_t>(index)].name,
                                     static_cast<int>(presetEntries_.size()));
        }
    }
    updateStateDisplay();
}

int EditorController::currentPresetEntry() const
{
    if (!attached())
        return -1;
    const auto packPreset = processor().packPreset();
    const int stock = static_cast<int>(processor().configurationSnapshot().preset);
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

void EditorController::selectPresetEntry(int entryIndex)
{
    if (!attached() || entryIndex < 0 || entryIndex >= static_cast<int>(presetEntries_.size()))
        return;
    const auto& entry = presetEntries_[static_cast<std::size_t>(entryIndex)];
    if (entry.stockIndex >= 0)
    {
        processor().clearPackPreset();
        processor().setChordPreset(static_cast<chordengine::core::ChordPresetId>(entry.stockIndex));
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
        processor().setPackPreset(selection);
    }
    updateStateDisplay();
}

void EditorController::refreshPackCatalog()
{
    if (packStore_ == nullptr || packCatalogLoading_)
        return;
    packCatalogRequested_ = true;
    packCatalogLoading_ = true;
    packMessage_ = "Loading the pack catalog...";
    packLibrary_->scan();
    updatePacksDisplay(true);
    const auto weak = lifetime_.weak();
    packStore_->fetchCatalog([this, weak](chordengine::packs::PackStoreClient::CatalogResult result)
    {
        if (!platform::LifetimeToken::alive(weak))
            return;
        packCatalogLoading_ = false;
        if (result.ok)
        {
            packCatalog_ = std::move(result.catalog);
            packMessage_.clear();
        }
        else
            packMessage_ = "Store unavailable - showing installed packs";
        updatePacksDisplay(true);
    });
}

void EditorController::updatePacksDisplay(bool force)
{
    if (packLibrary_ == nullptr || licensing_ == nullptr || packList_ == nullptr)
        return;
    const auto& snapshot = licensing_->snapshot();
    chordengine::packs::StoreAccess access;
    access.signedIn = chordengine::licensing::isSignedIn(snapshot);
    access.subscribed = snapshot.cached.subscribed;
    auto listings = chordengine::packs::mergeListings(packCatalog_.has_value() ? &*packCatalog_ : nullptr, *packLibrary_,
                                                      access);
    std::string key = std::string(access.signedIn ? "1" : "0") + (access.subscribed ? "1" : "0") + packMessage_
        + packDownloadingSlug_ + std::to_string(static_cast<int>(snapshot.phase));
    for (const auto& listing : listings)
        key += listing.slug + std::to_string(static_cast<int>(listing.status)) + listing.statusText;
    key += std::to_string(scale_);
    if (!force && key == packListingKey_)
        return;
    packListingKey_ = key;

    if (!access.signedIn)
        packsSubtitle_->setText(snapshot.phase == chordengine::licensing::Phase::linking
                                    ? "Finish signing in in your browser."
                                    : "Sign in with Music-Prod+ to download every pack.");
    else if (!access.subscribed)
        packsSubtitle_->setText("Packs are included with an active Music-Prod+ membership.");
    else
        packsSubtitle_->setText("Your Music-Prod+ membership includes every pack. "
                                "Installed presets appear in the CHORD PRESET menu.");
    packsStatus_->setText(packMessage_);
    packsSignIn_->setVisible(page_ == Page::packs && !gateVisible_ && !access.signedIn);
    packsSignIn_->setText(chordengine::licensing::signButtonText(snapshot));

    packList_->setListings(std::move(listings), packDownloadingSlug_);
    const CGFloat width = packsViewport_.contentSize.width;
    packList_->setBounds(CGRectMake(0, 0, std::max<CGFloat>(1, width),
                                    std::max(1, packList_->rowHeight() * static_cast<int>(packList_->listings().size()))));
    rebuildPresetMenu();
}

void EditorController::packActionClicked(int index)
{
    const auto& listings = packList_->listings();
    if (index < 0 || index >= static_cast<int>(listings.size()) || !packDownloadingSlug_.empty() || licensing_ == nullptr)
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
            platform::openUrl("https://music-prod.com/plus");
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
    const auto weak = lifetime_.weak();
    packStore_->download(licensing_->storeToken(), listing.slug,
                         [this, weak](chordengine::packs::PackStoreClient::DownloadResult result)
    {
        if (!platform::LifetimeToken::alive(weak))
            return;
        packDownloadingSlug_.clear();
        packMessage_ = result.message;
        if (result.outcome == chordengine::packs::PackStoreClient::DownloadOutcome::membershipRequired
            && !result.unlockUrl.empty())
            packMessage_ = "Music-Prod+ required - see music-prod.com";
        packLibrary_->scan();
        updatePacksDisplay(true);
    });
}

void EditorController::setFieldText(const std::string& identifier, const std::string& value)
{
    if (identifier == licenseKeyEditor_->identifier())
        licenseKeyEditor_->setText(value);
}
} // namespace chordengine::ui

// =====================================================================
@implementation CEEditorRootView
{
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

- (void)setController:(std::unique_ptr<chordengine::ui::EditorController>)controller
{
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
        controller_->paint(NSGraphicsContext.currentContext.CGContext, self.bounds);
}

- (void)dealloc
{
    controller_.reset();
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

void editorSizeForWidth(int width, int& outWidth, int& outHeight)
{
    outWidth = std::clamp(width, editorMinimumWidth, editorMaximumWidth);
    outHeight = static_cast<int>(std::lround(outWidth * static_cast<double>(editorDesignHeight) / editorDesignWidth));
}

NSView* createEditorViewWithOptions(EditorHost& host, int width, int height, EditorOptions options)
{
    CEEditorRootView* root = [[CEEditorRootView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    root.identifier = @"chordengine-editor";
    root.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    [root setController:std::make_unique<EditorController>(host, root, std::move(options))];
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
    return widgetOf(editor, identifier) != nullptr;
}

bool clickEditorControl(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    if (widget == nullptr || !widget->isVisible() || !widget->isEnabled())
        return false;
    widget->activate();
    return true;
}

bool selectEditorItem(NSView* editor, const std::string& identifier, int itemId)
{
    auto* combo = dynamic_cast<ComboBox*>(widgetOf(editor, identifier));
    return combo != nullptr && combo->isVisible() && combo->isEnabled() && combo->selectItem(itemId);
}

std::vector<int> editorItemIds(NSView* editor, const std::string& identifier)
{
    auto* combo = dynamic_cast<ComboBox*>(widgetOf(editor, identifier));
    return combo != nullptr ? combo->itemIds() : std::vector<int> {};
}

int editorSelectedItem(NSView* editor, const std::string& identifier)
{
    auto* combo = dynamic_cast<ComboBox*>(widgetOf(editor, identifier));
    return combo != nullptr ? combo->selectedId() : -1;
}

std::string editorControlText(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    if (auto* label = dynamic_cast<Label*>(widget))
        return label->text();
    if (auto* button = dynamic_cast<PillButton*>(widget))
        return button->text();
    if (auto* combo = dynamic_cast<ComboBox*>(widget))
        return combo->displayedText();
    if (auto* field = dynamic_cast<LicenseKeyField*>(widget))
        return field->text();
    return {};
}

bool isEditorControlVisible(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    if (widget == nullptr)
        return false;
    for (NSView* view = widget->view(); view != nil && view != editor; view = view.superview)
        if (view.hidden)
            return false;
    return true;
}

bool isEditorControlEnabled(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    return widget != nullptr && widget->isEnabled();
}

CGRect editorControlBounds(NSView* editor, const std::string& identifier)
{
    Widget* widget = widgetOf(editor, identifier);
    if (widget == nullptr)
        return CGRectNull;
    return NSRectToCGRect([widget->view() convertRect:widget->view().bounds toView:editor]);
}

bool setEditorSliderValue(NSView* editor, const std::string& identifier, double value)
{
    auto* slider = dynamic_cast<Slider*>(widgetOf(editor, identifier));
    if (slider == nullptr || !slider->isEnabled() || !slider->isVisible())
        return false;
    slider->setValueFromUser(value);
    return true;
}

bool setEditorFieldText(NSView* editor, const std::string& identifier, const std::string& text)
{
    auto* controller = controllerOf(editor);
    if (controller == nullptr || dynamic_cast<LicenseKeyField*>(controller->find(identifier)) == nullptr)
        return false;
    controller->setFieldText(identifier, text);
    return true;
}

void refreshEditor(NSView* editor)
{
    if (auto* controller = controllerOf(editor))
        controller->timerCallback();
}

int pressEditorPiano(NSView* editor, CGPoint point)
{
    auto* controller = controllerOf(editor);
    if (controller == nullptr || !controller->piano().isVisible())
        return -1;
    NSView* pianoView = controller->piano().view();
    const NSPoint local = [pianoView convertPoint:NSPointFromCGPoint(point) fromView:editor];
    const int note = controller->piano().noteAtPosition(NSPointToCGPoint(local));
    if (note >= 0)
        controller->piano().pressNote(note);
    return note;
}

void releaseEditorPiano(NSView* editor)
{
    if (auto* controller = controllerOf(editor))
        controller->piano().releaseHeldNote();
}

CGPoint editorPianoKeyCentre(NSView* editor, int note)
{
    auto* controller = controllerOf(editor);
    if (controller == nullptr)
        return CGPointZero;
    NSView* pianoView = controller->piano().view();
    // Scan the lower half (white keys) and the upper part (black keys).
    const NSRect bounds = pianoView.bounds;
    for (CGFloat y : { bounds.size.height * 0.85, bounds.size.height * 0.3 })
        for (CGFloat x = 0.5; x < bounds.size.width; x += 1.0)
            if (controller->piano().noteAtPosition(CGPointMake(x, y)) == note)
            {
                CGFloat end = x;
                while (end + 1.0 < bounds.size.width && controller->piano().noteAtPosition(CGPointMake(end + 1.0, y)) == note)
                    end += 1.0;
                return NSPointToCGPoint([pianoView convertPoint:NSMakePoint((x + end) * 0.5, y) toView:editor]);
            }
    return CGPointZero;
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
