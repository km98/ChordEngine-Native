// An OLE drag of one file (a recorded MIDI take) out of the Windows editor.
// The data object is the shell's own for the file (CF_HDROP plus the shell's
// formats), so a DAW sees exactly what a drag from Explorer gives it; copy,
// move and link are offered, as Explorer does.
#include "EditorWin.h"

#include "Platform/Platform.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace chordengine::ui
{
namespace
{
class DropSource final : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropSource)
        {
            *object = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto count = InterlockedDecrement(&references_);
        if (count == 0)
            delete this;
        return static_cast<ULONG>(count);
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
    {
        if (escapePressed)
            return DRAGDROP_S_CANCEL;
        if ((keyState & MK_LBUTTON) == 0)
            return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    LONG references_ = 1;
};
} // namespace

bool startFileDragWin(const std::filesystem::path& file, std::function<void(int operation)> onEnded)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
        return false;
    // DoDragDrop needs OLE on this (the UI) thread; hosts have it already.
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole))
        return false;
    bool started = false;
    {
        Microsoft::WRL::ComPtr<IShellItem> item;
        Microsoft::WRL::ComPtr<IDataObject> data;
        const auto absolute = std::filesystem::absolute(file, error);
        if (SUCCEEDED(SHCreateItemFromParsingName(absolute.wstring().c_str(), nullptr, IID_PPV_ARGS(&item)))
            && SUCCEEDED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))))
        {
            auto* source = new DropSource();
            DWORD effect = DROPEFFECT_NONE;
            const HRESULT result = DoDragDrop(data.Get(), source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                                              &effect);
            source->Release();
            started = true;
            const int performed = result == DRAGDROP_S_DROP ? static_cast<int>(effect) : 0;
            if (onEnded)
                platform::callOnMainThread([ended = std::move(onEnded), performed] { ended(performed); });
        }
    }
    OleUninitialize();
    return started;
}
} // namespace chordengine::ui
