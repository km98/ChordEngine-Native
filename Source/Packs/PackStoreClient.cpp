#include "PackStoreClient.h"

#include "Licensing/LicensingTypes.h" // the shared Music-Prod origin constant only

#include "Platform/Platform.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace chordengine::packs
{
namespace
{
constexpr int connectionTimeoutMs = 12000;
constexpr double readDeadlineMs = 30000.0;
constexpr const char* defaultUnlockUrl = "https://music-prod.com/plus";

platform::Json parseJsonBody(const platform::Bytes& body)
{
    if (body.empty())
        return {};
    auto parsed = platform::Json::parse(platform::textOf(body));
    return parsed.has_value() ? *parsed : platform::Json();
}

std::string stringAt(const platform::Json& object, const char* key)
{
    const auto& value = object[key];
    return value.isString() ? value.stringRef() : std::string();
}

bool isInteger(const platform::Json& value) noexcept
{
    return value.isInteger();
}

std::string interpunct()
{
    return " \xc2\xb7 ";
}

std::string presetsText(int count)
{
    return std::to_string(count) + (count == 1 ? " preset" : " presets");
}

std::optional<CatalogEntry> parseCatalogEntry(const platform::Json& value, std::string& error)
{
    if (!value.isObject())
    {
        error = "entry is not an object";
        return std::nullopt;
    }

    CatalogEntry entry;
    entry.slug = stringAt(value, "slug");
    entry.name = text::trim(stringAt(value, "name"));
    entry.description = stringAt(value, "description");
    entry.version = stringAt(value, "version");
    entry.sha256 = text::toLower(stringAt(value, "sha256"));
    entry.artworkUrl = stringAt(value, "artwork_url");
    const auto& stockValue = value["is_stock"];
    entry.isStock = stockValue.isBool() ? stockValue.asBool() : licensing::varNumber(stockValue) != 0.0;

    const auto& presetCount = value["preset_count"];
    const auto& sizeBytes = value["size_bytes"];
    // A missing sort_order counts as 0.
    const auto sortOrder = value.has("sort_order") ? value["sort_order"] : platform::Json(0);

    if (!isValidSlug(entry.slug))
        error = "invalid slug";
    else if (entry.name.empty() || text::utf8Length(entry.name) > static_cast<std::size_t>(maxPackNameLength))
        error = entry.slug + ": invalid name";
    else if (!isValidVersion(entry.version))
        error = entry.slug + ": invalid version";
    else if (!isValidSha256Hex(entry.sha256))
        error = entry.slug + ": invalid sha256";
    else if (!isInteger(presetCount) || presetCount.asInt() < minPresets
             || presetCount.asInt() > maxPresets)
        error = entry.slug + ": invalid preset_count";
    else if (!isInteger(sizeBytes) || sizeBytes.asInt() <= 0
             || sizeBytes.asInt() > static_cast<std::int64_t>(maxPackBytes))
        error = entry.slug + ": invalid size_bytes";
    else if (!isInteger(sortOrder))
        error = entry.slug + ": invalid sort_order";
    if (!error.empty())
        return std::nullopt;

    entry.presetCount = static_cast<int>(presetCount.asInt());
    entry.sizeBytes = sizeBytes.asInt();
    entry.sortOrder = static_cast<int>(sortOrder.asInt());
    if (text::utf8Length(entry.description) > static_cast<std::size_t>(maxDescriptionLength))
    {
        // Cut at a character boundary after maxDescriptionLength characters.
        std::size_t characters = 0;
        std::size_t index = 0;
        for (; index < entry.description.size(); ++index)
            if ((static_cast<unsigned char>(entry.description[index]) & 0xC0) != 0x80
                && characters++ == static_cast<std::size_t>(maxDescriptionLength))
                break;
        entry.description.resize(index);
    }
    // Artwork is optional decoration: an unsafe URL is dropped, not fatal.
    if (!entry.artworkUrl.empty() && !isAllowedStoreUrl(entry.artworkUrl))
        entry.artworkUrl.clear();
    return entry;
}

std::string messageFor(PackStoreClient::DownloadOutcome outcome)
{
    using Outcome = PackStoreClient::DownloadOutcome;
    switch (outcome)
    {
        case Outcome::installed: return "Installed";
        case Outcome::unauthorized: return "Sign in again to download packs";
        case Outcome::membershipRequired: return "Music-Prod+ required";
        case Outcome::notFound: return "This pack is not available";
        case Outcome::transportFailed: return "Could not reach Music-Prod";
        case Outcome::invalidResponse: return "Unexpected answer from Music-Prod";
        case Outcome::insecureUrl: return "Download refused: insecure link";
        case Outcome::tooLarge: return "Download refused: pack too large";
        case Outcome::installFailed: return "Install failed";
    }
    return {};
}

PackStoreClient::DownloadResult makeResult(PackStoreClient::DownloadOutcome outcome,
                                           const std::string& slug)
{
    PackStoreClient::DownloadResult result;
    result.outcome = outcome;
    result.slug = slug;
    result.message = messageFor(outcome);
    return result;
}
} // namespace

// ============================================================ transport
HttpPackStoreTransport::HttpPackStoreTransport() = default;

// The worker queue waits up to 3 s for a request in flight.
HttpPackStoreTransport::~HttpPackStoreTransport() = default;

void HttpPackStoreTransport::get(const std::string& url, std::size_t maxBytes, Callback callback)
{
    enqueue(url, {}, false, maxBytes, std::move(callback));
}

void HttpPackStoreTransport::postJson(const std::string& url, const std::string& json,
                                      std::size_t maxBytes, Callback callback)
{
    enqueue(url, json, true, maxBytes, std::move(callback));
}

void HttpPackStoreTransport::enqueue(std::string requestUrl, std::string requestBody, bool isPost,
                                     std::size_t maxBytes, Callback replyCallback)
{
    platform::HttpRequest request;
    request.method = isPost ? "POST" : "GET";
    request.url = std::move(requestUrl);
    if (isPost)
        request.headers = { { "Content-Type", "application/json" } };
    request.body = std::move(requestBody);
    request.connectTimeoutMs = connectionTimeoutMs;
    // Bounded: never buffer more than the caller accepts, and never let a
    // stalled body hold the worker indefinitely.
    request.totalTimeoutMs = connectionTimeoutMs + static_cast<int>(readDeadlineMs);
    // One byte beyond the limit, so an oversized body is seen as truncated.
    request.maxResponseBytes = maxBytes + 1;
    // A redirect could leave https; the store never redirects, so none is
    // followed.
    request.maxRedirects = 0;

    worker_.post(
        [request = std::move(request), maxBytes, callback = std::move(replyCallback)]
        {
            StoreReply reply;
            const auto response = platform::httpRequest(request);
            if (response.transportOk && !response.timedOut)
            {
                reply.transportOk = true;
                reply.status = response.status;
                reply.truncated = response.truncated || response.body.size() > maxBytes;
                reply.body = platform::bytesOf(response.body);
            }

            if (callback == nullptr)
                return;
            platform::callOnMainThread([callback, reply] { callback(reply); });
        });
}

// ============================================================== catalog
const CatalogEntry* Catalog::find(const std::string& slug) const noexcept
{
    for (const auto& entry : packs)
        if (entry.slug == slug)
            return &entry;
    return nullptr;
}

std::optional<Catalog> parseCatalog(const std::string& json, std::string& error)
{
    error.clear();
    const auto document = platform::Json::parse(json);
    if (!document.has_value() || !document->isObject())
    {
        error = "catalog is not a JSON object";
        return std::nullopt;
    }
    const auto& root = *document;

    const auto& version = root["version"];
    if (!isInteger(version) || version.asInt() != 1)
    {
        error = "unsupported catalog version";
        return std::nullopt;
    }

    const auto& packsValue = root["packs"];
    const auto* entries = packsValue.isArray() ? &packsValue.items() : nullptr;
    if (entries == nullptr)
    {
        error = "catalog has no packs array";
        return std::nullopt;
    }

    Catalog catalog;
    catalog.version = 1;
    catalog.generatedAt = stringAt(root, "generated_at");
    for (const auto& value : *entries)
    {
        std::string entryError;
        auto entry = parseCatalogEntry(value, entryError);
        if (!entry)
        {
            catalog.skipped.push_back(entryError);
            continue;
        }
        if (catalog.find(entry->slug) != nullptr)
        {
            catalog.skipped.push_back(entry->slug + ": duplicate");
            continue;
        }
        catalog.packs.push_back(std::move(*entry));
    }

    std::stable_sort(catalog.packs.begin(), catalog.packs.end(),
                     [](const CatalogEntry& a, const CatalogEntry& b)
                     {
                         if (a.sortOrder != b.sortOrder)
                             return a.sortOrder < b.sortOrder;
                         return text::toLower(a.name) < text::toLower(b.name);
                     });
    return catalog;
}

bool isAllowedStoreUrl(const std::string& url)
{
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos || schemeEnd == 0)
        return false;

    const auto scheme = text::toLower(url.substr(0, schemeEnd));
    const auto rest = url.substr(schemeEnd + 3);
    auto authorityEnd = rest.size();
    for (const auto delimiter : { '/', '?', '#' })
    {
        const auto at = rest.find(delimiter);
        if (at != std::string::npos)
            authorityEnd = std::min(authorityEnd, at);
    }
    const auto authority = rest.substr(0, authorityEnd);

    // No user-info ("https://good@evil/") and no IPv6 literals: the host has
    // to be exactly what it looks like.
    if (authority.empty() || authority.find_first_of("@[]\\ \r\n\t") != std::string::npos)
        return false;

    const auto host = text::toLower(authority.substr(0, authority.find(':')));
    if (host.empty())
        return false;

    if (scheme == "https")
        return true;
    return scheme == "http" && (host == "127.0.0.1" || host == "localhost");
}

