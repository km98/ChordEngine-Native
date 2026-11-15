// A host's view of the built Windows VST3's editor: load ChordEngine.vst3,
// create the component, ask it for its editor view, attach it to an HWND,
// resize it, change the content scale, remove it and attach it again, then
// unload - what a DAW does when the plug-in window is opened and closed.
//
//   ChordEngineVST3EditorSmoke <path to ChordEngine.vst3 (the DLL)>
//
// The real plug-in keeps its account and trial files in %APPDATA%, so this
// runs only on CI (GITHUB_ACTIONS=true) or when CHORDENGINE_SMOKE_REAL_PROFILE=1;
// otherwise it reports "skipped" (exit 77).
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <wincodec.h>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace Steinberg
{
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
DEF_CLASS_IID(IPlugViewContentScaleSupport)
} // namespace Steinberg

using namespace Steinberg;

namespace
{
int failures = 0;
int passes = 0;

void check(bool ok, const std::string& what)
{
    if (ok)
        ++passes;
    else
    {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
        std::fflush(stdout);
    }
}

void pump(int ms)
{
    const auto end = GetTickCount64() + static_cast<ULONGLONG>(ms);
    MSG message {};
    while (GetTickCount64() < end)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    }
}

// The host's frame: grants every resize, as most hosts do.
class Frame final : public IPlugFrame
{
public:
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* rect) override
    {
        ++resizes;
        lastWidth = rect->getWidth();
        lastHeight = rect->getHeight();
        return view->onSize(rect);
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
    {
        QUERY_INTERFACE(iid, object, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, object, IPlugFrame::iid, IPlugFrame)
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int resizes = 0;
    int lastWidth = 0, lastHeight = 0;
};

HWND createParent()
{
    WNDCLASSW windowClass {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"ChordEngineSmokeHost";
    RegisterClassW(&windowClass);
    return CreateWindowExW(0, L"ChordEngineSmokeHost", L"Smoke host", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1400,
                           1100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND editorChild(HWND parent)
{
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
    {
        wchar_t name[128] {};
        GetClassNameW(child, name, 128);
        if (std::wstring(name).rfind(L"ChordEngineEditor_", 0) == 0)
            return child;
    }
    return nullptr;
}

// What the host window shows on screen (PrintWindow with the DWM's full
// content, so Direct2D and the EDIT control are included), cropped to the
// editor's area at its top-left, as a PNG; returns the number of distinct
// colours sampled (0 on failure).
int captureWindow(HWND child, const std::wstring& file)
{
    HWND window = GetAncestor(child, GA_ROOT);
    RECT rect {};
    GetClientRect(child, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return 0;
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    const BOOL printed = PrintWindow(window, memory, PW_CLIENTONLY | 0x00000002 /* PW_RENDERFULLCONTENT */);
    GdiFlush();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    if (bits != nullptr)
        std::memcpy(pixels.data(), bits, pixels.size());
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!printed)
        return 0;
    for (std::size_t index = 3; index < pixels.size(); index += 4)
        pixels[index] = 255;
    std::vector<std::uint32_t> colours;
    for (std::size_t index = 0; index < pixels.size() && colours.size() < 64; index += 4 * 97)
    {
        std::uint32_t colour = 0;
        std::memcpy(&colour, &pixels[index], 4);
        bool seen = false;
        for (auto c : colours)
            seen = seen || c == colour;
        if (!seen)
            colours.push_back(colour);
    }
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE))
        && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)))
    {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
        frame->SetPixelFormat(&format);
        frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(width) * 4, static_cast<UINT>(pixels.size()),
                           pixels.data());
        frame->Commit();
        encoder->Commit();
    }
    for (IUnknown* object : std::initializer_list<IUnknown*> { frame, encoder, stream, factory })
        if (object != nullptr)
            object->Release();
    return static_cast<int>(colours.size());
}

std::wstring captureDirectory()
{
    const char* dir = std::getenv("CHORDENGINE_SMOKE_CAPTURE_DIR");
    if (dir == nullptr || *dir == 0)
        return {};
    std::wstring wide;
    for (const char* c = dir; *c != 0; ++c)
        wide += static_cast<wchar_t>(static_cast<unsigned char>(*c));
    CreateDirectoryW(wide.c_str(), nullptr);
    return wide + L"\\";
}

void click(HWND window, int x, int y)
{
    const LPARAM at = MAKELPARAM(x, y);
    SendMessageW(window, WM_MOUSEMOVE, 0, at);
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageW(window, WM_LBUTTONUP, 0, at);
}

