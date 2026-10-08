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
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Updates/UpdateTransport.h"
#include "Updates/UpdateTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace juce
{
void juce_VerifyPlugin();
void juce_VerifyPlugin() {}
}

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
    void start(const juce::String& deviceName, const juce::String& pluginVersion,
               Callback callback) override
    {
        ++startCalls;
        lastDeviceName = deviceName;
        lastPluginVersion = pluginVersion;
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

    int startCalls = 0;
    int pollCalls = 0;
    int entitlementCalls = 0;
    int logoutCalls = 0;
    juce::String lastDeviceName;
    juce::String lastPluginVersion;
    juce::String lastPolledCode;
    juce::String lastToken;
    Callback pendingStart;
    Callback pendingPoll;
    Callback pendingEntitlements;
    Callback pendingLogout;
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
    StubUpdateTransport(int httpStatus, juce::String responseBody)
        : status(httpStatus), body(std::move(responseBody))
    {
    }

    chordengine::updates::TransportReply get(const juce::String& url) override
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
    juce::String body;
    int calls = 0;
    juce::String lastUrl;
};

// --------------------------------------------------------------- helpers
juce::File temporaryStorageDirectory(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("ChordEngine-FunctionalTests")
                   .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

// LicensingBoundary owns the auth FILE (the editor appends auth.json to its
// storage directory); tests point it at a file inside a temp directory.
juce::File temporaryAuthFile(const juce::String& name)
{
    return temporaryStorageDirectory(name).getChildFile("auth.json");
}

void runBlock(ChordEngineAudioProcessor& processor, juce::AudioBuffer<float>& audio,
              juce::MidiBuffer& midi)
{
    processor.processBlock(audio, midi);
}

std::vector<int> generatedNoteOnPitches(const juce::MidiBuffer& buffer)
{
    std::vector<int> pitches;
    for (const auto metadata : buffer)
        if (metadata.getMessage().isNoteOn())
            pitches.push_back(metadata.getMessage().getNoteNumber());
    return pitches;
}

std::vector<int> generatedNoteOnVelocities(const juce::MidiBuffer& buffer)
{
    std::vector<int> velocities;
    for (const auto metadata : buffer)
        if (metadata.getMessage().isNoteOn())
            velocities.push_back(metadata.getMessage().getVelocity());
    return velocities;
}

juce::var jsonObject(const std::vector<std::pair<const char*, juce::var>>& fields)
{
    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    for (const auto& field : fields)
        object->setProperty(field.first, field.second);
    return juce::var(object.get());
}

void pumpMessages(int milliseconds)
{
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        manager->runDispatchLoopUntil(milliseconds);
}

juce::Component* findComponentWithId(juce::Component& parent, const juce::String& componentId)
{
    if (parent.getComponentID() == componentId)
        return &parent;

    for (auto* child : parent.getChildren())
        if (auto* found = findComponentWithId(*child, componentId))
            return found;

    return nullptr;
}

juce::Button* findButton(juce::Component& parent, const juce::String& componentId)
{
    return dynamic_cast<juce::Button*>(findComponentWithId(parent, componentId));
}

void clickButton(juce::Component& parent, const juce::String& componentId,
                 const juce::String& context)
{
    auto* button = findButton(parent, componentId);
    require(button != nullptr, context + ": control '" + componentId + "' must exist");
    require(button->isEnabled(), context + ": control '" + componentId + "' must be enabled");
    button->triggerClick();
    pumpMessages(30);
}

juce::String labelText(juce::Component& parent, const juce::String& componentId,
                       const juce::String& context)
{
    auto* label = dynamic_cast<juce::Label*>(findComponentWithId(parent, componentId));
    require(label != nullptr, context + ": label '" + componentId + "' must exist");
    return label->getText();
}

// Builds the SAME MouseEvent the desktop delivers, so interactive controls can
// be exercised through their real mouse handlers instead of a setValue
// shortcut. `localPosition` is relative to `component`, exactly as a real
// event's position is.
juce::MouseEvent makeMouseEvent(juce::Component& component, juce::Point<float> localPosition,
                                bool dragged)
{
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return { source, localPosition, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &component, &component, now, localPosition, now, 1, dragged };
}

// Presses at the position of `fromValue` and drags to the position of
// `toValue` through the slider's own mouse handlers - a real click-and-drag.
void dragSlider(juce::Slider& slider, double fromValue, double toValue)
{
    const auto localY = static_cast<float>(slider.getHeight()) * 0.5f;
    const juce::Point<float> from { slider.getPositionOfValue(fromValue), localY };
    const juce::Point<float> to { slider.getPositionOfValue(toValue), localY };

    slider.mouseDown(makeMouseEvent(slider, from, false));
    slider.mouseDrag(makeMouseEvent(slider, to, true));
    slider.mouseUp(makeMouseEvent(slider, to, true));
    pumpMessages(10);
}

// Reads a source file relative to the project root.
juce::String readSourceFile(const juce::String& relativePath)
{
    const juce::File file(juce::String(CHORDENGINE_SOURCE_DIR) + "/" + relativePath);
    require(file.existsAsFile(), "source file must exist: " + relativePath);
    return file.loadFileAsString();
}

// Every token that would prove GUI work or licensing work happening where it
// must never happen.
const std::array<const char*, 12> realtimeForbiddenTokens {{
    "juce::URL", "LicensingBoundary", "MusicProdAuthService", "AuthService",
    "callAsync", "MessageManager", "createEditor", "AudioProcessorEditor",
    "repaint", "setBounds", "getEditor", "ThreadPool" }};
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI guiInitialiser;
    juce::ignoreUnused(guiInitialiser);

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
                    "fixed velocity: " + juce::String(velocity) + " must be accepted");
            require(processor.configurationSnapshot().fixedVelocity == velocity,
                    "fixed velocity: Core state must read back "
                        + juce::String(velocity));
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
                    "fixed velocity MIDI: set " + juce::String(velocity));

            juce::AudioBuffer<float> audio(0, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(99)), 0);
            runBlock(processor, audio, midi);

            const auto velocities = generatedNoteOnVelocities(midi);
            require(velocities.size() > 0,
                    "fixed velocity MIDI: a chord must be generated at "
                        + juce::String(velocity));
            for (const auto generated : velocities)
                require(generated == velocity,
                        "fixed velocity MIDI: every generated note must carry "
                            + juce::String(velocity) + ", got " + juce::String(generated));
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
            juce::AudioBuffer<float> audio(0, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(inputVelocity)), 0);
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
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        runBlock(processor, audio, midi);
        require(processor.recorderState() == 2,
                "recorder stop: the first generated note-on starts RECORDING");

        juce::MidiBuffer stopBlock;
        juce::AudioBuffer<float> stopAudio(0, 512);
        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
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
        LicensingModel licensed;
        licensed.loadToken("token", "Martin", true, juce::Time::currentTimeMillis());
        licensed.applyCachedEntitlement(juce::Time::currentTimeMillis());
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
                    == juce::String(chordengine::licensing::updateStatusUnavailable),
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
        require(auth.lastPluginVersion == "0.4.0",
                "sign in: the reference product version is reported");
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

        juce::String verificationUrl;
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

        // A verified subscription licenses the session.
        auth.pendingEntitlements(HttpReply { 200,
            jsonObject({ { "ok", true }, { "subscribed", true } }), true });
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
        require(authFile.existsAsFile(),
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
            jsonObject({ { "subscribed", true } }), true });
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

        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(audio, midi);
        require(generatedNoteOnPitches(midi).size() > 0,
                "gate: an open gate generates the chord");

        processor.setLicensingGateOpen(false);
        require(!processor.licensingGateOpen(), "gate: the gate can be closed");

        // A trigger that was never played while the gate was open, so the
        // visual state below cannot be inherited from the earlier chord.
        juce::AudioBuffer<float> gatedAudio(0, 512);
        juce::MidiBuffer gatedMidi;
        gatedMidi.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(gatedAudio, gatedMidi);
        require(gatedMidi.isEmpty(),
                "gate: a closed gate generates no chord AND swallows the trigger");
        require(!processor.isAnyTriggerNoteActive(72)
                    && !processor.isAnyTriggerNoteDown(72),
                "gate: a closed gate produces no visual state either");

        processor.setLicensingGateOpen(true);
        juce::AudioBuffer<float> resumeAudio(0, 512);
        juce::MidiBuffer resumeMidi;
        resumeMidi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(resumeAudio, resumeMidi);
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

        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(audio, midi);

        require(processor.consumePlaybackActivity(),
                "trial start: a generated chord reports playback activity");
        require(!processor.consumePlaybackActivity(),
                "trial start: the flag is consumed exactly once");
        casePassed();
    }

    // =================================================================
    // 13. Real bound editor callbacks update real state.
    // =================================================================
    {
        auto processor = std::make_unique<ChordEngineAudioProcessor>();
        processor->prepareToPlay(44100.0, 512);

        // The update transport is injected: the INFO update rows are driven by
        // the REAL client, but no test ever touches the live service. The
        // scripted body is the verbatim live `chordengine` answer.
        auto updateTransport = std::make_unique<StubUpdateTransport>(200,
            R"({"success":true,"data":{"product":"chordengine","currentVersion":"0.4.0","latestVersion":null,"updateAvailable":false,"decision":"upToDate","mustUpdate":false,"reason":"no published release exists for this product yet","mandatory":false,"minimumSupportedVersion":null,"releaseNotes":null,"artifacts":[],"channel":"stable","latestBuildNumber":null,"latestReleaseId":null,"publishedAt":null,"status":"noPublishedRelease"}})");
        auto* updateStub = updateTransport.get();
        auto editor = std::make_unique<ChordEngineAudioProcessorEditor>(
            *processor, temporaryStorageDirectory("editor"), std::move(updateTransport));

        // The update check is asynchronous: wait for it to settle before
        // reading the INFO rows it drives.
        const auto waitForUpdateCheck = [&editor]
        {
            for (int tick = 0; tick < 200 && editor->updateService().isCheckInFlight(); ++tick)
                pumpMessages(10);
        };

        // ---- recorder arm / cancel through the real button -------------
        clickButton(*editor, "recorder-action", "editor buttons");
        require(processor->recorderState() == 1,
                "editor buttons: RECORD ARM arms the recorder");
        require(findButton(*editor, "recorder-action")->getButtonText() == "CANCEL",
                "editor buttons: the armed state relabels the control");
        clickButton(*editor, "recorder-action", "editor buttons");
        require(processor->recorderState() == 0,
                "editor buttons: CANCEL returns the recorder to idle");

        // ---- navigation -----------------------------------------------
        clickButton(*editor, "info-button", "editor buttons");
        auto* infoTitle = findComponentWithId(*editor, "info-title");
        require(infoTitle != nullptr && infoTitle->isVisible(),
                "editor buttons: INFO shows the INFO page");
        clickButton(*editor, "chord-button", "editor buttons");
        require(!infoTitle->isVisible(),
                "editor buttons: CHORD hides the INFO page");

        // ---- velocity mode + fixed velocity slider ---------------------
        auto* velocityMode = dynamic_cast<juce::ComboBox*>(
            findComponentWithId(*editor, "velocity-mode-selector"));
        require(velocityMode != nullptr, "editor buttons: the velocity mode selector exists");
        auto* fixedSlider = dynamic_cast<juce::Slider*>(
            findComponentWithId(*editor, "fixed-velocity-slider"));
        require(fixedSlider != nullptr, "editor buttons: the fixed velocity slider exists");

        require(!fixedSlider->isEnabled(),
                "editor buttons: the reference disables fixed velocity in Dynamic mode");

        velocityMode->setSelectedId(3, juce::sendNotificationSync);
        require(processor->configurationSnapshot().velocityMode
                    == chordengine::core::VelocityMode::fixed,
                "editor buttons: selecting Fixed updates Core state");
        require(fixedSlider->isEnabled(),
                "editor buttons: the reference enables fixed velocity in Fixed mode");

        fixedSlider->setValue(64.0, juce::sendNotificationSync);
        require(processor->configurationSnapshot().fixedVelocity == 64,
                "editor buttons: moving the slider updates Core state");
        require(labelText(*editor, "fixed-velocity-value", "editor buttons") == "64",
                "editor buttons: the numeric readout updates live");

        fixedSlider->setValue(1.0, juce::sendNotificationSync);
        require(processor->configurationSnapshot().fixedVelocity == 1,
                "editor buttons: the slider works at 1");
        fixedSlider->setValue(127.0, juce::sendNotificationSync);
        require(processor->configurationSnapshot().fixedVelocity == 127,
                "editor buttons: the slider works at 127");

        // ---- transpose bounds -----------------------------------------
        auto* down = findButton(*editor, "octave-down");
        auto* up = findButton(*editor, "octave-up");
        require(down != nullptr && up != nullptr, "editor buttons: the octave steppers exist");
        clickButton(*editor, "octave-down", "editor buttons");
        require(processor->configurationSnapshot().transpose.octaveSteps == -1,
                "editor buttons: minus steps the octave down");
        clickButton(*editor, "octave-down", "editor buttons");
        require(processor->configurationSnapshot().transpose.octaveSteps == -2,
                "editor buttons: the octave steps to the -2 bound");
        clickButton(*editor, "octave-down", "editor buttons");
        require(processor->configurationSnapshot().transpose.octaveSteps == -2,
                "editor buttons: the octave stays at the -2 bound");
        clickButton(*editor, "octave-up", "editor buttons");
        clickButton(*editor, "octave-up", "editor buttons");
        clickButton(*editor, "octave-up", "editor buttons");
        require(processor->configurationSnapshot().transpose.octaveSteps == 1,
                "editor buttons: plus steps the octave up one at a time");
        clickButton(*editor, "octave-up", "editor buttons");
        require(processor->configurationSnapshot().transpose.octaveSteps == 2,
                "editor buttons: the octave steps to the +2 bound");
        clickButton(*editor, "octave-up", "editor buttons");
        require(processor->configurationSnapshot().transpose.octaveSteps == 2,
                "editor buttons: the octave stays at the +2 bound");

        // ---- the update rows are the real client's, never placeholders --
        clickButton(*editor, "info-button", "editor buttons");
        waitForUpdateCheck();
        require(labelText(*editor, "info-current-version", "editor buttons")
                    == "Current version  0.1.0",
                "editor buttons: the version row uses the actual build metadata");
        require(labelText(*editor, "info-latest-version", "editor buttons")
                    == "Latest version  No published release",
                "editor buttons: the live no-release answer replaces the placeholder");
        require(labelText(*editor, "info-update-status", "editor buttons")
                    == "Status  No published release",
                "editor buttons: noPublishedRelease is never reported as up to date");

        // CHECK FOR UPDATES is a real button and performs exactly one real check.
        require(findButton(*editor, "check-updates") != nullptr,
                "editor buttons: CHECK FOR UPDATES is a real juce::Button");
        waitForUpdateCheck();
        const int requestsBeforeClick = updateStub->calls;
        clickButton(*editor, "check-updates", "editor buttons");
        waitForUpdateCheck();
        require(updateStub->calls == requestsBeforeClick + 1,
                "editor buttons: one click performs exactly one update request");
        require(updateStub->lastUrl.contains("product=chordengine")
                    && updateStub->lastUrl.contains("current=0.1.0")
                    && updateStub->lastUrl.contains("platform="),
                "editor buttons: the anonymous check carries the product, the build version "
                "and the platform");
        require(labelText(*editor, "info-update-status", "editor buttons")
                    == "Status  No published release",
                "editor buttons: the INFO status reflects the completed check");

        // ---- the real logo asset and the disabled internal-audio pill --
        require(labelText(*editor, "internal-audio-sub-label", "editor buttons")
                    == "MIDI effect - no audio bus",
                "editor buttons: internal audio states its unavailability");

        // ---- the expiry gate is wired and hidden until it is needed -----
        auto* gateTitle = dynamic_cast<juce::Label*>(
            findComponentWithId(*editor, "trial-gate-title"));
        auto* gateBody = dynamic_cast<juce::Label*>(
            findComponentWithId(*editor, "trial-gate-body"));
        require(gateTitle != nullptr && gateBody != nullptr,
                "editor buttons: the expiry gate surfaces exist");
        require(!gateTitle->isVisible() && !gateBody->isVisible(),
                "editor buttons: the expiry gate stays hidden while access is allowed");
        require(gateTitle->getText() == "ACCESS EXPIRED",
                "editor buttons: the gate heading matches the reference");
        require(gateBody->getText().contains("30-minute ChordEngine trial has ended"),
                "editor buttons: the gate body matches the reference wording");
        require(gateBody->getText().contains("Music-Prod"),
                "editor buttons: the gate body tells the user how to restore access");

        require(findButton(*editor, "trial-gate-studio") != nullptr
                    && findButton(*editor, "trial-gate-sign-in") != nullptr,
                "editor buttons: the gate carries both real recovery controls");

        editor.reset();
        processor.reset();
        casePassed();
    }

    // =================================================================
    // 14. Realtime-thread and GUI isolation invariants.
    // =================================================================
    {
        const auto processorSource = readSourceFile("Source/PluginProcessor.cpp");

        const auto blockStart = processorSource.indexOf("void ChordEngineAudioProcessor::processBlock");
        require(blockStart >= 0, "realtime isolation: processBlock must exist");
        const auto tail = processorSource.substring(blockStart);
        const auto relativeEnd = tail.indexOf("juce::AudioProcessorEditor* ChordEngineAudioProcessor::createEditor");
        require(relativeEnd > 0, "realtime isolation: processBlock bounds must be found");
        const auto processBlockBody = tail.substring(0, relativeEnd);

        for (const auto* token : realtimeForbiddenTokens)
            require(!processBlockBody.contains(token),
                    juce::String("realtime isolation: processBlock must not reference '")
                        + token + "'");

        // The only GUI interaction in the processor is createEditor().
        require(processorSource.contains("ChordEngineAudioProcessor::createEditor()"),
                "realtime isolation: createEditor remains the single GUI entry point");

        // The network path lives in the auth service, which hands every
        // request to a background thread pool.
        const auto authServiceHeader = readSourceFile("Source/Licensing/MusicProdAuthService.h");
        const auto authServiceSource = readSourceFile("Source/Licensing/MusicProdAuthService.cpp");
        require(authServiceHeader.contains("juce::ThreadPool"),
                "realtime isolation: the auth service owns a background thread pool");
        require(authServiceSource.contains("addJob"),
                "realtime isolation: every request is dispatched off the calling thread");
        require(authServiceSource.contains("MessageManager::callAsync"),
                "realtime isolation: replies return to the message thread only");

        // The licensing boundary never performs I/O inline.
        const auto boundarySource = readSourceFile("Source/Licensing/LicensingBoundary.cpp");
        require(!boundarySource.contains("createInputStream"),
                "realtime isolation: the boundary performs no HTTP itself");
        require(!boundarySource.contains("juce::URL"),
                "realtime isolation: the boundary opens no URL itself");

        // No licensing network call anywhere in the audio processor.
        require(!processorSource.contains("callWithPOST"),
                "realtime isolation: no server call exists in the processor");
        require(!processorSource.contains("wfpeajmdojcjqyrsnxbk"),
                "realtime isolation: the processor embeds no auth endpoint");
        casePassed();
    }

    // =================================================================
    // 15. Fixed velocity is a real mouse-driven control (drag + click).
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);
        ChordEngineAudioProcessorEditor editor(processor, temporaryStorageDirectory("velocity-drag"));
        editor.setSize(800, 650);
        pumpMessages(20);

        auto* slider = dynamic_cast<juce::Slider*>(
            findComponentWithId(editor, "fixed-velocity-slider"));
        require(slider != nullptr,
                "fixed velocity drag: the control must be a real juce::Slider, not a painted graphic");
        require(slider->getSliderStyle() == juce::Slider::LinearHorizontal,
                "fixed velocity drag: the reference linear horizontal style is used");
        require(slider->getMinimum() == 1.0 && slider->getMaximum() == 127.0
                    && slider->getInterval() == 1.0,
                "fixed velocity drag: the control spans 1..127 in steps of 1");

        auto* mode = dynamic_cast<juce::ComboBox*>(
            findComponentWithId(editor, "velocity-mode-selector"));
        require(mode != nullptr, "fixed velocity drag: the velocity mode selector exists");

        // Dynamic: inert AND visibly inert (the "live-looking but dead" bug).
        require(!slider->isEnabled(),
                "fixed velocity drag: the reference disables fixed velocity in Dynamic mode");
        require(slider->getAlpha() < 1.0f,
                "fixed velocity drag: an inactive slider is visibly dimmed, never live-looking");

        mode->setSelectedId(3, juce::sendNotificationSync);
        require(slider->isEnabled(),
                "fixed velocity drag: the reference enables fixed velocity in Fixed mode");
        require(slider->getAlpha() == 1.0f,
                "fixed velocity drag: the live slider is fully opaque");

        // A real press-and-drag through the slider's own mouse handlers - not a
        // setValue shortcut - must move the control, the Core and the MIDI.
        const auto generatedVelocities = [&processor]()
        {
            juce::AudioBuffer<float> audio(0, 512);
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(99)), 0);
            runBlock(processor, audio, on);
            const auto velocities = generatedNoteOnVelocities(on);

            juce::AudioBuffer<float> releaseAudio(0, 512);
            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            runBlock(processor, releaseAudio, off);
            return velocities;
        };

        dragSlider(*slider, 100.0, 1.0);
        require(static_cast<int>(slider->getValue()) == 1,
                "fixed velocity drag: dragging to the far left reaches 1");
        require(processor.configurationSnapshot().fixedVelocity == 1,
                "fixed velocity drag: the Core receives 1");
        require(labelText(editor, "fixed-velocity-value", "fixed velocity drag") == "1",
                "fixed velocity drag: the numeric readout follows the drag");
        for (const auto velocity : generatedVelocities())
            require(velocity == 1, "fixed velocity drag: generated MIDI carries velocity 1");

        for (const double target : { 64.0, 100.0, 127.0 })
        {
            dragSlider(*slider, 1.0, target);
            const auto reached = static_cast<int>(slider->getValue());
            require(std::abs(reached - static_cast<int>(target)) <= 1,
                    "fixed velocity drag: the slider reaches " + juce::String(target)
                        + " by dragging (got " + juce::String(reached) + ")");
            require(processor.configurationSnapshot().fixedVelocity == reached,
                    "fixed velocity drag: the Core receives the dragged value "
                        + juce::String(reached));
            require(labelText(editor, "fixed-velocity-value", "fixed velocity drag")
                        == juce::String(reached),
                    "fixed velocity drag: the readout shows the dragged value");
            for (const auto velocity : generatedVelocities())
                require(velocity == reached,
                        "fixed velocity drag: generated MIDI matches the dragged value "
                            + juce::String(reached));
        }

        // Clicking (press without drag) along the track must also move it.
        dragSlider(*slider, 127.0, 20.0);
        require(static_cast<int>(slider->getValue()) < 60,
                "fixed velocity drag: a single click low on the track moves the value down");
        require(processor.configurationSnapshot().fixedVelocity
                    == static_cast<int>(slider->getValue()),
                "fixed velocity drag: the clicked value reaches the Core");
        casePassed();
    }

    // =================================================================
    // 16. The C2-C7 on-screen keyboard is a real MIDI input surface.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);
        ChordEngineAudioProcessorEditor editor(processor, temporaryStorageDirectory("piano"));
        editor.setSize(800, 650);
        pumpMessages(20);

        auto* piano = findComponentWithId(editor, "piano-display");
        require(piano != nullptr, "piano: the on-screen keyboard exists");
        const auto area = editor.getLocalArea(piano, piano->getLocalBounds());
        require(area.getWidth() > 0 && area.getHeight() > 0, "piano: the keyboard is laid out");

        const auto noteAt = [&editor, &area](float localX, float localY)
        {
            return editor.pianoNoteAtEditorPoint({ static_cast<float>(area.getX()) + localX,
                                                   static_cast<float>(area.getY()) + localY });
        };

        const float whiteRowY = 170.0f; // below the black-key height (0.62 * keys)
        const float blackRowY = 40.0f;  // inside the black-key height

        // Painting and hit testing share one geometry, so mapping the visible
        // rows is a direct check of the key layout.
        std::vector<int> naturals;
        int previous = -1;
        for (float x = 0.0f; x < static_cast<float>(area.getWidth()); x += 1.0f)
        {
            const int note = noteAt(x, whiteRowY);
            if (note >= 0 && note != previous)
            {
                naturals.push_back(note);
                previous = note;
            }
        }
        require(naturals.size() == 36,
                "piano: the white row shows 36 natural keys C2..C7 (got "
                    + juce::String(static_cast<int>(naturals.size())) + ")");
        require(naturals.front() == 36 && naturals.back() == 96,
                "piano: the white row starts at C2 (36) and ends at C7 (96)");
        for (std::size_t index = 0; index < naturals.size(); ++index)
        {
            const int pitchClass = naturals[index] % 12;
            require(pitchClass == 0 || pitchClass == 2 || pitchClass == 4 || pitchClass == 5
                        || pitchClass == 7 || pitchClass == 9 || pitchClass == 11,
                    "piano: the white row only produces natural notes");
            if (index > 0)
                require(naturals[index] > naturals[index - 1],
                        "piano: the white row ascends left to right");
        }

        std::vector<int> chromatic;
        previous = -1;
        for (float x = 0.0f; x < static_cast<float>(area.getWidth()); x += 1.0f)
        {
            const int note = noteAt(x, blackRowY);
            if (note >= 0 && note != previous)
            {
                chromatic.push_back(note);
                previous = note;
            }
        }
        require(chromatic.size() == 61,
                "piano: the full key bed spans C2..C7 (61 keys, got "
                    + juce::String(static_cast<int>(chromatic.size())) + ")");
        for (std::size_t index = 0; index < chromatic.size(); ++index)
            require(chromatic[index] == 36 + static_cast<int>(index),
                    "piano: the key bed maps 1:1 to MIDI notes 36..96 in order");

        const auto xForNote = [&noteAt, &area](int wanted, float localY)
        {
            for (float x = 0.0f; x < static_cast<float>(area.getWidth()); x += 0.5f)
                if (noteAt(x, localY) == wanted)
                    return x;
            return -1.0f;
        };
        const auto mouseFor = [&piano](float localX, float localY, bool dragged)
        {
            return makeMouseEvent(*piano, { localX, localY }, dragged);
        };

        // ---- press C3: a real trigger note and a real chord -------------
        const float c3x = xForNote(48, whiteRowY);
        require(c3x >= 0.0f, "piano: C3 is reachable on the white row");

        piano->mouseDown(mouseFor(c3x, whiteRowY, false));
        juce::AudioBuffer<float> pressAudio(0, 512);
        juce::MidiBuffer pressMidi;
        runBlock(processor, pressAudio, pressMidi);
        require(processor.isTriggerNoteDown(1, 48),
                "piano: a mouse-down on C3 reaches the Core as a real trigger note");
        require(processor.activeTriggerCount() == 1,
                "piano: exactly one trigger note is held");
        const auto chord = generatedNoteOnPitches(pressMidi);
        require(chord.size() >= 3, "piano: a mouse-down generates a real chord");
        require(processor.activeOutputNoteCount() > 0,
                "piano: the generated chord is reported in the processor output state");

        // The SAME trigger through the external MIDI path must be byte-identical.
        {
            ChordEngineAudioProcessor reference;
            reference.prepareToPlay(44100.0, 512);
            juce::AudioBuffer<float> refAudio(0, 512);
            juce::MidiBuffer refMidi;
            refMidi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
            runBlock(reference, refAudio, refMidi);
            require(chord == generatedNoteOnPitches(refMidi),
                    "piano: an on-screen key generates the same chord as an external MIDI note");
            require(generatedNoteOnVelocities(pressMidi) == generatedNoteOnVelocities(refMidi),
                    "piano: an on-screen key uses the same velocity strategy as external MIDI");
            require(processor.activeOutputNoteCount() == reference.activeOutputNoteCount(),
                    "piano: an on-screen key drives the same Core output state");
        }

        // ---- drag C3 -> E3: release the old note, trigger the new one ---
        const float e3x = xForNote(52, whiteRowY);
        require(e3x >= 0.0f, "piano: E3 is reachable on the white row");
        piano->mouseDrag(mouseFor(e3x, whiteRowY, true));
        juce::AudioBuffer<float> dragAudio(0, 512);
        juce::MidiBuffer dragMidi;
        runBlock(processor, dragAudio, dragMidi);
        require(!processor.isTriggerNoteDown(1, 48), "piano: dragging off C3 releases it");
        require(processor.isTriggerNoteDown(1, 52), "piano: dragging onto E3 triggers it");
        require(generatedNoteOnPitches(dragMidi).size() >= 3,
                "piano: the drag transition generates a new chord");

        // ---- release: real note-offs downstream -------------------------
        piano->mouseUp(mouseFor(e3x, whiteRowY, true));
        juce::AudioBuffer<float> upAudio(0, 512);
        juce::MidiBuffer upMidi;
        runBlock(processor, upAudio, upMidi);
        require(!processor.isTriggerNoteDown(1, 52), "piano: mouse-up releases the trigger note");
        require(processor.activeTriggerCount() == 0, "piano: no trigger note is left held");
        bool sawNoteOff = false;
        for (const auto metadata : upMidi)
            if (metadata.getMessage().isNoteOff())
                sawNoteOff = true;
        require(sawNoteOff, "piano: mouse-up emits real note-offs for the generated chord");
        require(processor.activeOutputNoteCount() == 0,
                "piano: mouse-up releases every generated chord note");

        // The GUI never computes harmony: the editor source must contain no
        // chord-building call of its own.
        const auto editorSource = readSourceFile("Source/PluginEditor.cpp");
        require(!editorSource.contains("generateChordForTrigger"),
                "piano: the editor never resolves a chord itself");
        require(editorSource.contains("pushUiNoteOn"),
                "piano: the editor routes keys through the processor's note path");
        casePassed();
    }

    // =================================================================
    // 17. RECORD ARM is centred and contained inside the recorder card.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        ChordEngineAudioProcessorEditor editor(processor, temporaryStorageDirectory("recorder-layout"));

        const auto checkLayout = [&editor](const juce::String& sizeName)
        {
            auto* pill = findButton(editor, "recorder-action");
            require(pill != nullptr, "recorder layout (" + sizeName + "): the action pill exists");

            const auto card = editor.recorderCardBounds();
            const auto bounds = pill->getBounds();
            require(card.contains(bounds),
                    "recorder layout (" + sizeName
                        + "): RECORD ARM is fully inside the MIDI RECORDER card");
            require(std::abs(bounds.getCentreX() - card.getCentreX()) <= 1,
                    "recorder layout (" + sizeName
                        + "): RECORD ARM is horizontally centred in the card");
            require(bounds.getY() > card.getY() && bounds.getBottom() < card.getBottom(),
                    "recorder layout (" + sizeName
                        + "): RECORD ARM never touches the card boundary");
            require(bounds.getBottom() <= card.getBottom() - 2
                        && bounds.getY() >= card.getY() + 2,
                    "recorder layout (" + sizeName
                        + "): RECORD ARM keeps a visible inset inside the card");
        };

        editor.setSize(800, 650);
        pumpMessages(20);
        checkLayout("800x650");

        editor.setSize(ChordEngineAudioProcessor::minimumEditorWidth,
                       ChordEngineAudioProcessor::minimumEditorHeight);
        pumpMessages(20);
        checkLayout("640x520");

        editor.setSize(ChordEngineAudioProcessor::maximumEditorWidth,
                       ChordEngineAudioProcessor::maximumEditorHeight);
        pumpMessages(20);
        checkLayout("1280x1040");

        // The pill keeps its full arm / cancel / stop behaviour.
        editor.setSize(800, 650);
        pumpMessages(20);
        clickButton(editor, "recorder-action", "recorder layout");
        require(processor.recorderState() == 1, "recorder layout: arming still works");
        require(findButton(editor, "recorder-action")->getButtonText() == "CANCEL",
                "recorder layout: the armed state still relabels the pill");
        clickButton(editor, "recorder-action", "recorder layout");
        require(processor.recorderState() == 0, "recorder layout: cancelling still works");
        casePassed();
    }

    // =================================================================
    // 18. INTERNAL AUDIO is honestly unavailable, never a fake toggle.
    // =================================================================
    {
        ChordEngineAudioProcessor processor;
        ChordEngineAudioProcessorEditor editor(processor, temporaryStorageDirectory("internal-audio"));
        editor.setSize(800, 650);
        pumpMessages(20);

        auto* chip = findComponentWithId(editor, "internal-audio-status");
        require(chip != nullptr, "internal audio: the reference card chip exists");
        require(dynamic_cast<juce::Button*>(chip) == nullptr,
                "internal audio: the chip is not a button, so it can never fake an ON/OFF toggle");
        require(findButton(editor, "internal-audio-status") == nullptr,
                "internal audio: no clickable control is published for an unsupported feature");

        auto* tip = dynamic_cast<juce::SettableTooltipClient*>(chip);
        require(tip != nullptr && tip->getTooltip().contains("no audio buses"),
                "internal audio: the tooltip documents the exact architectural limitation");
        require(labelText(editor, "internal-audio-sub-label", "internal audio")
                    == "MIDI effect - no audio bus",
                "internal audio: the card caption states the limitation on screen");

        // The decision is architectural, so pin the architecture itself: the
        // product is a MIDI effect that rejects every audio bus layout.
        require(processor.isMidiEffect(),
                "internal audio: the plugin is a MIDI effect in both formats");
        ChordEngineAudioProcessor::BusesLayout midiOnlyLayout; // no buses at all
        require(processor.isBusesLayoutSupported(midiOnlyLayout),
                "internal audio: the MIDI-only (no audio bus) layout is the supported one");
        auto withAudio = midiOnlyLayout;
        withAudio.outputBuses.add(juce::AudioChannelSet::stereo());
        require(!processor.isBusesLayoutSupported(withAudio),
                "internal audio: an audio output bus is rejected, which is exactly why the "
                "reference's internal SineSynth cannot be provided");
        casePassed();
    }

    constexpr int expectedCaseCount = 18;
    require(casesPassed == expectedCaseCount,
            "every functional case must run: expected " + juce::String(expectedCaseCount)
                + ", executed " + juce::String(casesPassed));

    std::cout << "ChordEngineFunctionalTests: " << casesPassed << "/" << expectedCaseCount
              << " functional parity cases passed\n";
    return EXIT_SUCCESS;
}
