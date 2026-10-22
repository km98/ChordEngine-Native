#include "PackStoreClient.h"

#include "Licensing/LicensingTypes.h" // the shared Music-Prod origin constant only

#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>

namespace chordengine::packs
{
namespace
{
constexpr int connectionTimeoutMs = 12000;
constexpr double readDeadlineMs = 30000.0;
constexpr const char* defaultUnlockUrl = "https://music-prod.com/plus";

juce::var parseJsonBody(const juce::MemoryBlock& body)
{
    if (body.isEmpty())
        return {};
    juce::var parsed;
    const auto result = juce::JSON::parse(body.toString(), parsed);
    return result.failed() ? juce::var() : parsed;
}

juce::String stringAt(const juce::var& object, const char* key)
{
    const auto value = object.getProperty(key, juce::var());
    return value.isString() ? value.toString() : juce::String();
}

bool isInteger(const juce::var& value) noexcept
{
    return value.isInt() || value.isInt64();
}

juce::String interpunct()
{
    return juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
}

juce::String presetsText(int count)
{
    return juce::String(count) + (count == 1 ? " preset" : " presets");
}

std::optional<CatalogEntry> parseCatalogEntry(const juce::var& value, juce::String& error)
{
    if (!value.isObject())
    {
        error = "entry is not an object";
        return std::nullopt;
    }

    CatalogEntry entry;
    entry.slug = stringAt(value, "slug");
    entry.name = stringAt(value, "name").trim();
    entry.description = stringAt(value, "description");
    entry.version = stringAt(value, "version");
    entry.sha256 = stringAt(value, "sha256").toLowerCase();
    entry.artworkUrl = stringAt(value, "artwork_url");
    entry.isStock = static_cast<bool>(value.getProperty("is_stock", false));

    const auto presetCount = value.getProperty("preset_count", juce::var());
    const auto sizeBytes = value.getProperty("size_bytes", juce::var());
    const auto sortOrder = value.getProperty("sort_order", 0);

    if (!isValidSlug(entry.slug))
        error = "invalid slug";
    else if (entry.name.isEmpty() || entry.name.length() > maxPackNameLength)
        error = entry.slug + ": invalid name";
    else if (!isValidVersion(entry.version))
        error = entry.slug + ": invalid version";
    else if (!isValidSha256Hex(entry.sha256))
        error = entry.slug + ": invalid sha256";
    else if (!isInteger(presetCount) || static_cast<juce::int64>(presetCount) < minPresets
             || static_cast<juce::int64>(presetCount) > maxPresets)
        error = entry.slug + ": invalid preset_count";
    else if (!isInteger(sizeBytes) || static_cast<juce::int64>(sizeBytes) <= 0
             || static_cast<juce::int64>(sizeBytes) > static_cast<juce::int64>(maxPackBytes))
        error = entry.slug + ": invalid size_bytes";
    else if (!isInteger(sortOrder))
        error = entry.slug + ": invalid sort_order";
    if (error.isNotEmpty())
        return std::nullopt;

    entry.presetCount = static_cast<int>(presetCount);
    entry.sizeBytes = static_cast<juce::int64>(sizeBytes);
    entry.sortOrder = static_cast<int>(sortOrder);
    if (entry.description.length() > maxDescriptionLength)
        entry.description = entry.description.substring(0, maxDescriptionLength);
    // Artwork is optional decoration: an unsafe URL is dropped, not fatal.
    if (entry.artworkUrl.isNotEmpty() && !isAllowedStoreUrl(entry.artworkUrl))
        entry.artworkUrl.clear();
    return entry;
}

juce::String messageFor(PackStoreClient::DownloadOutcome outcome)
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
                                           const juce::String& slug)
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

HttpPackStoreTransport::~HttpPackStoreTransport()
{
    // Finish or cancel in-flight requests; each is a short round trip.
    pool_.removeAllJobs(true, 3000);
}

void HttpPackStoreTransport::get(const juce::String& url, std::size_t maxBytes, Callback callback)
{
    enqueue(url, {}, false, maxBytes, std::move(callback));
}

void HttpPackStoreTransport::postJson(const juce::String& url, const juce::String& json,
                                      std::size_t maxBytes, Callback callback)
{
    enqueue(url, json, true, maxBytes, std::move(callback));
}

void HttpPackStoreTransport::enqueue(juce::String requestUrl, juce::String requestBody, bool isPost,
                                     std::size_t maxBytes, Callback replyCallback)
{
    pool_.addJob(
        [url = std::move(requestUrl), postBody = std::move(requestBody), isPost, maxBytes,
         callback = std::move(replyCallback)]
        {
            StoreReply reply;
            int statusCode = 0;

            const auto options = juce::URL::InputStreamOptions(isPost
                                                                   ? juce::URL::ParameterHandling::inPostData
                                                                   : juce::URL::ParameterHandling::inAddress)
                                     .withExtraHeaders(isPost ? "Content-Type: application/json\r\n" : "")
                                     .withConnectionTimeoutMs(connectionTimeoutMs)
                                     .withStatusCode(&statusCode)
                                     // A redirect could leave https; the store
                                     // never redirects, so none is followed.
                                     .withNumRedirectsToFollow(0)
                                     .withHttpRequestCmd(isPost ? "POST" : "GET");

            auto target = juce::URL(url);
            if (isPost)
                target = target.withPOSTData(postBody);

            if (auto stream = target.createInputStream(options))
            {
                reply.transportOk = true;
                reply.status = statusCode;

                // Bounded read: never buffer more than the caller accepts, and
                // never let a stalled body hold the worker indefinitely.
                juce::MemoryOutputStream buffer(reply.body, false);
                std::array<char, 8192> chunk {};
                const auto startedAtMs = juce::Time::getMillisecondCounterHiRes();
                for (;;)
                {
                    if (juce::Time::getMillisecondCounterHiRes() - startedAtMs > readDeadlineMs)
                    {
                        reply.transportOk = false;
                        break;
                    }
                    const auto numRead = stream->read(chunk.data(), static_cast<int>(chunk.size()));
                    if (numRead <= 0)
                        break;
                    buffer.write(chunk.data(), static_cast<std::size_t>(numRead));
                    if (buffer.getDataSize() > maxBytes)
                    {
                        reply.truncated = true;
                        break;
                    }
                }
                buffer.flush();
            }

            if (callback == nullptr)
                return;
            juce::MessageManager::callAsync([callback, reply] { callback(reply); });
        });
}

// ============================================================== catalog
const CatalogEntry* Catalog::find(const juce::String& slug) const noexcept
{
    for (const auto& entry : packs)
        if (entry.slug == slug)
            return &entry;
    return nullptr;
}

std::optional<Catalog> parseCatalog(const juce::String& json, juce::String& error)
{
    error.clear();
    juce::var root;
    if (juce::JSON::parse(json, root).failed() || !root.isObject())
    {
        error = "catalog is not a JSON object";
        return std::nullopt;
    }

    const auto version = root.getProperty("version", juce::var());
    if (!isInteger(version) || static_cast<int>(version) != 1)
    {
        error = "unsupported catalog version";
        return std::nullopt;
    }

    const auto* entries = root.getProperty("packs", juce::var()).getArray();
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
        juce::String entryError;
        auto entry = parseCatalogEntry(value, entryError);
        if (!entry)
        {
            catalog.skipped.add(entryError);
            continue;
        }
        if (catalog.find(entry->slug) != nullptr)
        {
            catalog.skipped.add(entry->slug + ": duplicate");
            continue;
        }
        catalog.packs.push_back(std::move(*entry));
    }

