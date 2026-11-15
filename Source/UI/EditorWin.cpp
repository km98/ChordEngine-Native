// =====================================================================
// The ChordEngine editor's Windows back end (see EditorWin.h): a child HWND
// that paints the platform-neutral controller and widgets with Direct2D,
// routes mouse input to them, shows their tooltips, and hosts the native
// EDIT control of the licence key.
// =====================================================================
#include "EditorWin.h"

#include "EditorController.h"
#include "EditorStyleWin.h"

#include <commctrl.h>
#include <d2d1helper.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef WM_DPICHANGED_AFTERPARENT
 #define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

namespace chordengine::ui
{
namespace
{
namespace st = style;
using st::ComPtr;

COLORREF gdiColour(st::Colour colour)
{
    return RGB(colour.r(), colour.g(), colour.b());
}

const wchar_t* windowClassName()
{
    // Unique per module: two ChordEngine builds can share one host process.
    static const std::wstring name = L"ChordEngineEditor_"
        + std::to_wstring(reinterpret_cast<std::uintptr_t>(st::thisModule()));
    return name.c_str();
}

bool classRegistered = false;

std::vector<WinEditor*>& openEditors()
{
    static std::vector<WinEditor*> editors;
    return editors;
}
} // namespace

float windowScaleFactor(void* window)
{
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    using GetDpiForSystemFn = UINT(WINAPI*)();
    static const auto forWindow = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    static const auto forSystem = reinterpret_cast<GetDpiForSystemFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem")));
    UINT dpi = 0;
    if (window != nullptr && forWindow != nullptr)
        dpi = forWindow(static_cast<HWND>(window));
    if (dpi == 0 && forSystem != nullptr)
        dpi = forSystem();
    if (dpi == 0)
    {
        HDC screen = GetDC(nullptr);
        dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

// =====================================================================
class WinEditor final : public EditorRoot
{
public:
    WinEditor(EditorHost& host, HWND parent, int pixelWidth, int pixelHeight, float scale, EditorOptions options);
    ~WinEditor() override;

    HWND window() const noexcept { return window_; }
    EditorController* controller() const noexcept { return controller_.get(); }
    EditorHost* host() const noexcept { return host_; }
    float scale() const noexcept { return scale_; }
    void setScale(float scale);
    void setPixelSize(int width, int height);
    void detach();
    void setScaleListener(std::function<void(float)> listener) { scaleListener_ = std::move(listener); }
    void paintTree(st::Canvas& canvas, Size size);
    void invalidate();

    // ---- EditorRoot -------------------------------------------------------------
    void addWidget(Widget& widget) override;
    std::unique_ptr<ScrollAreaPeer> createScrollArea(Widget& content) override;
    std::unique_ptr<TextFieldPeer> createTextField(Widget& owner) override;
    Size size() const override;
    void setSize(int width, int height) override;
    void repaint() override { invalidate(); }
    bool startFileDrag(Widget& source, const MouseEvent& event, const std::filesystem::path& file,
                       std::function<void(int)> onEnded) override;
    std::string processName() const override;

    // ---- used by the peers --------------------------------------------------------
    void widgetRemoved(Widget& widget);
    void tooltipChanged(Widget& widget);
    int showMenu(Widget& widget, const std::vector<MenuEntry>& entries, Point at);
    Point originOf(const Widget& widget) const; // widget -> editor coordinates
    class ScrollArea;
    class TextField;
    void scrollAreaRemoved(ScrollArea& area);
    void textFieldRemoved(TextField& field);

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
    void onPaint();
    void releaseTarget();
    Point toPoints(LPARAM lParam) const;
    Point screenPoints() const;
    MouseEvent eventFor(Widget& widget, Point editorPoint) const;
    Widget* widgetAt(Point editorPoint) const;
    void setHover(Widget* widget, Point editorPoint);
    void endCapture(bool deliverUp, Point editorPoint);
    void updateTooltip();

    struct Item
    {
        Widget* widget = nullptr;
        ScrollArea* scroll = nullptr;
    };

    EditorHost* host_;
    HWND window_ = nullptr;
    HWND tooltip_ = nullptr;
    float scale_ = 1.0f;
    std::vector<Item> items_;
    std::vector<TextField*> fields_;
    std::unique_ptr<EditorController> controller_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    st::D2DResources resources_;
    Widget* hover_ = nullptr;
    Widget* capture_ = nullptr;
    bool trackingLeave_ = false;
    std::wstring tooltipText_;
    std::function<void(float)> scaleListener_;
    HBRUSH fieldBrush_ = nullptr;
    HBRUSH fieldFocusBrush_ = nullptr;
};

// ---------------------------------------------------------------- peers
namespace
{
class WinWidgetPeer final : public WidgetPeer
{
public:
    WinWidgetPeer(WinEditor& editor, Widget& widget) : editor_(editor), widget_(widget) {}
    ~WinWidgetPeer() override { editor_.widgetRemoved(widget_); }
    void boundsChanged() override { editor_.invalidate(); }
    void visibilityChanged() override { editor_.invalidate(); }
    void tooltipChanged() override { editor_.tooltipChanged(widget_); }
    void repaint() override { editor_.invalidate(); }
    int showMenu(const std::vector<MenuEntry>& entries, Point at, float) override
    {
        return editor_.showMenu(widget_, entries, at);
    }

private:
    WinEditor& editor_;
    Widget& widget_;
};
} // namespace

class WinEditor::ScrollArea final : public ScrollAreaPeer
{
public:
    ScrollArea(WinEditor& editor, Widget& content) : editor_(editor), content_(content) {}
    ~ScrollArea() override { editor_.scrollAreaRemoved(*this); }

    void setFrame(Rect frame) override
    {
        frame_ = frame;
        clampScroll();
        editor_.invalidate();
    }
    Rect frame() const override { return frame_; }
    void setVisible(bool visible) override
    {
        visible_ = visible;
        editor_.invalidate();
    }
    bool isVisible() const override { return visible_; }
    float contentWidth() const override { return frame_.width; } // the scroller overlays the content

    Widget& content() const noexcept { return content_; }
    float scrollY() const noexcept { return scrollY_; }
    float maxScroll() const { return std::max(0.0f, content_.bounds().height - frame_.height); }
    void scrollBy(float delta)
    {
        scrollY_ += delta;
        clampScroll();
        editor_.invalidate();
    }
    void clampScroll() { scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll()); }

private:
    WinEditor& editor_;
    Widget& content_;
    Rect frame_ {};
    bool visible_ = true;
    float scrollY_ = 0.0f;
};

// The licence key: a native single-line EDIT control.
class WinEditor::TextField final : public TextFieldPeer
{
public:
    TextField(WinEditor& editor, Widget& owner) : editor_(editor), owner_(owner)
    {
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL, 0, 0, 10, 10,
                                editor.window(), nullptr, st::thisModule(), nullptr);
        if (edit_ != nullptr)
        {
            SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
            SendMessageW(edit_, EM_LIMITTEXT, 128, 0);
            // EM_SETCUEBANNER: the placeholder (shown where comctl32 v6 is active).
            SendMessageW(edit_, 0x1501, FALSE, reinterpret_cast<LPARAM>(L"PC-XXXX-XXXX-XXXX-XXXX"));
        }
    }

    ~TextField() override
    {
        editor_.textFieldRemoved(*this);
        // A destroyed editor window took its children with it.
        if (edit_ != nullptr && editor_.window() != nullptr)
            DestroyWindow(edit_);
        if (font_ != nullptr)
            DeleteObject(font_);
    }

    // The control, while it exists (a destroyed editor window took it along).
    HWND edit() const noexcept { return editor_.window() != nullptr ? edit_ : nullptr; }
    Widget& owner() const noexcept { return owner_; }

    std::string text() const override
    {
        HWND edit_ = edit();
        if (edit_ == nullptr)
            return lastText_;
        const int length = GetWindowTextLengthW(edit_);
        std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(edit_, buffer.data(), length + 1);
        buffer.resize(static_cast<std::size_t>(length));
        return st::narrow(buffer);
    }

    void setText(const std::string& value) override
    {
        lastText_ = value;
        if (HWND control = edit())
            SetWindowTextW(control, st::widen(value).c_str());
    }

    void setTooltip(const std::string&) override {} // the owner widget's tooltip covers the field

    void applyFont(float height) override
    {
        height_ = height;
        rebuildFont();
    }

    void layout(Rect area, float indent, bool visible) override
    {
        area_ = area;
        indent_ = indent;
        visible_ = visible;
        place();
    }

    bool hasFocus() const override { return edit() != nullptr && GetFocus() == edit(); }
    void focus() override
    {
        if (HWND control = edit())
            SetFocus(control);
    }

    // The scale or the owner's place changed.
    void rebuildFont()
    {
        HWND edit_ = edit();
        if (edit_ == nullptr)
            return;
        const st::Font font { st::Face::mono, height_, 0.06f };
        const int pixels = std::max(6, static_cast<int>(std::lround(st::fontEmSize(font) * editor_.scale())));
        HFONT created = CreateFontW(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                                    st::gdiFaceName(st::Face::mono).c_str());
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(created), TRUE);
        if (font_ != nullptr)
            DeleteObject(font_);
        font_ = created;
        HDC dc = GetDC(edit_);
        HGDIOBJ old = SelectObject(dc, font_);
        TEXTMETRICW metrics {};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, old);
        ReleaseDC(edit_, dc);
        lineHeight_ = static_cast<int>(metrics.tmHeight) + 2;
        place();
    }

    void place()
    {
        HWND edit_ = edit();
        if (edit_ == nullptr)
            return;
        const float s = editor_.scale();
        const Point origin = editor_.originOf(owner_);
        const int left = static_cast<int>(std::lround((origin.x + area_.x + indent_) * s));
        const int width = std::max(1, static_cast<int>(std::lround((area_.width - indent_ * 2.0f) * s)));
        const int boxTop = static_cast<int>(std::lround((origin.y + area_.y) * s));
        const int boxHeight = static_cast<int>(std::lround(area_.height * s));
        const int height = std::min(boxHeight, std::max(1, lineHeight_));
        SetWindowPos(edit_, HWND_TOP, left, boxTop + (boxHeight - height) / 2, width, height,
                     SWP_NOACTIVATE | (visible_ ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }

private:
    WinEditor& editor_;
    Widget& owner_;
    HWND edit_ = nullptr;
    HFONT font_ = nullptr;
    float height_ = 12.0f;
    int lineHeight_ = 16;
    Rect area_ {};
    float indent_ = 10.0f;
    bool visible_ = false;
    std::string lastText_;
};

// ---------------------------------------------------------------- the window
WinEditor::WinEditor(EditorHost& host, HWND parent, int pixelWidth, int pixelHeight, float scale, EditorOptions options)
    : host_(&host), scale_(scale > 0.0f ? scale : windowScaleFactor(parent))
{
    if (!classRegistered)
    {
        WNDCLASSEXW windowClass {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = 0;
        windowClass.lpfnWndProc = &WinEditor::windowProc;
        windowClass.hInstance = st::thisModule();
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = windowClassName();
        classRegistered = RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    INITCOMMONCONTROLSEX controls { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&controls);

    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, windowClassName(), L"ChordEngine",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, std::max(1, pixelWidth),
                              std::max(1, pixelHeight), parent, nullptr, st::thisModule(), this);
    fieldBrush_ = CreateSolidBrush(gdiColour(st::inset));
    fieldFocusBrush_ = CreateSolidBrush(gdiColour(st::inset.brighter(0.05f)));

    if (window_ != nullptr)
    {
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr,
                                   st::thisModule(), nullptr);
        if (tooltip_ != nullptr)
        {
            TTTOOLINFOW tool {};
            tool.cbSize = TTTOOLINFOW_V2_SIZE;
            tool.uFlags = TTF_SUBCLASS;
            tool.hwnd = window_;
            tool.uId = 1;
            GetClientRect(window_, &tool.rect);
            tool.lpszText = const_cast<wchar_t*>(L"");
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(std::lround(360 * scale_)));
            SendMessageW(tooltip_, TTM_ACTIVATE, FALSE, 0);
        }
    }

    controller_ = std::make_unique<EditorController>(host, *this, std::move(options));
    openEditors().push_back(this);
}