// =============================================================== client
PackStoreClient::PackStoreClient(PackStoreTransport& transport, PackLibrary& library,
                                 std::string origin)
    : transport_(transport), library_(library),
      origin_(origin.empty() ? std::string(licensing::authOrigin) : std::move(origin))
{
    if (origin_.empty() || origin_.back() != '/')
        origin_ += '/';
}

PackStoreClient::~PackStoreClient() = default;

std::string PackStoreClient::functionUrl() const
{
    return origin_ + storeFunction;
}

std::string PackStoreClient::catalogUrl() const
{
    return functionUrl() + "?action=catalog";
}

void PackStoreClient::fetchCatalog(CatalogCallback catalogCallback)
{
    const auto weakThis = lifetime_.weak();
    transport_.get(catalogUrl(), maxCatalogBytes,
                   [weakThis, callback = std::move(catalogCallback)](StoreReply reply)
                   {
                       if (!platform::LifetimeToken::alive(weakThis) || callback == nullptr)
                           return;

                       CatalogResult result;
                       if (!reply.transportOk)
                           result.error = messageFor(DownloadOutcome::transportFailed);
                       else if (reply.truncated)
                           result.error = "catalog too large";
                       else if (reply.status != 200)
                           result.error = "catalog unavailable (HTTP " + std::to_string(reply.status) + ")";
                       else if (auto catalog = parseCatalog(platform::textOf(reply.body), result.error))
                       {
                           result.ok = true;
                           result.catalog = std::move(*catalog);
                       }
                       callback(std::move(result));
                   });
}

