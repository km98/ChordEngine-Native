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
// =====================================================================
class LicensingBoundary final
{
public:
    LicensingBoundary(AuthService& auth, UpdateService& update, juce::File authFile);
    ~LicensingBoundary();

    // Restores the persisted Music-Prod token + last VERIFIED entitlement
    // answer and applies the bounded offline window. No network call happens
    // here - tick() primes the first verification.
    void initialise();

    // The gate publisher: called with the value the processor's audio path
    // should read (open == chord generation allowed).
    void setGateSink(std::function<void(bool gateOpen)> sink);

    void setInfoPageVisible(bool visible) noexcept;

    // 10 Hz driver (message thread only).
    void tick();
    void notePlaybackStarted();

    // Real button callbacks - reference onControl handlers.
    void signInButtonClicked();      // SIGN IN / CANCEL LINK
    void signOutButtonClicked();     // SIGN OUT
    void checkForUpdatesClicked();   // CHECK FOR UPDATES
    void cancelPendingLink();        // CANCEL LINK

    // The URL the device-code flow asked us to open, consumed once.
    bool consumeVerificationUrl(juce::String& url);

    const Snapshot& snapshot() const noexcept { return model_.snapshot(); }
    bool expiredGateActive() const noexcept { return model_.expiredGateActive(); }

    // Test seam: drives the model directly for a mock reply.
    void deliverAuthStartReply(const HttpReply& reply);
    void deliverAuthPollReply(const HttpReply& reply);
    void deliverEntitlementsReply(const HttpReply& reply);

private:
    void publishGate();
    void saveAuthFile();
    void loadAuthFile();
    std::int64_t nowMs() const noexcept;

    LicensingModel model_;
    AuthService& auth_;
    UpdateService& update_;
    juce::File authFile_;
    std::function<void(bool)> gateSink_;
    juce::String pendingUrl_;

    // Async auth replies come back on the message thread after this object
    // may already be gone, so every callback holds a weak reference.
    JUCE_DECLARE_WEAK_REFERENCEABLE(LicensingBoundary)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicensingBoundary)
};
} // namespace chordengine::licensing
