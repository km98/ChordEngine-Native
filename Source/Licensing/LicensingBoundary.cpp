#include "LicensingBoundary.h"

namespace chordengine::licensing
{
namespace
{
juce::var objectToVar(juce::DynamicObject* object)
{
    return juce::var(object);
}

bool flagAt(const juce::var& object, const char* name, bool fallback)
{
    if (object.isVoid() || object.isUndefined())
        return fallback;

    const auto value = object.getProperty(name, juce::var());
    if (value.isVoid() || value.isUndefined())
        return fallback;

    return static_cast<bool>(value);
}

int intAt(const juce::var& object, const char* name, int fallback)
{
    if (object.isVoid() || object.isUndefined())
        return fallback;

    const auto value = object.getProperty(name, juce::var());
    if (value.isVoid() || value.isUndefined())
        return fallback;

    return static_cast<int>(static_cast<double>(value));
}
} // namespace

LicensingBoundary::LicensingBoundary(AuthService& auth, UpdateService& update,
                                     juce::File authFile)
    : auth_(auth), update_(update), authFile_(std::move(authFile))
{
    model_.requestLinkStart = [this]
    {
        const juce::WeakReference<LicensingBoundary> weakThis(this);
        // The product is EXPLICIT: the endpoint must never have to infer it
        // from the display device name, and the device id travels with the
        // pairing so the token carries this installation's identity.
        auth_.start(juce::String(deviceName), juce::String(productVersion),
                    juce::String(productSlug), deviceId_,
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

    model_.requestLicenseActivation = [this](const juce::String& licenseKey)
    {
        const juce::WeakReference<LicensingBoundary> weakThis(this);
        // The device id is required by the endpoint; the label and platform
        // are display-only and bounded server-side. The key is never stored.
        auth_.activateLicense(model_.token(), licenseKey, deviceId_, deviceLabel(),
                              juce::SystemStats::getOperatingSystemName(),
                              juce::String(productVersion),
                              [weakThis](HttpReply reply)
                              {
                                  if (auto* self = weakThis.get())
                                      self->deliverActivateLicenseReply(reply);
                              });
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
    model_.requestLicenseActivation = nullptr;
    model_.requestUpdateCheck = nullptr;
    gateSink_ = nullptr;
}

std::int64_t LicensingBoundary::nowMs() const noexcept
{
    return juce::Time::currentTimeMillis();
}

// The owner-visible name of this computer, beside the deactivate button.
juce::String LicensingBoundary::deviceLabel() const
{
    const auto name = juce::SystemStats::getComputerName().trim();
    return name.isEmpty() ? juce::String(deviceName) : name;
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

void LicensingBoundary::activateLicenseKey(const juce::String& licenseKey)
{
    model_.activateLicense(licenseKey);
    publishGate();
    if (model_.consumePersistenceDirty())
        saveAuthFile();
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

void LicensingBoundary::deliverActivateLicenseReply(const HttpReply& reply)
{
    if (!reply.transportOk && reply.status == 0)
    {
        model_.onActivateLicenseReply(0, {}, nowMs());
        publishGate();
        return;
    }

    model_.onActivateLicenseReply(reply.status, reply.body, nowMs());
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
// What is kept is exactly what the contract needs to re-derive access: the
// opaque token, this installation's device id, and the last VERIFIED
// entitlement answer with the moment it arrived. The licence key itself is
// never written: the server binds the key to the account, and the slot is
// the entitlement.
void LicensingBoundary::saveAuthFile()
{
    if (authFile_ == juce::File())
        return;

    authFile_.getParentDirectory().createDirectory();
    authFile_.deleteFile();

    const auto& cached = model_.cached();

    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("token", model_.token());
    object->setProperty("display_name", model_.user());
    object->setProperty("device_id", deviceId_);
    object->setProperty("subscribed", cached.subscribed);
    object->setProperty("plugin_entitled", cached.entitled);
    object->setProperty("subscription_entitles", cached.subscriptionEntitles);
    object->setProperty("license_entitles", cached.licenseEntitles);
    object->setProperty("license_account_active", cached.licenseAccountActive);
    object->setProperty("license_device_active", cached.licenseDeviceActive);
    object->setProperty("license_status", cached.licenseStatus);
    object->setProperty("devices_used", cached.devicesUsed);
    object->setProperty("device_limit", cached.deviceLimit);
    object->setProperty("verified_at", static_cast<juce::int64>(model_.verifiedAtMs()));
    authFile_.replaceWithText(juce::JSON::toString(objectToVar(object.get())));
}

void LicensingBoundary::loadAuthFile()
{
    auto restoreDeviceId = [this](const juce::String& stored)
    {
        // The device id is created ONCE and then kept. An id that is missing
        // or not the shape the backend accepts is replaced, so this install
        // can never present something the licence route would refuse.
        if (isDeviceIdFormat(stored))
        {
            deviceId_ = normalizeDeviceId(stored);
            return false;
        }

        deviceId_ = newDeviceId();
        return true;
    };

    if (authFile_ == juce::File() || !authFile_.existsAsFile())
    {
        restoreDeviceId({});
        model_.loadToken({}, {}, {}, 0);
        saveAuthFile();
        return;
    }

    const auto parsed = juce::JSON::parse(authFile_.loadFileAsString());
    if (parsed.isVoid() || parsed.isUndefined() || !parsed.isObject())
    {
        restoreDeviceId({});
        model_.loadToken({}, {}, {}, 0);
        saveAuthFile();
        return;
    }

    const auto deviceIdWasCreated = restoreDeviceId(
        parsed.getProperty("device_id", juce::var()).toString());

    CachedEntitlement cached;
    cached.entitled = flagAt(parsed, "plugin_entitled", false);
    cached.subscribed = flagAt(parsed, "subscribed", false);
    cached.subscriptionEntitles = flagAt(parsed, "subscription_entitles", false);
    cached.licenseEntitles = flagAt(parsed, "license_entitles", false);
    cached.licenseAccountActive = flagAt(parsed, "license_account_active", false);
    cached.licenseDeviceActive = flagAt(parsed, "license_device_active", false);
    cached.licenseStatus = parsed.getProperty("license_status", juce::var())
                               .toString();
    if (cached.licenseStatus.isEmpty())
        cached.licenseStatus = licenseStatusNone;
    cached.devicesUsed = intAt(parsed, "devices_used", 0);
    cached.deviceLimit = intAt(parsed, "device_limit", deviceActivationLimit);

    // A cache that claims access but names no route is not trusted, and
    // `licensed` is re-derived by applyCachedEntitlement from the route's own
    // window - never from the stored flag alone.
    if (offlineGraceDays(cached) <= 0.0)
    {
        cached.entitled = false;
        cached.licenseEntitles = false;
        cached.subscriptionEntitles = false;
    }

    const auto token = parsed.getProperty("token", juce::var()).toString();
    const auto user = parsed.getProperty("display_name", juce::var()).toString();
    const auto verifiedAt = static_cast<std::int64_t>(
        static_cast<double>(parsed.getProperty("verified_at", 0)));

    model_.loadToken(token, user, cached, verifiedAt);

    if (deviceIdWasCreated)
        saveAuthFile();
}
} // namespace chordengine::licensing
