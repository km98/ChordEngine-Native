#include "UpdateCache.h"

#include "UpdateResponse.h"

namespace chordengine::updates
{
namespace
{
constexpr int cacheFormatVersion = 1;

juce::String textOrEmpty(const juce::var& value)
{
    return value.isString() ? value.toString() : juce::String();
}
} // namespace

UpdateCache::UpdateCache(juce::File file) : file_(std::move(file)) {}

CachedUpdate UpdateCache::load() const
{
    CachedUpdate entry;
    if (file_ == juce::File() || !file_.existsAsFile())
        return entry;

    const auto root = parseJsonText(file_.loadFileAsString());
    if (!root.isObject())
        return entry;

    if (static_cast<int>(root.getProperty("cache_version", 0)) != cacheFormatVersion)
        return entry;

    // A cache written for another product or another platform is not ours.
    if (textOrEmpty(root.getProperty("product", juce::var())) != productSlug)
        return entry;
    if (textOrEmpty(root.getProperty("platform", juce::var())) != platformName())
        return entry;

    entry.product = productSlug;
    entry.platform = platformName();
    entry.currentVersion = textOrEmpty(root.getProperty("current_version", juce::var()));
    entry.latestVersion = textOrEmpty(root.getProperty("latest_version", juce::var()));
    entry.decision = textOrEmpty(root.getProperty("decision", juce::var()));
    entry.serverStatus = textOrEmpty(root.getProperty("status", juce::var()));
    entry.reason = textOrEmpty(root.getProperty("reason", juce::var()));
    entry.mustUpdate = root.getProperty("must_update", false);
    entry.checkedAtMs = static_cast<std::int64_t>(
        static_cast<double>(root.getProperty("checked_at", 0)));
    entry.valid = true;
    return entry;
}

bool UpdateCache::save(const CachedUpdate& entry) const
{
    if (file_ == juce::File())
        return false;

    file_.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("cache_version", cacheFormatVersion);
    object->setProperty("product", productSlug);
    object->setProperty("platform", platformName());
    object->setProperty("current_version", entry.currentVersion);
    // `latest_version` is written as an explicit JSON null when the service
    // has no version for this build, so a restored cache reads back exactly
    // the same "no published release" state.
    object->setProperty("latest_version",
                        entry.latestVersion.isEmpty() ? juce::var() : juce::var(entry.latestVersion));
    object->setProperty("decision", entry.decision);
    object->setProperty("status", entry.serverStatus);
    object->setProperty("reason", entry.reason);
    object->setProperty("must_update", entry.mustUpdate);
    object->setProperty("checked_at", static_cast<juce::int64>(entry.checkedAtMs));

    file_.deleteFile();
    return file_.replaceWithText(juce::JSON::toString(juce::var(object.get())));
}
} // namespace chordengine::updates
