#include "MusicProdAuthService.h"

#include <juce_events/juce_events.h>

namespace chordengine::licensing
{
namespace
{
constexpr int connectionTimeoutMs = 12000;

juce::var parseBody(const juce::String& text)
{
    if (text.isEmpty())
        return {};

    juce::var parsed;
    const auto result = juce::JSON::parse(text, parsed);
    if (result.failed() || result.getErrorMessage().isNotEmpty())
        return {};
    return parsed;
}
}

MusicProdAuthService::MusicProdAuthService() = default;

MusicProdAuthService::~MusicProdAuthService()
{
    // Finish or cancel in-flight requests before the callbacks' owner can go
    // away. The queued work is short-lived HTTP round trips.
    pool_.removeAllJobs(true, 3000);
}

void MusicProdAuthService::start(const juce::String& deviceName,
                                 const juce::String& pluginVersion,
                                 const juce::String& product,
                                 const juce::String& deviceId, Callback callback)
{
    // EXACT wire shape of vyre-plugin-auth `start`: `product` is the explicit
    // allowlisted slug (so the token's product never depends on the legacy
    // device_name fallback), and `device_id` is the opaque per-installation
    // id the device code stores and the token inherits - which is what later
    // lets the entitlement check recognise a licence slot held by THIS
    // computer.
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "start");
    body->setProperty("device_name", deviceName);
    body->setProperty("plugin_version", pluginVersion);
    body->setProperty("product", product);
    if (deviceId.isNotEmpty())
        body->setProperty("device_id", deviceId);
    post(juce::var(body.get()), std::move(callback));
}

void MusicProdAuthService::poll(const juce::String& deviceCode, Callback callback)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "poll");
    body->setProperty("device_code", deviceCode);
    post(juce::var(body.get()), std::move(callback));
}

void MusicProdAuthService::entitlements(const juce::String& token, Callback callback)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "entitlements");
    body->setProperty("token", token);
    post(juce::var(body.get()), std::move(callback));
}

void MusicProdAuthService::logout(const juce::String& token, Callback callback)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "logout");
    body->setProperty("token", token);
    post(juce::var(body.get()), std::move(callback));
}

void MusicProdAuthService::activateLicense(const juce::String& token,
                                           const juce::String& licenseKey,
                                           const juce::String& deviceId,
                                           const juce::String& deviceLabel,
                                           const juce::String& platform,
                                           const juce::String& pluginVersion,
                                           Callback callback)
{
    // EXACT wire shape of `activate-license` (vyre-plugin-auth/license.ts).
    // `device_id` is REQUIRED by the endpoint: the operation consumes one of
    // the licence's three slots, so a request that does not name the device
    // is refused as a structural error rather than treated as a verdict on
    // the customer's key. The key is sent and then dropped - the server binds
    // it to the account, and nothing here persists it.
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "activate-license");
    body->setProperty("token", token);
    body->setProperty("license_key", licenseKey);
    body->setProperty("device_id", deviceId);
    if (deviceLabel.isNotEmpty())
        body->setProperty("device_label", deviceLabel);
    if (platform.isNotEmpty())
        body->setProperty("platform", platform);
    if (pluginVersion.isNotEmpty())
        body->setProperty("plugin_version", pluginVersion);
    post(juce::var(body.get()), std::move(callback));
}

void MusicProdAuthService::post(const juce::var& body, Callback callback)
{
    const auto url = juce::String(authOrigin) + authFunction;
    const auto payload = juce::JSON::toString(body);

    pool_.addJob(
        [url, payload, callback = std::move(callback)]
        {
            HttpReply reply;
            int statusCode = 0;
            juce::StringPairArray responseHeaders;

            auto stream = juce::URL(url)
                .withPOSTData(payload)
                .createInputStream(juce::URL::InputStreamOptions(
                        juce::URL::ParameterHandling::inPostData)
                        .withExtraHeaders("Content-Type: application/json\r\n")
                        .withConnectionTimeoutMs(connectionTimeoutMs)
                        .withResponseHeaders(&responseHeaders)
                        .withStatusCode(&statusCode)
                        .withNumRedirectsToFollow(5)
                        .withHttpRequestCmd("POST"));

            if (stream != nullptr)
            {
                reply.transportOk = true;
                reply.status = statusCode;
                reply.body = parseBody(stream->readEntireStreamAsString());
            }

            if (callback == nullptr)
                return;

            juce::MessageManager::callAsync(
                [callback, reply] { callback(reply); });
        });
}
} // namespace chordengine::licensing
