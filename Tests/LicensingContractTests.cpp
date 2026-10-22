// Native licensing — THE VERIFIED BACKEND CONTRACT.
//
// ChordEngine Native and ChordEngine FL are the SAME commercial product, and
// the backend already defines both routes to it
// (supabase/functions/vyre-plugin-auth + _shared/pluginEntitlements.ts):
//
//     plugin_entitled = (an active, paid Music-Prod+ subscription that COVERS
//                        chordengine)
//                    || (an active licence key for chordengine AND a slot
//                        held by THIS device)
//
// `subscribed` is the user's RAW Music-Prod+ membership. The server keeps it
// deliberately product-agnostic, so it can express neither "this product is
// free" nor "this product is not part of Music-Prod+"; it is DISPLAY ONLY and
// is never the product-access decision.
//
// These cases are written against the exact wire shapes the function returns
// (vyre-plugin-auth/contract.ts) and the exact activation contract
// (vyre-plugin-auth/license.ts). Every reply is produced by a local mock: no
// test here performs a network request, and nothing contacts the production
// endpoint.

#include "Licensing/LicensingBoundary.h"
#include "Licensing/LicensingModel.h"
#include "Licensing/LicensingServices.h"
#include "Licensing/LicensingTypes.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
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

using chordengine::licensing::CachedEntitlement;
using chordengine::licensing::HttpReply;
using chordengine::licensing::LicensingBoundary;
using chordengine::licensing::Snapshot;

constexpr double dayMs = 1000.0 * 60.0 * 60.0 * 24.0;

// ----------------------------------------------------------- test doubles
// Records every request and hands the test full control of when (and whether)
// a reply is delivered - the proof that the boundary never blocks.
class MockAuthService final : public chordengine::licensing::AuthService
{
public:
    void start(const juce::String& deviceName, const juce::String& pluginVersion,
               const juce::String& product, const juce::String& deviceId,
               Callback callback) override
    {
        ++startCalls;
        lastDeviceName = deviceName;
        lastPluginVersion = pluginVersion;
        lastProduct = product;
        lastDeviceId = deviceId;
        pendingStart = std::move(callback);
    }

    void poll(const juce::String& deviceCode, Callback callback) override
    {
        ++pollCalls;
        lastPolledCode = deviceCode;
        pendingPoll = std::move(callback);
    }

    void entitlements(const juce::String& token, Callback callback) override
    {
        ++entitlementCalls;
        lastToken = token;
        pendingEntitlements = std::move(callback);
    }

    void logout(const juce::String& token, Callback callback) override
    {
        ++logoutCalls;
        lastToken = token;
        pendingLogout = std::move(callback);
    }

    void activateLicense(const juce::String& token, const juce::String& licenseKey,
                        const juce::String& deviceId, const juce::String& deviceLabel,
                        const juce::String& platform, const juce::String& pluginVersion,
                        Callback callback) override
    {
        ++activateCalls;
        lastToken = token;
        lastLicenseKey = licenseKey;
        lastActivateDeviceId = deviceId;
        lastDeviceLabel = deviceLabel;
        lastPlatform = platform;
        lastActivatePluginVersion = pluginVersion;
        pendingActivate = std::move(callback);
    }

    int startCalls = 0;
    int pollCalls = 0;
    int entitlementCalls = 0;
    int logoutCalls = 0;
    int activateCalls = 0;
    juce::String lastDeviceName;
    juce::String lastPluginVersion;
    juce::String lastProduct;
    juce::String lastDeviceId;
    juce::String lastPolledCode;
    juce::String lastToken;
    juce::String lastLicenseKey;
    juce::String lastActivateDeviceId;
    juce::String lastDeviceLabel;
    juce::String lastPlatform;
    juce::String lastActivatePluginVersion;
    Callback pendingStart;
    Callback pendingPoll;
    Callback pendingEntitlements;
    Callback pendingLogout;
    Callback pendingActivate;
};

class MockUpdateService final : public chordengine::licensing::UpdateService
{
public:
    Result check() override
    {
        ++calls;
        return next;
    }

