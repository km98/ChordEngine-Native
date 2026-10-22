#pragma once

#include "UpdateCache.h"
#include "UpdateResponse.h"
#include "UpdateTransport.h"
#include "UpdateTypes.h"

#include "Platform/Platform.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

namespace chordengine::updates
{
// Decides whether a failed attempt is worth one more try. ONLY the release
// backend being temporarily unavailable qualifies (HTTP 503, or the server's
// own releaseBackendUnavailable error code).
bool shouldRetryCheck(const TransportReply& reply, const UpdateResponse& parsed);

// =====================================================================
// The native ChordEngine update client.
//
//   * Every request runs on a ONE-worker background queue and its result is
//     delivered back on the main thread. The audio/MIDI thread never sees
//     this class.
//   * The check is anonymous: it sends the product slug, the actual build
//     version and the platform. It reads no account, session or licence state
//     and it needs none.
//   * A failure is never turned into "Up to date"; the last successful answer
//     is kept and shown as cached.
//   * At most ONE automatic check happens per launch; an explicit click may
//     always request another.
// =====================================================================
class UpdateService
{
public:
    // `currentVersion` is the actual build version of the binary this client
    // ships in, taken from that build's metadata (CHORDENGINE_PLUGIN_VERSION_STRING).
    // It is supplied by the plugin-side code because the value lives in the
    // plugin target's build definitions, never in this library.
    UpdateService(std::string currentVersion, std::filesystem::path cacheFile,
                  UpdateTransport::Ptr transport);
    ~UpdateService();

    // Editor open: publish the cached answer immediately (marked cached), then
    // perform exactly one automatic check. Calling it again does nothing.
    void ensureStartupCheck();

    // CHECK FOR UPDATES: always requests a check (unless one is in flight).
    void checkNow();

    // The current INFO-page state. Message thread only.
    const UpdateView& view() const noexcept { return view_; }

    bool isCheckInFlight() const noexcept { return checkInFlight_.load(); }
    bool startupCheckDone() const noexcept { return startupCheckStarted_; }
    int workerCount() const noexcept { return 1; }
    const UpdateCache& cache() const noexcept { return shared_->cache; }
    const std::string& currentVersion() const noexcept { return shared_->currentVersion; }

    // Runs one complete check ON THE CALLING THREAD: transport, retry policy,
    // strict validation and cache persistence. It never touches view_ or the
    // message thread, so the background worker and the deterministic tests
    // drive the identical code path.
    UpdateView performCheck();

    // Invoked on the message thread whenever the published view changes.
    std::function<void()> onViewChanged;

private:
    // What a background check reads. The worker job holds it by shared
    // pointer, so a check still running when the editor closes never touches
    // a destroyed service.
    struct Shared
    {
        std::string currentVersion;
        UpdateTransport::Ptr transport;
        UpdateCache cache;
    };

    static UpdateView performCheckOn(Shared& shared);
    static UpdateView buildView(const Shared& shared, const TransportReply& reply,
                                const UpdateResponse& parsed, std::int64_t nowMs);
    void finishBackgroundCheck(const UpdateView& result);
    void publish(UpdateView next);
    UpdateView buildCachedView(const CachedUpdate& entry) const;

    std::shared_ptr<Shared> shared_;
    UpdateView view_;
    bool startupCheckStarted_ = false;
    std::atomic<bool> checkInFlight_ { false };
    platform::WorkerQueue worker_ { 3000 };
    platform::LifetimeToken lifetime_;
};
} // namespace chordengine::updates
