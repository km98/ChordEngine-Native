#include "UpdateResponse.h"

namespace chordengine::updates
{
namespace
{
constexpr const char* noPublishedReleaseWord = "noPublishedRelease";

bool isKnownDecision(const std::string& decision)
{
    if (decision.empty())
        return true; // the decision field itself is optional

    return decision == "upToDate"
        || decision == "optionalUpdate"
        || decision == "mandatoryUpdate"
        || decision == "belowMinimumSupported";
}

std::string textOrEmpty(const platform::Json& value)
{
    return value.isString() ? value.stringRef() : std::string();
}

bool boolOrFalse(const platform::Json& value)
{
    return value.isBool() && value.asBool();
}

bool isNullLike(const platform::Json& value)
{
    return value.isNull();
}

UpdateResponse reject(const std::string& reason)
{
    UpdateResponse response;
    response.ok = false;
    response.status = UpdateStatus::error;
    response.error = reason;
    return response;
}
} // namespace

platform::Json parseJsonText(const std::string& text)
{
    if (text.empty())
        return {};

    auto parsed = platform::Json::parse(text);
    return parsed.has_value() ? *parsed : platform::Json();
}

UpdateStatus classifyServerAnswer(const std::string& serverStatus, const std::string& decision,
                                  bool mandatoryFlag, bool hasLatestVersion, bool& mustUpdateOut)
{
    mustUpdateOut = false;

    // The status word wins: a product with no published release reports
    // upToDate as its decision, and that must never be shown as "Up to date".
    if (serverStatus == noPublishedReleaseWord)
    {
        (void) hasLatestVersion;
        return UpdateStatus::noPublishedRelease;
    }

    const bool mandatory = mandatoryFlag
        || decision == "mandatoryUpdate"
        || decision == "belowMinimumSupported";

    if (mandatory || (hasLatestVersion && decision == "optionalUpdate"))
    {
        mustUpdateOut = mandatory;
        return UpdateStatus::updateAvailable;
    }

    if (decision == "optionalUpdate")
    {
        mustUpdateOut = false;
        return UpdateStatus::updateAvailable;
    }

    return UpdateStatus::upToDate;
}

UpdateResponse parseUpdateResponse(const platform::Json& root, const std::string& requestedPlatform)
{
    std::string envelopeErrorCode;
    if (root.isObject())
        envelopeErrorCode = textOrEmpty(root["errorCode"]);

    const auto withCode = [&envelopeErrorCode](UpdateResponse response)
    {
        response.errorCode = envelopeErrorCode;
        return response;
    };

    if (!root.isObject())
        return withCode(reject("malformed response"));

    // 1. success must be exactly true.
    const auto success = root["success"];
    if (!success.isBool() || !success.asBool())
        return withCode(reject("unsuccessful response"));

    // 2. the data object must exist.
    const auto data = root["data"];
    if (!data.isObject())
        return withCode(reject("malformed response data"));

    UpdateResponse response;

    // 3. product must be this product.
    const auto product = data["product"];
    if (!product.isString() || product.stringRef() != productSlug)
        return withCode(reject("unexpected product"));

    // 4. currentVersion (optional) must be valid semver when supplied.
    const auto currentValue = data["currentVersion"];
    if (!isNullLike(currentValue))
    {
        if (!currentValue.isString())
            return withCode(reject("invalid current version"));

        const auto current = currentValue.stringRef();
        if (!current.empty())
        {
            if (!isValidSemver(current))
                return withCode(reject("invalid current version"));
            response.currentVersion = current;
        }
    }

    // 5. latestVersion must be valid semver whenever it is non-null.
    const auto latestValue = data["latestVersion"];
    const bool hasLatest = !isNullLike(latestValue);
    if (hasLatest)
    {
        if (!latestValue.isString() || !isValidSemver(latestValue.stringRef()))
            return withCode(reject("invalid latest version"));

        response.latestVersion = latestValue.stringRef();
    }

    // 6. the requested platform must match the response platform when given.
    const auto platformValue = data["platform"];
    if (!isNullLike(platformValue))
    {
        if (!platformValue.isString())
            return withCode(reject("invalid platform"));

        const auto reported = platformValue.stringRef();
        if (!reported.empty() && reported != requestedPlatform)
            return withCode(reject("platform mismatch"));
    }

    response.decision = textOrEmpty(data["decision"]);
    response.serverStatus = textOrEmpty(data["status"]);
    response.reason = textOrEmpty(data["reason"]);

    const bool updateAvailable = boolOrFalse(data["updateAvailable"]);
    const bool mustUpdateFlag = boolOrFalse(data["mustUpdate"])
        || boolOrFalse(data["mandatory"]);

    // 7. no unknown decision values.
    if (!isKnownDecision(response.decision))
        return withCode(reject("unknown decision"));

    // 8. the payload must be internally consistent.
    const bool saysNoRelease = response.serverStatus == noPublishedReleaseWord;
    if (saysNoRelease && hasLatest)
        return withCode(reject("inconsistent response"));

    if (updateAvailable && !hasLatest)
        return withCode(reject("inconsistent response"));

    if (mustUpdateFlag && !hasLatest)
        return withCode(reject("inconsistent response"));

    const bool decisionAnnouncesRelease = response.decision == "optionalUpdate"
        || response.decision == "mandatoryUpdate"
        || response.decision == "belowMinimumSupported";
    if (decisionAnnouncesRelease && !hasLatest)
        return withCode(reject("inconsistent response"));

    if (!saysNoRelease && updateAvailable && response.decision == "upToDate")
        return withCode(reject("inconsistent response"));

    bool mustUpdateOut = false;
    response.status = classifyServerAnswer(response.serverStatus, response.decision,
                                           mustUpdateFlag, hasLatest, mustUpdateOut);
    response.mustUpdate = mustUpdateOut;
    response.ok = true;
    return response;
}
} // namespace chordengine::updates