    Result next {};
    int calls = 0;
};

// --------------------------------------------------------------- fixtures
juce::File storageDirectory(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("ChordEngine-LicensingContractTests")
                   .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

juce::File authFileFor(const juce::String& name)
{
    return storageDirectory(name).getChildFile("auth.json");
}

juce::var object(const std::vector<std::pair<const char*, juce::var>>& fields)
{
    juce::DynamicObject::Ptr dynamic = new juce::DynamicObject();
    for (const auto& field : fields)
        dynamic->setProperty(field.first, field.second);
    return juce::var(dynamic.get());
}

// The `entitlements` response body, built exactly as vyre-plugin-auth's
// contract.ts `entitlementsBody()` builds it.
juce::var entitlementBody(bool pluginEntitled, bool subscribed, bool coversProduct,
                          const juce::String& licenseStatus, bool licenseAccountActive,
                          bool licenseDeviceActive, int devicesUsed, int deviceLimit)
{
    return object({
        { "ok", true },
        { "product", juce::String(chordengine::licensing::productSlug) },
        { "subscribed", subscribed },
        { "plugin_entitled", pluginEntitled },
        { "subscription",
          object({ { "active", subscribed }, { "covers_product", coversProduct } }) },
        { "license",
          object({ { "active", licenseAccountActive && licenseDeviceActive },
                   { "status", licenseStatus },
                   { "account_active", licenseAccountActive },
                   { "device_active", licenseDeviceActive },
                   { "devices_used", devicesUsed },
                   { "device_limit", deviceLimit },
                   { "expires_at", juce::var() } }) },
        { "packs", juce::var(juce::Array<juce::var>()) },
    });
}

// The flat, pre-`plugin_entitled` shape: no nested blocks at all.
juce::var flatEntitlementBody(bool subscribed)
{
    return object({ { "ok", true }, { "subscribed", subscribed } });
}

// One MOCK 200 answer, delivered the way the real service delivers it.
void deliverEntitlements(LicensingBoundary& boundary,
                         const std::vector<std::pair<const char*, juce::var>>& fields)
{
    boundary.deliverEntitlementsReply(HttpReply { 200, object(fields), true });
}

// A signed-in session with a verified subscription, ready for a further
// answer. Returns the auth service so the test can inspect the requests.
struct SignedInSession
{
    std::unique_ptr<MockAuthService> auth;
    std::unique_ptr<MockUpdateService> update;
    std::unique_ptr<LicensingBoundary> boundary;
    juce::File authFile;
};

SignedInSession signedIn(const juce::String& name)
{
    SignedInSession session;
    session.auth = std::make_unique<MockAuthService>();
    session.update = std::make_unique<MockUpdateService>();
    session.authFile = authFileFor(name);
    session.boundary = std::make_unique<LicensingBoundary>(
        *session.auth, *session.update, session.authFile);
    session.boundary->initialise();

    session.boundary->signInButtonClicked();
    session.auth->pendingStart(HttpReply { 200,
        object({ { "device_code", "dev-contract" },
                 { "user_code", "ABCD-EFGH" },
                 { "product", juce::String(chordengine::licensing::productSlug) },
                 { "verification_url_complete", "https://music-prod.com/plugin/link?code=ABCD-EFGH" } }),
        true });

    // The poll cadence is the reference's ~0.8 s at 10 Hz; drive it so the
    // approval can be delivered.
    for (int tick = 0; tick < 12 && session.auth->pollCalls == 0; ++tick)
        session.boundary->tick();
    require(session.auth->pollCalls >= 1, "harness: the device code is polled");

    session.auth->pendingPoll(HttpReply { 200,
        object({ { "status", "approved" },
                 { "token", "opaque-contract-token" },
                 { "display_name", "Martin" } }),
        true });

    return session;
}
} // namespace

