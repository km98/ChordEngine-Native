#include "UpdateTransport.h"

#include "Platform/Platform.h"

namespace chordengine::updates
{
TransportReply HttpUpdateTransport::get(const std::string& url)
{
    platform::HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.connectTimeoutMs = connectTimeoutMs;
    // The read deadline on top of the connection timeout: a body that stalls
    // can never hold the worker indefinitely, and a body larger than the
    // accepted maximum is cut there rather than buffered.
    request.totalTimeoutMs = connectTimeoutMs + readTimeoutMs;
    request.maxResponseBytes = maxResponseBytes;

    const auto response = platform::httpRequest(request);
    TransportReply reply;
    if (!response.transportOk)
    {
        reply.timedOut = response.timedOut;
        return reply; // the server never answered: transportOk stays false
    }

    reply.transportOk = true;
    reply.status = response.status;
    reply.timedOut = response.timedOut;
    reply.body = response.body;
    return reply;
}

TransportReply OfflineUpdateTransport::get(const std::string& url)
{
    (void) url;
    return {}; // transportOk = false: the service was not reached
}
} // namespace chordengine::updates
