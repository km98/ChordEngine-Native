// Native functional parity tests.
//
// These cover the behaviour restored by the functional pass and are run
// against the SAME production classes the plugin ships:
//   * the MIDI/velocity path in ChordEngineAudioProcessor,
//   * the recorder state machine,
//   * the licensing / trial / update model and its boundary,
//   * the editor's real bound button callbacks,
//   * the realtime-thread and GUI isolation invariants.
//
// The Music-Prod auth service is replaced by a mock through the existing
// AuthService seam, so no test ever performs a network request. No existing
// reference assertion or fixture is modified by this file.

#include "Licensing/LicensingBoundary.h"
#include "Licensing/LicensingModel.h"
#include "Licensing/LicensingServices.h"
#include "Licensing/LicensingTypes.h"
#include "MidiTestSupport.h"
#include "Platform/Text.h"
#include "Updates/UpdateTransport.h"
#include "Updates/UpdateTypes.h"


#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

// ----------------------------------------------------------- test doubles
// Records every request the boundary makes and hands the test full control of
// when (and whether) a reply is delivered - the proof that the boundary never
// blocks waiting for the network.
class MockAuthService final : public chordengine::licensing::AuthService
{
public:
    void start(const std::string& deviceName, const std::string& pluginVersion,
               const std::string& product, const std::string& deviceId,
               Callback callback) override
    {
        ++startCalls;
        lastDeviceName = deviceName;
        lastPluginVersion = pluginVersion;
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
    std::string lastDeviceName;
    std::string lastPluginVersion;
    std::string lastProduct;
    std::string lastDeviceId;
    std::string lastPolledCode;
    std::string lastToken;
    std::string lastLicenseKey;
    std::string lastActivateDeviceId;
    std::string lastDeviceLabel;
    std::string lastPlatform;
    std::string lastActivatePluginVersion;
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

// The update transport the editor tests inject. It performs no I/O at all: it
// always answers with the recorded live `chordengine` payload and counts the
// requests, so the CHECK FOR UPDATES button can be verified end to end without
// ever touching the live service.
class StubUpdateTransport final : public chordengine::updates::UpdateTransport
{
public:
    StubUpdateTransport(int httpStatus, std::string responseBody)
        : status(httpStatus), body(std::move(responseBody))
    {
    }

    chordengine::updates::TransportReply get(const std::string& url) override
    {
        ++calls;
        lastUrl = url;

        chordengine::updates::TransportReply reply;
        reply.transportOk = true;
        reply.status = status;
        reply.body = body;
        return reply;
    }

    int status = 200;
    std::string body;
    int calls = 0;
    std::string lastUrl;
};

// --------------------------------------------------------------- helpers
std::filesystem::path temporaryStorageDirectory(const std::string& name)
{
    return chordengine::test::freshDirectory("ChordEngine-FunctionalTests", name);
}

// LicensingBoundary owns the auth FILE (the editor appends auth.json to its
// storage directory); tests point it at a file inside a temp directory.
std::filesystem::path temporaryAuthFile(const std::string& name)
{
    return temporaryStorageDirectory(name) / "auth.json";
}

using chordengine::test::MidiBuffer;
using chordengine::test::MidiMessage;
using AudioBuffer = chordengine::test::AudioBuffer;

void runBlock(ChordEngineAudioProcessor& processor, AudioBuffer& audio, MidiBuffer& midi)
{
    chordengine::test::processBlock(processor, audio, midi);
}

std::vector<int> generatedNoteOnPitches(const MidiBuffer& buffer)
{
    std::vector<int> pitches;
    for (const auto metadata : buffer)
        if (metadata.getMessage().isNoteOn())
            pitches.push_back(metadata.getMessage().getNoteNumber());
    return pitches;
}

std::vector<int> generatedNoteOnVelocities(const MidiBuffer& buffer)
{
    std::vector<int> velocities;
    for (const auto metadata : buffer)
        if (metadata.getMessage().isNoteOn())
            velocities.push_back(metadata.getMessage().getVelocity());
    return velocities;
}

chordengine::platform::Json jsonObject(const std::vector<std::pair<const char*, chordengine::platform::Json>>& fields)
{
    auto object = chordengine::platform::Json::object();
    for (const auto& field : fields)
        object.set(field.first, field.second);
    return object;
}

void pumpMessages(int milliseconds)
{
    chordengine::platform::runMainLoopFor(milliseconds);
}

// Reads a source file relative to the project root.
std::string readSourceFile(const std::string& relativePath)
{
    const std::filesystem::path file(std::string(CHORDENGINE_SOURCE_DIR) + "/" + relativePath);
    require(std::filesystem::exists(file), "source file must exist: " + relativePath);
    return chordengine::test::readText(file);
}

// Every token that would prove GUI work or licensing work happening where it
// must never happen.
const std::array<const char*, 12> realtimeForbiddenTokens {{
    "httpRequest", "LicensingBoundary", "MusicProdAuthService", "AuthService",
    "callOnMainThread", "WorkerQueue", "createEditorView", "NSView",
    "dispatch_", "writeFile", "readFile", "std::thread" }};
}

int main()
{

    // =================================================================
    // 1. Fixed velocity updates Core state.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);

        require(processor.configurationSnapshot().fixedVelocity == 100,
                "fixed velocity: artifact-backed default must stay 100");
        require(processor.setVelocityMode(chordengine::core::VelocityMode::fixed),
                "fixed velocity: fixed mode is selectable");

        for (const int velocity : { 1, 64, 100, 127 })
        {
            require(processor.setFixedVelocity(velocity),
                    "fixed velocity: " + std::to_string(velocity) + " must be accepted");
            require(processor.configurationSnapshot().fixedVelocity == velocity,
                    "fixed velocity: Core state must read back "
                        + std::to_string(velocity));
        }

        require(!processor.setFixedVelocity(0), "fixed velocity: 0 is out of range");
        require(!processor.setFixedVelocity(128), "fixed velocity: 128 is out of range");
        require(processor.configurationSnapshot().fixedVelocity == 127,
                "fixed velocity: a rejected value must not change Core state");
        casePassed();
    }

