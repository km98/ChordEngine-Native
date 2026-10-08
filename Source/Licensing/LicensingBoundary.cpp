#include "LicensingBoundary.h"

namespace chordengine::licensing
{
namespace
{
juce::var objectToVar(juce::DynamicObject* object)
{
    return juce::var(object);
}
}

LicensingBoundary::LicensingBoundary(AuthService& auth, UpdateService& update,
                                     juce::File authFile)
    : auth_(auth), update_(update), authFile_(std::move(authFile))
{
    model_.requestLinkStart = [this]
    {
        const juce::WeakReference<LicensingBoundary> weakThis(this);
        auth_.start(juce::String(deviceName), juce::String(productVersion),
                    [weakThis](HttpReply reply)
                    {
                        if (auto* self = weakThis.get())
                            self->deliverAuthStartReply(reply);
                    });
    };

    model_.requestLinkPoll = [this](const juce::String& deviceCode)
    {
        const juce::WeakReference<LicensingBoundary> weakThis(this);
        auth_.poll(deviceCode,
                   [weakThis](HttpReply reply)
                   {
                       if (auto* self = weakThis.get())
                           self->deliverAuthPollReply(reply);
                   });
    };

    model_.requestEntitlements = [this](const juce::String& token)
    {
        const juce::WeakReference<LicensingBoundary> weakThis(this);
        auth_.entitlements(token,
                           [weakThis](HttpReply reply)
                           {
                               if (auto* self = weakThis.get())
                                   self->deliverEntitlementsReply(reply);
                           });
    };

    model_.requestLogout = [this](const juce::String& token)
    {
        const juce::WeakReference<LicensingBoundary> weakThis(this);
        auth_.logout(token, [weakThis](HttpReply) {});
    };

    model_.requestUpdateCheck = [this]
    {
        // The shipped service performs no I/O (there is no plugin-facing
        // update API), so this stays a message-thread operation.
        const auto result = update_.check();
        model_.onUpdateResult(result.available, result.latestVersion, result.status);
    };
}

LicensingBoundary::~LicensingBoundary()
{
    model_.requestLinkStart = nullptr;
    model_.requestLinkPoll = nullptr;
    model_.requestEntitlements = nullptr;
    model_.requestLogout = nullptr;
    model_.requestUpdateCheck = nullptr;
    gateSink_ = nullptr;
}

std::int64_t LicensingBoundary::nowMs() const noexcept
{
    return juce::Time::currentTimeMillis();
}

void LicensingBoundary::initialise()
{
    loadAuthFile();
    model_.applyCachedEntitlement(nowMs());
    publishGate();
}

void LicensingBoundary::setGateSink(std::function<void(bool)> sink)
{
    gateSink_ = std::move(sink);
    publishGate();
}

void LicensingBoundary::publishGate()
{
    if (gateSink_)
        gateSink_(!expiredGateActive());
}

void LicensingBoundary::setInfoPageVisible(bool visible) noexcept
{
    juce::ignoreUnused(visible);
}

void LicensingBoundary::notePlaybackStarted()
{
    model_.notePlaybackStarted();
}

void LicensingBoundary::tick()
{
    // One 100 ms step of the reference's onTimer: the usage clock first,
    // then the auth driver (prime / poll / re-verify).
    model_.advanceTrial(tickMs);
    model_.tick();

    publishGate();

    if (model_.consumePersistenceDirty())
        saveAuthFile();

    if (pendingUrl_.isEmpty() && !model_.snapshot().verificationUrl.isEmpty())
        pendingUrl_ = model_.snapshot().verificationUrl;
}

void LicensingBoundary::signInButtonClicked()
{
    model_.signInOrCancel();
    pendingUrl_ = model_.snapshot().verificationUrl;
    publishGate();
    if (model_.consumePersistenceDirty())
        saveAuthFile();
}

void LicensingBoundary::signOutButtonClicked()
{
    model_.signOut();
    publishGate();
    saveAuthFile();
}

void LicensingBoundary::cancelPendingLink()
{
    model_.cancelLink();
    pendingUrl_.clear();
}

void LicensingBoundary::checkForUpdatesClicked()
{
    model_.checkForUpdates();
}

void LicensingBoundary::deliverAuthStartReply(const HttpReply& reply)
{
    if (!reply.transportOk && reply.status == 0)
    {
        model_.onAuthStartReply(0, {});
        pendingUrl_.clear();
        return;
    }

    model_.onAuthStartReply(reply.status, reply.body);
    pendingUrl_ = model_.snapshot().verificationUrl;
}

void LicensingBoundary::deliverAuthPollReply(const HttpReply& reply)
{
    const auto wasSignedIn = isSignedIn(model_.snapshot());
    if (!reply.transportOk && reply.status == 0)
        return; // transport failure: keep the code and retry on the next tick

    model_.onAuthPollReply(reply.status, reply.body);
    if (!wasSignedIn && isSignedIn(model_.snapshot()))
        saveAuthFile();
    publishGate();
}

void LicensingBoundary::deliverEntitlementsReply(const HttpReply& reply)
{
    if (!reply.transportOk && reply.status == 0)
    {
        model_.applyCachedEntitlement(nowMs());
        publishGate();
        return;
    }

    model_.onEntitlementsReply(reply.status, reply.body, nowMs());
    if (model_.consumePersistenceDirty())
        saveAuthFile();
    publishGate();
}

bool LicensingBoundary::consumeVerificationUrl(juce::String& url)
{
    if (pendingUrl_.isEmpty())
    {
        if (model_.snapshot().verificationUrl.isEmpty())
            return false;
        url = model_.snapshot().verificationUrl;
        pendingUrl_ = url;
    }

    url = pendingUrl_;
    pendingUrl_.clear();
    model_.clearVerificationUrl();
    return url.isNotEmpty();
}

// ---------------------------------------------------------- persistence
void LicensingBoundary::saveAuthFile()
{
    if (authFile_ == juce::File())
        return;

    authFile_.getParentDirectory().createDirectory();
    authFile_.deleteFile();

    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("token", model_.token());
    object->setProperty("display_name", model_.user());
    object->setProperty("subscribed", model_.plusCached());
    object->setProperty("verified_at", static_cast<juce::int64>(model_.verifiedAtMs()));
    authFile_.replaceWithText(juce::JSON::toString(objectToVar(object.get())));
}

void LicensingBoundary::loadAuthFile()
{
    if (authFile_ == juce::File() || !authFile_.existsAsFile())
    {
        model_.loadToken({}, {}, false, 0);
        return;
    }

    const auto parsed = juce::JSON::parse(authFile_.loadFileAsString());
    if (parsed.isVoid() || parsed.isUndefined())
    {
        model_.loadToken({}, {}, false, 0);
        return;
    }

    const auto token = parsed.getProperty("token", juce::var()).toString();
    const auto user = parsed.getProperty("display_name", juce::var()).toString();
    const bool subscribed = static_cast<bool>(parsed.getProperty("subscribed", false));
    const auto verifiedAt = static_cast<std::int64_t>(
        static_cast<double>(parsed.getProperty("verified_at", 0)));

    model_.loadToken(token, user, subscribed, verifiedAt);
}
} // namespace chordengine::licensing