int main()
{
    // =================================================================
    // 1. `plugin_entitled: true` unlocks ChordEngine for a Music-Prod+
    //    customer.
    // =================================================================
    {
        auto session = signedIn("subscriber");
        auto& boundary = *session.boundary;

        require(!boundary.snapshot().licensed,
                "subscriber: no access before the product-scoped answer");

        deliverEntitlements(boundary,
            { { "ok", true },
              { "product", juce::String(chordengine::licensing::productSlug) },
              { "subscribed", true },
              { "plugin_entitled", true },
              { "subscription", object({ { "active", true }, { "covers_product", true } }) },
              { "license", object({ { "active", false },
                                    { "status", "none" },
                                    { "account_active", false },
                                    { "device_active", false },
                                    { "devices_used", 0 },
                                    { "device_limit", 3 },
                                    { "expires_at", juce::var() } }) } });

        require(boundary.snapshot().licensed,
                "subscriber: plugin_entitled unlocks the full version");
        require(boundary.snapshot().cached.subscriptionEntitles,
                "subscriber: the answer is attributed to the subscription route");
        require(!boundary.expiredGateActive(),
                "subscriber: an entitled session is never gated");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  ACTIVE",
                "subscriber: the account line reports the verified membership");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Music-Prod+ verified",
                "subscriber: the licence line names the subscription route");
        casePassed();
    }

    // =================================================================
    // 2. `plugin_entitled: true` unlocks ChordEngine for an activated
    //    lifetime-licence customer - with NO subscription at all.
    // =================================================================
    {
        auto session = signedIn("license-holder");
        auto& boundary = *session.boundary;

        boundary.deliverEntitlementsReply(HttpReply { 200,
            entitlementBody(true, false, true, "active", true, true, 1, 3), true });

        require(boundary.snapshot().licensed,
                "licence holder: plugin_entitled unlocks the full version with no subscription");
        require(!boundary.snapshot().cached.subscribed,
                "licence holder: the raw membership is honestly false");
        require(boundary.snapshot().cached.licenseEntitles
                    && boundary.snapshot().cached.licenseDeviceActive,
                "licence holder: the device-bound licence route is what grants access");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  NOT ACTIVE",
                "licence holder: the account line never claims a subscription they do not have");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Licence active on this computer (1 of 3 computers)",
                "licence holder: the licence line names the route and the real slot usage");
        require(chordengine::licensing::trialStatusText(boundary.snapshot())
                    == "Licence active - full access",
                "licence holder: the trial line names the licence route, not Music-Prod+");
        casePassed();
    }

    // =================================================================
    // 3. `subscribed: true` ALONE must not grant access when the
    //    product-scoped field is false.
    // =================================================================
    {
        auto session = signedIn("membership-only");
        auto& boundary = *session.boundary;

        // The over-grant this whole rule exists to prevent: a live
        // membership with the product-scoped decision false. A subscription
        // DOES cover chordengine today, so the real server cannot emit this
        // combination yet - but the client must obey the gate field rather
        // than re-deriving the decision from the membership, which is exactly
        // how a future per-product change is handled correctly instead of
        // silently unlocking.
        boundary.deliverEntitlementsReply(HttpReply { 200,
            entitlementBody(false, true, true, "none", false, false, 0, 3), true });

        require(!boundary.snapshot().licensed,
                "membership only: subscribed alone must never grant ChordEngine");
        require(boundary.snapshot().cached.subscribed,
                "membership only: the membership is still reported for the account display");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  ACTIVE",
                "membership only: the account line shows the membership truthfully");
        require(!boundary.snapshot().cached.subscriptionEntitles,
                "membership only: the client never re-derives the decision from the membership");

        // ... and an explicit false answer withdraws access that a previous
        // answer had granted.
        deliverEntitlements(boundary,
            { { "ok", true },
              { "subscribed", true },
              { "plugin_entitled", true },
              { "subscription", object({ { "active", true }, { "covers_product", true } }) } });
        require(boundary.snapshot().licensed, "membership only: the covered case does unlock");
        boundary.deliverEntitlementsReply(HttpReply { 200,
            entitlementBody(false, true, true, "none", false, false, 0, 3), true });
        require(!boundary.snapshot().licensed,
                "membership only: an explicit plugin_entitled false withdraws access");
        casePassed();
    }

    // =================================================================
    // 4. A missing or malformed `plugin_entitled` never grants access.
    // =================================================================
    {
        auto session = signedIn("malformed");
        auto& boundary = *session.boundary;

        // The exact legacy shape: a 200 that carries only `subscribed`.
        boundary.deliverEntitlementsReply(HttpReply { 200,
            flatEntitlementBody(true), true });
        require(!boundary.snapshot().licensed,
                "malformed: a 200 without plugin_entitled grants nothing");
        require(boundary.snapshot().cached.subscribed,
                "malformed: the membership is still recorded for display");

        // Shapes that a lenient reader could mistake for true.
        for (const auto& bogus : { juce::var("true"), juce::var("yes"), juce::var(1),
                                   juce::var(2), juce::var(), juce::var::undefined() })
        {
            deliverEntitlements(boundary,
                { { "ok", true }, { "subscribed", true }, { "plugin_entitled", bogus } });
            require(!boundary.snapshot().licensed,
                    "malformed: only a real JSON boolean true may grant access");
        }

        // A malformed answer must not REFRESH a valid cache either: the gate
        // stays where the last verified answer left it.
        deliverEntitlements(boundary,
            { { "ok", true },
              { "subscribed", true },
              { "plugin_entitled", true },
              { "subscription", object({ { "active", true }, { "covers_product", true } }) } });
        const auto verifiedAt = boundary.snapshot().verifiedAtMs;
        require(verifiedAt > 0, "malformed: a verified answer is timestamped");
        boundary.deliverEntitlementsReply(HttpReply { 200,
            flatEntitlementBody(true), true });
        require(boundary.snapshot().verifiedAtMs == verifiedAt,
                "malformed: a reply without the gate never extends the verified window");
        require(boundary.snapshot().licensed,
                "malformed: the already-verified answer keeps its own bounded window");
        casePassed();
    }

    // =================================================================
    // 5. A revoked, expired or rejected licence never unlocks through
    //    stale account state.
    // =================================================================
    {
        auto session = signedIn("revocation");
        auto& boundary = *session.boundary;
        const auto authFile = session.authFile;

        boundary.deliverEntitlementsReply(HttpReply { 200,
            entitlementBody(true, false, true, "active", true, true, 1, 3), true });
        require(boundary.snapshot().licensed, "revocation: the licence route grants access first");
        require(authFile.existsAsFile(), "revocation: the verified answer is persisted");

        // An administrator revokes the key: the server answers with the gate
        // false and says why. A revoked row is not an active licence row, so
        // account_active is false too - exactly as licenseStatusForRows()
        // computes it.
        boundary.deliverEntitlementsReply(HttpReply { 200,
            entitlementBody(false, false, true, "revoked", false, false, 0, 3), true });
        require(!boundary.snapshot().licensed,
                "revocation: a revoked licence withdraws access in the same transition");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Licence revoked - contact Music-Prod support",
                "revocation: the reason is stated, never a generic expiry");

        // Neither a transport failure nor a restart may restore it: there is
        // no local entitlement to restore.
        boundary.deliverEntitlementsReply(HttpReply { 0, {}, false });
        require(!boundary.snapshot().licensed,
                "revocation: a network failure never restores a withdrawn licence");

        auto restarted = std::make_unique<LicensingBoundary>(
            *session.auth, *session.update, authFile);
        restarted->initialise();
        require(!restarted->snapshot().licensed,
                "revocation: a restarted client cannot restore access from its cache");
        require(!restarted->snapshot().cached.entitled,
                "revocation: the persisted cache no longer claims an entitlement");

        // The same for a token the server rejects: 401 clears the token AND
        // the remembered answer in one transition.
        deliverEntitlements(boundary,
            { { "ok", true },
              { "subscribed", true },
              { "plugin_entitled", true },
              { "subscription", object({ { "active", true }, { "covers_product", true } }) } });
        require(boundary.snapshot().licensed, "revocation: re-granted by a covered subscription");
        boundary.deliverEntitlementsReply(HttpReply { 401, {}, true });
        require(!boundary.snapshot().licensed && !boundary.snapshot().hasToken,
                "revocation: a 401 drops the token and the entitlement together");
        casePassed();
    }

    // =================================================================
    // 6. Network failures and cached entitlement follow the DOCUMENTED
    //    per-route grace windows (14 days subscription, 7 days licence).
    // =================================================================
    {
        require(chordengine::licensing::subscriptionGraceDays == 14.0,
                "grace: the documented subscription window is 14 days");
        require(chordengine::licensing::licenseGraceDays == 7.0,
                "grace: the documented device-bound licence window is 7 days");
        require(chordengine::licensing::licenseGraceDays
                    <= chordengine::licensing::subscriptionGraceDays,
                "grace: the licence window never exceeds the subscription window");

        const auto now = static_cast<double>(juce::Time::currentTimeMillis());

        // One cached answer, written the way the client persists it. The
        // device id is a real, valid one so the licence route is honoured.
        const auto writeCache = [&now](const juce::String& name, bool subscriptionRoute,
                                       bool licenseRoute, double ageDays)
        {
            auto file = authFileFor(name);
            juce::DynamicObject::Ptr cached = new juce::DynamicObject();
            cached->setProperty("token", "cached-token");
            cached->setProperty("display_name", "Martin");
            cached->setProperty("device_id", chordengine::licensing::newDeviceId());
            cached->setProperty("subscribed", subscriptionRoute);
            cached->setProperty("plugin_entitled", true);
            cached->setProperty("subscription_entitles", subscriptionRoute);
            cached->setProperty("license_entitles", licenseRoute);
            cached->setProperty("license_account_active", licenseRoute);
            cached->setProperty("license_device_active", licenseRoute);
            cached->setProperty("license_status", licenseRoute
                ? juce::String(chordengine::licensing::licenseStatusActive)
                : juce::String(chordengine::licensing::licenseStatusNone));
            cached->setProperty("devices_used", licenseRoute ? 1 : 0);
            cached->setProperty("device_limit", 3);
            cached->setProperty("verified_at",
                                static_cast<juce::int64>(now - ageDays * dayMs));
            file.replaceWithText(juce::JSON::toString(juce::var(cached.get())));
            return file;
        };

        const auto offlineLicensed = [](const juce::File& file)
        {
            MockAuthService auth;
            MockUpdateService update;
            LicensingBoundary boundary(auth, update, file);
            boundary.initialise();
            return boundary.snapshot().licensed;
        };

        // Subscription route: honoured up to 14 days, never longer.
        require(offlineLicensed(writeCache("sub-13d", true, false, 13.0)),
                "grace: a subscription answer is honoured 13 days offline");
        require(!offlineLicensed(writeCache("sub-15d", true, false, 15.0)),
                "grace: a subscription answer is NOT honoured 15 days offline");

        // Licence route: honoured up to 7 days, never longer. This is the
        // route a deactivation has to be able to withdraw.
        require(offlineLicensed(writeCache("lic-6d", false, true, 6.0)),
                "grace: a licence answer is honoured 6 days offline");
        require(!offlineLicensed(writeCache("lic-8d", false, true, 8.0)),
                "grace: a licence answer is NOT honoured 8 days offline");

        // A cache that granted nothing is honoured for no time at all.
        require(!offlineLicensed(writeCache("not-entitled", false, false, 0.0)),
                "grace: a cache that names no granting route grants nothing offline");

        // A clock that moved backwards never extends the window.
        {
            auto file = authFileFor("clock-back");
            juce::DynamicObject::Ptr cached = new juce::DynamicObject();
            cached->setProperty("token", "cached-token");
            cached->setProperty("device_id", chordengine::licensing::newDeviceId());
            cached->setProperty("plugin_entitled", true);
            cached->setProperty("subscribed", true);
            cached->setProperty("subscription_entitles", true);
            cached->setProperty("verified_at", static_cast<juce::int64>(now + 5.0 * dayMs));
            file.replaceWithText(juce::JSON::toString(juce::var(cached.get())));
            require(!offlineLicensed(file),
                    "grace: a backwards clock invalidates the cache instead of extending it");
        }

        // A live transport failure keeps the verified access within the same
        // route window - and never invents a new one.
        {
            auto session = signedIn("transport-failure");
            auto& boundary = *session.boundary;
            boundary.deliverEntitlementsReply(HttpReply { 200,
                entitlementBody(true, false, true, "active", true, true, 1, 3), true });
            const auto verifiedAt = boundary.snapshot().verifiedAtMs;

            boundary.deliverEntitlementsReply(HttpReply { 0, {}, false });
            require(boundary.snapshot().licensed,
                    "grace: a network failure never downgrades a verified licence holder");
            require(boundary.snapshot().verifiedAtMs == verifiedAt,
                    "grace: a network failure never refreshes the window either");

            boundary.deliverEntitlementsReply(HttpReply { 503, {}, true });
            require(boundary.snapshot().licensed,
                    "grace: a server error follows the same rule as a transport failure");

            // Past its own window, that same cached answer expires.
            boundary.deliverEntitlementsReply(HttpReply { 200,
                                                    entitlementBody(true, true, true, "none", false,
                                                                    false, 0, 3),
                                                    true });
            require(boundary.snapshot().licensed,
                    "grace: the subscription answer re-grants access");
        }
        casePassed();
    }

    // =================================================================
    // 7. The explicit product identifier stays `chordengine`, independent
    //    of any display name.
    // =================================================================
    {
        auto session = signedIn("product-slug");
        require(session.auth->lastProduct == chordengine::licensing::productSlug,
                "product: the pairing request names the product explicitly");
        require(juce::String(chordengine::licensing::productSlug) == "chordengine",
                "product: the slug is the backend allowlisted identifier");
        require(session.auth->lastDeviceName == "ChordEngine",
                "product: the display device name is a separate, cosmetic value");
        require(session.auth->lastProduct != session.auth->lastDeviceName,
                "product: the product is never the display name");
        require(chordengine::licensing::isDeviceIdFormat(session.auth->lastDeviceId),
                "product: the pairing request carries a valid device id");

        // The same identifier survives a restart: the pairing request keeps
        // sending the explicit slug.
        auto restarted = std::make_unique<LicensingBoundary>(
            *session.auth, *session.update, session.authFile);
        restarted->initialise();
        require(restarted->deviceId() == session.boundary->deviceId(),
                "product: the device id is stable across restarts");
        casePassed();
    }

    // =================================================================
    // 8. A failed licence activation never corrupts an existing valid
    //    Music-Prod+ entitlement - in either direction.
    // =================================================================
    {
        auto session = signedIn("activation-failure");
        auto& boundary = *session.boundary;
        auto& auth = *session.auth;

        deliverEntitlements(boundary,
            { { "ok", true },
              { "subscribed", true },
              { "plugin_entitled", true },
              { "subscription", object({ { "active", true }, { "covers_product", true } }) } });
        require(boundary.snapshot().licensed, "activation failure: the subscriber is entitled");

        // The key is not ours / is spent / is revoked: a server ANSWER about
        // the key, which must leave the subscription route exactly as it was.
        for (const auto* verdict : { "invalid", "revoked", "expired" })
        {
            boundary.activateLicenseKey("PC-7QF2-3MHD-NX4T-9BKR");
            require(auth.activateCalls > 0, "activation failure: the key reaches the service");
            require(auth.lastLicenseKey == "PC-7QF2-3MHD-NX4T-9BKR",
                    "activation failure: the typed key is forwarded verbatim");
            require(chordengine::licensing::isDeviceIdFormat(auth.lastActivateDeviceId),
                    "activation failure: a valid device id is sent (the endpoint requires it)");
            require(boundary.snapshot().activationPending,
                    "activation failure: the request reads as pending while it is in flight");

            auth.pendingActivate(HttpReply { 200,
                object({ { "ok", false },
                         { "status", juce::String(verdict) },
                         { "product", juce::String(chordengine::licensing::productSlug) },
                         { "message", "This licence has been revoked." } }),
                true });
            require(!boundary.snapshot().activationPending,
                    "activation failure: the answer ends the pending state");

            require(boundary.snapshot().licensed,
                    juce::String("activation failure: a ") + verdict
                        + " key never withdraws a valid Music-Prod+ entitlement");
            require(boundary.snapshot().cached.subscribed,
                    "activation failure: the membership is untouched");
            require(!boundary.snapshot().cached.licenseEntitles,
                    "activation failure: the licence route is not granted by a rejection");
        }

        // A structural failure (the endpoint's own 400) is not a verdict on
        // the key, and also changes nothing.
        boundary.activateLicenseKey("not-a-key");
        auth.pendingActivate(HttpReply { 400,
            object({ { "error", "malformed_key" },
                     { "message", "That does not look like a licence key." } }),
            true });
        require(boundary.snapshot().licensed,
                "activation failure: a malformed-key refusal keeps the subscription entitlement");
        require(boundary.snapshot().message.contains("does not look like"),
                "activation failure: the server's own wording is surfaced");

        // And a transport failure is never reported as a rejection.
        boundary.activateLicenseKey("PC-7QF2-3MHD-NX4T-9BKR");
        auth.pendingActivate(HttpReply { 0, {}, false });
        require(boundary.snapshot().licensed,
                "activation failure: a transport failure keeps the entitlement");
        require(boundary.snapshot().message.contains("not activated"),
                "activation failure: a transport failure is reported as no activation");

        // Activating without a session is refused honestly - there is no
        // identity, so the endpoint could not take a slot either.
        const auto callsBeforeRefusal = auth.activateCalls;
        boundary.signOutButtonClicked();
        boundary.activateLicenseKey("PC-7QF2-3MHD-NX4T-9BKR");
        require(auth.activateCalls == callsBeforeRefusal,
                "activation failure: no request is sent without a signed-in session");
        require(boundary.snapshot().message.contains("Sign in"),
                "activation failure: the missing session is explained");
        casePassed();
    }

    // =================================================================
    // 9. Device-limit answers are surfaced clearly, and never pretend the
    //    activation succeeded.
    // =================================================================
    {
        auto session = signedIn("device-limit");
        auto& boundary = *session.boundary;
        auto& auth = *session.auth;

        boundary.activateLicenseKey("PC-7QF2-3MHD-NX4T-9BKR");
        require(boundary.snapshot().activationPending,
                "device limit: an activation reads as pending while it is in flight");
        require(chordengine::licensing::activationButtonText(boundary.snapshot())
                    == "ACTIVATING...",
                "device limit: the control says what it is doing");

        auth.pendingActivate(HttpReply { 200,
            object({ { "ok", false },
                     { "status", "device_limit" },
                     { "product", juce::String(chordengine::licensing::productSlug) },
                     { "message", "This licence is already active on 3 of 3 computers. "
                                  "Deactivate a computer in your Music-Prod account, then try again." },
                     { "device", object({ { "activated", false },
                                          { "label", "This Mac" },
                                          { "limit", 3 },
                                          { "used", 3 } }) } }),
            true });

        const auto& snapshot = boundary.snapshot();
        require(!snapshot.licensed,
                "device limit: a full house never unlocks the full version");
        require(!snapshot.activationPending,
                "device limit: the pending state is cleared");
        require(chordengine::licensing::activationButtonText(snapshot) == "ACTIVATE",
                "device limit: the control returns to ACTIVATE");
        require(snapshot.message.contains("3 of 3")
                    && snapshot.message.contains("Deactivate a computer"),
                "device limit: the server's own explanation reaches the user");
        require(snapshot.cached.devicesUsed == 3 && snapshot.cached.deviceLimit == 3,
                "device limit: the real slot count is reported, not a guess");
        require(!snapshot.cached.licenseEntitles,
                "device limit: no entitlement is invented from a refusal");

        // The account-level fact is still shown honestly: the licence is
        // fine, this computer is simply not activated.
        boundary.deliverEntitlementsReply(HttpReply { 200,
            entitlementBody(false, false, true, "active", true, false, 3, 3), true });
        require(!boundary.snapshot().licensed,
                "device limit: the entitlement answer still refuses access");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Licence active on your account - this computer is not activated "
                       "(3 of 3 computers)",
                "device limit: the UI distinguishes a full house from a bad key");

        // A successful activation on a computer that has a free slot: the
        // server takes the slot and the entitlement answer opens the gate -
        // never the activation reply itself.
        boundary.activateLicenseKey("PC-7QF2-3MHD-NX4T-9BKR");
        auth.pendingActivate(HttpReply { 200,
            object({ { "ok", true },
                     { "status", "ok" },
                     { "product", juce::String(chordengine::licensing::productSlug) },
                     { "message", "Licence activated on this computer." },
                     { "license", object({ { "state", "assigned" },
                                           { "expires_at", juce::var() } }) },
                     { "device", object({ { "activated", true },
                                          { "label", "This Mac" },
                                          { "limit", 3 },
                                          { "used", 1 } }) } }),
            true });

        require(!boundary.snapshot().licensed,
                "activation: the activation reply alone never opens the gate");
        require(boundary.snapshot().message.contains("Licence activated on this computer"),
                "activation: the server's confirmation is surfaced");
        require(boundary.snapshot().cached.licenseDeviceActive,
                "activation: the slot is recorded as held by this computer");

        // The very next tick re-asks the entitlement question, because only
        // the product-scoped answer may grant access.
        const auto requestsBefore = auth.entitlementCalls;
        boundary.tick();
        require(auth.entitlementCalls == requestsBefore + 1,
                "activation: success triggers an immediate entitlement verification");
        require(auth.lastToken == "opaque-contract-token",
                "activation: the verification uses the signed-in token");

        auth.pendingEntitlements(HttpReply { 200,
            entitlementBody(true, false, true, "active", true, true, 1, 3), true });
        require(boundary.snapshot().licensed,
                "activation: the server's own entitlement answer opens the gate");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Licence active on this computer (1 of 3 computers)",
                "activation: the licence line reports the activated computer");

        // Nothing about the key is written to disk: only the token, the
        // device id and the verified answer.
        const auto persisted = session.authFile.loadFileAsString();
        require(!persisted.contains("7QF2"), "activation: the licence key is never persisted");
        require(persisted.contains("device_id"),
                "activation: the device id is persisted with the answer");
        casePassed();
    }

    // =================================================================
    // 10. The device identity is a stable, opaque per-installation value.
    // =================================================================
    {
        const auto first = chordengine::licensing::newDeviceId();
        const auto second = chordengine::licensing::newDeviceId();
        require(first.length() == 32, "device id: 128 bits of hex");
        require(first != second, "device id: a fresh install gets a fresh value");
        require(chordengine::licensing::isDeviceIdFormat(first),
                "device id: it satisfies the backend's own format rule");
        require(!chordengine::licensing::isDeviceIdFormat("short"),
                "device id: a too-short id is rejected (it would collide across computers)");
        require(chordengine::licensing::normalizeDeviceId(" AABBCCDDEEFF0011 ")
                    == "aabbccddeeff0011",
                "device id: normalisation trims and lowercases, as the server does");

        // It is created once, kept, and reused.
        auto session = signedIn("device-identity");
        const auto id = session.boundary->deviceId();
        require(chordengine::licensing::isDeviceIdFormat(id),
                "device id: the first run creates a valid one");
        require(session.authFile.loadFileAsString().contains(id),
                "device id: it is kept beside the auth cache");
        casePassed();
    }

    constexpr int expectedCaseCount = 10;
    require(casesPassed == expectedCaseCount,
            "every contract case must run: expected " + juce::String(expectedCaseCount)
                + ", executed " + juce::String(casesPassed));

    std::cout << "ChordEngineLicensingContractTests: " << casesPassed << "/"
              << expectedCaseCount << " verified-contract cases passed\n";
    return EXIT_SUCCESS;
}
