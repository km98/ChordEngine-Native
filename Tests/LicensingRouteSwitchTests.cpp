// Native licensing — ROUTE SWITCHES between the two ways to unlock ChordEngine.
//
// A Music-Prod+ subscription and a device-activated lifetime licence are
// INDEPENDENT routes to the SAME product, and the backend says so explicitly
// (_shared/pluginEntitlements.ts):
//
//     plugin_entitled = subscriptionEntitles || licenseDeviceActive
//
// So the invariant this suite pins down is a NEGATIVE one, in both directions:
// the loss of one route must never withdraw access while the other route is
// still valid, and a route that the server did not grant must never keep access
// alive. Only `plugin_entitled` decides; the two route flags decide which
// offline window applies and what the INFO page says.
//
// Every reply is a local fixture in the exact wire shape of the real function
// (vyre-plugin-auth/contract.ts). No test here performs a network request, and
// nothing contacts the production endpoint.

#include "Licensing/LicensingBoundary.h"
#include "Licensing/LicensingModel.h"
#include "Licensing/LicensingServices.h"
#include "Licensing/LicensingTypes.h"

#include "Platform/Text.h"
#include "TestSupport.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace
{
int casesPassed = 0;

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string& message)
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
using chordengine::licensing::LicensingModel;
using chordengine::licensing::Snapshot;

constexpr double dayMs = 1000.0 * 60.0 * 60.0 * 24.0;

// The trial budget, in 100 ms ticks: enough to exhaust it deterministically.
constexpr int trialTicks = 19000;

// ----------------------------------------------------------- test doubles
class MockAuthService final : public chordengine::licensing::AuthService
{
public:
    void start(const std::string& deviceName, const std::string& pluginVersion,
               const std::string& product, const std::string& deviceId,
               Callback callback) override
    {
        ++startCalls;
        lastProduct = product;
        lastDeviceId = deviceId;
        pendingStart = std::move(callback);
    }

    void poll(const std::string& deviceCode, Callback callback) override
    {
        ++pollCalls;
        lastPolledCode = deviceCode;
        pendingPoll = std::move(callback);
    }

    void entitlements(const std::string& token, Callback callback) override
    {
        ++entitlementCalls;
        lastToken = token;
        pendingEntitlements = std::move(callback);
    }

    void logout(const std::string& token, Callback callback) override
    {
        ++logoutCalls;
        lastToken = token;
        pendingLogout = std::move(callback);
    }

    void activateLicense(const std::string& token, const std::string& licenseKey,
                        const std::string& deviceId, const std::string& deviceLabel,
                        const std::string& platform, const std::string& pluginVersion,
                        Callback callback) override
    {
        ++activateCalls;
        lastLicenseKey = licenseKey;
        pendingActivate = std::move(callback);
    }