WinEditor::~WinEditor()
{
    auto& editors = openEditors();
    editors.erase(std::remove(editors.begin(), editors.end(), this), editors.end());
    if (controller_ != nullptr)
        controller_->detach();
    hover_ = nullptr;
    capture_ = nullptr;
    controller_.reset(); // the widgets (and their peers) go first
    releaseTarget();
    if (tooltip_ != nullptr)
        DestroyWindow(tooltip_);
    if (window_ != nullptr)
    {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (fieldBrush_ != nullptr)
        DeleteObject(fieldBrush_);
    if (fieldFocusBrush_ != nullptr)
        DeleteObject(fieldFocusBrush_);
    // The class goes with the last editor, so an unloaded and reloaded module
    // never finds a class pointing into its old image.
    if (editors.empty() && classRegistered && UnregisterClassW(windowClassName(), st::thisModule()))
        classRegistered = false;
}

void WinEditor::detach()
{
    if (capture_ != nullptr && GetCapture() == window_)
        ReleaseCapture();
    capture_ = nullptr;
    if (controller_ != nullptr)
        controller_->detach();
    host_ = nullptr;
}

void WinEditor::releaseTarget()
{
    resources_.reset();
    target_.Reset();
}

void WinEditor::invalidate()
{
    if (window_ != nullptr)
        InvalidateRect(window_, nullptr, FALSE);
}

void WinEditor::setScale(float scale)
{
    if (scale <= 0.0f || std::abs(scale - scale_) < 0.001f)
        return;
    scale_ = scale;
    if (target_ != nullptr)
        target_->SetDpi(96.0f * scale_, 96.0f * scale_);
    if (tooltip_ != nullptr)
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(std::lround(360 * scale_)));
    if (controller_ != nullptr)
        controller_->resized();
    for (auto* field : fields_)
        field->rebuildFont();
    invalidate();
}