    // =================================================================
    // 2. Fixed velocity affects generated MIDI (1 / 64 / 100 / 127).
    // =================================================================
    {
        for (const int velocity : { 1, 64, 100, 127 })
        {
            ChordEngineAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            require(processor.setVelocityMode(chordengine::core::VelocityMode::fixed),
                    "fixed velocity MIDI: fixed mode is selectable");
            require(processor.setFixedVelocity(velocity),
                    "fixed velocity MIDI: set " + std::to_string(velocity));

            AudioBuffer audio(0, 512);
            MidiBuffer midi;
            midi.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(99)), 0);
            runBlock(processor, audio, midi);

            const auto velocities = generatedNoteOnVelocities(midi);
            require(velocities.size() > 0,
                    "fixed velocity MIDI: a chord must be generated at "
                        + std::to_string(velocity));
            for (const auto generated : velocities)
                require(generated == velocity,
                        "fixed velocity MIDI: every generated note must carry "
                            + std::to_string(velocity) + ", got " + std::to_string(generated));
        }
        casePassed();
    }

    // =================================================================
    // 3. Velocity modes switch correctly (dynamic / maximum / fixed).
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);

        const auto playVelocity = [&processor](std::uint8_t inputVelocity,
                                               std::uint8_t mode) -> std::vector<int>
        {
            require(processor.setVelocityMode(static_cast<chordengine::core::VelocityMode>(mode)),
                    "velocity modes: mode is selectable");
            AudioBuffer audio(0, 512);
            MidiBuffer midi;
            midi.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(inputVelocity)), 0);
            runBlock(processor, audio, midi);
            return generatedNoteOnVelocities(midi);
        };

        // dynamic = 0, maximum = 1, fixed = 2 (Core enum order).
        require(processor.setFixedVelocity(100), "velocity modes: fixed value 100");
        for (const auto generated : playVelocity(77, 0))
            require(generated == 77, "velocity modes: dynamic follows the input velocity");
        for (const auto generated : playVelocity(77, 1))
            require(generated == 127, "velocity modes: maximum is 127");
        for (const auto generated : playVelocity(77, 2))
            require(generated == 100, "velocity modes: fixed uses the fixed value");

        require(!processor.setVelocityMode(static_cast<chordengine::core::VelocityMode>(3)),
                "velocity modes: an out-of-range mode is rejected");
        casePassed();
    }

    // =================================================================
    // 4. Recorder arm state changes.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);

        require(processor.recorderState() == 0, "recorder arm: starts idle");
        require(processor.armRecorder(), "recorder arm: arming succeeds");
        require(processor.recorderState() == 1, "recorder arm: state is ARMED");
        require(!processor.armRecorder(), "recorder arm: arming twice must not re-arm");
        require(processor.recorderState() == 1, "recorder arm: state stays ARMED");
        casePassed();
    }

    // =================================================================
    // 5. Recorder stop / cancel state changes.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);

        require(processor.armRecorder(), "recorder cancel: arming succeeds");
        require(processor.cancelRecorder(), "recorder cancel: cancelling succeeds");
        require(processor.recorderState() == 0, "recorder cancel: state returns to IDLE");
        require(processor.recorderTakeCount() == 0,
                "recorder cancel: a cancelled arm stores no take");

        require(processor.armRecorder(), "recorder stop: arming succeeds");
        AudioBuffer audio(0, 512);
        MidiBuffer midi;
        midi.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
        runBlock(processor, audio, midi);
        require(processor.recorderState() == 2,
                "recorder stop: the first generated note-on starts RECORDING");

        MidiBuffer stopBlock;
        AudioBuffer stopAudio(0, 512);
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 0);
        runBlock(processor, stopAudio, release);
        require(processor.stopRecorder(), "recorder stop: stopping stores the take");
        require(processor.recorderState() == 0, "recorder stop: state returns to IDLE");
        require(processor.recorderTakeCount() == 1, "recorder stop: one take is stored");
        require(!processor.stopRecorder(), "recorder stop: stopping while idle is rejected");
        casePassed();
    }

    // =================================================================
    // 6. INFO state model transitions.
    // =================================================================
    {
        MockAuthService auth;
        MockUpdateService update;
        LicensingBoundary boundary(auth, update, temporaryAuthFile("info-portrait"));
        update.next = { false, {}, chordengine::licensing::updateStatusUnavailable };
        boundary.initialise();

        const auto& initial = boundary.snapshot();
        require(initial.phase == chordengine::licensing::Phase::signedOut,
                "INFO model: a fresh session starts signed out");
        require(!initial.licensed, "INFO model: a fresh session is not licensed");
        require(!initial.trialExpired, "INFO model: a fresh session is not expired");
        require(initial.trialMs == chordengine::licensing::trialTotalMs,
                "INFO model: the trial starts at the full reference budget");
        require(!boundary.expiredGateActive(), "INFO model: no gate before expiry");

        require(chordengine::licensing::userLineText(initial) == "User  Not signed in",
                "INFO model: user line, signed out");
        require(chordengine::licensing::plusLineText(initial) == "Music-Prod+  NOT ACTIVE",
                "INFO model: Plus line, inactive");
        require(chordengine::licensing::licenseLineText(initial)
                    == "Not active - Music-Prod+ unlocks full access",
                "INFO model: licence line, inactive");
        require(chordengine::licensing::trialStatusText(initial)
                    == "Trial access  30:00 remaining",
                "INFO model: trial line, full budget");

        // A check that cannot produce a version must never fake one.
        boundary.checkForUpdatesClicked();
        require(boundary.snapshot().updateState == chordengine::licensing::UpdateState::checked,
                "INFO model: the update check reaches a checked state");
        require(chordengine::licensing::updateStatusText(boundary.snapshot())
                    == "Status  Open Music-Prod Studio to check for updates",
                "INFO model: the reference update status text is used verbatim");
        require(chordengine::licensing::latestVersionText(boundary.snapshot())
                    == "Latest version  -",
                "INFO model: no latest version is fabricated");
        casePassed();
    }

    // =================================================================
    // 7. Trial timer state transitions.
    // =================================================================
    {
        LicensingModel model;
        require(model.snapshot().trialMs == chordengine::licensing::trialTotalMs,
                "trial clock: starts at 30:00");

        // The clock must not run before the first generated chord.
        for (int tick = 0; tick < 100; ++tick)
            model.advanceTrial(chordengine::licensing::tickMs);
        require(model.snapshot().trialMs == chordengine::licensing::trialTotalMs,
                "trial clock: does not advance before the first chord");
        require(!model.snapshot().trialStarted, "trial clock: not started yet");
        require(!model.expiredGateActive(), "trial clock: no gate yet");

        model.notePlaybackStarted();
        require(model.snapshot().trialStarted, "trial clock: starts after the first chord");

        // 60 seconds of use.
        for (int tick = 0; tick < 600; ++tick)
            model.advanceTrial(chordengine::licensing::tickMs);
        require(model.snapshot().trialMs == chordengine::licensing::trialTotalMs - 60000.0,
                "trial clock: advances by exactly the elapsed usage");
        require(chordengine::licensing::trialStatusText(model.snapshot())
                    == "Trial access  29:00 remaining",
                "trial clock: the INFO line counts down live");

        // Run the remaining budget out.
        bool expired = false;
        for (int tick = 0; tick < 20000 && !expired; ++tick)
            expired = model.advanceTrial(chordengine::licensing::tickMs);

        require(expired, "trial clock: the budget actually expires");
        require(model.snapshot().trialExpired, "trial clock: expired flag is set");
        require(model.snapshot().trialMs == 0.0, "trial clock: remaining budget is 0");
        require(model.expiredGateActive(), "trial clock: the expiry gate is active");
        require(chordengine::licensing::trialStatusText(model.snapshot()) == "Access expired",
                "trial clock: the INFO line reports the expiry");
        require(!model.advanceTrial(chordengine::licensing::tickMs),
                "trial clock: expiry fires exactly once");
        require(model.snapshot().trialMs == 0.0,
                "trial clock: the budget can never go negative");

        // A licensed session ignores the clock entirely.
        CachedEntitlement verified;
        verified.entitled = true;
        verified.subscribed = true;
        verified.subscriptionEntitles = true;
        LicensingModel licensed;
        licensed.loadToken("token", "Martin", verified, chordengine::platform::currentTimeMillis());
        licensed.applyCachedEntitlement(chordengine::platform::currentTimeMillis());
        require(licensed.snapshot().licensed, "trial clock: a verified token licenses the session");
        licensed.notePlaybackStarted();
        for (int tick = 0; tick < 20000; ++tick)
            licensed.advanceTrial(chordengine::licensing::tickMs);
        require(licensed.snapshot().trialMs == chordengine::licensing::trialTotalMs,
                "trial clock: a licensed session never consumes the trial");
        require(!licensed.expiredGateActive(),
                "trial clock: a licensed session never reaches the gate");
        casePassed();
    }

    // =================================================================
    // 8. Update state transitions with a mocked service.
    // =================================================================
    {
        MockAuthService auth;
        MockUpdateService update;
        LicensingBoundary boundary(auth, update, temporaryAuthFile("updates"));
        boundary.initialise();

        require(boundary.snapshot().updateState == chordengine::licensing::UpdateState::idle,
                "update state: starts idle");

        update.next = { true, "0.5.0", "Update available" };
        boundary.checkForUpdatesClicked();
        require(update.calls == 1, "update state: the service is consulted exactly once");
        require(boundary.snapshot().updateState == chordengine::licensing::UpdateState::checked,
                "update state: a successful check reaches checked");
        require(chordengine::licensing::latestVersionText(boundary.snapshot())
                    == "Latest version  0.5.0",
                "update state: a real latest version populates the row");
        require(chordengine::licensing::updateStatusText(boundary.snapshot())
                    == "Status  Update available",
                "update state: the service status populates the row");

        update.next = { false, {}, chordengine::licensing::updateStatusUnavailable };
        boundary.checkForUpdatesClicked();
        require(chordengine::licensing::latestVersionText(boundary.snapshot())
                    == "Latest version  -",
                "update state: an unavailable version is never fabricated");
        require(chordengine::licensing::updateStatusText(boundary.snapshot())
                    == "Status  Open Music-Prod Studio to check for updates",
                "update state: the honest status is shown instead");

        // The shipped service (no plugin-facing update API) must report the
        // reference behaviour verbatim.
        chordengine::licensing::MusicProdUpdateService shipped;
        const auto shippedResult = shipped.check();
        require(!shippedResult.available,
                "update state: the shipped service reports no plugin-facing version");
        require(shippedResult.status
                    == std::string(chordengine::licensing::updateStatusUnavailable),
                "update state: the shipped status matches the reference exactly");
        casePassed();
    }

    // =================================================================
    // 9. Sign-in transitions with a testable abstraction (and no blocking).
    // =================================================================
    {
        MockAuthService auth;
        MockUpdateService update;
        const auto authFile = temporaryAuthFile("auth");
        LicensingBoundary boundary(auth, update, authFile);
        boundary.initialise();

        require(auth.startCalls == 0, "sign in: nothing is requested before the click");
        boundary.signInButtonClicked();

        // The boundary must have handed the request to the service and
        // returned immediately, without waiting for a reply.
        require(auth.startCalls == 1, "sign in: one device-code start is requested");
        require(auth.lastDeviceName == "ChordEngine",
                "sign in: the reference device name is reported");
        require(auth.lastPluginVersion == CHORDENGINE_PLUGIN_VERSION_STRING,
                "sign in: the version of THIS build is reported as plugin_version, "
                "never the reference product's 0.4.0");
        require(auth.lastProduct == "chordengine",
                "sign in: the explicit product slug is sent, never inferred from the "
                "display device name");
        require(chordengine::licensing::isDeviceIdFormat(auth.lastDeviceId),
                "sign in: a valid per-installation device id travels with the pairing code");
        require(boundary.snapshot().phase == chordengine::licensing::Phase::linking,
                "sign in: the session enters the linking phase");
        require(!boundary.snapshot().polling,
                "sign in: nothing polls until the server answers");
        require(!boundary.snapshot().hasToken,
                "sign in: no token exists before the server answers");

        // The server answers with the pairing code.
        auth.pendingStart(HttpReply { 200,
            jsonObject({ { "device_code", "dev-123" },
                         { "user_code", "ABCD-EFGH" },
                         { "verification_url_complete", "https://music-prod.com/link?code=ABCD-EFGH" } }),
            true });

        require(boundary.snapshot().polling, "sign in: polling starts after the start reply");
        require(boundary.snapshot().userCode == "ABCD-EFGH",
                "sign in: the public pairing code is available for display");

        std::string verificationUrl;
        require(boundary.consumeVerificationUrl(verificationUrl),
                "sign in: the approval URL is published once");
        require(verificationUrl == "https://music-prod.com/link?code=ABCD-EFGH",
                "sign in: the server-returned URL is opened verbatim");
        require(!boundary.consumeVerificationUrl(verificationUrl),
                "sign in: the approval URL is consumed only once");

        // The poll cadence is the reference's ~0.8 s at 10 Hz.
        for (int tick = 0; tick < 12 && auth.pollCalls == 0; ++tick)
            boundary.tick();
        require(auth.pollCalls >= 1, "sign in: the device code is polled");
        require(auth.lastPolledCode == "dev-123",
                "sign in: the poll carries the device code from the server");

        // Approval mints the token and immediately asks for entitlements.
        auth.pendingPoll(HttpReply { 200,
            jsonObject({ { "status", "approved" },
                         { "token", "opaque-bearer-token" },
                         { "display_name", "Martin" } }),
            true });

        require(chordengine::licensing::isSignedIn(boundary.snapshot()),
                "sign in: approval signs the session in");
        require(auth.entitlementCalls == 1,
                "sign in: entitlements are verified right after approval");
        require(auth.lastToken == "opaque-bearer-token",
                "sign in: the opaque token is forwarded, nothing else");
        require(boundary.snapshot().user == "Martin",
                "sign in: the server display name is shown");

        // A verified, PRODUCT-SCOPED entitlement licenses the session. The
        // gate is `plugin_entitled`; `subscribed` is display only.
        auth.pendingEntitlements(HttpReply { 200,
            jsonObject({ { "ok", true },
                         { "subscribed", true },
                         { "plugin_entitled", true } }),
            true });
        require(boundary.snapshot().licensed, "sign in: a verified subscription licenses Plus");
        require(chordengine::licensing::plusLineText(boundary.snapshot())
                    == "Music-Prod+  ACTIVE",
                "sign in: the Plus line reports the verified state");
        require(chordengine::licensing::trialStatusText(boundary.snapshot())
                    == "Music-Prod+ active - full access",
                "sign in: the trial line reports full access");
        require(!boundary.expiredGateActive(),
                "sign in: a licensed session is never gated");

        // A 401 clears the token AND the remembered entitlement.
        require(std::filesystem::exists(authFile),
                "sign in: the token is persisted next to the other plugin state");

        auth.pendingEntitlements(HttpReply { 401, {}, true });
        require(!boundary.snapshot().licensed,
                "sign in: a 401 drops the entitlement in the same transition");
        require(!boundary.snapshot().hasToken, "sign in: a 401 drops the token");
        require(boundary.snapshot().phase == chordengine::licensing::Phase::signedOut,
                "sign in: a 401 returns the session to signed out");
        casePassed();
    }

    // =================================================================
    // 10. Account button states (SIGN IN / CANCEL LINK / SIGN OUT).
    // =================================================================
    {
        MockAuthService auth;
        MockUpdateService update;
        LicensingBoundary boundary(auth, update, temporaryAuthFile("account"));
        boundary.initialise();

        require(chordengine::licensing::signButtonText(boundary.snapshot()) == "SIGN IN",
                "account buttons: the idle label is SIGN IN");
        require(!chordengine::licensing::isSignedIn(boundary.snapshot()),
                "account buttons: signed out by default");
        require(!boundary.snapshot().polling, "account buttons: not linking by default");

        // SIGN IN starts the link; the same button now cancels it.
        boundary.signInButtonClicked();
        auth.pendingStart(HttpReply { 200,
            jsonObject({ { "device_code", "dev-9" },
                         { "user_code", "ZZZZ-9999" },
                         { "verification_url_complete", "https://music-prod.com/link?code=ZZZZ-9999" } }),
            true });
        require(chordengine::licensing::signButtonText(boundary.snapshot()) == "CANCEL LINK",
                "account buttons: an in-flight link shows CANCEL LINK");

        boundary.signInButtonClicked();
        require(!boundary.snapshot().polling, "account buttons: CANCEL LINK stops the link");
        require(!chordengine::licensing::isSignedIn(boundary.snapshot()),
                "account buttons: cancelling does not sign anyone in");
        require(chordengine::licensing::signButtonText(boundary.snapshot()) == "SIGN IN",
                "account buttons: cancelling restores SIGN IN");

        // Sign in for real, then sign out: the server logout is requested.
        boundary.signInButtonClicked();
        auth.pendingStart(HttpReply { 200,
            jsonObject({ { "device_code", "dev-10" },
                         { "user_code", "AAAA-1111" },
                         { "verification_url_complete", "https://music-prod.com/link?code=AAAA-1111" } }),
            true });
        for (int tick = 0; tick < 12 && auth.pollCalls < 1; ++tick)
            boundary.tick();
        require(auth.pollCalls >= 1, "account buttons: the device code is polled");
        auth.pendingPoll(HttpReply { 200,
            jsonObject({ { "status", "approved" },
                         { "token", "tok-2" },
                         { "display_name", "Martin" } }),
            true });
        auth.pendingEntitlements(HttpReply { 200,
            jsonObject({ { "subscribed", true }, { "plugin_entitled", true } }), true });
        require(chordengine::licensing::isSignedIn(boundary.snapshot()),
                "account buttons: signed in after approval");

        boundary.signOutButtonClicked();
        require(auth.logoutCalls == 1,
                "account buttons: SIGN OUT revokes the token server-side");
        require(!boundary.snapshot().hasToken, "account buttons: SIGN OUT clears the token");
        require(!boundary.snapshot().licensed, "account buttons: SIGN OUT clears the entitlement");
        require(chordengine::licensing::signButtonText(boundary.snapshot()) == "SIGN IN",
                "account buttons: SIGN OUT restores SIGN IN");
        casePassed();
    }

    // =================================================================
    // 11. The licensing gate stops chord generation (reference onNoteOn).
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);

        AudioBuffer audio(0, 512);
        MidiBuffer midi;
        midi.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
        runBlock(processor, audio, midi);
        require(generatedNoteOnPitches(midi).size() > 0,
                "gate: an open gate generates the chord");

        processor.setLicensingGateOpen(false);
        require(!processor.licensingGateOpen(), "gate: the gate can be closed");

        // A trigger that was never played while the gate was open, so the
        // visual state below cannot be inherited from the earlier chord.
        AudioBuffer gatedAudio(0, 512);
        MidiBuffer gatedMidi;
        gatedMidi.addEvent(MidiMessage::noteOn(1, 72, static_cast<std::uint8_t>(100)), 0);
        runBlock(processor, gatedAudio, gatedMidi);
        require(gatedMidi.isEmpty(),
                "gate: a closed gate generates no chord AND swallows the trigger");
        require(!processor.isAnyTriggerNoteActive(72)
                    && !processor.isAnyTriggerNoteDown(72),
                "gate: a closed gate produces no visual state either");

        processor.setLicensingGateOpen(true);
        AudioBuffer resumeAudio(0, 512);
        MidiBuffer resumeMidi;
        resumeMidi.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
        runBlock(processor, resumeAudio, resumeMidi);
        require(generatedNoteOnPitches(resumeMidi).size() > 0,
                "gate: reopening the gate restores chord generation");
        casePassed();
    }

    // =================================================================
    // 12. The trial usage clock follows real generated chords.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);
        require(!processor.consumePlaybackActivity(),
                "trial start: a silent plugin reports no playback");

        AudioBuffer audio(0, 512);
        MidiBuffer midi;
        midi.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
        runBlock(processor, audio, midi);

        require(processor.consumePlaybackActivity(),
                "trial start: a generated chord reports playback activity");
        require(!processor.consumePlaybackActivity(),
                "trial start: the flag is consumed exactly once");
        casePassed();
    }

    // =================================================================
    // 14. Realtime-thread and GUI isolation invariants.
    // =================================================================
    {
        const auto processorSource = readSourceFile("Source/PluginProcessor.cpp");

        const auto blockStart = processorSource.find("void ChordEngineAudioProcessor::processBlock");
        require(blockStart != std::string::npos, "realtime isolation: processBlock must exist");
        const auto tail = processorSource.substr(blockStart);
        const auto relativeEnd = tail.find("std::string ChordEngineAudioProcessor::getStateInformation");
        require(relativeEnd != std::string::npos, "realtime isolation: processBlock bounds must be found");
        const auto processBlockBody = tail.substr(0, relativeEnd);

        for (const auto* token : realtimeForbiddenTokens)
            require(!chordengine::text::contains(processBlockBody, token),
                    std::string("realtime isolation: processBlock must not reference '") + token + "'");

        // The processor knows nothing of the editor: the adapters create it.
        require(!chordengine::text::contains(processorSource, "UI/"),
                "realtime isolation: the processor includes no editor code");

        // The network path lives in the auth service, which hands every
        // request to its background worker queue.
        const auto authServiceHeader = readSourceFile("Source/Licensing/MusicProdAuthService.h");
        const auto authServiceSource = readSourceFile("Source/Licensing/MusicProdAuthService.cpp");
        require(chordengine::text::contains(authServiceHeader, "platform::WorkerQueue"),
                "realtime isolation: the auth service owns a background worker");
        require(chordengine::text::contains(authServiceSource, "worker_.post"),
                "realtime isolation: every request is dispatched off the calling thread");
        require(chordengine::text::contains(authServiceSource, "platform::callOnMainThread"),
                "realtime isolation: replies return to the main thread only");

        // The licensing boundary never performs I/O inline.
        const auto boundarySource = readSourceFile("Source/Licensing/LicensingBoundary.cpp");
        require(!chordengine::text::contains(boundarySource, "httpRequest"),
                "realtime isolation: the boundary performs no HTTP itself");

        // No licensing network call anywhere in the audio processor.
        require(!chordengine::text::contains(processorSource, "httpRequest"),
                "realtime isolation: no server call exists in the processor");
        require(!chordengine::text::contains(processorSource, "wfpeajmdojcjqyrsnxbk"),
                "realtime isolation: the processor embeds no auth endpoint");
        casePassed();
    }

    constexpr int expectedCaseCount = 13;
    require(casesPassed == expectedCaseCount,
            "every functional case must run: expected " + std::to_string(expectedCaseCount)
                + ", executed " + std::to_string(casesPassed));

    std::cout << "ChordEngineFunctionalTests: " << casesPassed << "/" << expectedCaseCount
              << " processor and licensing functional cases passed (editor cases: ChordEngineEditorTests)\n";
    return EXIT_SUCCESS;
}
