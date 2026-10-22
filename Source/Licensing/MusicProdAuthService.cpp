#include "MusicProdAuthService.h"

#include "Platform/Platform.h"

namespace chordengine::licensing
{
namespace
{
constexpr int connectionTimeoutMs = 12000;

platform::Json parseBody(const std::string& text)
{
    if (text.empty())
        return {};
    auto parsed = platform::Json::parse(text);
    return parsed.has_value() ? *parsed : platform::Json();
}
}

MusicProdAuthService::MusicProdAuthService() = default;

MusicProdAuthService::~MusicProdAuthService() = default;

void MusicProdAuthService::start(const std::string& deviceName,
                                 const std::string& pluginVersion,
                                 const std::string& product,
                                 const std::string& deviceId, Callback callback)
{
    // EXACT wire shape of vyre-plugin-auth `start`: `product` is the explicit
    // allowlisted slug (so the token's product never depends on the legacy
    // device_name fallback), and `device_id` is the opaque per-installation
    // id the device code stores and the token inherits - which is what later
    // lets the entitlement check recognise a licence slot held by THIS
    // computer.
    auto body = platform::Json::object();
    body.set("action", "start");
    body.set("device_name", deviceName);
    body.set("plugin_version", pluginVersion);
    body.set("product", product);
    if (!deviceId.empty())
        body.set("device_id", deviceId);
    post(body, std::move(callback));
}

void MusicProdAuthService::poll(const std::string& deviceCode, Callback callback)
{
    auto body = platform::Json::object();
    body.set("action", "poll");
    body.set("device_code", deviceCode);
    post(body, std::move(callback));
}

void MusicProdAuthService::entitlements(const std::string& token, Callback callback)
{
    auto body = platform::Json::object();
    body.set("action", "entitlements");
    body.set("token", token);
    post(body, std::move(callback));
}

void MusicProdAuthService::logout(const std::string& token, Callback callback)
{
    auto body = platform::Json::object();
    body.set("action", "logout");
    body.set("token", token);
    post(body, std::move(callback));
}

void MusicProdAuthService::activateLicense(const std::string& token,
                                           const std::string& licenseKey,
                                           const std::string& deviceId,
                                           const std::string& deviceLabel,
                                           const std::string& platformName,
                                           const std::string& pluginVersion,
                                           Callback callback)
{
    // EXACT wire shape of `activate-license` (vyre-plugin-auth/license.ts).
    // `device_id` is REQUIRED by the endpoint: the operation consumes one of
    // the licence's three slots, so a request that does not name the device
    // is refused as a structural error rather than treated as a verdict on
    // the customer's key. The key is sent and then dropped - the server binds
    // it to the account, and nothing here persists it.
    auto body = platform::Json::object();
    body.set("action", "activate-license");
    body.set("token", token);
    body.set("license_key", licenseKey);
    body.set("device_id", deviceId);
    if (!deviceLabel.empty())
        body.set("device_label", deviceLabel);
    if (!platformName.empty())
        body.set("platform", platformName);
    if (!pluginVersion.empty())
        body.set("plugin_version", pluginVersion);
    post(body, std::move(callback));
}

void MusicProdAuthService::post(const platform::Json& body, Callback callback)
{
    platform::HttpRequest request;
    request.method = "POST";
    request.url = std::string(authOrigin) + authFunction;
    request.headers = { { "Content-Type", "application/json" } };
    request.body = body.dump();
    request.connectTimeoutMs = connectionTimeoutMs;
    request.totalTimeoutMs = 2 * connectionTimeoutMs;

    worker_.post(
        [request = std::move(request), callback = std::move(callback)]
        {
            HttpReply reply;
            const auto response = platform::httpRequest(request);
            if (response.transportOk)
            {
                reply.transportOk = true;
                reply.status = response.status;
                reply.body = parseBody(response.body);
            }

            if (callback == nullptr)
                return;

            platform::callOnMainThread([callback, reply] { callback(reply); });
        });
}
} // namespace chordengine::licensing