    std::stable_sort(catalog.packs.begin(), catalog.packs.end(),
                     [](const CatalogEntry& a, const CatalogEntry& b)
                     {
                         if (a.sortOrder != b.sortOrder)
                             return a.sortOrder < b.sortOrder;
                         return a.name.compareIgnoreCase(b.name) < 0;
                     });
    return catalog;
}

bool isAllowedStoreUrl(const juce::String& url)
{
    const auto schemeEnd = url.indexOf("://");
    if (schemeEnd <= 0)
        return false;

    const auto scheme = url.substring(0, schemeEnd).toLowerCase();
    const auto rest = url.substring(schemeEnd + 3);
    auto authorityEnd = rest.length();
    for (const auto delimiter : { '/', '?', '#' })
    {
        const auto at = rest.indexOfChar(delimiter);
        if (at >= 0)
            authorityEnd = juce::jmin(authorityEnd, at);
    }
    const auto authority = rest.substring(0, authorityEnd);

    // No user-info ("https://good@evil/") and no IPv6 literals: the host has
    // to be exactly what it looks like.
    if (authority.isEmpty() || authority.containsAnyOf("@[]\\ ") || authority.containsAnyOf("\r\n\t"))
        return false;

    const auto host = authority.upToFirstOccurrenceOf(":", false, false).toLowerCase();
    if (host.isEmpty())
        return false;

    if (scheme == "https")
        return true;
    return scheme == "http" && (host == "127.0.0.1" || host == "localhost");
}

// =============================================================== client
PackStoreClient::PackStoreClient(PackStoreTransport& transport, PackLibrary& library,
                                 juce::String origin)
    : transport_(transport), library_(library),
      origin_(origin.isEmpty() ? juce::String(licensing::authOrigin) : std::move(origin))
{
    if (!origin_.endsWithChar('/'))
        origin_ << '/';
}

PackStoreClient::~PackStoreClient() = default;

