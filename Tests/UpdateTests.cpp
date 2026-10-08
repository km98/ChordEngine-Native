// Deterministic tests for the native ChordEngine update client.
//
// Every case runs against the SAME production code the plugin ships:
//   * the strict response validator (UpdateResponse),
//   * the decision/status mapping and the INFO-page rendering (UpdateTypes),
//   * the cache (UpdateCache),
//   * the service, its single-worker pool, its retry policy and the
//     message-thread delivery (UpdateService).
//
// No case touches the live service. Either the transport is a local fake with a
// scripted reply, or it is the offline transport, so the suite is fully
// reproducible and needs no network.

#include "Updates/UpdateCache.h"
#include "Updates/UpdateResponse.h"
#include "Updates/UpdateService.h"
#include "Updates/UpdateTransport.h"
#include "Updates/UpdateTypes.h"

#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
int casesPassed = 0;

[[noreturn]] void fail(const juce::String& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

void casePassed()
{
    ++casesPassed;
}

using chordengine::updates::CachedUpdate;
using chordengine::updates::UpdateCache;
using chordengine::updates::UpdateResponse;
using chordengine::updates::UpdateService;
using chordengine::updates::UpdateStatus;
using chordengine::updates::UpdateTransport;
using chordengine::updates::UpdateView;

// --------------------------------------------------------------- fixtures
// The verbatim live response the read-only audit captured for `chordengine`.
const char* liveNoPublishedRelease =
    R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.4.0","latestVersion":null,"updateAvailable":false,"decision":"upToDate","mustUpdate":false,"reason":"no published release exists for this product yet","mandatory":false,"minimumSupportedVersion":null,"releaseNotes":null,"artifacts":[],"channel":"stable","latestBuildNumber":null,"latestReleaseId":null,"publishedAt":null,"status":"noPublishedRelease"}})";

const char* upToDateBody =
    R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.1.0","latestVersion":"0.1.0","updateAvailable":false,"decision":"upToDate","mustUpdate":false,"status":"upToDate"}})";

const char* optionalUpdateBody =
    R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.1.0","latestVersion":"0.2.0","updateAvailable":true,"decision":"optionalUpdate","mustUpdate":false,"status":"publishedRelease"}})";

const char* mandatoryUpdateBody =
    R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.1.0","latestVersion":"0.2.0","updateAvailable":true,"decision":"mandatoryUpdate","mustUpdate":true,"mandatory":true,"status":"publishedRelease"}})";

// ----------------------------------------------------------- test doubles
struct ScriptedReply
{
    int status = 200;
    juce::String body;
    bool transportOk = true;
    bool timedOut = false;
};

// Records every request and replays a scripted list of responses. When the
// script runs out the last entry repeats; an empty script is a total failure to
// reach the service.
class FakeTransport final : public UpdateTransport
{
public:
    chordengine::updates::TransportReply get(const juce::String& url) override
    {
        requestUrls.add(url);
        ++calls;

        chordengine::updates::TransportReply reply;
        if (scripted.empty())
            return reply; // transportOk stays false

        const auto index = static_cast<std::size_t>(
            juce::jmin(calls - 1, static_cast<int>(scripted.size()) - 1));

        const auto& entry = scripted[index];
        reply.status = entry.status;
        reply.body = entry.body;
        reply.transportOk = entry.transportOk;
        reply.timedOut = entry.timedOut;
        return reply;
    }

    std::vector<ScriptedReply> scripted;
    juce::Array<juce::String> requestUrls;
    int calls = 0;
};

// Records the status line of every published view, so the exact sequence of
// states the INFO page would show can be asserted.
struct ViewRecorder
{
    void attach(UpdateService& service)
    {
        service.onViewChanged = [this, &service]
        { statuses.push_back(chordengine::updates::statusLine(service.view())); };
    }

    std::vector<juce::String> statuses;
};

// --------------------------------------------------------------- helpers
juce::File temporaryDirectory(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("ChordEngine-UpdateTests")
                   .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

juce::File temporaryCacheFile(const juce::String& name)
{
    return temporaryDirectory(name).getChildFile(UpdateCache::fileName);
}

void pumpMessages(int milliseconds)
{
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        manager->runDispatchLoopUntil(milliseconds);
}

juce::String readSourceFile(const juce::String& relativePath)
{
    const juce::File file(juce::String(CHORDENGINE_SOURCE_DIR) + "/" + relativePath);
    require(file.existsAsFile(), "source file must exist: " + relativePath);
    return file.loadFileAsString();
}

juce::var parseBody(const juce::String& body)
{
    return chordengine::updates::parseJsonText(body);
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI guiInitialiser;
    juce::ignoreUnused(guiInitialiser);

    const auto platform = chordengine::updates::platformName();

    // =================================================================
    // 1. noPublishedRelease is never reported as "Up to date".
    // =================================================================
    {
        const auto parsed = chordengine::updates::parseUpdateResponse(
            parseBody(liveNoPublishedRelease), platform);
        require(parsed.ok, "noPublishedRelease: the live payload must validate");
        require(parsed.status == UpdateStatus::noPublishedRelease,
                "noPublishedRelease: the status word wins over the upToDate decision");
        require(parsed.status != UpdateStatus::upToDate,
                "noPublishedRelease: it must never be mapped to 'Up to date'");
        require(parsed.latestVersion.isEmpty(),
                "noPublishedRelease: no latest version may be invented");

        UpdateView view;
        view.currentVersion = chordengine::updates::currentBuildVersionString;
        view.status = parsed.status;
        view.latestVersion = parsed.latestVersion;
        require(chordengine::updates::latestVersionLine(view)
                    == "Latest version  No published release",
                "noPublishedRelease: the Latest row states it honestly");
        require(chordengine::updates::statusLine(view) == "Status  No published release",
                "noPublishedRelease: the Status row states it honestly");
        require(chordengine::updates::currentVersionLine(view) == "Current version  0.1.0",
                "noPublishedRelease: the Current row shows the actual build version");
        require(!chordengine::updates::latestVersionLine(view).contains(" -"),
                "noPublishedRelease: a real answer never renders as a placeholder dash");
        casePassed();
    }

    // =================================================================
    // 2. upToDate.
    // =================================================================
    {
        const auto parsed = chordengine::updates::parseUpdateResponse(
            parseBody(upToDateBody), platform);
        require(parsed.ok, "upToDate: the payload must validate");
        require(parsed.status == UpdateStatus::upToDate, "upToDate: classified as up to date");
        require(!parsed.mustUpdate, "upToDate: nothing is mandatory");
        require(parsed.latestVersion == "0.1.0", "upToDate: the published version is read");

        UpdateView view;
        view.currentVersion = chordengine::updates::currentBuildVersionString;
        view.status = parsed.status;
        view.latestVersion = parsed.latestVersion;
        require(chordengine::updates::latestVersionLine(view) == "Latest version  0.1.0",
                "upToDate: the Latest row shows the published version");
        require(chordengine::updates::statusLine(view) == "Status  Up to date",
                "upToDate: the Status row says up to date");
        casePassed();
    }

    // =================================================================
    // 3. optionalUpdate -> "Update available".
    // =================================================================
    {
        const auto parsed = chordengine::updates::parseUpdateResponse(
            parseBody(optionalUpdateBody), platform);
        require(parsed.ok, "optionalUpdate: the payload must validate");
        require(parsed.status == UpdateStatus::updateAvailable,
                "optionalUpdate: classified as an available update");
        require(!parsed.mustUpdate, "optionalUpdate: it is not mandatory");
        require(parsed.latestVersion == "0.2.0", "optionalUpdate: the latest version is read");

        UpdateView view;
        view.currentVersion = chordengine::updates::currentBuildVersionString;
        view.status = parsed.status;
        view.latestVersion = parsed.latestVersion;
        view.mustUpdate = parsed.mustUpdate;
        require(chordengine::updates::latestVersionLine(view) == "Latest version  0.2.0",
                "optionalUpdate: the Latest row shows the published version");
        require(chordengine::updates::statusLine(view) == "Status  Update available",
                "optionalUpdate: the Status row says an update is available");
        casePassed();
    }

    // =================================================================
    // 4. mandatoryUpdate -> "Update required" (and nothing is downloaded).
    // =================================================================
    {
        const auto parsed = chordengine::updates::parseUpdateResponse(
            parseBody(mandatoryUpdateBody), platform);
        require(parsed.ok, "mandatoryUpdate: the payload must validate");
        require(parsed.status == UpdateStatus::updateAvailable,
                "mandatoryUpdate: classified as an available update");
        require(parsed.mustUpdate, "mandatoryUpdate: marked as mandatory");
        require(parsed.latestVersion == "0.2.0", "mandatoryUpdate: the latest version is read");

        UpdateView view;
        view.currentVersion = chordengine::updates::currentBuildVersionString;
        view.status = parsed.status;
        view.latestVersion = parsed.latestVersion;
        view.mustUpdate = parsed.mustUpdate;
        require(chordengine::updates::statusLine(view) == "Status  Update required",
                "mandatoryUpdate: the Status row asks for the update");
        require(chordengine::updates::latestVersionLine(view) == "Latest version  0.2.0",
                "mandatoryUpdate: the Latest row shows the published version");
        casePassed();
    }

    // =================================================================
    // 5. An invalid current version is refused.
    // =================================================================
    {
        const auto parsed = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"chordengine","currentVersion":"not-a-version","latestVersion":null,"decision":"upToDate","status":"noPublishedRelease"}})"),
            platform);
        require(!parsed.ok, "invalid current version: the payload must be refused");
        require(parsed.status == UpdateStatus::error,
                "invalid current version: the refusal is an error state, never success");
        require(parsed.error == "invalid current version",
                "invalid current version: the precise reason is reported");

        // A well-formed one is accepted, so the check above is meaningful.
        require(chordengine::updates::isValidSemver("0.1.0"), "semver: 0.1.0 is valid");
        require(chordengine::updates::isValidSemver("1.2.3-rc.1+build.7"),
                "semver: a prerelease and build tail is valid");
        require(!chordengine::updates::isValidSemver("0.1"), "semver: 0.1 is not valid");
        require(!chordengine::updates::isValidSemver("0.1.0.1"), "semver: four parts are not valid");
        require(!chordengine::updates::isValidSemver("01.1.0"),
                "semver: a leading zero is not valid");
        casePassed();
    }

    // =================================================================
    // 6. An invalid latest version is refused.
    // =================================================================
    {
        const auto parsed = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.1.0","latestVersion":"v0.2","updateAvailable":true,"decision":"optionalUpdate"}})"),
            platform);
        require(!parsed.ok, "invalid latest version: the payload must be refused");
        require(parsed.status == UpdateStatus::error,
                "invalid latest version: the refusal is an error state");
        require(parsed.error == "invalid latest version",
                "invalid latest version: the precise reason is reported");
        casePassed();
    }

    // =================================================================
    // 7. A wrong product, an unsuccessful envelope and an inconsistent
    //    payload are all refused.
    // =================================================================
    {
        const auto wrongProduct = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"vyre","currentVersion":"0.1.0","latestVersion":null,"decision":"upToDate","status":"noPublishedRelease"}})"),
            platform);
        require(!wrongProduct.ok, "wrong product: the payload must be refused");
        require(wrongProduct.error == "unexpected product",
                "wrong product: the precise reason is reported");

        const auto unsuccessful = chordengine::updates::parseUpdateResponse(
            parseBody(R"({"success":false,"error":"nope","errorCode":"invalidRequest"})"), platform);
        require(!unsuccessful.ok, "unsuccessful envelope: must be refused");
        require(unsuccessful.errorCode == "invalidRequest",
                "unsuccessful envelope: the server error code is preserved");

        const auto inconsistent = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.1.0","latestVersion":null,"updateAvailable":true,"decision":"upToDate"}})"),
            platform);
        require(!inconsistent.ok, "inconsistent payload: an update with no version is refused");
        require(inconsistent.error == "inconsistent response",
                "inconsistent payload: the precise reason is reported");

        const auto unknownDecision = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"chordengine","latestVersion":null,"decision":"somethingElse"}})"),
            platform);
        require(!unknownDecision.ok, "unknown decision: the payload is refused");
        casePassed();
    }

    // =================================================================
    // 8. A platform mismatch is refused; an absent platform is accepted.
    // =================================================================
    {
        const auto wrongPlatform = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"chordengine","platform":"windows","currentVersion":"0.1.0","latestVersion":null,"decision":"upToDate","status":"noPublishedRelease"}})"),
            "macos");
        require(!wrongPlatform.ok, "wrong platform: the payload must be refused");
        require(wrongPlatform.error == "platform mismatch",
                "wrong platform: the precise reason is reported");

        const auto matchingPlatform = chordengine::updates::parseUpdateResponse(parseBody(
            R"({"success":true,"data":{"product":"chordengine","platform":"macos","currentVersion":"0.1.0","latestVersion":null,"decision":"upToDate","status":"noPublishedRelease"}})"),
            "macos");
        require(matchingPlatform.ok, "matching platform: the payload is accepted");

        // The live payload carries no platform field; that must still be valid.
        const auto absentPlatform = chordengine::updates::parseUpdateResponse(
            parseBody(liveNoPublishedRelease), "macos");
        require(absentPlatform.ok, "absent platform: the live payload is still accepted");
        casePassed();
    }

    // =================================================================
    // 9. HTTP 503 / releaseBackendUnavailable retries once; anything else
    //    does not retry and never becomes a success.
    // =================================================================
    {
        const char* backendUnavailableBody =
            R"({"success":false,"error":"release backend unavailable","errorCode":"releaseBackendUnavailable"})";
        const auto unavailable = chordengine::updates::parseUpdateResponse(
            parseBody(backendUnavailableBody), platform);
        require(!unavailable.ok, "503: the envelope is a failure");

        chordengine::updates::TransportReply serviceReply;
        serviceReply.transportOk = true;
        serviceReply.status = 503;
        require(chordengine::updates::shouldRetryCheck(serviceReply, unavailable),
                "503: a 503 is retried");

        serviceReply.status = 500;
        require(chordengine::updates::shouldRetryCheck(serviceReply, unavailable),
                "503: the server's own releaseBackendUnavailable code is retried too");

        const auto plainFailure = chordengine::updates::parseUpdateResponse(
            parseBody(R"({"success":false,"error":"boom","errorCode":"internalReleaseError"})"),
            platform);
        serviceReply.status = 500;
        require(!chordengine::updates::shouldRetryCheck(serviceReply, plainFailure),
                "503: any other failure is not retried");

        chordengine::updates::TransportReply offlineReply;
        require(!chordengine::updates::shouldRetryCheck(offlineReply, plainFailure),
                "503: an unreachable service is never retried in a loop");

        // Through the real service: one retry, then the published live answer.
        auto retrying = std::make_unique<FakeTransport>();
        retrying->scripted = { { 503, juce::String(backendUnavailableBody), true, false },
                               { 200, juce::String(liveNoPublishedRelease), true, false } };
        auto* retryingRaw = retrying.get();
        UpdateService retryService(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("retry"), std::move(retrying));
        const auto retried = retryService.performCheck();
        require(retryingRaw->calls == 2, "503: exactly one retry is attempted");
        require(retried.status == UpdateStatus::noPublishedRelease,
                "503: the retry publishes the real answer");

        // A 500 is not retried and reports an error, never success.
        auto single = std::make_unique<FakeTransport>();
        single->scripted = { { 500, juce::String(R"({"success":false,"errorCode":"internalReleaseError"})"),
                               true, false } };
        auto* singleRaw = single.get();
        UpdateService singleService(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("no-retry"), std::move(single));
        const auto failed = singleService.performCheck();
        require(singleRaw->calls == 1, "503: a non-503 failure is attempted exactly once");
        require(failed.status == UpdateStatus::error, "503: a failure is an error, not a success");
        require(failed.status != UpdateStatus::upToDate,
                "503: a failure never becomes 'Up to date'");
        casePassed();
    }

    // =================================================================
    // 10. Offline: with no cached answer it is an honest error; with one the
    //     last answer is kept and labelled.
    // =================================================================
    {
        auto nothingScripted = std::make_unique<FakeTransport>();
        UpdateService coldService(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("offline-cold"), std::move(nothingScripted));
        const auto cold = coldService.performCheck();
        require(cold.status == UpdateStatus::error, "offline: no cache means an error state");
        require(cold.status != UpdateStatus::upToDate, "offline: never 'Up to date'");
        require(cold.detail.contains("Offline"), "offline: the reason is stated");
        require(chordengine::updates::latestVersionLine(cold) == "Latest version  -",
                "offline: with no answer the Latest row stays a placeholder");
        require(chordengine::updates::statusLine(cold)
                    == "Status  Offline - could not reach the update service",
                "offline: the Status row states the offline state");

        // Seed the cache with a real answer, then go offline.
        const auto cacheFile = temporaryCacheFile("offline-warm");
        {
            auto online = std::make_unique<FakeTransport>();
            online->scripted = { { 200, juce::String(liveNoPublishedRelease), true, false } };
            UpdateService seeding(chordengine::updates::currentBuildVersionString,
                              cacheFile, std::move(online));
            const auto seeded = seeding.performCheck();
            require(seeded.status == UpdateStatus::noPublishedRelease, "offline: the seed succeeded");
            require(cacheFile.existsAsFile(), "offline: the successful answer is cached");
        }

        auto offline = std::make_unique<FakeTransport>(); // empty script: unreachable
        UpdateService warmService(chordengine::updates::currentBuildVersionString,
                              cacheFile, std::move(offline));
        const auto warm = warmService.performCheck();
        require(warm.status == UpdateStatus::offlineCached,
                "offline: the cached answer keeps the page honest");
        require(warm.cached, "offline: the answer is marked as cached");
        require(chordengine::updates::latestVersionLine(warm)
                    == "Latest version  No published release",
                "offline: the cached Latest row is the real one");
        require(chordengine::updates::statusLine(warm)
                    == "Status  Offline - showing cached result",
                "offline: the Status row says the cached answer is shown");
        require(!chordengine::updates::statusLine(warm).contains("Up to date"),
                "offline: the cached answer is not relabelled as up to date");
        casePassed();
    }

    // =================================================================
    // 11. The cached successful response: its contents, and it is shown
    //     immediately (marked cached) before the one fresh check.
    // =================================================================
    {
        const auto cacheFile = temporaryCacheFile("cache-round-trip");
        {
            auto online = std::make_unique<FakeTransport>();
            online->scripted = { { 200, juce::String(liveNoPublishedRelease), true, false } };
            UpdateService seeding(chordengine::updates::currentBuildVersionString,
                              cacheFile, std::move(online));
            seeding.performCheck();
        }

        UpdateCache cache(cacheFile);
        const auto loaded = cache.load();
        require(loaded.valid, "cache: a successful answer round-trips");
        require(loaded.product == "chordengine", "cache: the product is recorded");
        require(loaded.platform == platform, "cache: the platform is recorded");
        require(loaded.serverStatus == "noPublishedRelease", "cache: the status is recorded");
        require(loaded.latestVersion.isEmpty(), "cache: a null latest version stays empty");
        require(loaded.checkedAtMs > 0, "cache: the timestamp is recorded");

        // Only the normalised public fields are stored. Nothing account- or
        // licence-related is ever written.
        const auto cacheText = cacheFile.loadFileAsString();
        for (const auto* forbidden : { "\"token\"", "\"password\"", "\"secret\"",
                                       "\"credential\"", "\"authorization\"", "\"trial\"",
                                       "\"entitlement\"", "\"auth\"" })
            require(!cacheText.containsIgnoreCase(forbidden),
                    juce::String("cache: it must never store ") + forbidden);
        require(cacheText.contains("checked_at"), "cache: the timestamp field is present");

        // A fresh launch shows the cached answer AT ONCE (marked cached) and
        // then performs exactly one fresh check.
        auto offline = std::make_unique<FakeTransport>();
        auto* offlineRaw = offline.get();
        UpdateService restarted(chordengine::updates::currentBuildVersionString,
                              cacheFile, std::move(offline));
        ViewRecorder recorder;
        recorder.attach(restarted);
        restarted.ensureStartupCheck();
        pumpMessages(300);

        require(recorder.statuses.size() >= 2,
                "cache: the cached answer is published before the fresh check");
        require(recorder.statuses.front() == "Status  No published release (cached)",
                "cache: the first published state is the cached answer, marked cached");
        require(recorder.statuses[1] == "Status  Checking...",
                "cache: the fresh automatic check follows it");
        require(recorder.statuses.back() == "Status  Offline - showing cached result",
                "cache: the refresh failure keeps the cached answer");
        require(offlineRaw->calls == 1, "cache: exactly one automatic check per launch");

        // A second call can never start another automatic check.
        restarted.ensureStartupCheck();
        pumpMessages(100);
        require(offlineRaw->calls == 1,
                "cache: the automatic check happens at most once per launch");
        require(restarted.startupCheckDone(), "cache: the startup check is remembered");
        casePassed();
    }

    // =================================================================
    // 12. Update status transitions, and an explicit click may check again.
    // =================================================================
    {
        auto transport = std::make_unique<FakeTransport>();
        transport->scripted = { { 200, juce::String(liveNoPublishedRelease), true, false } };
        auto* raw = transport.get();
        UpdateService service(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("transitions"), std::move(transport));
        ViewRecorder recorder;
        recorder.attach(service);

        require(service.view().status == UpdateStatus::idle, "transitions: a fresh client is idle");

        service.checkNow();
        require(service.view().status == UpdateStatus::checking,
                "transitions: a click publishes the checking state at once");
        require(service.isCheckInFlight(), "transitions: the check is in flight");
        pumpMessages(300);
        require(!service.isCheckInFlight(), "transitions: the check finishes");
        require(service.view().status == UpdateStatus::noPublishedRelease,
                "transitions: the real answer is published");
        require(raw->calls == 1, "transitions: one request per check");
        require(recorder.statuses.size() == 2, "transitions: checking then the answer");
        require(recorder.statuses[0] == "Status  Checking...",
                "transitions: the first published state is checking");
        require(recorder.statuses[1] == "Status  No published release",
                "transitions: the second is the honest answer");

        // An explicit check is allowed again, and stacking is impossible.
        service.checkNow();
        require(service.isCheckInFlight(), "transitions: a second explicit check runs");
        service.checkNow();
        service.checkNow();
        require(service.view().status == UpdateStatus::checking,
                "transitions: a stacked click keeps the checking state");
        pumpMessages(300);
        require(raw->calls == 2, "transitions: stacked clicks never queue extra requests");
        require(!service.isCheckInFlight(), "transitions: the client settles in-flight free");
        casePassed();
    }

    // =================================================================
    // 13. The check reads no account or licence state.
    // =================================================================
    {
        // The request itself: only the product, the build version and the
        // platform are ever sent, on an unauthenticated GET.
        auto transport = std::make_unique<FakeTransport>();
        transport->scripted = { { 200, juce::String(liveNoPublishedRelease), true, false } };
        auto* raw = transport.get();
        UpdateService service(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("anonymous"), std::move(transport));
        const auto result = service.performCheck();

        require(raw->requestUrls.size() == 1, "anonymous: one request was made");
        const auto url = raw->requestUrls[0];
        const auto documentedUrl = chordengine::updates::buildUpdateUrl(
            "https://wfpeajmdojcjqyrsnxbk.supabase.co/", "chordengine",
            chordengine::updates::currentBuildVersionString, platform);
        require(url == documentedUrl,
                "anonymous: the request is exactly the documented URL (got '" + url
                    + "', expected '" + documentedUrl + "')");
        require(url.contains("product=chordengine"), "anonymous: the product is sent");
        require(!url.contains("token"), "anonymous: no token is sent");
        require(!url.contains("authorization"), "anonymous: no authorization header value is sent");
        require(!url.contains("api_key") && !url.contains("apikey"),
                "anonymous: no API key is sent");

        // The module's own sources carry no account wiring at all.
        for (const auto* file : { "Source/Updates/UpdateTypes.h", "Source/Updates/UpdateTypes.cpp",
                                  "Source/Updates/UpdateResponse.h", "Source/Updates/UpdateResponse.cpp",
                                  "Source/Updates/UpdateCache.h", "Source/Updates/UpdateCache.cpp",
                                  "Source/Updates/UpdateTransport.h", "Source/Updates/UpdateTransport.cpp",
                                  "Source/Updates/UpdateService.h", "Source/Updates/UpdateService.cpp" })
        {
            const auto source = readSourceFile(file);
            for (const auto* forbidden : { "AuthService", "authorization", "bearer", "password",
                                           "secret", "credential", "hasToken", "authToken",
                                           "access_token", "refresh_token", "LicensingModel",
                                           "Snapshot", "entitlements" })
                require(!source.containsIgnoreCase(forbidden),
                        juce::String("anonymous: ") + file + " must not reference " + forbidden);
        }

        // And the check needs no account: it performs identically with no
        // licensing object in scope at all (this whole suite is the proof).
        require(result.status == UpdateStatus::noPublishedRelease,
                "anonymous: the check succeeds without any account state");
        casePassed();
    }

    // =================================================================
    // 14. The audio path performs no update network work.
    // =================================================================
    {
        const auto processorSource = readSourceFile("Source/PluginProcessor.cpp");
        const auto processorHeader = readSourceFile("Source/PluginProcessor.h");

        require(!processorHeader.contains("Updates/"),
                "realtime isolation: the processor header includes no update code");

        const auto blockStart = processorSource.indexOf("void ChordEngineAudioProcessor::processBlock");
        require(blockStart >= 0, "realtime isolation: processBlock must exist");
        const auto tail = processorSource.substring(blockStart);
        const auto relativeEnd = tail.indexOf(
            "juce::AudioProcessorEditor* ChordEngineAudioProcessor::createEditor");
        require(relativeEnd > 0, "realtime isolation: processBlock bounds must be found");
        const auto processBlockBody = tail.substring(0, relativeEnd);

        for (const auto* token : { "UpdateService", "updateService", "updates::", "UpdateTransport",
                                   "juce::URL", "createInputStream", "ThreadPool", "callAsync",
                                   "MessageManager", "UpdateCache" })
            require(!processBlockBody.contains(token),
                    juce::String("realtime isolation: processBlock must not reference '") + token + "'");

        // The update client is reached only through the editor.
        require(!processorSource.contains("UpdateService"),
                "realtime isolation: the processor never names the update client");
        casePassed();
    }

    // =================================================================
    // 15. The client owns exactly one worker, however often it is asked.
    // =================================================================
    {
        auto transport = std::make_unique<FakeTransport>();
        transport->scripted = { { 200, juce::String(liveNoPublishedRelease), true, false } };
        UpdateService service(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("workers"), std::move(transport));
        require(service.workerCount() == 1, "workers: exactly one background worker is created");

        for (int i = 0; i < 5; ++i)
            service.checkNow();
        pumpMessages(300);
        require(service.workerCount() == 1, "workers: repeated checks never add a worker");
        require(!service.isCheckInFlight(), "workers: the client settles in-flight free");

        // The worker provision is a single, literal job of the source.
        const auto header = readSourceFile("Source/Updates/UpdateService.h");
        require(header.contains("juce::ThreadPool pool_ { 1 }"),
                "workers: the pool is declared with exactly one thread");
        require(header.contains("MessageManager"), "workers: results are delivered on the message thread");
        const auto source = readSourceFile("Source/Updates/UpdateService.cpp");
        require(source.contains("MessageManager::callAsync"),
                "workers: the result is handed back with callAsync");
        casePassed();
    }

    // =================================================================
    // 16. The actual build version is the one that is sent and displayed.
    // =================================================================
    {
        const juce::String expected(JucePlugin_VersionString);
        const juce::String reportedBuildVersion(chordengine::updates::currentBuildVersionString);
        require(reportedBuildVersion == expected,
                "build version: it comes from the build metadata");
        require(expected == "0.1.0", "build version: this development build reports 0.1.0");
        require(expected != "0.4.0",
                "build version: the development build must not pretend to be 0.4.0");
        require(chordengine::updates::isValidSemver(expected),
                "build version: it is a valid semver");
        require(reportedBuildVersion != juce::String("0.4.0"),
                "build version: 0.4.0 is not hardcoded anywhere in the client");

        auto transport = std::make_unique<FakeTransport>();
        transport->scripted = { { 200, juce::String(liveNoPublishedRelease), true, false } };
        auto* raw = transport.get();
        UpdateService service(chordengine::updates::currentBuildVersionString,
                              temporaryCacheFile("build-version"), std::move(transport));
        const auto view = service.performCheck();

        require(raw->requestUrls.size() == 1, "build version: the check was made");
        require(raw->requestUrls[0].contains("current=" + expected),
                "build version: the request carries the actual build version");
        require(raw->requestUrls[0].contains("platform=" + platform),
                "build version: the platform is always explicit");
        require(view.currentVersion == expected, "build version: the view reports the build version");
        require(chordengine::updates::currentVersionLine(view) == "Current version  0.1.0",
                "build version: the INFO row shows the actual build version");
        require(chordengine::updates::statusLine(view) == "Status  No published release",
                "build version: and the honest status of the live service");
        casePassed();
    }

    constexpr int expectedCaseCount = 16;
    require(casesPassed == expectedCaseCount,
            "every update case must run: expected " + juce::String(expectedCaseCount)
                + ", executed " + juce::String(casesPassed));

    std::cout << "ChordEngineUpdateTests: " << casesPassed << "/" << expectedCaseCount
              << " update client cases passed\n";
    return EXIT_SUCCESS;
}
