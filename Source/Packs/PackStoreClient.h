#pragma once

#include "PackFormat.h"
#include "PackLibrary.h"

#include "Platform/Bytes.h"
#include "Platform/Json.h"
#include "Platform/Platform.h"
#include "Platform/Text.h"

#include <string>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// =====================================================================
// The Preset Pack store client.
//
//   GET  <origin>functions/v1/chordengine-packs?action=catalog   (public)
//   POST <origin>functions/v1/chordengine-packs
//        { "action": "download", "token": "...", "slug": "..." }
//          200 { ok, slug, version, sha256, download_url }  (signed, 10 min)
//          401 { error: "unauthorized" }
//          403 { error: "membership_required", unlock_url, unlock_message }
//          404 { error: "not_found" }
//   GET  download_url  -> the pack JSON, installed only if its SHA-256 equals
//                         the digest of the 200 answer.
//
// The origin defaults to the one licensing uses (licensing::authOrigin) and
// can be overridden for Tools/LocalPackServer.py. The token is the opaque
// plugin token the licensing boundary already holds; it is sent, never
// stored here. Membership is decided by the server: everything this client
// derives locally (PackStatus) is presentation only.
// =====================================================================
namespace chordengine::packs
{
inline constexpr const char* storeFunction = "functions/v1/chordengine-packs";
inline constexpr std::size_t maxCatalogBytes = 1024u * 1024u;
inline constexpr std::size_t maxControlReplyBytes = 64u * 1024u;

// One HTTP round trip. `body` is the exact bytes received (the download is
// hashed as received). `truncated` is set when the body exceeded the limit
// the request named; such a body is never used.
struct StoreReply
{
    int status = 0;
    platform::Bytes body;
    bool transportOk = false; // the server answered, whatever the status
    bool truncated = false;
};

// ---------------------------------------------------------------------
// The network seam. Implementations MUST NOT block the calling thread and
// MUST deliver the callback on the message thread.
// ---------------------------------------------------------------------
class PackStoreTransport
{
public:
    using Callback = std::function<void(StoreReply)>;
    virtual ~PackStoreTransport() = default;

    virtual void get(const std::string& url, std::size_t maxBytes, Callback callback) = 0;
    virtual void postJson(const std::string& url, const std::string& json,
                          std::size_t maxBytes, Callback callback) = 0;
};

// Production transport: the platform HTTP layer on a private single-worker
// queue, replies marshalled to the main thread (the MusicProdAuthService model).
class HttpPackStoreTransport final : public PackStoreTransport
{
public:
    HttpPackStoreTransport();
    ~HttpPackStoreTransport() override;

    void get(const std::string& url, std::size_t maxBytes, Callback callback) override;
    void postJson(const std::string& url, const std::string& json,
                  std::size_t maxBytes, Callback callback) override;

private:
    void enqueue(std::string url, std::string postBody, bool isPost,
                 std::size_t maxBytes, Callback callback);

    platform::WorkerQueue worker_ { 3000 };
};

// ---------------------------------------------------------------- catalog
struct CatalogEntry
{
    std::string slug;
    std::string name;
    std::string description;
    std::string version;
    int presetCount = 0;
    bool isStock = false;
    std::int64_t sizeBytes = 0;
    std::string sha256;
    std::string artworkUrl; // "" when absent
    int sortOrder = 0;
};

struct Catalog
{
    int version = 0;
    std::string generatedAt;
    std::vector<CatalogEntry> packs; // ordered by sortOrder, then name
    std::vector<std::string> skipped; // entries that failed validation

