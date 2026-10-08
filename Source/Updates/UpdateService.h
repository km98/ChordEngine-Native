#pragma once

#include "UpdateCache.h"
#include "UpdateResponse.h"
#include "UpdateTransport.h"
#include "UpdateTypes.h"

#include <atomic>
#include <functional>

namespace chordengine::updates
{
// Decides whether a failed attempt is worth one more try. ONLY the release
// backend being temporarily unavailable qualifies (HTTP 503, or the server's
// own releaseBackendUnavailable error code).
bool shouldRetryCheck(const TransportReply& reply, const UpdateResponse& parsed);

// =====================================================================
// The native ChordEngine update client.
//
//   * Every request runs on a ONE-worker background ThreadPool and its result
//     is delivered back with MessageManager::callAsync. The audio/MIDI thread
//     never sees this class.
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
    // ships in, taken from that build's metadata (JucePlugin_VersionString).
    // It is supplied by the plugin-side code because the value lives in the
    // plugin target's build definitions, never in this library.
    UpdateService(juce::String currentVersion, juce::File cacheFile,
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
    int workerCount() const noexcept { return pool_.getNumThreads(); }
    const UpdateCache& cache() const noexcept { return cache_; }
    const juce::String& currentVersion() const noexcept { return currentVersion_; }

    // Runs one complete check ON THE CALLING THREAD: transport, retry policy,
    // strict validation and cache persistence. It never touches view_ or the
    // message thread, so the background worker and the deterministic tests
    // drive the identical code path.
    UpdateView performCheck();

    // Invoked on the message thread whenever the published view changes.
    std::function<void()> onViewChanged;

    JUCE_DECLARE_WEAK_REFERENCEABLE(UpdateService)

private:
    void runBackgroundCheck();
    void finishBackgroundCheck(const UpdateView& result);
    void publish(UpdateView next);
    UpdateView buildView(const TransportReply& reply, const UpdateResponse& parsed,
                         std::int64_t nowMs) const;
    UpdateView buildCachedView(const CachedUpdate& entry) const;

    juce::ThreadPool pool_ { 1 };
    juce::String currentVersion_;
    UpdateTransport::Ptr transport_;
    UpdateCache cache_;
    UpdateView view_;
    bool startupCheckStarted_ = false;
    std::atomic<bool> checkInFlight_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateService)
};
} // namespace chordengine::updates