void WinEditor::setPixelSize(int width, int height)
{
    if (window_ != nullptr)
        SetWindowPos(window_, nullptr, 0, 0, std::max(1, width), std::max(1, height),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

Size WinEditor::size() const
{
    RECT client {};
    if (window_ == nullptr || !GetClientRect(window_, &client))
        return {};
    return { static_cast<float>(client.right - client.left) / scale_,
             static_cast<float>(client.bottom - client.top) / scale_ };
}

void WinEditor::setSize(int width, int height)
{
    setPixelSize(static_cast<int>(std::lround(width * scale_)), static_cast<int>(std::lround(height * scale_)));
}

std::string WinEditor::processName() const
{
    wchar_t buffer[MAX_PATH * 2] {};
    if (GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer))) == 0)
        return {};
    return st::narrow(std::filesystem::path(buffer).stem().wstring());
}

bool WinEditor::startFileDrag(Widget&, const MouseEvent&, const std::filesystem::path& file,
                              std::function<void(int)> onEnded)
{
    // DoDragDrop runs its own loop and owns the mouse until the drop.
    capture_ = nullptr;
    if (GetCapture() == window_)
        ReleaseCapture();
    return startFileDragWin(file, std::move(onEnded));
}

void WinEditor::addWidget(Widget& widget)
{
    widget.setPeer(std::make_unique<WinWidgetPeer>(*this, widget));
    items_.push_back({ &widget, nullptr });
}

