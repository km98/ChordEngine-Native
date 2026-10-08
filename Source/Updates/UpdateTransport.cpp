#include "UpdateTransport.h"

#include <array>

namespace chordengine::updates
{
TransportReply HttpUpdateTransport::get(const juce::String& url)
{
    TransportReply reply;

    int statusCode = 0;
    juce::StringPairArray responseHeaders;

    auto stream = juce::URL(url)
        .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                               .withConnectionTimeoutMs(connectTimeoutMs)
                               .withResponseHeaders(&responseHeaders)
                               .withStatusCode(&statusCode)
                               .withNumRedirectsToFollow(5)
                               .withHttpRequestCmd("GET"));

    if (stream == nullptr)
        return reply; // the server never answered: transportOk stays false

    reply.transportOk = true;
    reply.status = statusCode;

    // juce::URL exposes a single socket timeout, so the read deadline is
    // enforced on top of it with a bounded read loop: a body that stalls can
    // never hold the worker indefinitely. A body larger than the accepted
    // maximum is refused rather than buffered.
    juce::MemoryOutputStream buffer;
    const auto startedAtMs = juce::Time::getMillisecondCounterHiRes();
    std::array<char, 4096> chunk {};

    for (;;)
    {
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAtMs;
        if (elapsedMs > static_cast<double>(readTimeoutMs))
        {
            reply.timedOut = true;
            break;
        }

        const auto numRead = stream->read(chunk.data(), static_cast<int>(chunk.size()));
        if (numRead <= 0)
            break;

        buffer.write(chunk.data(), static_cast<std::size_t>(numRead));
        if (buffer.getDataSize() >= maxResponseBytes)
            break;
    }

    reply.body = buffer.toString();
    return reply;
}

TransportReply OfflineUpdateTransport::get(const juce::String& url)
{
    juce::ignoreUnused(url);
    return {}; // transportOk = false: the service was not reached
}
} // namespace chordengine::updates