void PackStoreClient::download(const std::string& token, const std::string& slug,
                               DownloadCallback callback)
{
    if (!isValidSlug(slug) || slug == stockSlug)
    {
        // The stock pack is built in; nothing to fetch. Deliver asynchronously
        // like every other answer.
        auto result = makeResult(slug == stockSlug ? DownloadOutcome::installed
                                                   : DownloadOutcome::notFound, slug);
        if (slug == stockSlug)
            result.version = stockVersion;
        platform::callOnMainThread([callback, result] { if (callback) callback(result); });
        return;
    }

    auto body = platform::Json::object();
    body.set("action", "download");
    body.set("token", token);
    body.set("slug", slug);

    const auto weakThis = lifetime_.weak();
    auto* const owner = this;
    transport_.postJson(functionUrl(), body.dump(), maxControlReplyBytes,
                        [weakThis, owner, slug, done = std::move(callback)](StoreReply reply)
                        {
                            if (platform::LifetimeToken::alive(weakThis))
                                owner->deliverDownloadGrant(slug, reply, done);
                        });
}

void PackStoreClient::deliverDownloadGrant(const std::string& slug, const StoreReply& reply,
                                           DownloadCallback callback)
{
    const auto finish = [&callback](DownloadResult result)
    {
        if (callback)
            callback(std::move(result));
    };

    if (!reply.transportOk)
        return finish(makeResult(DownloadOutcome::transportFailed, slug));

    const auto body = parseJsonBody(reply.body);
    switch (reply.status)
    {
        case 200: break;
        case 401: return finish(makeResult(DownloadOutcome::unauthorized, slug));
        case 404: return finish(makeResult(DownloadOutcome::notFound, slug));
        case 403:
        {
            auto result = makeResult(DownloadOutcome::membershipRequired, slug);
            const auto unlockUrl = stringAt(body, "unlock_url");
            result.unlockUrl = text::startsWith(text::toLower(unlockUrl), "https://")
                ? unlockUrl : std::string(defaultUnlockUrl);
            const auto unlockMessage = text::trim(stringAt(body, "unlock_message"));
            if (!unlockMessage.empty() && text::utf8Length(unlockMessage) <= 200)
                result.message = unlockMessage;
            return finish(result);
        }
        default: return finish(makeResult(DownloadOutcome::invalidResponse, slug));
    }

    const auto grantedSlug = stringAt(body, "slug");
    const auto version = stringAt(body, "version");
    const auto sha256 = text::toLower(stringAt(body, "sha256"));
    const auto downloadUrl = stringAt(body, "download_url");

    const auto& okValue = body["ok"];
    const bool ok = okValue.isBool() ? okValue.asBool() : licensing::varNumber(okValue) != 0.0;
    if (reply.truncated || !ok || grantedSlug != slug
        || !isValidSha256Hex(sha256) || downloadUrl.empty())
        return finish(makeResult(DownloadOutcome::invalidResponse, slug));
    if (!isAllowedStoreUrl(downloadUrl))
        return finish(makeResult(DownloadOutcome::insecureUrl, slug));

    const auto weakThis = lifetime_.weak();
    auto* const owner = this;
    transport_.get(downloadUrl, maxPackBytes,
                   [weakThis, owner, slug, version, sha256, callback](StoreReply packReply)
                   {
                       if (platform::LifetimeToken::alive(weakThis))
                           owner->deliverPackBytes(slug, version, sha256, packReply, callback);
                   });
}