SIZE clientSize(HWND window)
{
    RECT rect {};
    GetClientRect(window, &rect);
    return { rect.right - rect.left, rect.bottom - rect.top };
}
} // namespace

int main(int argc, char** argv)
{
    const char* ci = std::getenv("GITHUB_ACTIONS");
    const char* allowed = std::getenv("CHORDENGINE_SMOKE_REAL_PROFILE");
    if (!(ci != nullptr && std::string(ci) == "true") && !(allowed != nullptr && std::string(allowed) == "1"))
    {
        std::printf("skipped: loads the real plug-in, which uses the account files in %%APPDATA% "
                    "(set CHORDENGINE_SMOKE_REAL_PROFILE=1 to run it here)\n");
        return 77;
    }
    if (argc < 2)
    {
        std::printf("usage: ChordEngineVST3EditorSmoke <ChordEngine.vst3>\n");
        return 2;
    }
    OleInitialize(nullptr);

    HMODULE module = LoadLibraryA(argv[1]);
    check(module != nullptr, "the VST3 module loads");
    if (module == nullptr)
        return 1;
    using InitDll = bool (*)();
    using GetFactory = IPluginFactory* (PLUGIN_API*)();
    auto init = reinterpret_cast<InitDll>(reinterpret_cast<void*>(GetProcAddress(module, "InitDll")));
    auto exitDll = reinterpret_cast<InitDll>(reinterpret_cast<void*>(GetProcAddress(module, "ExitDll")));
    auto getFactory = reinterpret_cast<GetFactory>(reinterpret_cast<void*>(GetProcAddress(module, "GetPluginFactory")));
    check(init != nullptr && exitDll != nullptr && getFactory != nullptr, "the module exports the VST3 entry points");
    if (init == nullptr || getFactory == nullptr)
        return 1;
    init();

    {
        IPluginFactory* factory = getFactory();
        PClassInfo info {};
        check(factory != nullptr && factory->getClassInfo(0, &info) == kResultOk, "the factory describes its class");
        Vst::IComponent* component = nullptr;
        check(factory->createInstance(info.cid, Vst::IComponent::iid, reinterpret_cast<void**>(&component)) == kResultOk
                  && component != nullptr,
              "the component is created");
        if (component == nullptr)
            return 1;
        check(component->initialize(nullptr) == kResultOk, "the component initialises");
        Vst::IEditController* controller = nullptr;
        check(component->queryInterface(Vst::IEditController::iid, reinterpret_cast<void**>(&controller)) == kResultOk
                  && controller != nullptr,
              "the component is its own edit controller");

        HWND parent = createParent();
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        Frame frame;
        for (int round = 0; round < 2; ++round)
        {
            const std::string r = " (round " + std::to_string(round + 1) + ")";
            IPlugView* view = controller->createView(Vst::ViewType::kEditor);
            check(view != nullptr, "createView(editor)" + r);
            if (view == nullptr)
                break;
            check(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue, "HWND is supported" + r);
            check(view->isPlatformTypeSupported(kPlatformTypeNSView) != kResultTrue, "NSView is not offered" + r);
            check(view->setFrame(&frame) == kResultTrue, "setFrame" + r);
            IPlugViewContentScaleSupport* scaling = nullptr;
            view->queryInterface(IPlugViewContentScaleSupport::iid, reinterpret_cast<void**>(&scaling));
            check(scaling != nullptr, "the view supports content scaling" + r);
            if (scaling != nullptr)
                check(scaling->setContentScaleFactor(1.0f) == kResultTrue, "setContentScaleFactor(1)" + r);
            ViewRect size {};
            check(view->getSize(&size) == kResultOk && size.getWidth() == 800 && size.getHeight() == 650,
                  "getSize is the 800 x 650 design" + r);
            check(view->canResize() == kResultTrue, "the view is resizable" + r);
            check(view->attached(parent, kPlatformTypeHWND) == kResultOk, "attached(HWND)" + r);
            pump(250);
            HWND child = editorChild(parent);
            check(child != nullptr, "the editor window is a child of the host window" + r);
            if (child != nullptr)
                check(clientSize(child).cx == 800 && clientSize(child).cy == 650, "the editor window is 800 x 650" + r);
            HWND field = child != nullptr ? FindWindowExW(child, nullptr, L"EDIT", nullptr) : nullptr;
            check(field != nullptr, "the licence key field exists" + r);

            // The host resizes (within the constraints, aspect kept).
            ViewRect wanted(0, 0, 1000, 1000);
            check(view->checkSizeConstraint(&wanted) == kResultTrue && wanted.getWidth() == 1000
                      && wanted.getHeight() == 813,
                  "checkSizeConstraint keeps the aspect" + r);
            check(view->onSize(&wanted) == kResultOk, "onSize" + r);
            pump(50);
            if (child != nullptr)
                check(clientSize(child).cx == 1000 && clientSize(child).cy == 813, "the window follows onSize" + r);
            ViewRect tooBig(0, 0, 5000, 5000);
            view->checkSizeConstraint(&tooBig);
            check(tooBig.getWidth() == 1280 && tooBig.getHeight() == 1040, "sizes clamp at 1280 x 1040" + r);

            // The host's display scale: 150 % asks for 1.5x the pixels.
            if (scaling != nullptr)
            {
                const int before = frame.resizes;
                check(scaling->setContentScaleFactor(1.5f) == kResultTrue, "setContentScaleFactor(1.5)" + r);
                check(frame.resizes == before + 1 && frame.lastWidth == 1500 && frame.lastHeight == 1220,
                      "150 %: the view asks the host for 1500 x 1220 (" + std::to_string(frame.lastWidth) + " x "
                          + std::to_string(frame.lastHeight) + ")" + r);
                pump(50);
                if (child != nullptr)
                    check(clientSize(child).cx == 1500, "150 %: the window is resized" + r);
                ViewRect scaledMin(0, 0, 10, 10);
                view->checkSizeConstraint(&scaledMin);
                check(scaledMin.getWidth() == 960, "150 %: the minimum is 640 points = 960 pixels" + r);
                scaling->setContentScaleFactor(1.0f);
                scaling->release();
            }

            // A message pump with the editor open: timer, paint, input.
            InvalidateRect(child, nullptr, FALSE);
            UpdateWindow(child);
            SendMessageW(child, WM_MOUSEMOVE, 0, MAKELPARAM(400, 400));
            pump(200);

            // What the hosted window really shows (first round): the CHORD
            // page, then INFO with a key typed into the native field.
            if (round == 0 && child != nullptr)
            {
                ViewRect design(0, 0, 800, 650);
                view->onSize(&design);
                InvalidateRect(child, nullptr, FALSE);
                pump(150);
                const auto dir = captureDirectory();
                const int colours = captureWindow(child, dir + L"ChordEngine-Windows-hosted-chord.png");
                check(colours > 4, "the hosted window shows the editor (" + std::to_string(colours) + " colours sampled)");
                click(child, 734, 36); // INFO
                pump(100);
                HWND key = FindWindowExW(child, nullptr, L"EDIT", nullptr);
                check(key != nullptr && (GetWindowLongPtrW(key, GWL_STYLE) & WS_VISIBLE) != 0,
                      "INFO (clicked in the hosted window) shows the licence key field");
                if (key != nullptr)
                {
                    SetFocus(key);
                    for (const wchar_t c : std::wstring(L"PC-7QF2-3MHD-NX4T-9BKR"))
                        SendMessageW(key, WM_CHAR, c, 0);
                }
                pump(150);
                captureWindow(child, dir + L"ChordEngine-Windows-hosted-info-key.png");
                click(child, 615, 36); // back to CHORD
                pump(50);
            }

            check(view->removed() == kResultOk, "removed()" + r);
            pump(50);
            check(editorChild(parent) == nullptr, "removing the view destroys the editor window" + r);
            check(view->setFrame(nullptr) == kResultTrue, "setFrame(nullptr)" + r);
            view->release();
        }

        // The plug-in goes away while a view is still attached (some hosts).
        IPlugView* view = controller->createView(Vst::ViewType::kEditor);
        if (view != nullptr)
        {
            view->attached(parent, kPlatformTypeHWND);
            pump(50);
            controller->release();
            component->terminate();
            component->release();
            controller = nullptr;
            component = nullptr;
            pump(50);
            view->removed();
            view->release();
            check(true, "a view outliving its component is removed safely");
        }
        if (controller != nullptr)
            controller->release();
        if (component != nullptr)
        {
            component->terminate();
            component->release();
        }
        DestroyWindow(parent);
    }

    if (exitDll != nullptr)
        exitDll();
    FreeLibrary(module);
    OleUninitialize();
    std::printf("%s: %d passed, %d failed\n", failures == 0 ? "VST3 EDITOR SMOKE PASSED" : "VST3 EDITOR SMOKE FAILED",
                passes, failures);
    return failures == 0 ? 0 : 1;
}