std::unique_ptr<ScrollAreaPeer> WinEditor::createScrollArea(Widget& content)
{
    auto area = std::make_unique<ScrollArea>(*this, content);
    content.setPeer(std::make_unique<WinWidgetPeer>(*this, content));
    items_.push_back({ nullptr, area.get() });
    return area;
}

std::unique_ptr<TextFieldPeer> WinEditor::createTextField(Widget& owner)
{
    auto field = std::make_unique<TextField>(*this, owner);
    fields_.push_back(field.get());
    return field;
}

void WinEditor::widgetRemoved(Widget& widget)
{
    if (hover_ == &widget)
        hover_ = nullptr;
    if (capture_ == &widget)
        capture_ = nullptr;
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Item& item) { return item.widget == &widget; }),
                 items_.end());
}

void WinEditor::scrollAreaRemoved(ScrollArea& area)
{
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Item& item) { return item.scroll == &area; }),
                 items_.end());
}

void WinEditor::textFieldRemoved(TextField& field)
{
    fields_.erase(std::remove(fields_.begin(), fields_.end(), &field), fields_.end());
}

Point WinEditor::originOf(const Widget& widget) const
{
    for (const auto& item : items_)
        if (item.scroll != nullptr && &item.scroll->content() == &widget)
        {
            const Rect frame = item.scroll->frame();
            return { frame.x + widget.bounds().x, frame.y + widget.bounds().y - item.scroll->scrollY() };
        }
    return { widget.bounds().x, widget.bounds().y };
}