void PackStoreClient::deliverPackBytes(const std::string& slug, const std::string& version,
                                       const std::string& sha256, const StoreReply& reply,
                                       DownloadCallback callback)
{
    DownloadResult result;
    if (!reply.transportOk)
        result = makeResult(DownloadOutcome::transportFailed, slug);
    else if (reply.truncated || reply.body.size() > maxPackBytes)
        result = makeResult(DownloadOutcome::tooLarge, slug);
    else if (reply.status == 404 || reply.status == 400 || reply.status == 403)
        result = makeResult(DownloadOutcome::notFound, slug); // e.g. an expired signed URL
    else if (reply.status != 200)
        result = makeResult(DownloadOutcome::invalidResponse, slug);
    else
    {
        const auto error = library_.install(slug, reply.body, sha256);
        if (!error.empty())
        {
            result = makeResult(DownloadOutcome::installFailed, slug);
            result.message += ": " + error;
        }
        else
        {
            result = makeResult(DownloadOutcome::installed, slug);
            const auto* pack = library_.find(slug);
            result.version = pack != nullptr ? pack->version : version;
            if (pack != nullptr)
                result.message = "Installed " + pack->name;
        }
    }

    if (callback)
        callback(std::move(result));
}

// =========================================================== store view
std::string statusText(PackStatus status, int presetCount, const std::string& latestVersion)
{
    switch (status)
    {
        case PackStatus::builtIn: return "Built in" + interpunct() + presetsText(presetCount);
        case PackStatus::installed: return "Installed" + interpunct() + presetsText(presetCount);
        case PackStatus::updateAvailable:
            return latestVersion.empty() ? std::string("Update available")
                                           : "Update available" + interpunct() + latestVersion;
        case PackStatus::available: return "Click to download";
        case PackStatus::requiresMembership: return "Music-Prod+ required";
        case PackStatus::signInRequired: return "Sign in to download";
    }
    return {};
}

