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
    std::string currentVersion; // echoed by the server (optional)
    std::string latestVersion;  // empty when the service has none
    std::string decision;       // upToDate | optionalUpdate | mandatoryUpdate | belowMinimumSupported
    std::string serverStatus;   // the server's status word (e.g. noPublishedRelease)
    std::string reason;         // the server's human reason (never a version)
    std::string errorCode;      // envelope errorCode, when the server sent one
    std::string error;          // why a payload was rejected (empty when ok)
    bool mustUpdate = false;
};

// Classifies a server answer. `requestedPlatform` is always explicit, and a
// response that reports a different platform is refused.
UpdateResponse parseUpdateResponse(const platform::Json& root, const std::string& requestedPlatform);

// The single shared mapping from the server's (status, decision) pair to the
// view state. Used both by the parser and by the cache restore path, so a
// restored answer can never be classified differently from a fresh one.
UpdateStatus classifyServerAnswer(const std::string& serverStatus, const std::string& decision,
                                  bool mandatoryFlag, bool hasLatestVersion, bool& mustUpdateOut);

// Parses a response body. An unparsable body yields a failed result.
platform::Json parseJsonText(const std::string& text);
} // namespace chordengine::updates