// ---------------------------------------------------------------- painting
void WinEditor::paintTree(st::Canvas& canvas, Size size)
{
    if (controller_ == nullptr)
        return;
    controller_->paint(canvas, Rect(0.0f, 0.0f, size.width, size.height));
    const auto paintWidget = [&canvas](Widget& widget)
    {
        const Rect bounds = widget.bounds();
        canvas.save();
        canvas.translate(bounds.x, bounds.y);
        canvas.clipTo(bounds.local());
        canvas.beginLayer(widget.alpha());
        widget.paint(canvas, bounds.local());
        canvas.endLayer();
        canvas.restore();
    };
    for (const auto& item : items_)
    {
        if (item.widget != nullptr)
        {
            if (item.widget->isVisible() && !item.widget->bounds().isEmpty())
                paintWidget(*item.widget);
            continue;
        }
        auto& area = *item.scroll;
        if (!area.isVisible() || area.frame().isEmpty())
            continue;
        const Rect frame = area.frame();
        canvas.save();
        canvas.translate(frame.x, frame.y);
        canvas.clipTo(frame.local());
        canvas.save();
        canvas.translate(0.0f, -area.scrollY());
        if (area.content().isVisible())
            paintWidget(area.content());
        canvas.restore();
        // An overlay scroller while the content is taller than the viewport.
        if (area.maxScroll() > 0.0f)
        {
            const float contentHeight = area.content().bounds().height;
            const float thumbHeight = std::max(24.0f, frame.height * frame.height / contentHeight);
            const float thumbY = (frame.height - thumbHeight) * (area.scrollY() / area.maxScroll());
            canvas.fillRoundedRect(Rect(frame.width - 7.0f, thumbY + 2.0f, 5.0f, thumbHeight - 4.0f), 2.5f,
                                   st::white.withAlpha(0.22f));
        }
        canvas.restore();
    }
}

void WinEditor::onPaint()
{
    PAINTSTRUCT paint {};
    BeginPaint(window_, &paint);
    RECT client {};
    GetClientRect(window_, &client);
    const auto pixelWidth = static_cast<UINT32>(std::max<LONG>(1, client.right - client.left));
    const auto pixelHeight = static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top));
    if (target_ == nullptr && st::d2dFactory() != nullptr)
    {
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                               D2D1_ALPHA_MODE_IGNORE),
                                                             96.0f * scale_, 96.0f * scale_);
        st::d2dFactory()->CreateHwndRenderTarget(properties,
                                                 D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(pixelWidth, pixelHeight)),
                                                 &target_);
    }
    if (target_ != nullptr)
    {
        const auto current = target_->GetPixelSize();
        if (current.width != pixelWidth || current.height != pixelHeight)
            target_->Resize(D2D1::SizeU(pixelWidth, pixelHeight));
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->Clear(D2D1::ColorF(st::background.r() / 255.0f, st::background.g() / 255.0f,
                                    st::background.b() / 255.0f));
        {
            st::D2DCanvas canvas(*target_.Get(), resources_);
            paintTree(canvas, size());
        }
        if (target_->EndDraw() == D2DERR_RECREATE_TARGET)
            releaseTarget();
    }
    EndPaint(window_, &paint);
}

// ---------------------------------------------------------------- input
Point WinEditor::toPoints(LPARAM lParam) const
{
    return { static_cast<float>(GET_X_LPARAM(lParam)) / scale_, static_cast<float>(GET_Y_LPARAM(lParam)) / scale_ };
}

Point WinEditor::screenPoints() const
{
    POINT cursor {};
    GetCursorPos(&cursor);
    return { static_cast<float>(cursor.x) / scale_, static_cast<float>(cursor.y) / scale_ };
}