    int startCalls = 0;
    int pollCalls = 0;
    int entitlementCalls = 0;
    int logoutCalls = 0;
    int activateCalls = 0;
    std::string lastProduct;
    std::string lastDeviceId;
    std::string lastPolledCode;
    std::string lastToken;
    std::string lastLicenseKey;
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
std::filesystem::path storageDirectory(const std::string& name)
{
    return chordengine::test::freshDirectory("ChordEngine-RouteSwitchTests", name);
}

std::filesystem::path authFileFor(const std::string& name)
{
    return storageDirectory(name) / "auth.json";
}

chordengine::platform::Json object(const std::vector<std::pair<const char*, chordengine::platform::Json>>& fields)
{
    auto dynamic = chordengine::platform::Json::object();
    for (const auto& field : fields)
        dynamic.set(field.first, field.second);
    return dynamic;
}

// One route's answer, in the exact shape the server builds. `subscriptionActive`
// and `licenseDeviceActive` are the two independent facts; `pluginEntitled` is
// the gate the server derives from them.
struct RouteState
{
    bool entitled = false;
    bool subscribed = false;         // the raw membership (display only)
    bool subscriptionActive = false; // subscription.active
    bool coversProduct = true;       // subscription.covers_product
    std::string licenseStatus = chordengine::licensing::licenseStatusNone;
    bool licenseAccountActive = false;
    bool licenseDeviceActive = false;
    int devicesUsed = 0;
    int deviceLimit = 3;
};

chordengine::platform::Json entitlementBody(const RouteState& state)
{
    return object({
        { "ok", true },
        { "product", std::string(chordengine::licensing::productSlug) },
        { "subscribed", state.subscribed },
        { "plugin_entitled", state.entitled },
        { "subscription",
          object({ { "active", state.subscriptionActive },
                   { "covers_product", state.coversProduct } }) },
        { "license",
          object({ { "active", state.licenseAccountActive && state.licenseDeviceActive },
                   { "status", state.licenseStatus },
                   { "account_active", state.licenseAccountActive },
                   { "device_active", state.licenseDeviceActive },
                   { "devices_used", state.devicesUsed },
                   { "device_limit", state.deviceLimit },
                   { "expires_at", chordengine::platform::Json() } }) },
        { "packs", chordengine::platform::Json(chordengine::platform::Json::array()) },
    });
}

// A subscription-entitled answer: the account is a Music-Prod+ member and no
// licence row is involved at all.
RouteState subscriptionRoute(bool active)
{
    RouteState state;
    state.entitled = active;
    state.subscribed = active;
    state.subscriptionActive = active;
    state.coversProduct = true;
    return state;
}

// A device-activated lifetime-licence answer: an active licence row that THIS
// computer holds a slot on. No subscription anywhere.
RouteState licenseRoute(bool active, int used = 1)
{
    RouteState state;
    state.entitled = active;
    state.subscribed = false;
    state.subscriptionActive = false;
    state.coversProduct = true; // chordengine is a subscription-covered product
    state.licenseStatus = active ? std::string(chordengine::licensing::licenseStatusActive)
                                 : std::string(chordengine::licensing::licenseStatusNone);
    state.licenseAccountActive = active;
    state.licenseDeviceActive = active;
    state.devicesUsed = active ? used : 0;
    return state;
}

// Both routes live at once.
RouteState bothRoutes(int used = 2)
{
    RouteState state = licenseRoute(true, used);
    state.subscribed = true;
    state.subscriptionActive = true;
    return state;
}

struct Session
{
    std::unique_ptr<MockAuthService> auth;
    std::unique_ptr<MockUpdateService> update;
    std::unique_ptr<LicensingBoundary> boundary;
    std::filesystem::path authFile;
    // The value the audio path reads. Shared so it stays valid for the
    // boundary's whole lifetime.
    std::shared_ptr<bool> gateOpen;
};

// A signed-in session whose gate sink records exactly what the audio path
// would read, so a route switch can be checked at the level that matters.
Session sessionFor(const std::string& name)
{
    Session session;
    session.auth = std::make_unique<MockAuthService>();
    session.update = std::make_unique<MockUpdateService>();
    session.authFile = authFileFor(name);
    session.boundary = std::make_unique<LicensingBoundary>(
        *session.auth, *session.update, session.authFile);
    session.gateOpen = std::make_shared<bool>(true);
    session.boundary->setGateSink(
        [gate = session.gateOpen](bool open) { *gate = open; });
    session.boundary->initialise();

    session.boundary->signInButtonClicked();
    session.auth->pendingStart(HttpReply { 200,
        object({ { "device_code", "dev-route" },
                 { "user_code", "ABCD-EFGH" },
                 { "product", std::string(chordengine::licensing::productSlug) },
                 { "verification_url_complete", "https://music-prod.com/plugin/link?code=ABCD-EFGH" } }),
        true });
    for (int tick = 0; tick < 12 && session.auth->pollCalls == 0; ++tick)
        session.boundary->tick();
    require(session.auth->pollCalls >= 1, "harness: the device code is polled");
    session.auth->pendingPoll(HttpReply { 200,
        object({ { "status", "approved" },
                 { "token", "opaque-route-token" },
                 { "display_name", "Martin" } }),
        true });

    return session;
}

// Spends the whole trial budget so the gate can only be open because a route
// grants access - never because the demo is still running.
void expireTrial(LicensingBoundary& boundary)
{
    boundary.notePlaybackStarted();
    for (int tick = 0; tick < trialTicks && !boundary.expiredGateActive(); ++tick)
        boundary.tick();
    require(boundary.expiredGateActive(), "harness: the trial budget is exhausted");
}

void deliver(LicensingBoundary& boundary, const RouteState& state)
{
    boundary.deliverEntitlementsReply(HttpReply { 200, entitlementBody(state), true });
}

// Reads one persisted flag by MEANING, not by its text encoding: the file must
// carry the field, and a JSON boolean (or its 1/0 form) is what it says.
bool persistedFlag(const std::filesystem::path& file, const char* name)
{
    const auto parsed = chordengine::test::parseJson(chordengine::test::readText(file));
    require(parsed.isObject(), "persistence: auth.json holds a JSON object");
    const auto& value = parsed[name];
    require(parsed.has(name) && !value.isNull(),
            std::string("persistence: '") + name + "' must be written");
    if (value.isBool())
        return value.asBool();
    const auto text = chordengine::text::toLower(chordengine::text::trim(chordengine::licensing::varText(value)));
    return text == "true" || text == "1";
}

// A cached answer written the way the client writes it, aged by `ageDays`.
std::filesystem::path writeCachedEntitlement(const std::string& name, const RouteState& state,
                                  double ageDays)
{
    auto file = authFileFor(name);
    auto cached = chordengine::platform::Json::object();
    cached.set("token", "cached-token");
    cached.set("display_name", "Martin");
    cached.set("device_id", chordengine::licensing::newDeviceId());
    cached.set("subscribed", state.subscribed);
    cached.set("plugin_entitled", state.entitled);
    cached.set("subscription_entitles",
                        state.entitled && state.subscriptionActive && state.coversProduct);
    cached.set("license_entitles",
                        state.entitled && state.licenseDeviceActive);
    cached.set("license_account_active", state.licenseAccountActive);
    cached.set("license_device_active", state.licenseDeviceActive);
    cached.set("license_status", state.licenseStatus);
    cached.set("devices_used", state.devicesUsed);
    cached.set("device_limit", state.deviceLimit);
    cached.set("verified_at", static_cast<std::int64_t>(
        static_cast<double>(chordengine::platform::currentTimeMillis()) - ageDays * dayMs));
    chordengine::platform::writeFileAtomically(file, (cached).dump());
    return file;
}
} // namespace

