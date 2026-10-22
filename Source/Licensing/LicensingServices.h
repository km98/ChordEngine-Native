#pragma once

#include "LicensingTypes.h"

#include <functional>

namespace chordengine::licensing
{
// One HTTP reply. transportOk is false when the request never reached the
// server (offline, DNS, timeout): the reference keeps the last verified
// answer in that case, so the distinction is part of the contract.
struct HttpReply
{
    int status = 0;
    juce::var body;
    bool transportOk = false;
};

// ---------------------------------------------------------------------
// The device-code auth flow of the existing Music-Prod plugin auth
// function (the one VYRE ships), plus the licence-key activation route.
// Implementations MUST NOT block the calling thread: the callback is
// delivered later on the message thread.
//
// Every request body here is the EXACT wire shape the function documents
// (vyre-plugin-auth/index.ts and ./license.ts). No field is invented.
// ---------------------------------------------------------------------
class AuthService
{
public:
    using Callback = std::function<void(HttpReply)>;
    virtual ~AuthService() = default;

    // { action: "start", device_name, plugin_version, product, device_id }
    //
    // `product` is explicit so the token's product never depends on the
    // legacy device_name fallback, and `device_id` lets the token carry this
    // installation's identity - which is what later lets the entitlement
    // check recognise a licence slot held by THIS computer.
    virtual void start(const juce::String& deviceName, const juce::String& pluginVersion,
                       const juce::String& product, const juce::String& deviceId,
                       Callback callback) = 0;
    // { action: "poll", device_code }
    virtual void poll(const juce::String& deviceCode, Callback callback) = 0;
    // { action: "entitlements", token }
    virtual void entitlements(const juce::String& token, Callback callback) = 0;
    // { action: "logout", token }
    virtual void logout(const juce::String& token, Callback callback) = 0;

    // { action: "activate-license", token, license_key, device_id,
    //   device_label, platform, plugin_version }
    //
    // `device_id` is REQUIRED by the endpoint: the operation consumes one of
    // the licence's three device slots, so a request that does not say which
    // computer it is is refused as a structural error (400
    // device_id_required) rather than being treated as a verdict on the key.
    virtual void activateLicense(const juce::String& token, const juce::String& licenseKey,
                                const juce::String& deviceId, const juce::String& deviceLabel,
                                const juce::String& platform, const juce::String& pluginVersion,
                                Callback callback) = 0;
};

// ---------------------------------------------------------------------
// The update-check seam. The reference has NO plugin-facing update API for
// ChordEngine, so the shipped implementation reports exactly that and sends
// the user to Music-Prod Studio, which owns installation and updates.
// ---------------------------------------------------------------------
class UpdateService
{
public:
    struct Result
    {
        bool available = false; // true only when a real latest version exists
        juce::String latestVersion;
        juce::String status; // the honest, user-facing result line
    };

    virtual ~UpdateService() = default;
    virtual Result check() = 0;
};

// Default production implementation - reference ceCheckForUpdates().
class MusicProdUpdateService final : public UpdateService
{
public:
    Result check() override;
};
} // namespace chordengine::licensing