MouseEvent WinEditor::eventFor(Widget& widget, Point editorPoint) const
{
    const Point origin = originOf(widget);
    MouseEvent event;
    event.position = { editorPoint.x - origin.x, editorPoint.y - origin.y };
    event.screen = screenPoints();
    return event;
}

Widget* WinEditor::widgetAt(Point p) const
{
    const auto accepts = [](const Widget& widget)
    { return widget.isVisible() && widget.interactive() && widget.isEnabled(); };
    for (auto it = items_.rbegin(); it != items_.rend(); ++it)
    {
        if (it->widget != nullptr)
        {
            if (accepts(*it->widget) && it->widget->bounds().contains(p))
                return it->widget;
            continue;
        }
        const auto& area = *it->scroll;
        if (!area.isVisible() || !area.frame().contains(p))
            continue;
        Widget& content = area.content();
        const Point origin = originOf(content);
        if (accepts(content) && content.localBounds().contains({ p.x - origin.x, p.y - origin.y }))
            return &content;
        return nullptr; // the viewport swallows clicks outside its content
    }
    return nullptr;
}

void WinEditor::setHover(Widget* widget, Point p)
{
    if (widget != hover_)
    {
        if (hover_ != nullptr)
        {
            hover_->hover = false;
            hover_->mouseMove({ -1.0f, -1.0f });
        }
        hover_ = widget;
        if (hover_ != nullptr)
            hover_->hover = true;
        updateTooltip();
        invalidate();
    }
    if (hover_ != nullptr)
        hover_->mouseMove(eventFor(*hover_, p).position);
}

void WinEditor::updateTooltip()
{
    if (tooltip_ == nullptr)
        return;
    const std::wstring text = hover_ != nullptr ? st::widen(hover_->tooltip()) : std::wstring();
    if (text == tooltipText_)
        return;
    tooltipText_ = text;
    SendMessageW(tooltip_, TTM_POP, 0, 0);
    TTTOOLINFOW tool {};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.hwnd = window_;
    tool.uId = 1;
    tool.lpszText = const_cast<wchar_t*>(tooltipText_.c_str());
    SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(tooltip_, TTM_ACTIVATE, tooltipText_.empty() ? FALSE : TRUE, 0);
}

void WinEditor::tooltipChanged(Widget& widget)
{
    if (&widget == hover_)
    {
        tooltipText_.clear();
        updateTooltip();
    }
}

void WinEditor::endCapture(bool deliverUp, Point p)
{
    Widget* widget = capture_;
    capture_ = nullptr;
    if (GetCapture() == window_)
        ReleaseCapture();
    if (widget == nullptr)
        return;
    widget->pressed = false;
    if (deliverUp)
        widget->mouseUp(eventFor(*widget, p));
    invalidate();
}

int WinEditor::showMenu(Widget& widget, const std::vector<MenuEntry>& entries, Point at)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
        return 0;
    for (const auto& entry : entries)
    {
        if (entry.kind == MenuEntry::Kind::separator)
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        else if (entry.kind == MenuEntry::Kind::heading)
            AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, st::widen(entry.text).c_str());
        else
            AppendMenuW(menu, MF_STRING | (entry.checked ? MF_CHECKED : 0u), static_cast<UINT_PTR>(entry.itemId),
                        st::widen(entry.text).c_str());
    }
    // The menu owns the mouse from here; the press that opened it is over.
    capture_ = nullptr;
    if (GetCapture() == window_)
        ReleaseCapture();
    const Point origin = originOf(widget);
    POINT position { static_cast<LONG>(std::lround((origin.x + at.x) * scale_)),
                     static_cast<LONG>(std::lround((origin.y + at.y) * scale_)) };
    ClientToScreen(window_, &position);
    const int chosen = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                                                         position.x, position.y, window_, nullptr));
    DestroyMenu(menu);
    return chosen;
}

