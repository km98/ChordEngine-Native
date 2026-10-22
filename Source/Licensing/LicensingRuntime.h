#pragma once

#include "LicensingBoundary.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace chordengine::licensing
{
// =====================================================================
// The licensing driver the PROCESSOR owns, so access is decided whether or
// not the editor window is ever opened.
//
//   * At construction the persisted token, the last verified entitlement
//     (with its 14 / 7-day offline window) and the remaining trial budget
//     are restored and the gate is published before the first audio block.
//   * A 10 Hz message-thread timer advances the trial clock (only after the
//     first generated chord, only while unlicensed) and drives the account
//     state machine, which re-verifies the entitlement in the background on
//     the auth service's own worker thread.
//   * prepareToPlay and editor-open request an immediate re-check, so a
//     lapsed Music-Prod+ membership falls back to the trial without waiting
//     for the next periodic check.
//   * The remaining trial budget is persisted, so neither reopening the
//     editor nor reloading the plug-in restarts the trial.
//
// Everything here runs on the message thread except requestRecheck(),
// which only sets a flag and may be called from any thread.
// =====================================================================
class LicensingRuntime final : private juce::Timer
{
public:
    // A null update service means the shipped MusicProdUpdateService.
    LicensingRuntime(std::unique_ptr<AuthService> auth, std::unique_ptr<UpdateService> update,
                     juce::File authFile);
    ~LicensingRuntime() override;

    // The gate the audio path reads (open == chord generation allowed). The
    // sink is called at once and then whenever the decision changes.
    void setGateSink(std::function<void(bool)> sink);
    // Polled each tick: true once a chord was generated since the last poll.
    void setPlaybackActivitySource(std::function<bool()> source);

    // Starts the 10 Hz timer (production). Tests call tick() themselves.
    void start();

    // Any thread: ask for an entitlement re-check at the next tick. Rate
    // limited, so a host calling prepareToPlay repeatedly costs one request.
    void requestRecheck() noexcept { recheckRequested_.store(true, std::memory_order_release); }

    // Message thread: the editor opened. Picks up a sign-in made by another
    // instance (auth file changed on disk) and requests a re-check.
    void editorOpened();

    // One driver step; deltaMs is the real time since the previous step.
    void tick(double deltaMs, std::int64_t nowMs);

    LicensingBoundary& boundary() noexcept { return boundary_; }
    const LicensingBoundary& boundary() const noexcept { return boundary_; }

    static constexpr double recheckIntervalMs = 60.0 * 1000.0;

private:
    void timerCallback() override;

    std::unique_ptr<AuthService> auth_;
    std::unique_ptr<UpdateService> update_;
    LicensingBoundary boundary_;
    std::function<bool()> activitySource_;
    std::atomic<bool> recheckRequested_ { false };
    std::int64_t lastRecheckMs_ = 0;
    double lastTimerMs_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicensingRuntime)
};
} // namespace chordengine::licensing
