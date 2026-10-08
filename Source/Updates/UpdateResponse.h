#pragma once

#include "UpdateTypes.h"

namespace chordengine::updates
{
// The validated, normalised meaning of one /updates answer.
//
// Nothing from the raw JSON is used before it has passed the strict checks in
// parseUpdateResponse(): the envelope's success flag, the product slug, both
// version fields, the platform and the internal consistency of the decision
// and status fields.
struct UpdateResponse
{
    bool ok = false;            // the payload passed every check
    UpdateStatus status = UpdateStatus::error;
    juce::String currentVersion; // echoed by the server (optional)
    juce::String latestVersion;  // empty when the service has none
    juce::String decision;       // upToDate | optionalUpdate | mandatoryUpdate | belowMinimumSupported
    juce::String serverStatus;   // the server's status word (e.g. noPublishedRelease)
    juce::String reason;         // the server's human reason (never a version)
    juce::String errorCode;      // envelope errorCode, when the server sent one
    juce::String error;          // why a payload was rejected (empty when ok)
    bool mustUpdate = false;
};

// Classifies a server answer. `requestedPlatform` is always explicit, and a
// response that reports a different platform is refused.
UpdateResponse parseUpdateResponse(const juce::var& root, const juce::String& requestedPlatform);

// The single shared mapping from the server's (status, decision) pair to the
// view state. Used both by the parser and by the cache restore path, so a
// restored answer can never be classified differently from a fresh one.
UpdateStatus classifyServerAnswer(const juce::String& serverStatus, const juce::String& decision,
                                  bool mandatoryFlag, bool hasLatestVersion, bool& mustUpdateOut);

// Parses a response body. An unparsable body yields a failed result.
juce::var parseJsonText(const juce::String& text);
} // namespace chordengine::updates