std::vector<PackListing> mergeListings(const Catalog* catalog, const PackLibrary& library,
                                       StoreAccess access)
{
    const bool member = access.isMember();
    const auto lockedStatus = access.signedIn ? PackStatus::requiresMembership
                                              : PackStatus::signInRequired;

    std::vector<PackListing> listings;
    const auto finalise = [&listings](PackListing listing)
    {
        listing.statusText = statusText(listing.status, listing.presetCount, listing.latestVersion);
        listings.push_back(std::move(listing));
    };

    // 1. The stock pack: built in, never gated here.
    if (const auto* stock = library.find(stockSlug))
    {
        PackListing listing;
        listing.slug = stock->slug;
        listing.name = stock->name;
        listing.description = stock->description;
        listing.presetCount = static_cast<int>(stock->presets.size());
        listing.status = PackStatus::builtIn;
        listing.usable = true;
        listing.installedVersion = stock->version;
        if (catalog != nullptr)
            if (const auto* entry = catalog->find(stockSlug))
            {
                listing.inCatalog = true;
                listing.latestVersion = entry->version;
            }
        finalise(std::move(listing));
    }

    // 2. Catalog order.
    if (catalog != nullptr)
    {
        for (const auto& entry : catalog->packs)
        {
            if (entry.slug == stockSlug)
                continue;

            PackListing listing;
            listing.slug = entry.slug;
            listing.name = entry.name;
            listing.description = entry.description;
            listing.inCatalog = true;
            listing.latestVersion = entry.version;
            listing.presetCount = entry.presetCount;

            if (const auto* installed = library.find(entry.slug))
            {
                listing.installedVersion = installed->version;
                listing.presetCount = static_cast<int>(installed->presets.size());
                if (!member)
                    listing.status = lockedStatus;
                else if (compareVersions(entry.version, installed->version) > 0)
                    listing.status = PackStatus::updateAvailable;
                else
                    listing.status = PackStatus::installed;
            }
            else
            {
                listing.status = member ? PackStatus::available : lockedStatus;
            }

            listing.usable = !listing.installedVersion.empty() && member;
            listing.canDownload = member
                && (listing.status == PackStatus::available
                    || listing.status == PackStatus::updateAvailable);
            finalise(std::move(listing));
        }
    }

    // 3. Installed packs the catalog does not (or no longer) list - or every
    //    installed pack while the catalog is unavailable.
    for (const auto& pack : library.list())
    {
        if (pack.isStock || (catalog != nullptr && catalog->find(pack.slug) != nullptr))
            continue;

        PackListing listing;
        listing.slug = pack.slug;
        listing.name = pack.name;
        listing.description = pack.description;
        listing.presetCount = static_cast<int>(pack.presets.size());
        listing.installedVersion = pack.version;
        listing.status = member ? PackStatus::installed : lockedStatus;
        listing.usable = member;
        finalise(std::move(listing));
    }

    return listings;
}
} // namespace chordengine::packs
