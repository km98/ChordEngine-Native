// Preset Packs - format, library, store client and store view.
//
// Every store reply in the default run is produced by a local fake
// transport: no case performs a network request and nothing contacts the
// production endpoint. Two optional modes exist for development:
//
//   ChordEnginePackTests --emit-stock <file>
//       writes toJson(makeStockPack()) - how Resources/Packs/chordengine-stock.json
//       is (re)generated. The default run fails if the file drifts.
//
//   ChordEnginePackTests --e2e http://127.0.0.1:<port>/
//       drives the REAL HttpPackStoreTransport against Tools/LocalPackServer.py
//       (catalog -> download -> install for member-token, 403 for
//       nonmember-token, 401 for an unknown token). Never point it at the
//       production origin.

#include "Core/ChordPresetSystem.h"
#include "Licensing/LicensingTypes.h"
#include "Packs/PackFormat.h"
#include "Packs/PackLibrary.h"
#include "Packs/PackStoreClient.h"

#include "Platform/Bytes.h"
#include "Platform/Text.h"
#include "TestSupport.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#ifndef CHORDENGINE_SOURCE_DIR
 #error "CHORDENGINE_SOURCE_DIR must point at the repository root"
#endif

namespace
{
namespace packs = chordengine::packs;
using packs::PackStatus;
using packs::PackStoreClient;
using packs::StoreReply;
using Outcome = PackStoreClient::DownloadOutcome;

int casesPassed = 0;

[[noreturn]] void fail(const std::string& message)
{
    std::cout << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

void casePassed(const char* name)
{
    ++casesPassed;
    std::cout << "PASS: " << name << '\n';
}

std::filesystem::path repoFile(const std::string& relative)
{
    return std::filesystem::path(CHORDENGINE_SOURCE_DIR) / relative;
}

std::string loadText(const std::string& relative)
{
    const auto file = repoFile(relative);
    require(std::filesystem::exists(file), "missing repository file " + relative);
    return chordengine::test::readText(file);
}

chordengine::platform::Bytes bytesOf(const std::string& text)
{
    return chordengine::platform::bytesOf(text);
}

chordengine::platform::Bytes fileBytes(const std::filesystem::path& file)
{
    return chordengine::platform::bytesOf(chordengine::test::readText(file));
}

void pump(const std::function<bool()>& done, int timeoutMs = 5000)
{
    const auto deadline = chordengine::platform::millisecondCounterHiRes() + timeoutMs;
    while (!done() && chordengine::platform::millisecondCounterHiRes() < deadline)
        chordengine::platform::runMainLoopFor(10);
}

// A scratch directory, removed on scope exit.
struct ScratchDir
{
    ScratchDir()
        : dir(chordengine::platform::temporaryDirectory() / ("ChordEnginePackTests-" + chordengine::platform::randomHex(6)))
    {
        std::filesystem::create_directories(dir);
    }
    ~ScratchDir()
    {
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }
    std::filesystem::path dir;
};

std::string repeated(const char* text, int count)
{
    std::string out;
    for (int index = 0; index < count; ++index)
        out += text;
    return out;
}

// ------------------------------------------------------- pack mutations
chordengine::platform::Json testPackVar()
{
    return chordengine::test::parseJson(loadText("Resources/Packs/chordengine-test-pack.json"));
}

// A deep copy, so every mutation starts from the pristine document.
chordengine::platform::Json clone(const chordengine::platform::Json& value)
{
    return chordengine::test::parseJson(value.dump());
}

chordengine::platform::Json& presetAt(chordengine::platform::Json& root, int index)
{
    return root.member("presets").mutableItems()[static_cast<std::size_t>(index)];
}

std::string mutated(const std::function<void(chordengine::platform::Json&)>& change)
{
    auto root = clone(testPackVar());
    change(root);
    return root.dump();
}

std::string withPreset(const std::string& name, const char* voicing, std::uint8_t code)
{
    packs::PackDefinition pack;
    pack.slug = "custom-pack";
    pack.name = "Custom";
    pack.version = "1.0.0";
    packs::PackPreset preset;
    preset.name = name;
    preset.voicing = packs::voicingFromString(voicing).value_or(packs::VoicingStyle::close);
    preset.triggerMap.fill(code);
    pack.presets.push_back(preset);
    return packs::toJson(pack);
}

// ------------------------------------------------------- fake transport
// Scripted by URL (GET) or by function URL (POST). Replies are delivered
// synchronously unless `deferred` is set, in which case they wait in
// `pending` until the test releases them.
class FakeTransport final : public packs::PackStoreTransport
{
public:
    struct Request
    {
        std::string method, url, body;
        std::size_t maxBytes = 0;
    };

    void get(const std::string& url, std::size_t maxBytes, Callback callback) override
    {
        requests.push_back({ "GET", url, {}, maxBytes });
        respond(url, maxBytes, std::move(callback));
    }

    void postJson(const std::string& url, const std::string& json, std::size_t maxBytes,
                  Callback callback) override
    {
        requests.push_back({ "POST", url, json, maxBytes });
        respond(url, maxBytes, std::move(callback));
    }

    void script(const std::string& url, StoreReply reply) { replies[url].push_back(std::move(reply)); }

    std::vector<Request> requests;
    std::map<std::string, std::deque<StoreReply>> replies;
    std::vector<std::function<void()>> pending;
    bool deferred = false;

private:
    void respond(const std::string& url, std::size_t maxBytes, Callback callback)
    {
        StoreReply reply; // unscripted URL: transport failure
        auto& queue = replies[url];
        if (!queue.empty())
        {
            reply = queue.front();
            queue.pop_front();
        }
        // Emulate the production transport's bounded read.
        if (reply.body.size() > maxBytes)
        {
            reply.body.resize(maxBytes + 1);
            reply.truncated = true;
        }
        auto deliver = [callback, reply] { callback(reply); };
        if (deferred)
            pending.push_back(std::move(deliver));
        else
            deliver();
    }
};

StoreReply jsonReply(int status, const std::string& json)
{
    StoreReply reply;
    reply.status = status;
    reply.transportOk = true;
    reply.body = bytesOf(json);
    return reply;
}

StoreReply bytesReply(int status, chordengine::platform::Bytes body)
{
    StoreReply reply;
    reply.status = status;
    reply.transportOk = true;
    reply.body = std::move(body);
    return reply;
}

std::string grantJson(const std::string& slug, const std::string& sha, const std::string& url,
                       const std::string& version = "1.0.0")
{
    auto body = chordengine::platform::Json::object();
    body.set("ok", true);
    body.set("slug", slug);
    body.set("version", version);
    body.set("sha256", sha);
    body.set("download_url", url);
    return body.dump();
}

constexpr const char* fakeOrigin = "https://store.example.test/";
constexpr const char* fakeFunction = "https://store.example.test/functions/v1/chordengine-packs";
constexpr const char* fakeFileUrl = "https://files.example.test/signed/chordengine-test-pack.json?token=x";

std::optional<PackStoreClient::DownloadResult> runDownload(PackStoreClient& client,
                                                           const std::string& token,
                                                           const std::string& slug)
{
    std::optional<PackStoreClient::DownloadResult> result;
    client.download(token, slug, [&result](PackStoreClient::DownloadResult r) { result = std::move(r); });
    pump([&result] { return result.has_value(); });
    return result;
}

std::string catalogJson(const std::string& testVersion, const std::string& testSha)
{
    return "{\"version\":1,\"generated_at\":\"2026-10-18T00:00:00Z\",\"packs\":["
           "{\"slug\":\"chordengine-test-pack\",\"name\":\"ChordEngine Test Pack\",\"description\":\"t\","
           "\"version\":\"" + testVersion + "\",\"preset_count\":4,\"is_stock\":false,\"size_bytes\":2000,"
           "\"sha256\":\"" + testSha + "\",\"sort_order\":20},"
           "{\"slug\":\"chordengine-stock\",\"name\":\"ChordEngine Stock Pack\",\"description\":\"s\","
           "\"version\":\"1.0.0\",\"preset_count\":12,\"is_stock\":true,\"size_bytes\":9000,"
           "\"sha256\":\"" + repeated("a", 64) + "\",\"sort_order\":10,"
           "\"artwork_url\":\"http://evil.example/x.png\"},"
           "{\"slug\":\"Bad Slug\",\"name\":\"x\",\"version\":\"1.0.0\",\"preset_count\":1,"
           "\"size_bytes\":1,\"sha256\":\"" + repeated("b", 64) + "\"}]}";
}

// =================================================================== e2e
int runEndToEnd(const std::string& origin)
{
    require(chordengine::text::startsWith(origin, "http://127.0.0.1:") || chordengine::text::startsWith(origin, "http://localhost:"),
            "--e2e only accepts a local origin");

    ScratchDir scratch;
    packs::PackLibrary library(scratch.dir / "Packs");
    library.scan();
    packs::HttpPackStoreTransport transport;
    PackStoreClient client(transport, library, origin);

    std::optional<PackStoreClient::CatalogResult> catalog;
    client.fetchCatalog([&catalog](PackStoreClient::CatalogResult r) { catalog = std::move(r); });
    pump([&catalog] { return catalog.has_value(); }, 15000);
    require(catalog && catalog->ok, "e2e catalog: " + (catalog ? catalog->error : std::string("timeout")));
    require(catalog->catalog.find(packs::stockSlug) != nullptr
                && catalog->catalog.find("chordengine-test-pack") != nullptr,
            "e2e catalog lists the stock and the test pack");
    casePassed("e2e: catalog fetched over HTTP");

    const auto before = packs::mergeListings(&catalog->catalog, library, { true, true });
    require(before.size() == 2 && before[0].status == PackStatus::builtIn
                && before[1].status == PackStatus::available,
            "e2e: before download the test pack is 'Click to download'");

    auto denied = runDownload(client, "nonmember-token", "chordengine-test-pack");
    require(denied && denied->outcome == Outcome::membershipRequired && !denied->unlockUrl.empty(),
            "e2e nonmember-token is refused with 403 membership_required");
    require(library.find("chordengine-test-pack") == nullptr, "e2e: refusal installs nothing");
    casePassed("e2e: nonmember-token -> 403 membership_required");

    auto unknown = runDownload(client, "some-other-token", "chordengine-test-pack");
    require(unknown && unknown->outcome == Outcome::unauthorized, "e2e unknown token is 401");
    casePassed("e2e: unknown token -> 401 unauthorized");

    auto missing = runDownload(client, "member-token", "no-such-pack");
    require(missing && missing->outcome == Outcome::notFound, "e2e unknown slug is 404");
    casePassed("e2e: unknown slug -> 404 not_found");

    auto granted = runDownload(client, "member-token", "chordengine-test-pack");
    require(granted && granted->outcome == Outcome::installed,
            "e2e member-token installs the pack: " + (granted ? granted->message : std::string("timeout")));
    const auto installedFile = library.fileFor("chordengine-test-pack");
    const auto onDisk = fileBytes(installedFile);
    require(packs::sha256Hex(onDisk) == catalog->catalog.find("chordengine-test-pack")->sha256,
            "e2e: the installed file hashes to the catalog digest");
    packs::PackLibrary reloaded(scratch.dir / "Packs");
    reloaded.scan();
    require(reloaded.find("chordengine-test-pack") != nullptr
                && reloaded.find("chordengine-test-pack")->presets.size() == 4,
            "e2e: a fresh scan finds the installed pack");
    const auto after = packs::mergeListings(&catalog->catalog, library, { true, true });
    require(after[1].status == PackStatus::installed
                && after[1].statusText == std::string("Installed \xc2\xb7 4 presets"),
            "e2e: after download the test pack is 'Installed (dot) 4 presets'");
    casePassed("e2e: member-token -> catalog -> download -> install");

    std::cout << "ChordEnginePackTests --e2e: " << casesPassed << " end-to-end cases passed against "
              << origin << '\n';
    return EXIT_SUCCESS;
}
} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "--emit-stock")
    {
        const auto ok = chordengine::platform::writeFileAtomically(std::filesystem::current_path() / args[1],
                                                                   packs::toJson(packs::makeStockPack()));
        std::cout << (ok ? "wrote " : "could not write ") << args[1] << '\n';
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (args.size() == 2 && args[0] == "--e2e")
        return runEndToEnd(args[1]);

    // =================================================================
    // 1. The shipped test pack parses, is canonical and is musically
    //    distinct.
    // =================================================================
    {
        const auto text = loadText("Resources/Packs/chordengine-test-pack.json");
        std::string error;
        const auto pack = packs::parse(text, error);
        require(pack.has_value(), "test pack valid: " + error);
        require(error.empty(), "a successful parse clears the error");
        require(pack->slug == "chordengine-test-pack" && pack->name == "ChordEngine Test Pack"
                    && pack->version == "1.0.0" && pack->author == "Music-Prod" && !pack->isStock,
                "test pack header");
        require(pack->presets.size() == 4, "test pack has four presets");
        const std::vector<std::string> names { "Test Gospel", "Test Jazz Ninths", "Test Sus Pads", "Test Power" };
        for (int index = 0; index < 4; ++index)
            require(pack->presets[static_cast<std::size_t>(index)].name == names[static_cast<std::size_t>(index)],
                    "test preset name " + names[static_cast<std::size_t>(index)]);
        require(pack->presets[2].voicing == packs::VoicingStyle::open
                    && pack->presets[3].voicing == packs::VoicingStyle::spread,
                "test pack voicings");
        require(packs::toJson(*pack) == text, "test pack file is in canonical toJson form");

        const auto stock = packs::makeStockPack();
        for (std::size_t a = 0; a < pack->presets.size(); ++a)
        {
            for (std::size_t b = a + 1; b < pack->presets.size(); ++b)
                require(pack->presets[a].triggerMap != pack->presets[b].triggerMap,
                        "test pack maps are distinct from each other");
            for (const auto& stockPreset : stock.presets)
                require(pack->presets[a].triggerMap != stockPreset.triggerMap,
                        "test pack maps are distinct from the stock maps");
            for (const auto code : pack->presets[a].triggerMap)
            {
                const auto* chord = chordengine::core::ChordPresetSystem::chordTemplate(code / 7u);
                require(chord != nullptr && chord->toneCount >= 3,
                        "every test pack code names a real chord template");
            }
            // ...and the engine really resolves every note to a chord, in
            // every scale, through the resolve-by-map entry point.
            for (int scale = 0; scale <= static_cast<int>(chordengine::core::ScaleId::harmonicMinor); ++scale)
                for (int note = chordengine::core::ChordPresetSystem::triggerLow;
                     note <= chordengine::core::ChordPresetSystem::triggerHigh; ++note)
                    require(chordengine::core::ChordPresetSystem::resolve(
                                pack->presets[a].triggerMap.data(), note,
                                static_cast<chordengine::core::ScaleId>(scale), 0).has_value(),
                            pack->presets[a].name + " resolves note " + std::to_string(note));
        }
        casePassed("test pack: valid, canonical, distinct, every note resolves in every scale");
    }

    // =================================================================
    // 2. The validator rejects every malformed pack with a reason.
    // =================================================================
    {
        const auto longName = repeated("x", 41);
        const std::vector<std::pair<const char*, std::string>> bad {
            { "not JSON", "{ nope" },
            { "array root", "[]" },
            { "empty", "" },
            { "wrong format", mutated([](chordengine::platform::Json& r) { r.set("format", "vyre-pack"); }) },
            { "missing format", mutated([](chordengine::platform::Json& r) { r.remove("format"); }) },
            { "formatVersion 2", mutated([](chordengine::platform::Json& r) { r.set("formatVersion", 2); }) },
            { "formatVersion string", mutated([](chordengine::platform::Json& r) { r.set("formatVersion", "1"); }) },
            { "missing slug", mutated([](chordengine::platform::Json& r) { r.remove("slug"); }) },
            { "uppercase slug", mutated([](chordengine::platform::Json& r) { r.set("slug", "Test-Pack"); }) },
            { "leading dash slug", mutated([](chordengine::platform::Json& r) { r.set("slug", "-test"); }) },
            { "one-char slug", mutated([](chordengine::platform::Json& r) { r.set("slug", "a"); }) },
            { "64-char slug", mutated([](chordengine::platform::Json& r) { r.set("slug", repeated("a", 64)); }) },
            { "underscore slug", mutated([](chordengine::platform::Json& r) { r.set("slug", "test_pack"); }) },
            { "slug number", mutated([](chordengine::platform::Json& r) { r.set("slug", 42); }) },
            { "empty pack name", mutated([](chordengine::platform::Json& r) { r.set("name", ""); }) },
            { "bad version", mutated([](chordengine::platform::Json& r) { r.set("version", "1.0"); }) },
            { "version with suffix", mutated([](chordengine::platform::Json& r) { r.set("version", "1.0.0-beta"); }) },
            { "presets not array", mutated([](chordengine::platform::Json& r) { r.set("presets", "x"); }) },
            { "no presets", mutated([](chordengine::platform::Json& r) { r.set("presets", chordengine::platform::Json::array()); }) },
            { "65 presets", mutated([](chordengine::platform::Json& r)
                {
                    auto many = chordengine::platform::Json::array();
                    for (int i = 0; i < 65; ++i)
                    {
                        auto preset = clone(presetAt(r, 0));
                        preset.set("name", "P" + std::to_string(i));
                        many.push(preset);
                    }
                    r.set("presets", many);
                }) },
            { "preset not object", mutated([](chordengine::platform::Json& r) { r.member("presets").mutableItems()[0] = 5; }) },
            { "empty preset name", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).set("name", ""); }) },
            { "41-char preset name", mutated([&longName](chordengine::platform::Json& r) { presetAt(r, 0).set("name", longName); }) },
            { "control char in name", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).set("name", "Bad\tName"); }) },
            { "leading space in name", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).set("name", " Gospel"); }) },
            { "duplicate names (case)", mutated([](chordengine::platform::Json& r) { presetAt(r, 1).set("name", "test GOSPEL"); }) },
            { "unknown voicing", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).set("voicing", "wide"); }) },
            { "missing voicing", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).remove("voicing"); }) },
            { "missing map", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).remove("triggerMap"); }) },
            { "72 codes", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems().pop_back(); }) },
            { "74 codes", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems().push_back(0); }) },
            { "code 70", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems()[10] = chordengine::platform::Json(70); }) },
            { "code -1", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems()[10] = chordengine::platform::Json(-1); }) },
            { "code 3.5", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems()[10] = chordengine::platform::Json(3.5); }) },
            { "code string", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems()[10] = chordengine::platform::Json("3"); }) },
            { "code bool", mutated([](chordengine::platform::Json& r) { presetAt(r, 0).member("triggerMap").mutableItems()[10] = chordengine::platform::Json(true); }) },
            { "over 256 KiB", mutated([](chordengine::platform::Json& r)
                { r.set("padding", repeated("x", 262144)); }) },
        };
        for (const auto& [label, json] : bad)
        {
            std::string error;
            require(!packs::parse(json, error).has_value(), std::string("rejects ") + label);
            require(!error.empty(), std::string("gives a reason for ") + label);
        }

        // Invalid UTF-8 bytes are refused before JSON parsing.
        auto invalidUtf8 = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        invalidUtf8[40] = 0xff;
        std::string error;
        require(!packs::parse(invalidUtf8, error).has_value() && chordengine::text::contains(error, "UTF-8"),
                "rejects invalid UTF-8");

        // Accepted edge cases: unknown keys ignored, author optional, a
        // 63-char slug, a 40-char name, code 69, 64 presets.
        const auto accepted = mutated([](chordengine::platform::Json& r)
        {
            auto& object = r;
            object.set("futureKey", chordengine::platform::Json::object());
            object.remove("author");
            object.set("slug", "a" + repeated("-", 61) + "z");
            presetAt(r, 0).set("name", repeated("y", 40));
            presetAt(r, 0).set("extra", 1);
            presetAt(r, 0).member("triggerMap").mutableItems()[0] = chordengine::platform::Json(69);
            auto many = chordengine::platform::Json::array();
            for (int i = 0; i < 64; ++i)
            {
                auto preset = clone(presetAt(r, 0));
                preset.set("name", "Preset " + std::to_string(i));
                many.push(preset);
            }
            object.set("presets", many);
        });
        const auto edge = packs::parse(accepted, error);
        require(edge.has_value(), "accepts the edge-case pack: " + error);
        require(edge->author == "Music-Prod" && edge->presets.size() == 64
                    && edge->presets[0].triggerMap[0] == 69,
                "edge-case pack fields");
        require(packs::parse(withPreset(std::string("Caf\xc3\xa9 \xe2\x99\xaf"), "open", 3),
                             error).has_value(),
                "non-ASCII printable names are accepted");
        std::cout << "      (" << bad.size() << " malformed documents + invalid UTF-8 rejected)\n";
        casePassed("validator: malformed packs rejected with reasons, edge cases accepted");
    }

    // =================================================================
    // 3. The stock pack IS the built-in presets.
    // =================================================================
    {
        using chordengine::core::ChordPresetSystem;
        const auto stock = packs::makeStockPack();
        require(stock.isStock && stock.slug == "chordengine-stock" && stock.name == "ChordEngine Stock Pack",
                "stock header");
        require(stock.presets.size() == ChordPresetSystem::count(), "stock has the 12 presets");
        for (int index = 0; index < static_cast<int>(ChordPresetSystem::count()); ++index)
        {
            const auto* builtIn = ChordPresetSystem::findByIndex(index);
            const auto& preset = stock.presets[static_cast<std::size_t>(index)];
            require(preset.name == std::string(std::string(builtIn->name)), "stock name " + preset.name);
            require(std::string(packs::toString(preset.voicing)) == std::string(std::string(builtIn->voicingStyle)),
                    "stock voicing " + preset.name);
            for (std::size_t slot = 0; slot < packs::triggerCount; ++slot)
                require(preset.triggerMap[slot] == builtIn->triggerMap[slot], "stock map " + preset.name);
        }

        const auto text = loadText("Resources/Packs/chordengine-stock.json");
        require(text == packs::toJson(stock),
                "Resources/Packs/chordengine-stock.json is byte-identical to toJson(makeStockPack()) "
                "(regenerate with --emit-stock)");
        std::string error;
        const auto parsed = packs::parse(text, error);
        require(parsed.has_value() && *parsed == stock, "the stock JSON parses back to makeStockPack()");
        casePassed("stock pack: JSON == makeStockPack() == ChordPresetSystem maps");
    }

    // =================================================================
    // 4. Versions and URL policy.
    // =================================================================
    {
        require(packs::compareVersions("1.0.0", "1.0.1") < 0 && packs::compareVersions("1.10.0", "1.9.9") > 0
                    && packs::compareVersions("2.0.0", "2.0.0") == 0
                    && packs::compareVersions("junk", "0.0.1") < 0,
                "numeric version comparison");
        const std::vector<std::pair<const char*, bool>> urls {
            { "https://abc.supabase.co/storage/v1/object/sign/x?token=1", true },
            { "HTTPS://abc.supabase.co/x", true },
            { "http://127.0.0.1:8765/files/x.json", true },
            { "http://localhost:8765/files/x.json", true },
            { "http://abc.supabase.co/x", false },
            { "http://127.0.0.1.evil.com/x", false },
            { "https://good.com@evil.com/x", false },
            { "http://localhost@evil.com/", false },
            { "ftp://127.0.0.1/x", false },
            { "file:///etc/passwd", false },
            { "https:///nohost", false },
            { "javascript:alert(1)", false },
            { "", false },
        };
        for (const auto& [url, allowed] : urls)
            require(packs::isAllowedStoreUrl(url) == allowed, std::string("URL policy for ") + url);
        casePassed("versions and download URL policy");
    }

    // =================================================================
    // 5. Library: install, checksum, slug, size, atomic replace, remove.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir / "Packs");
        library.scan();
        require(library.list().size() == 1 && library.list()[0].isStock, "an empty library holds only stock");

        const auto bytes = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        const auto sha = packs::sha256Hex(bytes);
        const auto file = library.fileFor("chordengine-test-pack");

        require(library.install("chordengine-test-pack", bytes, repeated("0", 64))
                    == "checksum mismatch",
                "wrong sha is refused");
        require(library.install("chordengine-test-pack", bytes, "abc") == "missing or malformed checksum",
                "malformed sha is refused");
        require(library.install("other-pack", bytes, sha) == "pack id mismatch", "slug mismatch is refused");
        require(!library.install(packs::stockSlug, bytes, sha).empty(), "the stock slug cannot be installed");
        chordengine::platform::Bytes huge(packs::maxPackBytes + 1, 0);
        require(!library.install("chordengine-test-pack", huge, packs::sha256Hex(huge)).empty(),
                "an oversize pack is refused");
        const auto garbage = bytesOf("{\"format\":\"chordengine-pack\"}");
        require(chordengine::text::startsWith(library.install("chordengine-test-pack", garbage, packs::sha256Hex(garbage)), "invalid pack"),
                "a correctly hashed but invalid pack is refused");
        require(!std::filesystem::exists(file) && library.list().size() == 1, "refusals leave nothing behind");

        require(library.install("chordengine-test-pack", bytes, chordengine::text::toUpper(sha)).empty(),
                "a verified pack installs (digest case-insensitive)");
        require(std::filesystem::exists(file) && library.list().size() == 2 && library.list()[0].isStock
                    && library.find("chordengine-test-pack") != nullptr,
                "installed pack listed after stock");

        // Atomic replace with a newer version.
        auto newer = testPackVar();
        newer.set("version", "1.1.0");
        std::string parseError;
        const auto newerPack = *packs::parse(newer.dump(), parseError);
        const auto newerBytes = bytesOf(packs::toJson(newerPack));
        require(library.install("chordengine-test-pack", newerBytes, packs::sha256Hex(newerBytes)).empty(),
                "replacing with a newer version");
        auto onDisk = fileBytes(file);
        require(onDisk == newerBytes && library.find("chordengine-test-pack")->version == "1.1.0"
                    && library.list().size() == 2,
                "the replace swapped the file and the in-memory pack");
        bool anyTemporary = false;
        for (const auto& entry : std::filesystem::directory_iterator(scratch.dir / "Packs"))
            anyTemporary = anyTemporary || entry.path().extension() == ".tmp";
        require(!anyTemporary,
                "no temporary file survives an install");

        // A failed install never disturbs the installed one.
        require(!library.install("chordengine-test-pack", bytes, repeated("f", 64)).empty(),
                "bad update refused");
        onDisk = fileBytes(file);
        require(onDisk == newerBytes && library.find("chordengine-test-pack")->version == "1.1.0",
                "a refused update keeps the previous pack");

        require(library.usablePacks(false).size() == 1 && library.usablePacks(true).size() == 2,
                "downloaded packs are usable only for members; stock always");

        require(!library.remove(packs::stockSlug).empty(), "stock cannot be removed");
        require(library.remove("chordengine-test-pack").empty() && !std::filesystem::exists(file)
                    && library.list().size() == 1,
                "remove deletes the file and the entry");
        require(!library.remove("chordengine-test-pack").empty(), "removing twice reports it");
        casePassed("library: checksum, slug, size, atomic replace, remove");
    }

    // =================================================================
    // 6. Library scan skips corrupt and misplaced files.
    // =================================================================
    {
        ScratchDir scratch;
        const auto dir = scratch.dir / "Packs";
        std::filesystem::create_directories(dir);
        const auto good = loadText("Resources/Packs/chordengine-test-pack.json");
        chordengine::platform::writeFileAtomically(dir / "chordengine-test-pack.chordpack", good);
        chordengine::platform::writeFileAtomically(dir / "broken.chordpack", "{ not json");
        chordengine::platform::writeFileAtomically(dir / "wrong-name.chordpack", good); // slug mismatch
        chordengine::platform::writeFileAtomically(dir / "chordengine-stock.chordpack", packs::toJson(packs::makeStockPack())); // the stock pack is virtual only
        chordengine::platform::writeFileAtomically(dir / ".install-chordengine-test-pack.tmp", "partial");
        chordengine::platform::writeFileAtomically(dir / "notes.txt", "ignored");

        packs::PackLibrary library(dir);
        library.scan();
        require(library.list().size() == 2 && library.list()[0].isStock
                    && library.list()[1].slug == "chordengine-test-pack",
                "scan loads stock + the one valid pack");
        require(library.scanErrors().size() == 3, "scan reports the three bad .chordpack files");
        require(!std::filesystem::exists(dir / ".install-chordengine-test-pack.tmp"),
                "scan removes a stale temp file");

        packs::PackLibrary missing(scratch.dir / "does-not-exist");
        missing.scan();
        require(missing.list().size() == 1 && missing.scanErrors().empty(),
                "a missing folder is an empty library");
        casePassed("library scan: corrupt, misplaced, stock-on-disk and temp files skipped");
    }

    // =================================================================
    // 7. Store client: catalog.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir / "Packs");
        FakeTransport transport;

        {
            PackStoreClient defaultClient(transport, library);
            require(defaultClient.functionUrl()
                        == std::string(chordengine::licensing::authOrigin) + "functions/v1/chordengine-packs",
                    "the default origin is the licensing origin");
        }
        PackStoreClient noSlash(transport, library, "http://127.0.0.1:9");
        require(noSlash.catalogUrl() == "http://127.0.0.1:9/functions/v1/chordengine-packs?action=catalog",
                "origin override, slash appended");

        PackStoreClient client(transport, library, fakeOrigin);
        const auto sha = repeated("c", 64);
        transport.script(client.catalogUrl(), jsonReply(200, catalogJson("1.0.0", sha)));

        std::optional<PackStoreClient::CatalogResult> result;
        client.fetchCatalog([&result](PackStoreClient::CatalogResult r) { result = std::move(r); });
        require(result && result->ok, "catalog 200 parses");
        require(transport.requests.back().method == "GET"
                    && transport.requests.back().url
                           == std::string(fakeOrigin) + "functions/v1/chordengine-packs?action=catalog",
                "catalog is a GET of ?action=catalog");
        const auto& catalog = result->catalog;
        require(catalog.packs.size() == 2 && catalog.skipped.size() == 1, "a malformed entry is skipped");
        require(catalog.packs[0].slug == packs::stockSlug && catalog.packs[1].slug == "chordengine-test-pack",
                "entries are ordered by sort_order");
        require(catalog.packs[0].isStock && catalog.packs[0].presetCount == 12 && catalog.packs[1].sha256 == sha,
                "entry fields");
        require(catalog.packs[0].artworkUrl.empty(), "an insecure artwork URL is dropped");

        for (const auto& [label, reply] : std::vector<std::pair<const char*, StoreReply>> {
                 { "500", jsonReply(500, "{\"error\":\"server_error\"}") },
                 { "not JSON", jsonReply(200, "<html>") },
                 { "version 2", jsonReply(200, "{\"version\":2,\"packs\":[]}") },
                 { "transport failure", StoreReply {} } })
        {
            transport.script(client.catalogUrl(), reply);
            result.reset();
            client.fetchCatalog([&result](PackStoreClient::CatalogResult r) { result = std::move(r); });
            require(result && !result->ok && !result->error.empty(), std::string("catalog fails on ") + label);
        }
        casePassed("store client: catalog 200 / skipped entry / 500 / malformed / offline");
    }

    // =================================================================
    // 8. Store client: download outcomes.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir / "Packs");
        library.scan();
        FakeTransport transport;
        PackStoreClient client(transport, library, fakeOrigin);
        const std::string slug = "chordengine-test-pack";
        const auto bytes = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        const auto sha = packs::sha256Hex(bytes);

        // 401 / 403 / 404 / 500 / offline: one request, nothing installed.
        transport.script(fakeFunction, jsonReply(401, "{\"error\":\"unauthorized\"}"));
        auto r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::unauthorized, "401 -> unauthorized");
        const auto& post = transport.requests.back();
        const auto sent = chordengine::test::parseJson(post.body);
        require(post.method == "POST" && post.url == fakeFunction && sent["action"].asString() == "download"
                    && sent["token"].asString() == "tok" && sent["slug"].asString() == slug,
                "download POSTs {action, token, slug} to the function");

        transport.script(fakeFunction, jsonReply(403,
            "{\"error\":\"membership_required\",\"unlock_url\":\"https://music-prod.com/plus\","
            "\"unlock_message\":\"Join Music-Prod+ to download preset packs\"}"));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::membershipRequired && r->unlockUrl == "https://music-prod.com/plus"
                    && r->message == "Join Music-Prod+ to download preset packs",
                "403 -> membershipRequired with the server's unlock link and message");

        transport.script(fakeFunction, jsonReply(403, "{\"error\":\"membership_required\",\"unlock_url\":\"javascript:x\"}"));
        r = runDownload(client, "tok", slug);
        require(r && r->unlockUrl == "https://music-prod.com/plus" && r->message == "Music-Prod+ required",
                "403 with an unsafe unlock link falls back to the Music-Prod+ page");

        transport.script(fakeFunction, jsonReply(404, "{\"error\":\"not_found\"}"));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::notFound, "404 -> notFound");

        transport.script(fakeFunction, jsonReply(500, "{}"));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::invalidResponse, "500 -> invalidResponse");

        r = runDownload(client, "tok", slug); // nothing scripted: offline
        require(r && r->outcome == Outcome::transportFailed, "offline POST -> transportFailed");
        require(library.find(slug) == nullptr, "no failure installed anything");

        // 200 but malformed grants.
        transport.script(fakeFunction, jsonReply(200, grantJson("someone-else", sha, fakeFileUrl)));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::invalidResponse, "a grant for another slug is refused");
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, "nothex", fakeFileUrl)));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::invalidResponse, "a grant without a valid sha256 is refused");

        // Non-https download URL: refused BEFORE any request is made to it.
        const auto requestsBefore = transport.requests.size();
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, "http://files.example.test/p.json")));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::insecureUrl, "http download URL -> insecureUrl");
        require(transport.requests.size() == requestsBefore + 1, "the insecure URL is never fetched");

        // SHA mismatch: the file is fetched, hashed, and refused.
        auto tampered = bytes;
        tampered[20] ^= 0x01;
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, fakeFileUrl)));
        transport.script(fakeFileUrl, bytesReply(200, tampered));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::installFailed && chordengine::text::contains(r->message, "checksum mismatch"),
                "tampered bytes -> installFailed (checksum mismatch)");
        require(library.find(slug) == nullptr, "tampered bytes install nothing");

        // Oversize body.
        chordengine::platform::Bytes huge(packs::maxPackBytes + 10, 0);
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, fakeFileUrl)));
        transport.script(fakeFileUrl, bytesReply(200, huge));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::tooLarge, "oversize pack -> tooLarge");
        require(transport.requests.back().maxBytes == packs::maxPackBytes, "the GET is bounded to 256 KiB");

        // Expired signed URL / offline file fetch.
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, fakeFileUrl)));
        transport.script(fakeFileUrl, jsonReply(400, "{\"error\":\"expired\"}"));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::notFound, "expired signed URL -> notFound");
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, fakeFileUrl)));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::transportFailed, "offline file fetch -> transportFailed");

        // Success, including a local http URL.
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, "http://127.0.0.1:8765/files/p.json")));
        transport.script("http://127.0.0.1:8765/files/p.json", bytesReply(200, bytes));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::installed && r->version == "1.0.0"
                    && r->message == "Installed ChordEngine Test Pack",
                "200 + matching bytes -> installed");
        require(library.find(slug) != nullptr && std::filesystem::exists(library.fileFor(slug)), "the pack is on disk");

        // The stock pack is never fetched.
        const auto beforeStock = transport.requests.size();
        r = runDownload(client, "tok", packs::stockSlug);
        require(r && r->outcome == Outcome::installed && transport.requests.size() == beforeStock,
                "downloading the stock pack is a local no-op");

        // A reply that arrives after the client is gone is dropped.
        transport.deferred = true;
        bool called = false;
        {
            PackStoreClient shortLived(transport, library, fakeOrigin);
            transport.script(fakeFunction, jsonReply(401, "{}"));
            shortLived.download("tok", slug, [&called](PackStoreClient::DownloadResult) { called = true; });
        }
        for (auto& deliver : transport.pending)
            deliver();
        require(!called, "no callback after the client is destroyed");
        casePassed("store client: 200 / 401 / 403 / 404 / 500 / offline / insecure URL / sha mismatch / oversize");
    }

    // =================================================================
    // 9. Store view: the status merge table.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir / "Packs");
        library.scan();
        const auto dot = std::string(" \xc2\xb7 ");

        std::string error;
        auto catalog = *packs::parseCatalog(catalogJson("1.0.0", repeated("c", 64)), error);

        struct Row
        {
            const char* label;
            packs::StoreAccess access;
            bool installed;
            const char* catalogVersion;
            PackStatus expected;
            std::string text;
            bool usable;
            bool canDownload;
        };
        const std::vector<Row> rows {
            { "signed out, not installed", { false, false }, false, "1.0.0", PackStatus::signInRequired, "Sign in to download", false, false },
            { "signed out, stale subscribed flag", { false, true }, false, "1.0.0", PackStatus::signInRequired, "Sign in to download", false, false },
            { "signed in non-member", { true, false }, false, "1.0.0", PackStatus::requiresMembership, "Music-Prod+ required", false, false },
            { "member, not installed", { true, true }, false, "1.0.0", PackStatus::available, "Click to download", false, true },
            { "member, installed", { true, true }, true, "1.0.0", PackStatus::installed, "Installed" + dot + "4 presets", true, false },
            { "member, update", { true, true }, true, "1.2.0", PackStatus::updateAvailable, "Update available" + dot + "1.2.0", true, true },
            { "lapsed member, installed", { true, false }, true, "1.0.0", PackStatus::requiresMembership, "Music-Prod+ required", false, false },
            { "signed out, installed", { false, false }, true, "1.0.0", PackStatus::signInRequired, "Sign in to download", false, false },
        };

        const auto bytes = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        for (const auto& row : rows)
        {
            if (row.installed && library.find("chordengine-test-pack") == nullptr)
                library.install("chordengine-test-pack", bytes, packs::sha256Hex(bytes));
            if (!row.installed && library.find("chordengine-test-pack") != nullptr)
                library.remove("chordengine-test-pack");
            catalog.packs[1].version = row.catalogVersion;

            const auto listings = packs::mergeListings(&catalog, library, row.access);
            require(listings.size() == 2, std::string(row.label) + ": stock + test pack");
            require(listings[0].status == PackStatus::builtIn && listings[0].usable
                        && listings[0].statusText == "Built in" + dot + "12 presets",
                    std::string(row.label) + ": the stock pack is always built in and usable");
            const auto& test = listings[1];
            require(test.status == row.expected, std::string(row.label) + ": status");
            require(test.statusText == row.text, std::string(row.label) + ": text was \"" + test.statusText + "\"");
            require(test.usable == row.usable && test.canDownload == row.canDownload,
                    std::string(row.label) + ": usable/canDownload");
        }

        // Offline (no catalog): installed packs still listed; withdrawn packs too.
        const auto offline = packs::mergeListings(nullptr, library, { true, true });
        require(offline.size() == 2 && offline[1].status == PackStatus::installed && !offline[1].inCatalog,
                "offline: installed packs stay listed from disk");
        catalog.packs.erase(catalog.packs.begin() + 1);
        const auto withdrawn = packs::mergeListings(&catalog, library, { true, false });
        require(withdrawn.size() == 2 && withdrawn[1].status == PackStatus::requiresMembership
                    && !withdrawn[1].usable,
                "a pack the catalog withdrew stays listed and gated");
        require(packs::statusText(PackStatus::installed, 1) == "Installed" + dot + "1 preset", "singular text");
        casePassed("store view: status merge table (8 rows + offline + withdrawn)");
    }

    constexpr int expectedCaseCount = 9;
    require(casesPassed == expectedCaseCount,
            "every case must run: expected " + std::to_string(expectedCaseCount) + ", executed "
                + std::to_string(casesPassed));
    std::cout << "ChordEnginePackTests: " << casesPassed << "/" << expectedCaseCount << " cases passed\n";
    return EXIT_SUCCESS;
}
