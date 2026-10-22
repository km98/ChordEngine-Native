#include "LicensingTypes.h"

#include "Platform/Platform.h"

#include <cmath>
#include <cstdlib>

namespace chordengine::licensing
{
std::string formatTrial(double msRemaining)
{
    auto totalSec = static_cast<int>(std::llround(std::ceil(msRemaining / 1000.0)));
    if (totalSec < 0)
        totalSec = 0;

    const auto minutes = totalSec / 60;
    const auto seconds = totalSec - minutes * 60;

    // Reference ceFormatTrial(): minutes and seconds only, exactly two
    // digits per component.
    return text::padLeft(std::to_string(minutes), 2, '0') + ":" + text::padLeft(std::to_string(seconds), 2, '0');
}

std::string uiText(const std::string& value, const std::string& fallback)
{
    if (value.empty() || value == "undefined" || value == "null" || value == "NaN")
        return fallback;

    return value;
}

std::string varText(const platform::Json& value)
{
    if (value.isBool())
        return value.asBool() ? "1" : "0";
    if (value.isString() || value.isNumber())
        return value.asString();
    return {};
}

double varNumber(const platform::Json& value)
{
    if (value.isNumber() || value.isBool())
        return value.asDouble();
    if (value.isString())
        return std::strtod(value.stringRef().c_str(), nullptr);
    return 0.0;
}

// -------------------------------------------------- the device identity
std::string newDeviceId()
{
    // 128 bits of system entropy, lowercase hex - the shape the backend
    // accepts (16..128 characters of [A-Za-z0-9._:-]). It is drawn from the
    // operating system's secure random source, and the value is stored only
    // as a SHA-256 hash server-side.
    return platform::randomHex(16);
}

std::string normalizeDeviceId(const std::string& deviceId)
{
    return text::toLower(text::trim(deviceId));
}

bool isDeviceIdFormat(const std::string& deviceId) noexcept
{
    const auto value = normalizeDeviceId(deviceId);
    if (value.size() < static_cast<std::size_t>(deviceIdMinLength)
        || value.size() > static_cast<std::size_t>(deviceIdMaxLength))
        return false;

    // Same rule as the backend: a printable token with no whitespace. Its
    // exact shape belongs to the plugin; the LENGTH is what matters, because
    // a short id would collide across computers.
    return text::containsOnly(value, "abcdefghijklmnopqrstuvwxyz0123456789._:-");
}

// ------------------------------------------------------- derived lines
std::string trialStatusText(const Snapshot& snapshot)
{
    if (snapshot.licensed)
    {
        // Say WHICH route granted access: a licence holder has no
        // Music-Prod+ subscription, and telling them "Music-Prod+ active"
        // would be a lie about their account.
        if (snapshot.cached.licenseEntitles && !snapshot.cached.subscriptionEntitles)
            return "Licence active - full access";
        return "Music-Prod+ active - full access";
    }
    if (snapshot.trialExpired)
        return "Access expired";
    return "Trial access  " + formatTrial(snapshot.trialMs) + " remaining";
}

std::uint32_t trialStatusColour(const Snapshot& snapshot) noexcept
{
    if (snapshot.licensed)
        return colourPlusActive;
    if (snapshot.trialExpired)
        return colourTrialExpired;
    return colourTrialActive;
}

std::string deviceUsageText(const Snapshot& snapshot)
{
    const auto& cached = snapshot.cached;
    if (cached.devicesUsed <= 0 && !cached.licenseAccountActive && !cached.licenseEntitles)
        return {};

    const auto limit = cached.deviceLimit > 0 ? cached.deviceLimit : deviceActivationLimit;
    return std::to_string(cached.devicesUsed) + " of " + std::to_string(limit) + " computers";
}

std::string licenseLineText(const Snapshot& snapshot)
{
    const auto& cached = snapshot.cached;
    const auto slots = deviceUsageText(snapshot);
    const auto slotsText = slots.empty() ? std::string() : " (" + slots + ")";

    if (snapshot.licensed)
    {
        // Both routes can be live at once; neither needs the other, so the
        // line states exactly what is true.
        if (cached.licenseEntitles && cached.subscriptionEntitles)
            return "Music-Prod+ verified - licence active on this computer" + slotsText;
        if (cached.licenseEntitles)
            return "Licence active on this computer" + slotsText;
        return "Music-Prod+ verified";
    }

    // A key the server rejected is stated as such FIRST: it must never be
    // described as an active licence that merely needs a slot.
    if (cached.licenseStatus == licenseStatusRevoked)
        return "Licence revoked - contact Music-Prod support";
    if (cached.licenseStatus == licenseStatusExpired)
        return "Licence expired";

    // The licence is fine but THIS computer is not activated - the case the
    // server reports so the UI never pretends the key is bad.
    if (cached.licenseAccountActive && !cached.licenseDeviceActive)
        return "Licence active on your account - this computer is not activated" + slotsText;

    if (cached.subscribed && snapshot.hasToken)
        return "Verification required - reconnect to Music-Prod";
    return "Not active - Music-Prod+ unlocks full access";
}

std::string userLineText(const Snapshot& snapshot)
{
    if (isSignedIn(snapshot))
        return "User  " + uiText(snapshot.user, "Signed in");
    return "User  Not signed in";
}

std::string plusLineText(const Snapshot& snapshot)
{
    // The Plus line is the ACCOUNT's Music-Prod+ membership, which the
    // backend deliberately keeps product-agnostic. A licence holder with no
    // subscription correctly reads NOT ACTIVE here while the licence line
    // reports their granted access.
    return snapshot.cached.subscribed ? "Music-Prod+  ACTIVE" : "Music-Prod+  NOT ACTIVE";
}

std::uint32_t plusLineColour(const Snapshot& snapshot) noexcept
{
    return snapshot.cached.subscribed ? colourPlusActive : colourPlusInactive;
}

std::string activationButtonText(const Snapshot& snapshot)
{
    return snapshot.activationPending ? "ACTIVATING..." : "ACTIVATE";
}

std::string signButtonText(const Snapshot& snapshot)
{
    return snapshot.polling ? "CANCEL LINK" : "SIGN IN";
}

std::string latestVersionText(const Snapshot& snapshot)
{
    const auto version = uiText(snapshot.latestVersion, {});
    return "Latest version  " + (version.empty() ? std::string("-") : version);
}

std::string updateStatusText(const Snapshot& snapshot)
{
    const auto status = uiText(snapshot.updateStatus, {});
    return status.empty() ? std::string("Status  -") : "Status  " + status;
}

std::uint32_t updateStatusColour(const Snapshot& snapshot) noexcept
{
    switch (snapshot.updateState)
    {
        case UpdateState::checking: return 0xff8a93a0;
        case UpdateState::failed: return colourTrialExpired;
        case UpdateState::checked:
            return snapshot.latestVersion.empty() ? colourUpdateWarning : colourPlusActive;
        case UpdateState::idle:
        default: return 0xff8a93a0;
    }
}

std::string expiryModalBody(const Snapshot& snapshot)
{
    auto body = std::string("Your 30-minute ChordEngine trial has ended.");
    if (isSignedIn(snapshot))
        body += " Music-Prod+ unlocks unlimited access, or activate your ChordEngine"
                " licence key on the INFO page. Open Music-Prod Studio or "
                "music-prod.com to upgrade.";
    else
        body += " Open Music-Prod Studio to sign in or to activate a ChordEngine licence"
                " key, or upgrade to Music-Prod+ for unlimited access.";
    return body;
}
} // namespace chordengine::licensing