juce::String PackStoreClient::functionUrl() const
{
    return origin_ + storeFunction;
}

juce::String PackStoreClient::catalogUrl() const
{
    return functionUrl() + "?action=catalog";
}

void PackStoreClient::fetchCatalog(CatalogCallback catalogCallback)
{
    const juce::WeakReference<PackStoreClient> weakThis(this);
    transport_.get(catalogUrl(), maxCatalogBytes,
                   [weakThis, callback = std::move(catalogCallback)](StoreReply reply)
                   {
                       if (weakThis.get() == nullptr || callback == nullptr)
                           return;

                       CatalogResult result;
                       if (!reply.transportOk)
                           result.error = messageFor(DownloadOutcome::transportFailed);
                       else if (reply.truncated)
                           result.error = "catalog too large";
                       else if (reply.status != 200)
                           result.error = "catalog unavailable (HTTP " + juce::String(reply.status) + ")";
                       else if (auto catalog = parseCatalog(reply.body.toString(), result.error))
                       {
                           result.ok = true;
                           result.catalog = std::move(*catalog);
                       }
                       callback(std::move(result));
                   });
}

void PackStoreClient::download(const juce::String& token, const juce::String& slug,
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
        juce::MessageManager::callAsync([callback, result] { if (callback) callback(result); });
        return;
    }

    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("action", "download");
    body->setProperty("token", token);
    body->setProperty("slug", slug);

    const juce::WeakReference<PackStoreClient> weakThis(this);
    transport_.postJson(functionUrl(), juce::JSON::toString(juce::var(body.get()), true),
                        maxControlReplyBytes,
                        [weakThis, slug, done = std::move(callback)](StoreReply reply)
                        {
                            if (auto* self = weakThis.get())
                                self->deliverDownloadGrant(slug, reply, done);
                        });
}

void PackStoreClient::deliverDownloadGrant(const juce::String& slug, const StoreReply& reply,
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
            result.unlockUrl = unlockUrl.startsWithIgnoreCase("https://") ? unlockUrl
                                                                          : juce::String(defaultUnlockUrl);
            const auto unlockMessage = stringAt(body, "unlock_message").trim();
            if (unlockMessage.isNotEmpty() && unlockMessage.length() <= 200)
                result.message = unlockMessage;
            return finish(result);
        }
        default: return finish(makeResult(DownloadOutcome::invalidResponse, slug));
    }

    const auto grantedSlug = stringAt(body, "slug");
    const auto version = stringAt(body, "version");
    const auto sha256 = stringAt(body, "sha256").toLowerCase();
    const auto downloadUrl = stringAt(body, "download_url");

    if (reply.truncated || !static_cast<bool>(body.getProperty("ok", false)) || grantedSlug != slug
        || !isValidSha256Hex(sha256) || downloadUrl.isEmpty())
        return finish(makeResult(DownloadOutcome::invalidResponse, slug));
    if (!isAllowedStoreUrl(downloadUrl))
        return finish(makeResult(DownloadOutcome::insecureUrl, slug));

    const juce::WeakReference<PackStoreClient> weakThis(this);
    transport_.get(downloadUrl, maxPackBytes,
                   [weakThis, slug, version, sha256, callback](StoreReply packReply)
                   {
                       if (auto* self = weakThis.get())
                           self->deliverPackBytes(slug, version, sha256, packReply, callback);
                   });
}

void PackStoreClient::deliverPackBytes(const juce::String& slug, const juce::String& version,
                                       const juce::String& sha256, const StoreReply& reply,
                                       DownloadCallback callback)
{
    DownloadResult result;
    if (!reply.transportOk)
        result = makeResult(DownloadOutcome::transportFailed, slug);
    else if (reply.truncated || reply.body.getSize() > maxPackBytes)
        result = makeResult(DownloadOutcome::tooLarge, slug);
    else if (reply.status == 404 || reply.status == 400 || reply.status == 403)
        result = makeResult(DownloadOutcome::notFound, slug); // e.g. an expired signed URL
    else if (reply.status != 200)
        result = makeResult(DownloadOutcome::invalidResponse, slug);
    else
    {
        const auto error = library_.install(slug, reply.body, sha256);
        if (error.isNotEmpty())
        {
            result = makeResult(DownloadOutcome::installFailed, slug);
            result.message << ": " << error;
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
juce::String statusText(PackStatus status, int presetCount, const juce::String& latestVersion)
{
    switch (status)
    {
        case PackStatus::builtIn: return "Built in" + interpunct() + presetsText(presetCount);
        case PackStatus::installed: return "Installed" + interpunct() + presetsText(presetCount);
        case PackStatus::updateAvailable:
            return latestVersion.isEmpty() ? juce::String("Update available")
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

            listing.usable = listing.installedVersion.isNotEmpty() && member;
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
