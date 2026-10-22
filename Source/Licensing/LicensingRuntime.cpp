#include "LicensingRuntime.h"

#include <algorithm>

namespace chordengine::licensing
{
LicensingRuntime::LicensingRuntime(std::unique_ptr<AuthService> auth,
                                   std::unique_ptr<UpdateService> update, std::filesystem::path authFile)
    : auth_(std::move(auth)),
      update_(update != nullptr ? std::move(update) : std::make_unique<MusicProdUpdateService>()),
      boundary_(*auth_, *update_, std::move(authFile))
{
    boundary_.initialise();
}

LicensingRuntime::~LicensingRuntime()
{
    timer_.stop();
    // Keep the trial budget used in this session.
    boundary_.flushTrial();
}

void LicensingRuntime::setGateSink(std::function<void(bool)> sink)
{
    boundary_.setGateSink(std::move(sink));
}

void LicensingRuntime::setPlaybackActivitySource(std::function<bool()> source)
{
    activitySource_ = std::move(source);
}

void LicensingRuntime::start()
{
    lastTimerMs_ = platform::millisecondCounterHiRes();
    timer_.start(static_cast<int>(tickMs), [this] { timerCallback(); });
}

void LicensingRuntime::editorOpened()
{
    boundary_.reloadIfChangedOnDisk(platform::currentTimeMillis());
    requestRecheck();
}

void LicensingRuntime::timerCallback()
{
    const auto now = platform::millisecondCounterHiRes();
    const auto delta = now - lastTimerMs_;
    lastTimerMs_ = now;
    tick(delta, platform::currentTimeMillis());
}

void LicensingRuntime::tick(double deltaMs, std::int64_t nowMs)
{
    // The trial clock counts time the plug-in is actually running: a stalled
    // timer (a sleeping computer, a blocked message thread) is credited at
    // most one second, never the whole gap.
    deltaMs = std::clamp(deltaMs, 0.0, 1000.0);

    if (activitySource_ && activitySource_())
        boundary_.notePlaybackStarted();

    if (recheckRequested_.exchange(false, std::memory_order_acq_rel)
        && (lastRecheckMs_ == 0 || static_cast<double>(nowMs - lastRecheckMs_) >= recheckIntervalMs
            || nowMs < lastRecheckMs_))
    {
        lastRecheckMs_ = nowMs;
        boundary_.refreshNow();
    }

    boundary_.tick(deltaMs);
}
} // namespace chordengine::licensing
