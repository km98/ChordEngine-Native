#pragma once

#include "UpdateTypes.h"

#include <memory>

namespace chordengine::updates
{
// One HTTP round trip for the update endpoint.
struct TransportReply
{
    int status = 0;           // HTTP status; 0 when the request never completed
    juce::String body;        // response body (empty when the server never answered)
    bool transportOk = false; // the server answered, whatever the status
    bool timedOut = false;    // the read deadline elapsed (or the socket timed out)
};

// Implementations MUST be safe to call from a background thread and MUST NOT
// touch the message thread, the audio thread or any plugin state.
class UpdateTransport
{
public:
    virtual ~UpdateTransport() = default;
    virtual TransportReply get(const juce::String& url) = 0;

    using Ptr = std::unique_ptr<UpdateTransport>;
};

// Production transport: one unauthenticated GET through juce::URL, with the
// 5 s connection timeout and a 5 s read deadline enforced on top of it.
class HttpUpdateTransport final : public UpdateTransport
{
public:
    TransportReply get(const juce::String& url) override;
};

// Deterministic "the service could not be reached" transport. It performs no
// I/O at all, so test builds (which select it by default) never touch the
// network, and it is also the honest offline path.
class OfflineUpdateTransport final : public UpdateTransport
{
public:
    TransportReply get(const juce::String& url) override;
};

// The transport the plugin editor uses unless one is injected. It is inline on
// purpose: the choice is made per translation unit, so a test target can define
// CHORDENGINE_UPDATE_OFFLINE_TRANSPORT and never depend on the live service,
// while the shipped plugin, compiled without that define, always gets the real
// HTTP transport.
inline UpdateTransport::Ptr makeDefaultUpdateTransport()
{
#if defined(CHORDENGINE_UPDATE_OFFLINE_TRANSPORT) && CHORDENGINE_UPDATE_OFFLINE_TRANSPORT
    return std::make_unique<OfflineUpdateTransport>();
#else
    return std::make_unique<HttpUpdateTransport>();
#endif
}
} // namespace chordengine::updates
