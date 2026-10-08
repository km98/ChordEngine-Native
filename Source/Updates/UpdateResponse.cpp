#include "UpdateResponse.h"

namespace chordengine::updates
{
namespace
{
constexpr const char* noPublishedReleaseWord = "noPublishedRelease";

bool isKnownDecision(const juce::String& decision)
{
    if (decision.isEmpty())
        return true; // the decision field itself is optional

    return decision == "upToDate"
        || decision == "optionalUpdate"
        || decision == "mandatoryUpdate"
        || decision == "belowMinimumSupported";
}

juce::String textOrEmpty(const juce::var& value)
{
    return value.isString() ? value.toString() : juce::String();
}

bool boolOrFalse(const juce::var& value)
{
    return value.isBool() && static_cast<bool>(value);
}

bool isNullLike(const juce::var& value)
{
    return value.isVoid() || value.isUndefined();
}

UpdateResponse reject(const juce::String& reason)
{
    UpdateResponse response;
    response.ok = false;
    response.status = UpdateStatus::error;
    response.error = reason;
    return response;
}
} // namespace

juce::var parseJsonText(const juce::String& text)
{
    if (text.isEmpty())
        return {};

    juce::var parsed;
    const auto result = juce::JSON::parse(text, parsed);
    if (result.failed() || result.getErrorMessage().isNotEmpty())
        return {};

    return parsed;
}

UpdateStatus classifyServerAnswer(const juce::String& serverStatus, const juce::String& decision,
                                  bool mandatoryFlag, bool hasLatestVersion, bool& mustUpdateOut)
{
    mustUpdateOut = false;

    // The status word wins: a product with no published release reports
    // upToDate as its decision, and that must never be shown as "Up to date".
    if (serverStatus == noPublishedReleaseWord)
    {
        juce::ignoreUnused(hasLatestVersion);
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

UpdateResponse parseUpdateResponse(const juce::var& root, const juce::String& requestedPlatform)
{
    juce::String envelopeErrorCode;
    if (root.isObject())
        envelopeErrorCode = textOrEmpty(root.getProperty("errorCode", juce::var()));

    const auto withCode = [&envelopeErrorCode](UpdateResponse response)
    {
        response.errorCode = envelopeErrorCode;
        return response;
    };

    if (!root.isObject())
        return withCode(reject("malformed response"));

    // 1. success must be exactly true.
    const auto success = root.getProperty("success", juce::var());
    if (!success.isBool() || !static_cast<bool>(success))
        return withCode(reject("unsuccessful response"));

    // 2. the data object must exist.
    const auto data = root.getProperty("data", juce::var());
    if (!data.isObject())
        return withCode(reject("malformed response data"));

    UpdateResponse response;

    // 3. product must be this product.
    const auto product = data.getProperty("product", juce::var());
    if (!product.isString() || product.toString() != productSlug)
        return withCode(reject("unexpected product"));

    // 4. currentVersion (optional) must be valid semver when supplied.
    const auto currentValue = data.getProperty("currentVersion", juce::var());
    if (!isNullLike(currentValue))
    {
        if (!currentValue.isString())
            return withCode(reject("invalid current version"));

        const auto current = currentValue.toString();
        if (current.isNotEmpty())
        {
            if (!isValidSemver(current))
                return withCode(reject("invalid current version"));
            response.currentVersion = current;
        }
    }

    // 5. latestVersion must be valid semver whenever it is non-null.
    const auto latestValue = data.getProperty("latestVersion", juce::var());
    const bool hasLatest = !isNullLike(latestValue);
    if (hasLatest)
    {
        if (!latestValue.isString() || !isValidSemver(latestValue.toString()))
            return withCode(reject("invalid latest version"));

        response.latestVersion = latestValue.toString();
    }

    // 6. the requested platform must match the response platform when given.
    const auto platformValue = data.getProperty("platform", juce::var());
    if (!isNullLike(platformValue))
    {
        if (!platformValue.isString())
            return withCode(reject("invalid platform"));

        const auto reported = platformValue.toString();
        if (reported.isNotEmpty() && reported != requestedPlatform)
            return withCode(reject("platform mismatch"));
    }

    response.decision = textOrEmpty(data.getProperty("decision", juce::var()));
    response.serverStatus = textOrEmpty(data.getProperty("status", juce::var()));
    response.reason = textOrEmpty(data.getProperty("reason", juce::var()));

    const bool updateAvailable = boolOrFalse(data.getProperty("updateAvailable", juce::var()));
    const bool mustUpdateFlag = boolOrFalse(data.getProperty("mustUpdate", juce::var()))
        || boolOrFalse(data.getProperty("mandatory", juce::var()));

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
