#pragma once

#include "LicensingServices.h"

#include "Platform/Platform.h"

#include <string>

namespace chordengine::licensing
{
// The production AuthService: the EXISTING Music-Prod plugin auth function
// (functions/v1/vyre-plugin-auth) called with JSON POSTs, exactly as the
// reference bridge does. No API key, no secret, no user id and no email are
// sent - identity is resolved server-side from the opaque bearer token.
//
// Every request runs on a private background thread and the reply is
// delivered back on the message thread, so neither the audio thread nor the
// GUI thread is ever blocked by the network.
class MusicProdAuthService final : public AuthService
{
public:
    MusicProdAuthService();
    ~MusicProdAuthService() override;

    void start(const std::string& deviceName, const std::string& pluginVersion,
               const std::string& product, const std::string& deviceId,
               Callback callback) override;
    void poll(const std::string& deviceCode, Callback callback) override;
    void entitlements(const std::string& token, Callback callback) override;
    void logout(const std::string& token, Callback callback) override;
    void activateLicense(const std::string& token, const std::string& licenseKey,
                        const std::string& deviceId, const std::string& deviceLabel,
                        const std::string& platform, const std::string& pluginVersion,
                        Callback callback) override;

private:
    void post(const platform::Json& body, Callback callback);

    // One background worker; destroying the service drops queued requests
    // and waits up to 3 s for the one in flight (as before).
    platform::WorkerQueue worker_ { 3000 };
};
} // namespace chordengine::licensing
