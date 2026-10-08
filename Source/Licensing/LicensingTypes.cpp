#include "LicensingTypes.h"

#include <cmath>

namespace chordengine::licensing
{
juce::String formatTrial(double msRemaining)
{
    auto totalSec = static_cast<int>(std::llround(std::ceil(msRemaining / 1000.0)));
    if (totalSec < 0)
        totalSec = 0;

    const auto minutes = totalSec / 60;
    const auto seconds = totalSec - minutes * 60;

    // Reference ceFormatTrial(): minutes and seconds only, exactly two
    // digits per component.
    return juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(seconds).paddedLeft('0', 2);
}

juce::String uiText(const juce::var& value, const juce::String& fallback)
{
    if (value.isVoid() || value.isUndefined())
        return fallback;

    const auto text = value.toString();
    if (text.isEmpty() || text == "undefined" || text == "null" || text == "NaN")
        return fallback;

    return text;
}

juce::String trialStatusText(const Snapshot& snapshot)
{
    if (snapshot.licensed)
        return "Music-Prod+ active - full access";
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

juce::String licenseLineText(const Snapshot& snapshot)
{
    if (snapshot.licensed)
        return "Music-Prod+ verified";
    if (snapshot.plusCached && snapshot.hasToken)
        return "Verification required - reconnect to Music-Prod";
    return "Not active - Music-Prod+ unlocks full access";
}

juce::String userLineText(const Snapshot& snapshot)
{
    if (isSignedIn(snapshot))
        return "User  " + uiText(snapshot.user, "Signed in");
    return "User  Not signed in";
}

juce::String plusLineText(const Snapshot& snapshot)
{
    return snapshot.licensed ? "Music-Prod+  ACTIVE" : "Music-Prod+  NOT ACTIVE";
}

std::uint32_t plusLineColour(const Snapshot& snapshot) noexcept
{
    return snapshot.licensed ? colourPlusActive : colourPlusInactive;
}

juce::String signButtonText(const Snapshot& snapshot)
{
    return snapshot.polling ? "CANCEL LINK" : "SIGN IN";
}

juce::String latestVersionText(const Snapshot& snapshot)
{
    const auto version = uiText(snapshot.latestVersion, {});
    return "Latest version  " + (version.isEmpty() ? juce::String("-") : version);
}

juce::String updateStatusText(const Snapshot& snapshot)
{
    const auto status = uiText(snapshot.updateStatus, {});
    return status.isEmpty() ? juce::String("Status  -") : "Status  " + status;
}

std::uint32_t updateStatusColour(const Snapshot& snapshot) noexcept
{
    switch (snapshot.updateState)
    {
        case UpdateState::checking: return 0xff8a93a0;
        case UpdateState::failed: return colourTrialExpired;
        case UpdateState::checked:
            return snapshot.latestVersion.isEmpty() ? colourUpdateWarning : colourPlusActive;
        case UpdateState::idle:
        default: return 0xff8a93a0;
    }
}

juce::String expiryModalBody(const Snapshot& snapshot)
{
    auto body = juce::String("Your 30-minute ChordEngine trial has ended.");
    if (isSignedIn(snapshot))
        body << " Music-Prod+ unlocks unlimited access - open Music-Prod Studio or "
                "music-prod.com to upgrade.";
    else
        body << " Open Music-Prod Studio to sign in, or upgrade to Music-Prod+ for "
                "unlimited access.";
    return body;
}
} // namespace chordengine::licensing