int main()
{
    // =================================================================
    // 1. Subscription -> lifetime licence. The membership lapses, but the
    //    licence activated on this computer is still valid: access must not
    //    be withdrawn, and the gate the audio path reads must stay open.
    // =================================================================
    {
        auto session = sessionFor("sub-to-licence");
        auto& boundary = *session.boundary;

        // A spent trial, so an open gate can only mean a granted route.
        expireTrial(boundary);
        require(!*session.gateOpen, "sub->licence: a spent trial closes the gate");

        deliver(boundary, subscriptionRoute(true));
        require(boundary.snapshot().licensed, "sub->licence: the subscription grants access");
        require(*session.gateOpen, "sub->licence: the grant opens the gate");
        require(boundary.snapshot().cached.subscriptionEntitles,
                "sub->licence: the answer is attributed to the subscription route");

        // The membership ends; the licence route takes over in the same answer.
        deliver(boundary, licenseRoute(true));
        require(boundary.snapshot().licensed,
                "sub->licence: losing the subscription does not withdraw access from the "
                "still-valid licence route");
        require(*session.gateOpen, "sub->licence: the gate stays open across the switch");
        require(boundary.snapshot().cached.licenseEntitles
                    && !boundary.snapshot().cached.subscriptionEntitles,
                "sub->licence: the licence route is now the granting route");
        require(!boundary.snapshot().cached.subscribed,
                "sub->licence: the lapsed membership is reported honestly");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  NOT ACTIVE",
                "sub->licence: the account line never claims a lapsed subscription");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Licence active on this computer (1 of 3 computers)",
                "sub->licence: the licence line names the surviving route");
        require(chordengine::licensing::trialStatusText(boundary.snapshot())
                    == "Licence active - full access",
                "sub->licence: the trial line names the licence route");
        casePassed();
    }

    // =================================================================
    // 2. Lifetime licence -> subscription. The licence is released or
    //    revoked, but Music-Prod+ still entitles ChordEngine: access must
    //    stay enabled.
    // =================================================================
    {
        auto session = sessionFor("licence-to-sub");
        auto& boundary = *session.boundary;

        expireTrial(boundary);
        deliver(boundary, licenseRoute(true));
        require(boundary.snapshot().licensed && *session.gateOpen,
                "licence->sub: the licence route grants access and opens the gate");

        RouteState lapsed = licenseRoute(false);
        lapsed.licenseStatus = chordengine::licensing::licenseStatusRevoked;
        lapsed.subscribed = true;
        lapsed.subscriptionActive = true;
        lapsed.entitled = true; // the subscription is what entitles now
        deliver(boundary, lapsed);

        require(boundary.snapshot().licensed,
                "licence->sub: a released licence does not withdraw access while Music-Prod+ "
                "still entitles the product");
        require(*session.gateOpen, "licence->sub: the gate stays open across the switch");
        require(boundary.snapshot().cached.subscriptionEntitles
                    && !boundary.snapshot().cached.licenseEntitles,
                "licence->sub: the subscription route is now the granting route");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  ACTIVE",
                "licence->sub: the account line reports the verified membership");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Music-Prod+ verified",
                "licence->sub: the licence line names the route that actually grants access");
        casePassed();
    }

    // =================================================================
    // 3. A revoked licence must not grant access - and must not damage a
    //    valid subscription entitlement either.
    // =================================================================
    {
        // (a) A revoked licence with NO subscription grants nothing, on its
        // own merits.
        auto alone = sessionFor("revoked-alone");
        expireTrial(*alone.boundary);
        RouteState revoked = licenseRoute(false);
        revoked.licenseStatus = chordengine::licensing::licenseStatusRevoked;
        revoked.licenseAccountActive = false;
        deliver(*alone.boundary, revoked);
        require(!alone.boundary->snapshot().licensed,
                "revoked: a revoked licence never grants access by itself");
        require(!*alone.gateOpen, "revoked: the gate closes when nothing grants access");
        require(chordengine::licensing::licenseLineText(alone.boundary->snapshot())
                    == "Licence revoked - contact Music-Prod support",
                "revoked: the revocation is stated, not hidden behind a generic expiry");

        // (b) The same revocation while Music-Prod+ is active leaves that
        // route completely intact.
        auto both = sessionFor("revoked-with-sub");
        expireTrial(*both.boundary);
        deliver(*both.boundary, bothRoutes(2));
        require(both.boundary->snapshot().licensed,
                "revoked: both routes grant access to begin with");

        RouteState lapsedLicence = bothRoutes(2);
        lapsedLicence.entitled = true; // still entitled, via the subscription
        lapsedLicence.licenseStatus = chordengine::licensing::licenseStatusRevoked;
        lapsedLicence.licenseAccountActive = false;
        lapsedLicence.licenseDeviceActive = false;
        lapsedLicence.devicesUsed = 0;
        deliver(*both.boundary, lapsedLicence);

        require(both.boundary->snapshot().licensed && *both.gateOpen,
                "revoked: revoking the licence keeps the subscription entitlement alive");
        require(both.boundary->snapshot().cached.subscriptionEntitles
                    && !both.boundary->snapshot().cached.licenseEntitles,
                "revoked: only the licence route is withdrawn");
        require(persistedFlag(both.authFile, "plugin_entitled"),
                "revoked: the persisted answer still records the surviving entitlement");
        require(persistedFlag(both.authFile, "subscription_entitles"),
                "revoked: the persisted answer keeps the subscription route");
        require(!persistedFlag(both.authFile, "license_entitles"),
                "revoked: the persisted answer drops the revoked licence route");

        // ... and it survives both an offline stretch and a restart.
        both.boundary->deliverEntitlementsReply(HttpReply { 0, {}, false });
        require(both.boundary->snapshot().licensed,
                "revoked: an offline stretch keeps the surviving route");
        auto restarted = std::make_unique<LicensingBoundary>(
            *both.auth, *both.update, both.authFile);
        restarted->initialise();
        require(restarted->snapshot().licensed,
                "revoked: a restart re-applies the surviving route's own window");
        require(chordengine::licensing::offlineGraceDays(restarted->snapshot()) == 14.0,
                "revoked: the surviving subscription route gets its documented 14 days");
        casePassed();
    }

    // =================================================================
    // 4. The subscription expires while an activated licence is active:
    //    access continues on the licence route and its shorter window.
    // =================================================================
    {
        auto session = sessionFor("sub-expires");
        auto& boundary = *session.boundary;

        expireTrial(boundary);
        deliver(boundary, bothRoutes(1));
        require(boundary.snapshot().licensed, "sub expiry: both routes grant access");
        require(chordengine::licensing::offlineGraceDays(boundary.snapshot()) == 7.0,
                "sub expiry: the licence route is the binding, shorter window");

        RouteState licenceOnly = licenseRoute(true, 1);
        deliver(boundary, licenceOnly);
        require(boundary.snapshot().licensed,
                "sub expiry: an expired subscription does not withdraw access while the "
                "licence route is active");
        require(*session.gateOpen, "sub expiry: the gate stays open");
        require(boundary.snapshot().cached.licenseEntitles
                    && !boundary.snapshot().cached.subscriptionEntitles,
                "sub expiry: the licence route is the surviving one");
        require(chordengine::licensing::offlineGraceDays(boundary.snapshot()) == 7.0,
                "sub expiry: the offline window follows the surviving licence route");

        // Offline, and across a restart, the licence route keeps access.
        boundary.deliverEntitlementsReply(HttpReply { 503, {}, true });
        require(boundary.snapshot().licensed,
                "sub expiry: a server error keeps the surviving route");
        auto restarted = std::make_unique<LicensingBoundary>(
            *session.auth, *session.update, session.authFile);
        restarted->initialise();
        require(restarted->snapshot().licensed,
                "sub expiry: a restart keeps access on the licence route");
        casePassed();
    }

    // =================================================================
    // 5. Both routes active: the product stays entitled, whichever route the
    //    account area prefers to display.
    // =================================================================
    {
        auto session = sessionFor("both-routes");
        auto& boundary = *session.boundary;

        expireTrial(boundary);
        deliver(boundary, bothRoutes(2));

        require(boundary.snapshot().licensed && *session.gateOpen,
                "both: plugin_entitled authorizes the product with both routes live");
        require(boundary.snapshot().cached.subscriptionEntitles
                    && boundary.snapshot().cached.licenseEntitles,
                "both: both granting routes are recorded");
        require(chordengine::licensing::licenseLineText(boundary.snapshot())
                    == "Music-Prod+ verified - licence active on this computer "
                       "(2 of 3 computers)",
                "both: the licence line states both routes and the real slot usage");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  ACTIVE",
                "both: the account line reports the membership");
        require(chordengine::licensing::trialStatusText(boundary.snapshot())
                    == "Music-Prod+ active - full access",
                "both: the trial line stays honest about the subscription");
        require(chordengine::licensing::offlineGraceDays(boundary.snapshot()) == 7.0,
                "both: the shorter licence window binds while a licence grants access");
        casePassed();
    }

    // =================================================================
    // 6. Both routes inactive: a definitive `plugin_entitled: false` denies
    //    access even though `subscribed` is still true, and nothing about the
    //    previous answer lingers.
    // =================================================================
    {
        auto session = sessionFor("both-inactive");
        auto& boundary = *session.boundary;

        deliver(boundary, bothRoutes(2));
        require(boundary.snapshot().licensed, "both inactive: access granted first");

        RouteState neither = licenseRoute(false);
        neither.subscribed = true;         // the membership is still there ...
        neither.subscriptionActive = false; // ... but it is not active
        neither.entitled = false;           // ... and the product gate says no
        deliver(boundary, neither);

        require(!boundary.snapshot().licensed,
                "both inactive: plugin_entitled false denies access despite subscribed true");
        require(!boundary.snapshot().cached.licenseEntitles
                    && !boundary.snapshot().cached.subscriptionEntitles,
                "both inactive: no route flag survives from the previous answer");
        require(!boundary.snapshot().cached.entitled,
                "both inactive: the cached entitlement is cleared");
        require(!boundary.snapshot().cached.licenseAccountActive
                    && !boundary.snapshot().cached.licenseDeviceActive,
                "both inactive: the old slot facts are not left behind");
        require(!persistedFlag(session.authFile, "plugin_entitled"),
                "both inactive: the persisted answer stops claiming an entitlement");

        // Neither an offline stretch nor a restart may resurrect it.
        boundary.deliverEntitlementsReply(HttpReply { 0, {}, false });
        require(!boundary.snapshot().licensed,
                "both inactive: a network failure cannot restore a denied answer");
        auto restarted = std::make_unique<LicensingBoundary>(
            *session.auth, *session.update, session.authFile);
        restarted->initialise();
        require(!restarted->snapshot().licensed,
                "both inactive: a restart cannot restore a denied answer");
        casePassed();
    }

    // =================================================================
    // 7. Reconnect after offline use: a fresh authoritative answer reconciles
    //    the cached routes, and neither route's loss revokes the other.
    // =================================================================
    {
        // The client comes back from 13 days offline with a subscription-only
        // cached answer (still inside that route's 14-day window).
        auto file = writeCachedEntitlement("reconnect", subscriptionRoute(true), 13.0);
        MockAuthService auth;
        MockUpdateService update;
        LicensingBoundary boundary(auth, update, file);
        boundary.initialise();
        require(boundary.snapshot().licensed,
                "reconnect: the cached subscription answer is still inside its window");
        require(chordengine::licensing::offlineGraceDays(boundary.snapshot()) == 14.0,
                "reconnect: a subscription-only cache gets the subscription window");
        const auto staleVerifiedAt = boundary.snapshot().verifiedAtMs;

        // The device reconnects and the server reports BOTH routes.
        deliver(boundary, bothRoutes(1));
        require(boundary.snapshot().licensed,
                "reconnect: the fresh answer keeps access");
        require(boundary.snapshot().cached.subscriptionEntitles
                    && boundary.snapshot().cached.licenseEntitles,
                "reconnect: the cached route is reconciled with the licence route");
        require(boundary.snapshot().verifiedAtMs > staleVerifiedAt,
                "reconnect: the fresh answer refreshes the verified timestamp");
        require(chordengine::licensing::offlineGraceDays(boundary.snapshot()) == 7.0,
                "reconnect: the offline window follows the newly binding licence route");

        // The subscription then lapses on the same account: the licence route
        // must survive on its own, not be dragged down with it.
        deliver(boundary, licenseRoute(true));
        require(boundary.snapshot().licensed,
                "reconnect: the licence route survives the subscription's loss");
        require(!boundary.snapshot().cached.subscriptionEntitles,
                "reconnect: the lapsed subscription route is recorded as lapsed");

        // And the reverse: the licence is released while the subscription
        // comes back.
        RouteState subscriptionBack = subscriptionRoute(true);
        subscriptionBack.licenseStatus = chordengine::licensing::licenseStatusNone;
        deliver(boundary, subscriptionBack);
        require(boundary.snapshot().licensed,
                "reconnect: the subscription route survives the licence's loss");
        require(boundary.snapshot().cached.subscriptionEntitles
                    && !boundary.snapshot().cached.licenseEntitles,
                "reconnect: the granting route is the one the server reported");
        casePassed();
    }

    // =================================================================
    // 8. A malformed or missing entitlement response fails closed for new
    //    authorization and never extends a stale cache window.
    // =================================================================
    {
        // (a) A fresh, never-verified install gets nothing from a reply that
        // omits the gate, in any shape.
        auto session = sessionFor("malformed");
        auto& boundary = *session.boundary;
        require(!boundary.snapshot().licensed, "malformed: no access before any answer");

        for (const auto& bogus : { chordengine::platform::Json("true"), chordengine::platform::Json("yes"), chordengine::platform::Json(1),
                                   chordengine::platform::Json(0), chordengine::platform::Json(), chordengine::platform::Json(),
                                   chordengine::platform::Json(chordengine::platform::Json::array()) })
        {
            boundary.deliverEntitlementsReply(HttpReply { 200,
                object({ { "ok", true },
                         { "subscribed", true },
                         { "plugin_entitled", bogus } }),
                true });
            require(!boundary.snapshot().licensed,
                    "malformed: only a real JSON boolean true may authorize ChordEngine");
            require(boundary.snapshot().verifiedAtMs == 0,
                    "malformed: a malformed reply never becomes a verified answer");
        }

        // (b) An unrecognised shape cannot create a route, and cannot extend
        // the window of an answer that has one.
        deliver(boundary, subscriptionRoute(true));
        const auto verifiedAt = boundary.snapshot().verifiedAtMs;

        boundary.deliverEntitlementsReply(HttpReply { 200,
            object({ { "ok", true }, { "subscribed", true } }), true });
        require(boundary.snapshot().verifiedAtMs == verifiedAt,
                "malformed: a reply without the gate never extends the verified window");
        require(boundary.snapshot().cached.subscriptionEntitles
                    && !boundary.snapshot().cached.licenseEntitles,
                "malformed: it cannot add a route either");
        require(boundary.snapshot().licensed,
                "malformed: the already-verified answer keeps its own bounded window");

        // (d) The distinction that matters for route preservation: a gate that
        // is present but is not a boolean carries no decision at all, so it
        // cannot withdraw a valid route, while a real boolean false is a
        // definitive denial that does.
        boundary.deliverEntitlementsReply(HttpReply { 200,
            object({ { "ok", true },
                     { "subscribed", true },
                     { "plugin_entitled", chordengine::platform::Json("true") } }),
            true });
        require(boundary.snapshot().licensed,
                "malformed: a non-boolean gate is silence, not a verdict, and never withdraws "
                "a valid route");
        require(boundary.snapshot().cached.subscriptionEntitles,
                "malformed: the valid cached route is left exactly as it was");
        require(persistedFlag(session.authFile, "plugin_entitled"),
                "malformed: the persisted entitlement survives the silence");
        auto restarted = std::make_unique<LicensingBoundary>(
            *session.auth, *session.update, session.authFile);
        restarted->initialise();
        require(restarted->snapshot().licensed,
                "malformed: and a restart still re-applies the untouched valid route");

        boundary.deliverEntitlementsReply(HttpReply { 200,
            object({ { "ok", true },
                     { "subscribed", true },
                     { "plugin_entitled", false } }),
            true });
        require(!boundary.snapshot().licensed,
                "malformed: a real boolean false is a definitive denial");
        require(!persistedFlag(session.authFile, "plugin_entitled"),
                "malformed: and that denial is persisted");

        // (c) The same reply cannot resurrect an expired window.
        auto expired = writeCachedEntitlement("malformed-expired", subscriptionRoute(true),
                                              15.0);
        MockAuthService expiredAuth;
        MockUpdateService expiredUpdate;
        LicensingBoundary expiredBoundary(expiredAuth, expiredUpdate, expired);
        expiredBoundary.initialise();
        require(!expiredBoundary.snapshot().licensed,
                "malformed: a cache past its window grants nothing");
        expiredBoundary.deliverEntitlementsReply(HttpReply { 200,
            object({ { "ok", true }, { "subscribed", true } }), true });
        require(!expiredBoundary.snapshot().licensed,
                "malformed: it cannot revive an expired cache window");
        casePassed();
    }

    // =================================================================
    // 9. Activation failures. A rejected key must not corrupt a valid
    //    subscription entitlement - in any of the documented verdicts.
    // =================================================================
    {
        auto session = sessionFor("activation-verdicts");
        auto& boundary = *session.boundary;
        auto& auth = *session.auth;

        expireTrial(boundary);
        deliver(boundary, subscriptionRoute(true));
        require(boundary.snapshot().licensed && *session.gateOpen,
                "activation verdicts: the subscriber is entitled first");

        // The four licence ANSWERS (HTTP 200, with a verdict), all of which
        // are statements about the key or the licence - never about the
        // subscription.
        for (const auto* verdict : { "invalid", "expired", "revoked", "device_limit" })
        {
            boundary.activateLicenseKey("PC-7QF2-3MHD-NX4T-9BKR");
            require(auth.activateCalls > 0, "activation verdicts: the key reaches the service");

            chordengine::platform::Json body;
            if (std::string(verdict) == "device_limit")
            {
                body = object({ { "ok", false },
                                { "status", "device_limit" },
                                { "product", std::string(chordengine::licensing::productSlug) },
                                { "message", "This licence is already active on 3 of 3 computers." },
                                { "device", object({ { "activated", false },
                                                     { "label", "This Mac" },
                                                     { "limit", 3 },
                                                     { "used", 3 } }) } });
            }
            else
            {
                body = object({ { "ok", false },
                                { "status", std::string(verdict) },
                                { "product", std::string(chordengine::licensing::productSlug) },
                                { "message", "That licence key was not accepted." } });
            }

            auth.pendingActivate(HttpReply { 200, body, true });

            require(boundary.snapshot().licensed && *session.gateOpen,
                    std::string("activation verdicts: a ") + verdict
                        + " answer never withdraws a valid subscription entitlement");
            require(boundary.snapshot().cached.subscriptionEntitles,
                    std::string("activation verdicts: the ") + verdict
                        + " answer keeps the subscription route intact");
            require(!boundary.snapshot().cached.licenseEntitles,
                    "activation verdicts: no failure may invent a licence route");
            require(persistedFlag(session.authFile, "plugin_entitled"),
                    "activation verdicts: the persisted entitlement is untouched");
            require(persistedFlag(session.authFile, "subscription_entitles"),
                    "activation verdicts: the persisted subscription route is untouched");
        }

        // The other direction: a licence-route-only customer whose key is
        // rejected loses only the licence half, and the next authoritative
        // answer restores the access the server still grants.
        auto licenceOnlySession = sessionFor("activation-licence-route");
        auto& licenceOnly = *licenceOnlySession.boundary;
        expireTrial(licenceOnly);
        deliver(licenceOnly, licenseRoute(true));
        require(licenceOnly.snapshot().licensed,
                "activation verdicts: the licence route grants access first");
        require(!licenceOnly.snapshot().cached.subscriptionEntitles,
                "activation verdicts: and there is no subscription to fall back on");

        licenceOnly.activateLicenseKey("PC-0000-0000-0000-0000");
        licenceOnlySession.auth->pendingActivate(HttpReply { 200,
            object({ { "ok", false },
                     { "status", "revoked" },
                     { "product", std::string(chordengine::licensing::productSlug) },
                     { "message", "This licence has been revoked." } }),
            true });
        require(!licenceOnly.snapshot().licensed,
                "activation verdicts: the rejected key withdraws the licence route "
                "(fail closed - the cache may not out-argue a server verdict)");
        require(!licenceOnly.snapshot().cached.subscriptionEntitles,
                "activation verdicts: nothing is promoted to replace it");

        // The model re-asks the server at once, and the server's own answer is
        // what decides: this computer still holds its slot, so access returns.
        const auto requestsBefore = licenceOnlySession.auth->entitlementCalls;
        licenceOnly.tick();
        require(licenceOnlySession.auth->entitlementCalls == requestsBefore + 1,
                "activation verdicts: a key verdict triggers an immediate verification");
        licenceOnlySession.auth->pendingEntitlements(HttpReply { 200,
            entitlementBody(licenseRoute(true)), true });
        require(licenceOnly.snapshot().licensed,
                "activation verdicts: the server's answer restores the still-valid licence route");
        casePassed();
    }

    // =================================================================
    // 10. Grace-period boundaries: the documented 14-day subscription and
    //     7-day licence windows, exactly, with an explicit clock.
    // =================================================================
    {
        require(chordengine::licensing::subscriptionGraceDays == 14.0,
                "grace: the documented subscription window is 14 days");
        require(chordengine::licensing::licenseGraceDays == 7.0,
                "grace: the documented device-bound licence window is 7 days");

        const std::int64_t verifiedAt = 1'800'000'000'000LL; // a fixed clock

        const auto licensedAt = [verifiedAt](const CachedEntitlement& cached,
                                             std::int64_t nowMs, const char* note)
        {
            LicensingModel model;
            model.loadToken("token", "Martin", cached, verifiedAt);
            model.applyCachedEntitlement(nowMs);
            require(model.snapshot().licensed, std::string("grace: ") + note);
        };

        const auto deniedAt = [verifiedAt](const CachedEntitlement& cached,
                                          std::int64_t nowMs, const char* note)
        {
            LicensingModel model;
            model.loadToken("token", "Martin", cached, verifiedAt);
            model.applyCachedEntitlement(nowMs);
            require(!model.snapshot().licensed, std::string("grace: ") + note);
        };

        CachedEntitlement subscription;
        subscription.entitled = true;
        subscription.subscribed = true;
        subscription.subscriptionEntitles = true;

        CachedEntitlement licence;
        licence.entitled = true;
        licence.licenseEntitles = true;
        licence.licenseDeviceActive = true;
        licence.licenseAccountActive = true;
        licence.licenseStatus = chordengine::licensing::licenseStatusActive;

        CachedEntitlement both = subscription;
        both.licenseEntitles = true;
        both.licenseDeviceActive = true;
        both.licenseAccountActive = true;

        const auto days = [](double value) {
            return static_cast<std::int64_t>(value * dayMs);
        };

        require(chordengine::licensing::offlineGraceDays(subscription) == 14.0,
                "grace: the subscription route reports 14 days");
        require(chordengine::licensing::offlineGraceDays(licence) == 7.0,
                "grace: the licence route reports 7 days");
        require(chordengine::licensing::offlineGraceDays(both) == 7.0,
                "grace: the licence window never exceeds the subscription window");
        require(chordengine::licensing::offlineGraceDays(CachedEntitlement {}) == 0.0,
                "grace: an answer that granted nothing gets no window at all");

        // Subscription: honoured to the 14th day inclusive, never past it.
        licensedAt(subscription, verifiedAt + days(14.0), "day 14 of the subscription window");
        deniedAt(subscription, verifiedAt + days(14.0) + 1,
                 "one millisecond past the subscription window");

        // Licence: the shorter, binding window.
        licensedAt(licence, verifiedAt + days(7.0), "day 7 of the licence window");
        deniedAt(licence, verifiedAt + days(7.0) + 1, "one millisecond past the licence window");

        // Both: the licence route's window is what binds, not the longer one.
        licensedAt(both, verifiedAt + days(7.0), "day 7 with both routes live");
        deniedAt(both, verifiedAt + days(7.0) + 1,
                 "past the licence window even though a subscription is also cached");

        // At the instant it was verified, and a backwards clock, which may
        // never extend or restore a window.
        licensedAt(both, verifiedAt, "the instant the answer was verified");
        deniedAt(both, verifiedAt - 1, "a clock that moved one millisecond backwards");
        deniedAt(subscription, verifiedAt - days(400.0),
                 "a clock that moved far backwards");

        // Only a real, granting answer is honoured: a cache that names no
        // route denies at once, however fresh it is.
        CachedEntitlement noRoute;
        noRoute.entitled = true;
        deniedAt(noRoute, verifiedAt, "a cached entitlement that names no granting route");
        CachedEntitlement notEntitled;
        notEntitled.entitled = false;
        notEntitled.subscribed = true;
        deniedAt(notEntitled, verifiedAt, "a cached answer that granted nothing");
        casePassed();
    }

    constexpr int expectedCaseCount = 10;
    require(casesPassed == expectedCaseCount,
            "every route-switch case must run: expected " + std::to_string(expectedCaseCount)
                + ", executed " + std::to_string(casesPassed));

    std::cout << "ChordEngineRouteSwitchTests: " << casesPassed << "/" << expectedCaseCount
              << " route-switch cases passed\n";
    return EXIT_SUCCESS;
}
