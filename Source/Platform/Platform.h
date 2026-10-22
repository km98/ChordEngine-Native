#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// =====================================================================
// The operating-system seams ChordEngine needs, in plain C++ (macOS:
// Platform.mm; Windows: PlatformWin.cpp). No third-party code.
//
//   * time, random device ids, SHA-256, the computer's name;
//   * the per-user application data folder and atomic file writes;
//   * HTTP requests (blocking, for worker threads);
//   * a single-worker job queue and "run this on the main thread";
//   * a repeating main-thread timer;
//   * opening a URL in the default browser / registered app.
// =====================================================================
namespace chordengine::platform
{
// ---- time ------------------------------------------------------------
std::int64_t currentTimeMillis() noexcept;  // wall clock, ms since 1970
double millisecondCounterHiRes() noexcept;  // monotonic, ms

// ---- identity & crypto -------------------------------------------------
std::string randomHex(std::size_t bytes);   // lowercase hex from the OS CSPRNG
std::string sha256Hex(const void* data, std::size_t size);
std::string computerName();                 // "" when unknown
std::string operatingSystemName();          // e.g. "Mac OSX 14.5" style text

// ---- files ---------------------------------------------------------------
// ~/Library/Application Support on macOS, %APPDATA% on Windows.
std::filesystem::path userApplicationDataDirectory();
std::filesystem::path temporaryDirectory();
std::optional<std::string> readFile(const std::filesystem::path& path, std::size_t maxBytes = 64u << 20);
// Writes to a sibling temporary file and renames it over `path`.
bool writeFileAtomically(const std::filesystem::path& path, const std::string& bytes);
// The last-write time in ms since 1970 (0 when the file does not exist).
std::int64_t fileModificationTimeMs(const std::filesystem::path& path);

// ---- HTTP ------------------------------------------------------------------
struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int connectTimeoutMs = 12000;     // until the server answers
    int totalTimeoutMs = 30000;       // the whole exchange, body included
    std::size_t maxResponseBytes = 8u << 20;
    int maxRedirects = 5;
};

struct HttpResponse
{
    bool transportOk = false; // the server answered (any status)
    bool timedOut = false;
    bool truncated = false;   // the body exceeded maxResponseBytes
    int status = 0;
    std::string body;
};

// Blocking: call it from a worker thread, never from the audio thread or the
// main thread.
HttpResponse httpRequest(const HttpRequest& request);

// ---- threads ---------------------------------------------------------------
// Marks the calling thread as the main (UI) thread. macOS knows it already;
// on Windows the format adapter calls this from the host's UI thread before
// anything is queued, so main-thread jobs and timers run there.
void initialiseMainThread();
bool isMainThread() noexcept;
// Queues `job` on the main thread (the host's message thread).
void callOnMainThread(std::function<void()> job);
// Test helper: runs the main loop for up to `ms`, delivering queued jobs.
void runMainLoopFor(int ms);

// One background worker running jobs in order. Destroying the queue drops
// pending jobs and waits up to `shutdownWaitMs` for the running one; a job
// still running after that is left to finish on its own (it must therefore
// not reference the queue's owner except through a weak token).
class WorkerQueue
{
public:
    explicit WorkerQueue(int shutdownWaitMs = 3000);
    ~WorkerQueue();
    void post(std::function<void()> job);
    // Drops jobs not yet started.
    void clearPending();

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
    std::thread thread_;
    int shutdownWaitMs_;
};

// A repeating timer on the main thread. start() may be called from any
// thread; the callback always runs on the main thread. stop() (and the
// destructor) guarantee no callback runs afterwards when called on the main
// thread.
class RepeatingTimer
{
public:
    RepeatingTimer();
    ~RepeatingTimer();
    void start(int intervalMs, std::function<void()> callback);
    void stop();
    bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A token for "is the owner still alive" checks in asynchronous callbacks
// (main thread only).
class LifetimeToken
{
public:
    LifetimeToken() : alive_(std::make_shared<bool>(true)) {}
    ~LifetimeToken() { *alive_ = false; }
    LifetimeToken(const LifetimeToken&) = delete;
    LifetimeToken& operator=(const LifetimeToken&) = delete;
    std::weak_ptr<bool> weak() const { return alive_; }
    static bool alive(const std::weak_ptr<bool>& token)
    {
        const auto locked = token.lock();
        return locked && *locked;
    }

private:
    std::shared_ptr<bool> alive_;
};

// ---- desktop ---------------------------------------------------------------
bool openUrl(const std::string& url);
bool revealInFileManager(const std::filesystem::path& path);
} // namespace chordengine::platform