LRESULT CALLBACK WinEditor::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* editor = static_cast<WinEditor*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
        if (editor != nullptr)
            editor->window_ = window;
    }
    auto* editor = reinterpret_cast<WinEditor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (editor != nullptr && editor->window_ == window)
        return editor->handle(message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT WinEditor::handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_PAINT:
            onPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (controller_ != nullptr)
                controller_->resized();
            for (auto* field : fields_)
                field->place();
            if (tooltip_ != nullptr)
            {
                TTTOOLINFOW tool {};
                tool.cbSize = TTTOOLINFOW_V2_SIZE;
                tool.hwnd = window_;
                tool.uId = 1;
                GetClientRect(window_, &tool.rect);
                SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
            }
            invalidate();
            return 0;
        case WM_MOUSEMOVE:
        {
            const Point p = toPoints(lParam);
            if (!trackingLeave_)
            {
                TRACKMOUSEEVENT track { sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0 };
                trackingLeave_ = TrackMouseEvent(&track) != FALSE;
            }
            if (capture_ != nullptr)
            {
                Widget* widget = capture_;
                widget->mouseDrag(eventFor(*widget, p));
                return 0;
            }
            setHover(widgetAt(p), p);
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            if (capture_ == nullptr)
                setHover(nullptr, {});
            return 0;
        case WM_LBUTTONDOWN:
        {
            const Point p = toPoints(lParam);
            Widget* widget = widgetAt(p);
            setHover(widget, p);
            if (widget == nullptr)
                return 0;
            capture_ = widget;
            SetCapture(window_);
            widget->pressed = true;
            widget->mouseDown(eventFor(*widget, p));
            // A menu or a drag may have taken the mouse (and ended the press).
            if (capture_ == widget && GetCapture() != window_)
            {
                capture_ = nullptr;
                widget->pressed = false;
            }
            invalidate();
            return 0;
        }
        case WM_LBUTTONUP:
            endCapture(true, toPoints(lParam));
            return 0;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != window_ && capture_ != nullptr)
                endCapture(false, {});
            return 0;
        case WM_RBUTTONDOWN:
        {
            const Point p = toPoints(lParam);
            if (Widget* widget = widgetAt(p))
                widget->rightMouseDown(eventFor(*widget, p));
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            POINT screen { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(window_, &screen);
            const Point p { static_cast<float>(screen.x) / scale_, static_cast<float>(screen.y) / scale_ };
            for (const auto& item : items_)
                if (item.scroll != nullptr && item.scroll->isVisible() && item.scroll->frame().contains(p))
                {
                    item.scroll->scrollBy(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * 48.0f);
                    return 0;
                }
            break;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && reinterpret_cast<HWND>(wParam) == window_)
            {
                const Widget* widget = capture_ != nullptr ? capture_ : hover_;
                LPCWSTR shape = IDC_ARROW;
                switch (widget != nullptr ? widget->cursor() : Cursor::arrow)
                {
                    case Cursor::pointingHand: shape = IDC_HAND; break;
                    case Cursor::openHand: shape = IDC_SIZEALL; break;
                    case Cursor::iBeam: shape = IDC_IBEAM; break;
                    case Cursor::crosshair: shape = IDC_SIZENWSE; break;
                    case Cursor::arrow: break;
                }
                SetCursor(LoadCursorW(nullptr, shape));
                return TRUE;
            }
            break;
        case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const bool focused = GetFocus() == reinterpret_cast<HWND>(lParam);
            SetTextColor(dc, gdiColour(st::textBright));
            SetBkColor(dc, gdiColour(focused ? st::inset.brighter(0.05f) : st::inset));
            return reinterpret_cast<LRESULT>(focused ? fieldFocusBrush_ : fieldBrush_);
        }
        case WM_COMMAND:
            if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS)
                for (auto* field : fields_)
                    if (field->edit() == reinterpret_cast<HWND>(lParam))
                    {
                        field->owner().repaint();
                        InvalidateRect(field->edit(), nullptr, TRUE);
                    }
            return 0;
        case WM_DPICHANGED_AFTERPARENT:
        {
            const float scale = windowScaleFactor(window_);
            if (std::abs(scale - scale_) >= 0.001f)
            {
                if (scaleListener_)
                    scaleListener_(scale); // the VST3 view resizes the host window
                else
                    setScale(scale);
            }
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_NCDESTROY:
        {
            // The host destroyed the parent first: forget the handle (it may be
            // reused) and every device object.
            const HWND window = window_;
            if (capture_ != nullptr)
                capture_->pressed = false;
            capture_ = nullptr;
            hover_ = nullptr;
            releaseTarget();
            tooltip_ = nullptr;
            window_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }
        default:
            break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

// ---------------------------------------------------------------- API
WinEditor* createWinEditor(EditorHost& host, void* parent, int pixelWidth, int pixelHeight, float scale,
                           EditorOptions options)
{
    auto* editor = new WinEditor(host, static_cast<HWND>(parent), pixelWidth, pixelHeight, scale, std::move(options));
    if (editor->window() == nullptr)
    {
        delete editor;
        return nullptr;
    }
    return editor;
}

void destroyWinEditor(WinEditor* editor)
{
    delete editor;
}

void detachAllWinEditors(EditorHost* host)
{
    for (auto* editor : openEditors())
        if (editor->host() == host)
            editor->detach();
}

void* winEditorWindow(WinEditor* editor)
{
    return editor != nullptr ? editor->window() : nullptr;
}

EditorController* winEditorController(WinEditor* editor)
{
    return editor != nullptr ? editor->controller() : nullptr;
}

float winEditorScale(WinEditor* editor)
{
    return editor != nullptr ? editor->scale() : 1.0f;
}

void setWinEditorScale(WinEditor* editor, float scale)
{
    if (editor != nullptr)
        editor->setScale(scale);
}

void setWinEditorPixelSize(WinEditor* editor, int pixelWidth, int pixelHeight)
{
    if (editor != nullptr)
        editor->setPixelSize(pixelWidth, pixelHeight);
}

void setWinEditorScaleListener(WinEditor* editor, std::function<void(float)> listener)
{
    if (editor != nullptr)
        editor->setScaleListener(std::move(listener));
}

// ---------------------------------------------------------------- offscreen
namespace
{
ComPtr<IWICImagingFactory> imagingFactory()
{
    ComPtr<IWICImagingFactory> factory;
    if (CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))
        == CO_E_NOTINITIALIZED)
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return factory;
}
} // namespace

