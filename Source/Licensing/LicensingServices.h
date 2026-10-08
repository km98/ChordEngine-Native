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
// function (the one VYRE ships). Implementations MUST NOT block the
// calling thread: the callback is delivered later on the message thread.
// ---------------------------------------------------------------------
class AuthService
{
public:
    using Callback = std::function<void(HttpReply)>;
    virtual ~AuthService() = default;

    // { action: "start", device_name, plugin_version }
    virtual void start(const juce::String& deviceName, const juce::String& pluginVersion,
                       Callback callback) = 0;
    // { action: "poll", device_code }
    virtual void poll(const juce::String& deviceCode, Callback callback) = 0;
    // { action: "entitlements", token }
    virtual void entitlements(const juce::String& token, Callback callback) = 0;
    // { action: "logout", token }
    virtual void logout(const juce::String& token, Callback callback) = 0;
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
