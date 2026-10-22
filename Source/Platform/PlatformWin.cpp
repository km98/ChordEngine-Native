// Windows implementation of Platform.h (WinHTTP, CNG, the Shell, and a
// message-only window for main-thread jobs and timers).
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#include "Platform.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace chordengine::platform
{
namespace
{
std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::string hex(const unsigned char* data, std::size_t size)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index)
    {
        out += digits[data[index] >> 4];
        out += digits[data[index] & 15];
    }
    return out;
}

// ---- the main-thread dispatcher: a message-only window on the thread that
// first created it (the host's UI thread: the VST3 factory and its
// initialize() run there).
constexpr UINT kJobMessage = WM_APP + 0x43;
std::atomic<DWORD> mainThreadId { 0 };
std::atomic<HWND> dispatcher { nullptr };
// Jobs queued before the main thread is known (a worker can finish before the
// UI thread has registered): delivered when it registers.
std::mutex pendingMutex;
std::vector<std::function<void()>*> pendingJobs;
std::mutex timerMutex;
std::map<UINT_PTR, std::weak_ptr<std::function<void()>>> timers;
UINT_PTR nextTimerId = 1;

LRESULT CALLBACK dispatcherProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == kJobMessage)
    {
        auto* job = reinterpret_cast<std::function<void()>*>(lParam);
        (*job)();
        delete job;
        return 0;
    }
    if (message == WM_TIMER)
    {
        std::shared_ptr<std::function<void()>> callback;
        {
            std::lock_guard<std::mutex> lock(timerMutex);
            const auto it = timers.find(static_cast<UINT_PTR>(wParam));
            if (it != timers.end())
                callback = it->second.lock();
        }
        if (callback)
            (*callback)();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Main thread only (the UI thread registers itself through
// initialiseMainThread, the timer start or the test loop).
HWND ensureDispatcher()
{
    if (auto existing = dispatcher.load())
        return existing;
    static const wchar_t* className = L"ChordEngineDispatcher";
    WNDCLASSW windowClass {};
    windowClass.lpfnWndProc = dispatcherProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    RegisterClassW(&windowClass); // fails harmlessly when already registered
    HWND window = CreateWindowExW(0, className, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    mainThreadId = GetCurrentThreadId();
    std::vector<std::function<void()>*> pending;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        dispatcher = window;
        pending.swap(pendingJobs);
    }
    for (auto* job : pending)
        if (!PostMessageW(window, kJobMessage, 0, reinterpret_cast<LPARAM>(job)))
            delete job;
    return window;
}
} // namespace

std::int64_t currentTimeMillis() noexcept
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double millisecondCounterHiRes() noexcept
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::string randomHex(std::size_t bytes)
{
    std::vector<unsigned char> buffer(bytes);
    if (bytes > 0)
        BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return hex(buffer.data(), buffer.size());
}

std::string sha256Hex(const void* data, std::size_t size)
{
    unsigned char digest[32] {};
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)),
               static_cast<ULONG>(size), digest, sizeof(digest));
    return hex(digest, sizeof(digest));
}

std::string computerName()
{
    wchar_t name[256] {};
    DWORD length = 256;
    if (!GetComputerNameExW(ComputerNamePhysicalDnsHostname, name, &length))
        return {};
    return narrow(std::wstring(name, length));
}

std::string operatingSystemName()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (auto* ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
            rtlGetVersion(&version);
    if (version.dwMajorVersion >= 10)
        return version.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10";
    return "Windows";
}

std::filesystem::path userApplicationDataDirectory()
{
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
        result = std::filesystem::path(path);
    CoTaskMemFree(path);
    return result.empty() ? temporaryDirectory() : result;
}

std::filesystem::path temporaryDirectory()
{
    std::error_code error;
    auto path = std::filesystem::temp_directory_path(error);
    return error ? std::filesystem::path(L"C:\\Temp") : path;
}

std::optional<std::string> readFile(const std::filesystem::path& path, std::size_t maxBytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::string bytes;
    char buffer[16384];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0)
    {
        bytes.append(buffer, static_cast<std::size_t>(in.gcount()));
        if (bytes.size() > maxBytes)
            return std::nullopt;
    }
    return bytes;
}