bool renderWinEditor(WinEditor* editor, float scale, std::vector<std::uint8_t>& bgra, int& width, int& height)
{
    auto factory = imagingFactory();
    if (editor == nullptr || factory == nullptr || st::d2dFactory() == nullptr)
        return false;
    const Size size = editor->size();
    width = std::max(1, static_cast<int>(std::lround(size.width * scale)));
    height = std::max(1, static_cast<int>(std::lround(size.height * scale)));
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(factory->CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, &bitmap))
        || FAILED(st::d2dFactory()->CreateWicBitmapRenderTarget(
            bitmap.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                         96.0f * scale, 96.0f * scale),
            &target)))
        return false;
    st::D2DResources resources;
    target->BeginDraw();
    target->Clear(D2D1::ColorF(st::background.r() / 255.0f, st::background.g() / 255.0f, st::background.b() / 255.0f));
    {
        st::D2DCanvas canvas(*target.Get(), resources);
        editor->paintTree(canvas, size);
    }
    const HRESULT drawn = target->EndDraw();
    resources.reset();
    if (FAILED(drawn))
        return false;
    bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    return SUCCEEDED(bitmap->CopyPixels(nullptr, static_cast<UINT>(width) * 4, static_cast<UINT>(bgra.size()), bgra.data()));
}

bool writePng(const std::vector<std::uint8_t>& bgra, int width, int height, const std::filesystem::path& file)
{
    auto factory = imagingFactory();
    if (factory == nullptr || width <= 0 || height <= 0
        || bgra.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(file.wstring().c_str(), GENERIC_WRITE))
        || FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        || FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    frame->SetPixelFormat(&format);
    // The editor is opaque: premultiplied and straight alpha are the same.
    if (FAILED(frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(width) * 4,
                                  static_cast<UINT>(width) * static_cast<UINT>(height) * 4,
                                  const_cast<BYTE*>(bgra.data())))
        || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return false;
    return true;
}

bool writeWinEditorPng(WinEditor* editor, const std::filesystem::path& file, float scale)
{
    std::vector<std::uint8_t> pixels;
    int width = 0, height = 0;
    return renderWinEditor(editor, scale, pixels, width, height) && writePng(pixels, width, height, file);
}
} // namespace chordengine::ui
