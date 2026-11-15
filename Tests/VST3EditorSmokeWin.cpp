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

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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
