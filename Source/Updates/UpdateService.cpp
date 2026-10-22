#include "UpdateService.h"

#include "Licensing/LicensingTypes.h" // the shared Music-Prod origin constant only

#include "Platform/Platform.h"

#include <chrono>
#include <thread>

namespace chordengine::updates
{
bool shouldRetryCheck(const TransportReply& reply, const UpdateResponse& parsed)
{
    // A body that never arrived (offline, DNS, timeout) is not retried: there
    // is nothing transient to wait out, and the editor must not loop.
    if (!reply.transportOk)
        return false;

    if (reply.status == 503)
        return true;

    return parsed.errorCode == "releaseBackendUnavailable";
}

UpdateService::UpdateService(std::string currentVersion, std::filesystem::path cacheFile,
                             UpdateTransport::Ptr transport)
    : shared_(std::make_shared<Shared>(Shared { std::move(currentVersion), std::move(transport),
                                                UpdateCache(std::move(cacheFile)) }))
{
}

// The worker queue waits up to 3 s for the in-flight round trip; a slower one
// finishes on its own with the shared state it holds.
UpdateService::~UpdateService() = default;

void UpdateService::ensureStartupCheck()
{
    if (startupCheckStarted_)
        return;

    startupCheckStarted_ = true;

    const auto cached = shared_->cache.load();
    if (cached.valid)
        publish(buildCachedView(cached)); // shown at once, marked as cached

    checkNow();
}

void UpdateService::checkNow()
{
    // One check at a time: a click landing while a check is running must not
    // stack another worker or another request.
    if (checkInFlight_.exchange(true))
        return;

    UpdateView checking;
    checking.status = UpdateStatus::checking;
    checking.currentVersion = shared_->currentVersion;
    publish(checking);

    const auto weakThis = lifetime_.weak();
    auto* const owner = this;
    worker_.post(
        [shared = shared_, weakThis, owner]
        {
            const auto result = performCheckOn(*shared);
            platform::callOnMainThread([weakThis, owner, result]
            {
                if (platform::LifetimeToken::alive(weakThis))
                    owner->finishBackgroundCheck(result);
            });
        });
}

void UpdateService::finishBackgroundCheck(const UpdateView& result)
{
    checkInFlight_ = false;
    publish(result);
}

void UpdateService::publish(UpdateView next)
{
    view_ = std::move(next);
    if (onViewChanged != nullptr)
        onViewChanged();
}

UpdateView UpdateService::performCheck()
{
    return performCheckOn(*shared_);
}

UpdateView UpdateService::performCheckOn(Shared& shared)
{
    const auto platform = platformName();
    const auto url = buildUpdateUrl(std::string(licensing::authOrigin), productSlug,
                                    shared.currentVersion, platform);

    TransportReply reply;
    UpdateResponse parsed;

    for (int attempt = 1; attempt <= maxCheckAttempts; ++attempt)
    {
        reply = shared.transport != nullptr ? shared.transport->get(url) : TransportReply {};
        parsed = parseUpdateResponse(parseJsonText(reply.body), platform);

        if (reply.transportOk && parsed.ok)
            break;

        if (attempt == maxCheckAttempts || !shouldRetryCheck(reply, parsed))
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(retryBackoffMs));
    }

    const auto finishedAtMs = platform::currentTimeMillis();

    if (parsed.ok)
    {
        CachedUpdate entry;
        entry.valid = true;
        entry.product = productSlug;
        entry.platform = platform;
        entry.currentVersion = shared.currentVersion;
        entry.latestVersion = parsed.latestVersion;
        entry.decision = parsed.decision;
        entry.serverStatus = parsed.serverStatus;
        entry.reason = parsed.reason;
        entry.mustUpdate = parsed.mustUpdate;
        entry.checkedAtMs = finishedAtMs;
        shared.cache.save(entry);
    }

    return buildView(shared, reply, parsed, finishedAtMs);
}

UpdateView UpdateService::buildView(const Shared& shared, const TransportReply& reply,
                                    const UpdateResponse& parsed, std::int64_t nowMs)
{
    UpdateView next;
    next.currentVersion = shared.currentVersion;

    if (parsed.ok)
    {
        next.status = parsed.status;
        next.cachedStatus = parsed.status;
        next.latestVersion = parsed.latestVersion;
        next.detail = parsed.reason;
        next.mustUpdate = parsed.mustUpdate;
        next.cached = false;
        next.checkedAtMs = nowMs;
        return next;
    }

    // A rejected request or payload is NEVER reported as success. The last
    // successful answer is kept and shown as cached.
    const auto cached = shared.cache.load();
    if (cached.valid)
    {
        next.latestVersion = cached.latestVersion;
        next.mustUpdate = cached.mustUpdate;
        next.checkedAtMs = cached.checkedAtMs;
        next.cached = true;

        bool cachedMustUpdate = false;
        next.cachedStatus = classifyServerAnswer(cached.serverStatus, cached.decision,
                                                 cached.mustUpdate,
                                                 !cached.latestVersion.empty(),
                                                 cachedMustUpdate);
    }

    if (!reply.transportOk)
    {
        next.status = cached.valid ? UpdateStatus::offlineCached : UpdateStatus::error;
        next.detail = reply.timedOut ? "Offline - request timed out"
                                     : "Offline - could not reach the update service";
    }
    else if (reply.status == 503)
    {
        next.status = UpdateStatus::error;
        next.detail = "Error - update service temporarily unavailable";
    }
    else if (reply.status != 200)
    {
        next.status = UpdateStatus::error;
        next.detail = "Error - server returned HTTP " + std::to_string(reply.status);
    }
    else
    {
        next.status = UpdateStatus::error;
        next.detail = !parsed.error.empty() ? "Error - " + parsed.error
                                                : std::string("Error checking for updates");
    }

    return next;
}

UpdateView UpdateService::buildCachedView(const CachedUpdate& entry) const
{
    UpdateView next;
    next.currentVersion = shared_->currentVersion;
    next.latestVersion = entry.latestVersion;
    next.detail = entry.reason;
    next.cached = true;
    next.checkedAtMs = entry.checkedAtMs;

    bool mustUpdateOut = false;
    next.status = classifyServerAnswer(entry.serverStatus, entry.decision, entry.mustUpdate,
                                       !entry.latestVersion.empty(), mustUpdateOut);
    next.cachedStatus = next.status;
    next.mustUpdate = mustUpdateOut;
    return next;
}
} // namespace chordengine::updates