    const CatalogEntry* find(const std::string& slug) const noexcept;
};

// Validates the catalog document. A malformed document fails as a whole; a
// malformed ENTRY is skipped (and named in `skipped`) so one bad row on the
// server never empties the store page.
std::optional<Catalog> parseCatalog(const std::string& json, std::string& error);

// https only - except plain http to 127.0.0.1 / localhost, so the local test
// server works. Anything else (other schemes, http to a remote host, no
// host) is refused before a request is made.
bool isAllowedStoreUrl(const std::string& url);

// ---------------------------------------------------------------- client
class PackStoreClient
{
public:
    enum class DownloadOutcome
    {
        installed,
        unauthorized,       // 401: token unknown/revoked/not a ChordEngine token
        membershipRequired, // 403: signed in, but not an active Music-Prod+ member
        notFound,           // 404: no such released pack (or file unavailable)
        transportFailed,    // never reached the server
        invalidResponse,    // unexpected status or malformed answer
        insecureUrl,        // the download URL is not https (or local http)
        tooLarge,           // the pack exceeded maxPackBytes
        installFailed       // checksum mismatch, invalid pack, disk error
    };

    struct CatalogResult
    {
        bool ok = false;
        Catalog catalog;
        std::string error;
    };

    struct DownloadResult
    {
        DownloadOutcome outcome = DownloadOutcome::invalidResponse;
        std::string slug;
        std::string version;   // installed version on success
        std::string message;   // short, user-facing
        std::string unlockUrl; // set for membershipRequired
    };

    using CatalogCallback = std::function<void(CatalogResult)>;
    using DownloadCallback = std::function<void(DownloadResult)>;

    // `origin` must end with "/" (one is appended otherwise). The library is
    // borrowed and must outlive the client.
    PackStoreClient(PackStoreTransport& transport, PackLibrary& library, std::string origin = {});
    ~PackStoreClient();

    std::string catalogUrl() const;
    std::string functionUrl() const;

    // Callbacks run on the message thread, and never after the client has
    // been destroyed.
    void fetchCatalog(CatalogCallback callback);
    void download(const std::string& token, const std::string& slug, DownloadCallback callback);

private:
    void deliverDownloadGrant(const std::string& slug, const StoreReply& reply,
                              DownloadCallback callback);
    void deliverPackBytes(const std::string& slug, const std::string& version,
                          const std::string& sha256, const StoreReply& reply,
                          DownloadCallback callback);

    PackStoreTransport& transport_;
    PackLibrary& library_;
    std::string origin_;

    platform::LifetimeToken lifetime_;
};

// ---------------------------------------------------------- store view
enum class PackStatus
{
    builtIn,            // the stock pack
    installed,          // on disk, usable, up to date
    updateAvailable,    // on disk, usable, the catalog has a newer version
    available,          // member, not installed: click to download
    requiresMembership, // signed in, not a member
    signInRequired      // signed out
};

// What the store view needs to know about the account. `subscribed` is the
// cached, VERIFIED Music-Prod+ membership (CachedEntitlement::subscribed).
struct StoreAccess
{
    bool signedIn = false;
    bool subscribed = false;

    bool isMember() const noexcept { return signedIn && subscribed; }
};

struct PackListing
{
    std::string slug;
    std::string name;
    std::string description;
    int presetCount = 0;
    PackStatus status = PackStatus::signInRequired;
    std::string statusText;      // "Installed · 12 presets", "Click to download", ...
    bool usable = false;          // its presets may be offered right now
    bool canDownload = false;     // a click should call download()
    bool inCatalog = false;
    std::string installedVersion; // "" when not installed
    std::string latestVersion;    // "" when not in the catalog
};

// "Built in · 12 presets", "Installed · 4 presets", "Update available · 1.1.0",
// "Click to download", "Music-Prod+ required", "Sign in to download".
std::string statusText(PackStatus status, int presetCount, const std::string& latestVersion = {});

// Merges the catalog (may be null: offline / not fetched yet), the installed
// packs and the account state. Order: stock first, then catalog order, then
// installed packs the catalog no longer lists.
std::vector<PackListing> mergeListings(const Catalog* catalog, const PackLibrary& library,
                                       StoreAccess access);
} // namespace chordengine::packs
