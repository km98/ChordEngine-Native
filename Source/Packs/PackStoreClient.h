#pragma once

#include "PackFormat.h"
#include "PackLibrary.h"

#include <juce_core/juce_core.h>

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
    juce::MemoryBlock body;
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

    virtual void get(const juce::String& url, std::size_t maxBytes, Callback callback) = 0;
    virtual void postJson(const juce::String& url, const juce::String& json,
                          std::size_t maxBytes, Callback callback) = 0;
};

// Production transport: juce::URL on a private single-worker ThreadPool,
// replies marshalled to the message thread (the MusicProdAuthService model).
class HttpPackStoreTransport final : public PackStoreTransport
{
public:
    HttpPackStoreTransport();
    ~HttpPackStoreTransport() override;

    void get(const juce::String& url, std::size_t maxBytes, Callback callback) override;
    void postJson(const juce::String& url, const juce::String& json,
                  std::size_t maxBytes, Callback callback) override;

private:
    void enqueue(juce::String url, juce::String postBody, bool isPost,
                 std::size_t maxBytes, Callback callback);

    juce::ThreadPool pool_ { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HttpPackStoreTransport)
};

// ---------------------------------------------------------------- catalog
struct CatalogEntry
{
    juce::String slug;
    juce::String name;
    juce::String description;
    juce::String version;
    int presetCount = 0;
    bool isStock = false;
    juce::int64 sizeBytes = 0;
    juce::String sha256;
    juce::String artworkUrl; // "" when absent
    int sortOrder = 0;
};

struct Catalog
{
    int version = 0;
    juce::String generatedAt;
    std::vector<CatalogEntry> packs; // ordered by sortOrder, then name
    juce::StringArray skipped;       // entries that failed validation

    const CatalogEntry* find(const juce::String& slug) const noexcept;
};

// Validates the catalog document. A malformed document fails as a whole; a
// malformed ENTRY is skipped (and named in `skipped`) so one bad row on the
// server never empties the store page.
std::optional<Catalog> parseCatalog(const juce::String& json, juce::String& error);

// https only - except plain http to 127.0.0.1 / localhost, so the local test
// server works. Anything else (other schemes, http to a remote host, no
// host) is refused before a request is made.
bool isAllowedStoreUrl(const juce::String& url);

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
        juce::String error;
    };

    struct DownloadResult
    {
        DownloadOutcome outcome = DownloadOutcome::invalidResponse;
        juce::String slug;
        juce::String version;   // installed version on success
        juce::String message;   // short, user-facing
        juce::String unlockUrl; // set for membershipRequired
    };

    using CatalogCallback = std::function<void(CatalogResult)>;
    using DownloadCallback = std::function<void(DownloadResult)>;

    // `origin` must end with "/" (one is appended otherwise). The library is
    // borrowed and must outlive the client.
    PackStoreClient(PackStoreTransport& transport, PackLibrary& library, juce::String origin = {});
    ~PackStoreClient();

    juce::String catalogUrl() const;
    juce::String functionUrl() const;

    // Callbacks run on the message thread, and never after the client has
    // been destroyed.
    void fetchCatalog(CatalogCallback callback);
    void download(const juce::String& token, const juce::String& slug, DownloadCallback callback);

private:
    void deliverDownloadGrant(const juce::String& slug, const StoreReply& reply,
                              DownloadCallback callback);
    void deliverPackBytes(const juce::String& slug, const juce::String& version,
                          const juce::String& sha256, const StoreReply& reply,
                          DownloadCallback callback);

    PackStoreTransport& transport_;
    PackLibrary& library_;
    juce::String origin_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(PackStoreClient)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PackStoreClient)
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
    juce::String slug;
    juce::String name;
    juce::String description;
    int presetCount = 0;
    PackStatus status = PackStatus::signInRequired;
    juce::String statusText;      // "Installed · 12 presets", "Click to download", ...
    bool usable = false;          // its presets may be offered right now
    bool canDownload = false;     // a click should call download()
    bool inCatalog = false;
    juce::String installedVersion; // "" when not installed
    juce::String latestVersion;    // "" when not in the catalog
};

// "Built in · 12 presets", "Installed · 4 presets", "Update available · 1.1.0",
// "Click to download", "Music-Prod+ required", "Sign in to download".
juce::String statusText(PackStatus status, int presetCount, const juce::String& latestVersion = {});

// Merges the catalog (may be null: offline / not fetched yet), the installed
// packs and the account state. Order: stock first, then catalog order, then
// installed packs the catalog no longer lists.
std::vector<PackListing> mergeListings(const Catalog* catalog, const PackLibrary& library,
                                       StoreAccess access);
} // namespace chordengine::packs
