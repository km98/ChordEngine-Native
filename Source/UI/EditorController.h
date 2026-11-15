#pragma once

// =====================================================================
// The ChordEngine editor's platform-neutral core: every control, the layout in
// the 800 x 650 design space, the page / gate / recorder / packs / account
// logic and the programmatic access the tests use. The processor is the
// source of truth: the editor shows it and calls it; licensing and the trial
// run in the processor whether or not an editor exists.
//
// The window system is reached only through EditorRoot (and the widget, text
// field and scroll area peers it creates):
//   * macOS: EditorView.mm (AppKit views, Core Graphics);
//   * Windows: EditorWin.cpp (a child HWND, Direct2D / DirectWrite).
// =====================================================================

#include "EditorHost.h"
#include "EditorStyle.h"
#include "EditorWidgets.h"

#include "Core/ChordEngineCore.h"
#include "Licensing/LicensingBoundary.h"
#include "Packs/PackLibrary.h"
#include "Packs/PackStoreClient.h"
#include "Platform/Platform.h"
#include "Updates/UpdateService.h"

#include <array>
#include <filesystem>
#include <optional>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chordengine::ui
{
class LicenseKeyField;
class ResizeGrip;

// A native single-line text field inside a widget (the licence key).
class TextFieldPeer
{
public:
    virtual ~TextFieldPeer() = default;
    virtual std::string text() const = 0;
    virtual void setText(const std::string& text) = 0;
    virtual void setTooltip(const std::string& tooltip) = 0;
    // The field's type: the monospaced face at `height` (ascent + descent).
    virtual void applyFont(float height) = 0;
    // Positions the field inside its owner (owner-local `area`, `indent` on
    // each side) and follows the owner's visibility.
    virtual void layout(Rect area, float indent, bool visible) = 0;
    virtual bool hasFocus() const = 0;
    virtual void focus() = 0;
};

// A vertically scrolling viewport around one widget (the pack list).
class ScrollAreaPeer
{
public:
    virtual ~ScrollAreaPeer() = default;
    virtual void setFrame(Rect frame) = 0;
    virtual Rect frame() const = 0;
    virtual void setVisible(bool visible) = 0;
    virtual bool isVisible() const = 0;
    // The width the content can use (the viewport minus any scroller).
    virtual float contentWidth() const = 0;
};

// The editor's window-system root.
class EditorRoot
{
public:
    virtual ~EditorRoot() = default;
    // Gives `widget` its peer and shows it above everything added before.
    virtual void addWidget(Widget& widget) = 0;
    virtual std::unique_ptr<ScrollAreaPeer> createScrollArea(Widget& content) = 0;
    virtual std::unique_ptr<TextFieldPeer> createTextField(Widget& owner) = 0;
    // The editor's size in points.
    virtual Size size() const = 0;
    virtual void setSize(int width, int height) = 0;
    virtual void repaint() = 0;
    // Starts an OS drag of `file` from `source`; `onEnded` receives the
    // operation performed (0 = refused or cancelled) on the main thread.
    virtual bool startFileDrag(Widget& source, const MouseEvent& event, const std::filesystem::path& file,
                               std::function<void(int operation)> onEnded) = 0;
    // The host process's name (drag log).
    virtual std::string processName() const = 0;
};

class EditorController
{
public:
    enum class Page { chord, packs, info };

    EditorController(EditorHost& host, EditorRoot& root, EditorOptions options);
    ~EditorController();
    EditorController(const EditorController&) = delete;
    EditorController& operator=(const EditorController&) = delete;

    // Nothing reaches the processor or the host after this.
    void detach();
    bool attached() const noexcept { return host_ != nullptr; }
    EditorHost* host() const noexcept { return host_; }
    // Draws everything that is not a widget (ground, page, hairlines, mark).
    void paint(st::Canvas& canvas, Rect bounds);
    void resized();
    void timerCallback();
    void resizeTo(float proposedWidth);
    Page page() const noexcept { return page_; }

    Widget* find(const std::string& identifier) const;
    // Every widget in paint order (the pack list excluded: it lives in the
    // scroll area).
    const std::vector<std::unique_ptr<Widget>>& widgets() const noexcept { return widgets_; }
    PackList* packList() const noexcept { return packList_.get(); }
    Piano& piano() { return *piano_; }
    std::filesystem::path writeRecorderTakeFile(std::size_t index);

    // ---- programmatic access (tests, snapshot tool) -----------------------
    // Each call does what the user's click or choice would do, through the
    // same code path. Coordinates are editor points.
    bool hasControl(const std::string& identifier) const;
    bool clickControl(const std::string& identifier);
    bool selectItem(const std::string& identifier, int itemId);
    std::vector<int> itemIds(const std::string& identifier) const;
    int selectedItem(const std::string& identifier) const;
    std::string controlText(const std::string& identifier) const;
    bool isControlVisible(const std::string& identifier) const;
    bool isControlEnabled(const std::string& identifier) const;
    Rect controlBounds(const std::string& identifier) const;
    bool setSliderValue(const std::string& identifier, double value);
    bool setFieldText(const std::string& identifier, const std::string& text);
    int pressPiano(Point editorPoint);
    void releasePiano();
    Point pianoKeyCentre(int note) const;

private:
    template <typename T, typename... Args>
    T* make(Args&&... args)
    {
        auto widget = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = widget.get();
        root_->addWidget(*raw);
        widgets_.push_back(std::move(widget));
        return raw;
    }
    Label* label(const std::string& id, const std::string& text, st::Colour colour, unsigned justification = st::centredLeft,
                 Label::Mode mode = Label::Mode::plain);
    PillButton* pill(const std::string& id, const std::string& text, float radius, float height, float fontHeight);

    Rect design(int x, int y, int width, int height) const;
    float textScale() const;
    void applyTypography();
    void layout();

    ChordEngineAudioProcessor& processor() const;
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
    bool startRecorderTakeDrag(std::size_t index, const MouseEvent& event);
    void revealRecorderTake(std::size_t index);
    chordengine::updates::UpdateView updateView() const;

    bool packMemberActive() const;
    void rebuildPresetMenu();
    int currentPresetEntry() const;
    void selectPresetEntry(int entryIndex);
    void refreshPackCatalog();
    void updatePacksDisplay(bool force = false);
    void packActionClicked(int index);

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

    EditorHost* host_;
    EditorRoot* root_;
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
    std::unique_ptr<ScrollAreaPeer> packsViewport_;
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
} // namespace chordengine::ui