bool writeFileAtomically(const std::filesystem::path& path, const std::string& bytes)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp-" + randomHex(6);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out)
        {
            out.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::int64_t fileModificationTimeMs(const std::filesystem::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    ULARGE_INTEGER time {};
    time.LowPart = data.ftLastWriteTime.dwLowDateTime;
    time.HighPart = data.ftLastWriteTime.dwHighDateTime;
    // 100 ns intervals since 1601-01-01 -> ms since 1970-01-01.
    return static_cast<std::int64_t>((time.QuadPart - 116444736000000000ULL) / 10000ULL);
}

// ---- HTTP (WinHTTP) ------------------------------------------------------------
HttpResponse httpRequest(const HttpRequest& request)
{
    HttpResponse response;
    const auto url = widen(request.url);
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] {}, path[2048] {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    wchar_t extra[2048] {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
        return response;

    HINTERNET session = WinHttpOpen(L"ChordEngine", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return response;
    WinHttpSetTimeouts(session, request.connectTimeoutMs, request.connectTimeoutMs, request.connectTimeoutMs,
                       request.totalTimeoutMs);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    const std::wstring target = std::wstring(path) + extra;
    HINTERNET handle = connection != nullptr
        ? WinHttpOpenRequest(connection, widen(request.method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
        : nullptr;
    if (handle != nullptr)
    {
        DWORD policy = request.maxRedirects == 0 ? WINHTTP_OPTION_REDIRECT_POLICY_NEVER
                                                 : WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        if (request.maxRedirects > 0)
        {
            DWORD maximum = static_cast<DWORD>(request.maxRedirects);
            WinHttpSetOption(handle, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &maximum, sizeof(maximum));
        }
        std::wstring headers;
        for (const auto& [name, value] : request.headers)
            headers += widen(name) + L": " + widen(value) + L"\r\n";
        const auto started = millisecondCounterHiRes();
        if (WinHttpSendRequest(handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                               static_cast<DWORD>(-1L), const_cast<char*>(request.body.data()),
                               static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0)
            && WinHttpReceiveResponse(handle, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            response.transportOk = true;
            response.status = static_cast<int>(status);
            for (;;)
            {
                if (millisecondCounterHiRes() - started > request.totalTimeoutMs)
                {
                    response.timedOut = true;
                    break;
                }
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(handle, &available) || available == 0)
                    break;
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(handle, chunk.data(), available, &read) || read == 0)
                    break;
                response.body.append(chunk.data(), read);
                if (response.body.size() > request.maxResponseBytes)
                {
                    response.body.resize(request.maxResponseBytes);
                    response.truncated = true;
                    break;
                }
            }
        }
        else if (GetLastError() == ERROR_WINHTTP_TIMEOUT)
            response.timedOut = true;
        WinHttpCloseHandle(handle);
    }
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

// ---- threads ----------------------------------------------------------------------
void initialiseMainThread()
{
    ensureDispatcher();
}

bool isMainThread() noexcept
{
    return mainThreadId != 0 && GetCurrentThreadId() == mainThreadId;
}

void callOnMainThread(std::function<void()> job)
{
    auto* heap = new std::function<void()>(std::move(job));
    HWND window = nullptr;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        window = dispatcher.load();
        if (window == nullptr)
        {
            pendingJobs.push_back(heap); // delivered once the main thread registers
            return;
        }
    }
    if (!PostMessageW(window, kJobMessage, 0, reinterpret_cast<LPARAM>(heap)))
        delete heap;
}

void runMainLoopFor(int ms)
{
    ensureDispatcher();
    const auto until = millisecondCounterHiRes() + ms;
    MSG message {};
    while (millisecondCounterHiRes() < until)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
}

struct RepeatingTimer::Impl
{
    UINT_PTR id = 0;
    std::shared_ptr<std::function<void()>> callback;
    std::atomic<bool> running { false };
};

RepeatingTimer::RepeatingTimer() : impl_(std::make_unique<Impl>()) {}

RepeatingTimer::~RepeatingTimer()
{
    stop();
}

void RepeatingTimer::start(int intervalMs, std::function<void()> callback)
{
    stop();
    ensureDispatcher();
    impl_->callback = std::make_shared<std::function<void()>>(std::move(callback));
    {
        std::lock_guard<std::mutex> lock(timerMutex);
        impl_->id = nextTimerId++;
        timers[impl_->id] = impl_->callback;
    }
    SetTimer(dispatcher.load(), impl_->id, static_cast<UINT>(intervalMs), nullptr);
    impl_->running = true;
}

void RepeatingTimer::stop()
{
    if (impl_->id != 0)
    {
        if (auto window = dispatcher.load())
            KillTimer(window, impl_->id);
        std::lock_guard<std::mutex> lock(timerMutex);
        timers.erase(impl_->id);
        impl_->id = 0;
    }
    impl_->callback.reset();
    impl_->running = false;
}

bool RepeatingTimer::isRunning() const noexcept
{
    return impl_->running;
}

// ---- desktop ------------------------------------------------------------------------
bool openUrl(const std::string& url)
{
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool revealInFileManager(const std::filesystem::path& path)
{
    const std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr,
                                                   SW_SHOWNORMAL)) > 32;
}
} // namespace chordengine::platform
