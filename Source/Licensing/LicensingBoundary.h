#pragma once

#include "LicensingModel.h"
#include "LicensingServices.h"

#include <functional>

namespace chordengine::licensing
{
// =====================================================================
// The one place the GUI talks to. It owns the licensing model, drives the
// existing Music-Prod auth service through the shared seams, persists the
// opaque token next to the other Music-Prod plugin state, and publishes the
// single boolean the audio path reads.
//
// Contracts, enforced by the functional tests:
//   * Every public method here is a MESSAGE-THREAD operation.
//   * No method blocks: network work is handed to the AuthService, which
//     delivers its reply back on the message thread.
//   * The real-time audio/MIDI thread never calls into this class; it only
//     reads the lock-free gate via the sink.
//   * It also owns this installation's device id, because the lifetime
//     licence route entitles a DEVICE: the id is created once, kept beside
//     the token, and sent with the pairing request and every activation.
// =====================================================================
class LicensingBoundary final
{
public:
    LicensingBoundary(AuthService& auth, UpdateService& update, juce::File authFile);
    ~LicensingBoundary();

    // Restores the persisted Music-Prod token, this device's id and the last
    // VERIFIED entitlement answer, and applies the bounded, route-specific
    // offline window. No network call happens here - tick() primes the first
    // verification.
    void initialise();

    // The gate publisher: called with the value the processor's audio path
    // should read (open == chord generation allowed).
    void setGateSink(std::function<void(bool)> sink);

    void setInfoPageVisible(bool visible) noexcept;

    // 10 Hz driver (message thread only).
    void tick();
    void notePlaybackStarted();

    // Real button callbacks - reference onControl handlers.
    void signInButtonClicked();      // SIGN IN / CANCEL LINK
    void signOutButtonClicked();     // SIGN OUT
    void checkForUpdatesClicked();   // CHECK FOR UPDATES
    void cancelPendingLink();        // CANCEL LINK

    // ACTIVATE: the customer's ChordEngine licence key, typed on the INFO
    // page. The key is forwarded and then dropped; only the server's own
    // entitlement answer decides what is kept.
    void activateLicenseKey(const juce::String& licenseKey);

    // The URL the device-code flow asked us to open, consumed once.
    bool consumeVerificationUrl(juce::String& url);

    const Snapshot& snapshot() const noexcept { return model_.snapshot(); }
    // The account token for the pack store's download request, which the
    // store validates exactly like the entitlement check. Never displayed,
    // logged or persisted by the caller.
    const juce::String& storeToken() const noexcept { return model_.token(); }
    bool expiredGateActive() const noexcept { return model_.expiredGateActive(); }

    // This installation's opaque device id (never a hardware identifier).
    // Exposed for display and for tests; it grants nothing on its own.
    const juce::String& deviceId() const noexcept { return deviceId_; }

    // Test seam: drives the model directly for a mock reply.
    void deliverAuthStartReply(const HttpReply& reply);
    void deliverAuthPollReply(const HttpReply& reply);
    void deliverEntitlementsReply(const HttpReply& reply);
    void deliverActivateLicenseReply(const HttpReply& reply);

private:
    void publishGate();
    void saveAuthFile();
    void loadAuthFile();
    std::int64_t nowMs() const noexcept;
    juce::String deviceLabel() const;

    LicensingModel model_;
    AuthService& auth_;
    UpdateService& update_;
    juce::File authFile_;
    juce::String deviceId_;
    std::function<void(bool)> gateSink_;
    juce::String pendingUrl_;

    // Async auth replies come back on the message thread after this object
    // may already be gone, so every callback holds a weak reference.
    JUCE_DECLARE_WEAK_REFERENCEABLE(LicensingBoundary)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicensingBoundary)
};
} // namespace chordengine::licensing
