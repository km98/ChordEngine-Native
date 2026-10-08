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
                                 const juce::String& pluginVersion, Callback callback)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "start");
    body->setProperty("device_name", deviceName);
    body->setProperty("plugin_version", pluginVersion);
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
