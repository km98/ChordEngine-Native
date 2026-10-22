// The processor-owned licensing runtime: access is decided, re-checked and
// enforced with NO editor, the trial budget survives a reload, and a lapsed
// Music-Prod+ membership falls back to the trial (and closes when it is used
// up). Every reply is a local fixture; no case touches the network.

#include "Licensing/LicensingRuntime.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>
#include <memory>

using chordengine::licensing::HttpReply;
using chordengine::licensing::LicensingRuntime;

namespace
{
int casesPassed = 0;

void require(bool condition, const juce::String& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

void casePassed() { ++casesPassed; }

constexpr double dayMs = 1000.0 * 60.0 * 60.0 * 24.0;
constexpr double minuteMs = 60.0 * 1000.0;

class MockAuthService final : public chordengine::licensing::AuthService
{
public:
    void start(const juce::String&, const juce::String&, const juce::String&, const juce::String&,
               Callback) override {}
    void poll(const juce::String&, Callback) override {}
    void entitlements(const juce::String& token, Callback callback) override
    {
        ++entitlementCalls;
        lastToken = token;
        pending = std::move(callback);
    }
    void logout(const juce::String&, Callback) override {}
    void activateLicense(const juce::String&, const juce::String&, const juce::String&,
                        const juce::String&, const juce::String&, const juce::String&,
                        Callback) override {}

    int entitlementCalls = 0;
    juce::String lastToken;
    Callback pending;
};

juce::var object(const std::vector<std::pair<const char*, juce::var>>& fields)
{
    juce::DynamicObject::Ptr dynamic = new juce::DynamicObject();
    for (const auto& field : fields)
        dynamic->setProperty(field.first, field.second);
    return juce::var(dynamic.get());
}

juce::File freshAuthFile(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("ChordEngine-LicensingRuntimeTests")
                   .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir.getChildFile("auth.json");
}

// A Music-Prod+ member's saved answer, verified `ageDays` ago.
void writeMemberFile(const juce::File& file, double ageDays, bool viaLicense = false)
{
    const auto verifiedAt = juce::Time::currentTimeMillis() - static_cast<juce::int64>(ageDays * dayMs);
    const auto json = object({
        { "token", "member-token" },
        { "display_name", "Member" },
        { "device_id", "" },
        { "subscribed", !viaLicense },
        { "plugin_entitled", true },
        { "subscription_entitles", !viaLicense },
        { "license_entitles", viaLicense },
        { "license_account_active", viaLicense },
        { "license_device_active", viaLicense },
        { "license_status", viaLicense ? "active" : "none" },
        { "devices_used", viaLicense ? 1 : 0 },
        { "device_limit", 3 },
        { "verified_at", static_cast<juce::int64>(verifiedAt) },
    });
    file.replaceWithText(juce::JSON::toString(json));
}

// The server's answer for a member whose Music-Prod+ ended.
juce::var lapsedAnswer()
{
    return object({
        { "ok", true },
        { "product", "chordengine" },
        { "subscribed", false },
        { "plugin_entitled", false },
        { "subscription", object({ { "active", false }, { "covers_product", true } }) },
        { "license", object({ { "active", false }, { "status", "none" }, { "account_active", false },
                              { "device_active", false }, { "devices_used", 0 }, { "device_limit", 3 } }) },
    });
}

struct Rig
{
    MockAuthService* auth = nullptr;
    std::unique_ptr<LicensingRuntime> runtime;
    bool gateOpen = false;
    int gateCalls = 0;
    bool activity = false;

    explicit Rig(const juce::File& file)
    {
        auto service = std::make_unique<MockAuthService>();
        auth = service.get();
        runtime = std::make_unique<LicensingRuntime>(std::move(service), nullptr, file);
        runtime->setGateSink([this](bool open) { gateOpen = open; ++gateCalls; });
        runtime->setPlaybackActivitySource([this] { const auto value = activity; activity = false; return value; });
    }

    double trialMs() const { return runtime->boundary().snapshot().trialMs; }
    bool licensed() const { return runtime->boundary().snapshot().licensed; }

    // Runs the clock for `ms` of real time in 100 ms steps.
    void run(double ms)
    {
        const auto now = juce::Time::currentTimeMillis();
        for (double elapsed = 0.0; elapsed < ms; elapsed += 100.0)
            runtime->tick(100.0, now);
    }
};
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI guiInitialiser;
    juce::ignoreUnused(guiInitialiser);

    // 1. A new installation: the gate is decided at construction (no editor,
    //    no tick) and open for the trial; the clock waits for the first chord.
    {
        const auto file = freshAuthFile("fresh");
        Rig rig(file);
        require(rig.gateCalls == 1 && rig.gateOpen, "fresh: the gate is published open before any tick");
        rig.run(5000.0);
        require(rig.trialMs() == chordengine::licensing::trialTotalMs,
                "fresh: the trial clock does not run before the first chord");
        rig.activity = true;
        rig.run(5000.0);
        require(rig.trialMs() <= chordengine::licensing::trialTotalMs - 4900.0,
                "fresh: the first chord starts the trial clock without an editor");
        casePassed();
    }

    // 2. The trial is persisted: a reload continues it instead of restarting,
    //    and an expired trial closes the gate at construction.
    {
        const auto file = freshAuthFile("persisted");
        {
            Rig rig(file);
            rig.activity = true;
            rig.run(10.0 * minuteMs);
            require(rig.gateCalls == 1, "persisted: the open gate is not republished every tick");
        }
        {
            Rig rig(file);
            require(rig.trialMs() <= chordengine::licensing::trialTotalMs - 10.0 * minuteMs + 200.0,
                    "persisted: a reload keeps the used trial time");
            rig.activity = true;
            rig.run(21.0 * minuteMs);
            require(!rig.gateOpen && rig.runtime->boundary().expiredGateActive(),
                    "persisted: the gate closes when the trial runs out, without an editor");
        }
        {
            Rig rig(file);
            require(rig.gateCalls == 1 && !rig.gateOpen,
                    "persisted: an expired trial keeps the gate closed after a reload");
        }
        casePassed();
    }

    // 3. A stalled timer (sleep, blocked message thread) is credited at most
    //    one second per step.
    {
        Rig rig(freshAuthFile("clamp"));
        rig.activity = true;
        rig.runtime->tick(100.0, juce::Time::currentTimeMillis());
        const auto before = rig.trialMs();
        rig.runtime->tick(10.0 * minuteMs, juce::Time::currentTimeMillis());
        require(before - rig.trialMs() <= 1000.0, "clamp: a long gap costs at most one second");
        casePassed();
    }

    // 4. A Music-Prod+ member is licensed from the saved answer at load and
    //    the trial never runs; the offline window is 14 days (7 for a licence).
    {
        const auto file = freshAuthFile("member");
        writeMemberFile(file, 1.0);
        Rig rig(file);
        require(rig.licensed() && rig.gateOpen, "member: licensed at load, before any network reply");
        rig.activity = true;
        rig.run(5000.0);
        require(rig.trialMs() == chordengine::licensing::trialTotalMs, "member: the trial does not run");

        const auto stale = freshAuthFile("member-stale");
        writeMemberFile(stale, 15.0);
        Rig staleRig(stale);
        require(!staleRig.licensed(), "member: a 15-day-old subscription answer is past its 14-day window");

        const auto licence = freshAuthFile("member-licence");
        writeMemberFile(licence, 8.0, true);
        Rig licenceRig(licence);
        require(!licenceRig.licensed(), "member: an 8-day-old licence answer is past its 7-day window");
        casePassed();
    }

    // 5. Re-checks: requested from any thread (prepareToPlay, editor open),
    //    performed at the next tick, rate limited to one a minute.
    {
        const auto file = freshAuthFile("recheck");
        writeMemberFile(file, 1.0);
        Rig rig(file);
        rig.runtime->tick(100.0, juce::Time::currentTimeMillis()); // the first tick primes one check
        const auto primed = rig.auth->entitlementCalls;
        require(primed == 1 && rig.auth->lastToken == "member-token", "recheck: the first tick verifies the token");

        const auto now = juce::Time::currentTimeMillis();
        rig.runtime->requestRecheck();
        rig.runtime->tick(100.0, now);
        require(rig.auth->entitlementCalls == primed + 1, "recheck: a requested re-check runs at the next tick");
        rig.runtime->requestRecheck();
        rig.runtime->tick(100.0, now + 1000);
        require(rig.auth->entitlementCalls == primed + 1, "recheck: a second request within a minute is skipped");
        rig.runtime->requestRecheck();
        rig.runtime->tick(100.0, now + 61000);
        require(rig.auth->entitlementCalls == primed + 2, "recheck: after a minute it runs again");
        casePassed();
    }

    // 6. Membership ended: the re-check answer relocks to the trial, which then
    //    runs out and closes the gate. With the trial already used, the gate
    //    closes on the answer itself.
    {
        const auto file = freshAuthFile("lapsed");
        writeMemberFile(file, 1.0);
        Rig rig(file);
        rig.runtime->tick(100.0, juce::Time::currentTimeMillis());
        require(rig.auth->pending != nullptr, "lapsed: a check is in flight");
        rig.runtime->boundary().deliverEntitlementsReply(HttpReply { 200, lapsedAnswer(), true });
        require(!rig.licensed() && rig.gateOpen, "lapsed: an ended membership falls back to the trial");
        rig.activity = true;
        rig.run(chordengine::licensing::trialTotalMs + 1000.0);
        require(!rig.gateOpen, "lapsed: the gate closes once the trial is used up");

        Rig reloaded(file);
        require(!reloaded.licensed() && !reloaded.gateOpen,
                "lapsed: after a reload the ended membership and the used trial keep the gate closed");

        const auto usedTrial = freshAuthFile("lapsed-used");
        writeMemberFile(usedTrial, 1.0);
        auto stored = juce::JSON::parse(usedTrial.loadFileAsString());
        stored.getDynamicObject()->setProperty("trial_remaining_ms", 0);
        stored.getDynamicObject()->setProperty("trial_expired", true);
        usedTrial.replaceWithText(juce::JSON::toString(stored));
        Rig member(usedTrial);
        require(member.gateOpen, "lapsed: a member with a used trial plays");
        member.runtime->tick(100.0, juce::Time::currentTimeMillis());
        member.runtime->boundary().deliverEntitlementsReply(HttpReply { 200, lapsedAnswer(), true });
        require(!member.gateOpen, "lapsed: with the trial used, the ended membership closes the gate at once");
        casePassed();
    }

    // 7. Two instances share one budget: neither writes back more trial time
    //    than the other already left on disk.
    {
        const auto file = freshAuthFile("shared");
        Rig first(file);
        Rig second(file);
        first.activity = true;
        first.run(6.0 * minuteMs);
        first.runtime->boundary().flushTrial();
        second.runtime->boundary().flushTrial();
        Rig third(file);
        require(third.trialMs() <= chordengine::licensing::trialTotalMs - 6.0 * minuteMs + 200.0,
                "shared: an idle instance does not restore the time another one used");
        casePassed();
    }

    // 8. Editor open picks up a sign-in written by another instance.
    {
        const auto file = freshAuthFile("other-instance");
        Rig rig(file);
        require(!rig.licensed(), "other instance: starts unlicensed");
        juce::Thread::sleep(1100); // file times have one-second resolution on some volumes
        writeMemberFile(file, 0.0);
        rig.runtime->editorOpened();
        require(rig.licensed() && rig.gateOpen, "other instance: opening the editor loads the new sign-in");
        casePassed();
    }

    constexpr int expectedCaseCount = 8;
    require(casesPassed == expectedCaseCount, "every licensing runtime case must run");
    std::cout << "ChordEngine licensing runtime tests passed (" << casesPassed << " cases)" << std::endl;
    return 0;
}
