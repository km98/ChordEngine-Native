#include "UpdateCache.h"

#include "UpdateResponse.h"

#include "Platform/Platform.h"

namespace chordengine::updates
{
namespace
{
constexpr int cacheFormatVersion = 1;

std::string textOrEmpty(const platform::Json& value)
{
    return value.isString() ? value.stringRef() : std::string();
}
} // namespace

UpdateCache::UpdateCache(std::filesystem::path file) : file_(std::move(file)) {}

CachedUpdate UpdateCache::load() const
{
    CachedUpdate entry;
    const auto stored = file_.empty() ? std::nullopt : platform::readFile(file_, 1u << 20);
    if (!stored.has_value())
        return entry;

    const auto root = parseJsonText(*stored);
    if (!root.isObject())
        return entry;

    if (!root["cache_version"].isNumber() || static_cast<int>(root["cache_version"].asDouble()) != cacheFormatVersion)
        return entry;

    // A cache written for another product or another platform is not ours.
    if (textOrEmpty(root["product"]) != productSlug)
        return entry;
    if (textOrEmpty(root["platform"]) != platformName())
        return entry;

    entry.product = productSlug;
    entry.platform = platformName();
    entry.currentVersion = textOrEmpty(root["current_version"]);
    entry.latestVersion = textOrEmpty(root["latest_version"]);
    entry.decision = textOrEmpty(root["decision"]);
    entry.serverStatus = textOrEmpty(root["status"]);
    entry.reason = textOrEmpty(root["reason"]);
    entry.mustUpdate = root["must_update"].isBool() ? root["must_update"].asBool() : root["must_update"].asDouble() != 0.0;
    entry.checkedAtMs = static_cast<std::int64_t>(root["checked_at"].asDouble());
    entry.valid = true;
    return entry;
}

bool UpdateCache::save(const CachedUpdate& entry) const
{
    if (file_.empty())
        return false;

    auto object = platform::Json::object();
    object.set("cache_version", cacheFormatVersion);
    object.set("product", productSlug);
    object.set("platform", platformName());
    object.set("current_version", entry.currentVersion);
    // `latest_version` is written as an explicit JSON null when the service
    // has no version for this build, so a restored cache reads back exactly
    // the same "no published release" state.
    object.set("latest_version",
                        entry.latestVersion.empty() ? platform::Json() : platform::Json(entry.latestVersion));
    object.set("decision", entry.decision);
    object.set("status", entry.serverStatus);
    object.set("reason", entry.reason);
    object.set("must_update", entry.mustUpdate);
    object.set("checked_at", static_cast<std::int64_t>(entry.checkedAtMs));

    return platform::writeFileAtomically(file_, object.dump());
}
} // namespace chordengine::updates
