#include "UpdateService.h"

#include "Licensing/LicensingTypes.h" // the shared Music-Prod origin constant only

#include <juce_events/juce_events.h>

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

UpdateService::UpdateService(juce::String currentVersion, juce::File cacheFile,
                             UpdateTransport::Ptr transport)
    : currentVersion_(std::move(currentVersion)),
      transport_(std::move(transport)),
      cache_(std::move(cacheFile))
{
    jassert(currentVersion_.isNotEmpty());
}

UpdateService::~UpdateService()
{
    // Finish or cancel the in-flight round trip before the owner can go away.
    pool_.removeAllJobs(true, 3000);
}

void UpdateService::ensureStartupCheck()
{
    if (startupCheckStarted_)
        return;

    startupCheckStarted_ = true;

    const auto cached = cache_.load();
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
    checking.currentVersion = currentVersion_;
    publish(checking);

    const juce::WeakReference<UpdateService> weakThis(this);
    pool_.addJob(
        [weakThis]
        {
            if (auto* self = weakThis.get())
                self->runBackgroundCheck();
        });
}

void UpdateService::runBackgroundCheck()
{
    const auto result = performCheck();

    const juce::WeakReference<UpdateService> weakThis(this);
    juce::MessageManager::callAsync(
        [weakThis, result]
        {
            if (auto* self = weakThis.get())
                self->finishBackgroundCheck(result);
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
    const auto platform = platformName();
    const auto url = buildUpdateUrl(juce::String(licensing::authOrigin), productSlug,
                                    currentVersion_, platform);

    TransportReply reply;
    UpdateResponse parsed;

    for (int attempt = 1; attempt <= maxCheckAttempts; ++attempt)
    {
        reply = transport_ != nullptr ? transport_->get(url) : TransportReply {};
        parsed = parseUpdateResponse(parseJsonText(reply.body), platform);

        if (reply.transportOk && parsed.ok)
            break;

        if (attempt == maxCheckAttempts || !shouldRetryCheck(reply, parsed))
            break;

        juce::Thread::sleep(retryBackoffMs);
    }

    const auto finishedAtMs = juce::Time::currentTimeMillis();

    if (parsed.ok)
    {
        CachedUpdate entry;
        entry.valid = true;
        entry.product = productSlug;
        entry.platform = platform;
        entry.currentVersion = currentVersion_;
        entry.latestVersion = parsed.latestVersion;
        entry.decision = parsed.decision;
        entry.serverStatus = parsed.serverStatus;
        entry.reason = parsed.reason;
        entry.mustUpdate = parsed.mustUpdate;
        entry.checkedAtMs = finishedAtMs;
        cache_.save(entry);
    }

    return buildView(reply, parsed, finishedAtMs);
}

UpdateView UpdateService::buildView(const TransportReply& reply, const UpdateResponse& parsed,
                                    std::int64_t nowMs) const
{
    UpdateView next;
    next.currentVersion = currentVersion_;

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
    const auto cached = cache_.load();
    if (cached.valid)
    {
        next.latestVersion = cached.latestVersion;
        next.mustUpdate = cached.mustUpdate;
        next.checkedAtMs = cached.checkedAtMs;
        next.cached = true;

        bool cachedMustUpdate = false;
        next.cachedStatus = classifyServerAnswer(cached.serverStatus, cached.decision,
                                                 cached.mustUpdate,
                                                 cached.latestVersion.isNotEmpty(),
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
        next.detail = "Error - server returned HTTP " + juce::String(reply.status);
    }
    else
    {
        next.status = UpdateStatus::error;
        next.detail = parsed.error.isNotEmpty() ? "Error - " + parsed.error
                                                : juce::String("Error checking for updates");
    }

    return next;
}

UpdateView UpdateService::buildCachedView(const CachedUpdate& entry) const
{
    UpdateView next;
    next.currentVersion = currentVersion_;
    next.latestVersion = entry.latestVersion;
    next.detail = entry.reason;
    next.cached = true;
    next.checkedAtMs = entry.checkedAtMs;

    bool mustUpdateOut = false;
    next.status = classifyServerAnswer(entry.serverStatus, entry.decision, entry.mustUpdate,
                                       entry.latestVersion.isNotEmpty(), mustUpdateOut);
    next.cachedStatus = next.status;
    next.mustUpdate = mustUpdateOut;
    return next;
}
} // namespace chordengine::updates
