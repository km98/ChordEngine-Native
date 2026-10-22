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

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

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

[[noreturn]] void fail(const juce::String& message)
{
    std::cout << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

void casePassed(const char* name)
{
    ++casesPassed;
    std::cout << "PASS: " << name << '\n';
}

juce::File repoFile(const juce::String& relative)
{
    return juce::File(CHORDENGINE_SOURCE_DIR).getChildFile(relative);
}

juce::String loadText(const juce::String& relative)
{
    const auto file = repoFile(relative);
    require(file.existsAsFile(), "missing repository file " + relative);
    return file.loadFileAsString();
}

juce::MemoryBlock bytesOf(const juce::String& text)
{
    return { text.toRawUTF8(), text.getNumBytesAsUTF8() };
}

void pump(const std::function<bool()>& done, int timeoutMs = 5000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (!done() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

// A scratch directory, removed on scope exit.
struct ScratchDir
{
    ScratchDir()
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getNonexistentChildFile("ChordEnginePackTests", "", false))
    {
        dir.createDirectory();
    }
    ~ScratchDir() { dir.deleteRecursively(); }
    juce::File dir;
};

// ------------------------------------------------------- pack mutations
juce::var testPackVar()
{
    juce::var root;
    require(juce::JSON::parse(loadText("Resources/Packs/chordengine-test-pack.json"), root).wasOk(),
            "test pack JSON parses");
    return root;
}

// A deep copy, so every mutation starts from the pristine document.
juce::var clone(const juce::var& value)
{
    juce::var copy;
    juce::JSON::parse(juce::JSON::toString(value), copy);
    return copy;
}

juce::var& presetAt(juce::var& root, int index)
{
    return root["presets"].getArray()->getReference(index);
}

juce::String mutated(const std::function<void(juce::var&)>& change)
{
    auto root = clone(testPackVar());
    change(root);
    return juce::JSON::toString(root);
}

juce::String withPreset(const juce::String& name, const char* voicing, std::uint8_t code)
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
        juce::String method, url, body;
        std::size_t maxBytes = 0;
    };

    void get(const juce::String& url, std::size_t maxBytes, Callback callback) override
    {
        requests.push_back({ "GET", url, {}, maxBytes });
        respond(url, maxBytes, std::move(callback));
    }

    void postJson(const juce::String& url, const juce::String& json, std::size_t maxBytes,
                  Callback callback) override
    {
        requests.push_back({ "POST", url, json, maxBytes });
        respond(url, maxBytes, std::move(callback));
    }

    void script(const juce::String& url, StoreReply reply) { replies[url].push_back(std::move(reply)); }

    std::vector<Request> requests;
    std::map<juce::String, std::deque<StoreReply>> replies;
    std::vector<std::function<void()>> pending;
    bool deferred = false;

private:
    void respond(const juce::String& url, std::size_t maxBytes, Callback callback)
    {
        StoreReply reply; // unscripted URL: transport failure
        auto& queue = replies[url];
        if (!queue.empty())
        {
            reply = queue.front();
            queue.pop_front();
        }
        // Emulate the production transport's bounded read.
        if (reply.body.getSize() > maxBytes)
        {
            reply.body.setSize(maxBytes + 1);
            reply.truncated = true;
        }
        auto deliver = [callback, reply] { callback(reply); };
        if (deferred)
            pending.push_back(std::move(deliver));
        else
            deliver();
    }
};

StoreReply jsonReply(int status, const juce::String& json)
{
    StoreReply reply;
    reply.status = status;
    reply.transportOk = true;
    reply.body = bytesOf(json);
    return reply;
}

StoreReply bytesReply(int status, juce::MemoryBlock body)
{
    StoreReply reply;
    reply.status = status;
    reply.transportOk = true;
    reply.body = std::move(body);
    return reply;
}

juce::String grantJson(const juce::String& slug, const juce::String& sha, const juce::String& url,
                       const juce::String& version = "1.0.0")
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("ok", true);
    body->setProperty("slug", slug);
    body->setProperty("version", version);
    body->setProperty("sha256", sha);
    body->setProperty("download_url", url);
    return juce::JSON::toString(juce::var(body.get()));
}

constexpr const char* fakeOrigin = "https://store.example.test/";
constexpr const char* fakeFunction = "https://store.example.test/functions/v1/chordengine-packs";
constexpr const char* fakeFileUrl = "https://files.example.test/signed/chordengine-test-pack.json?token=x";

std::optional<PackStoreClient::DownloadResult> runDownload(PackStoreClient& client,
                                                           const juce::String& token,
                                                           const juce::String& slug)
{
    std::optional<PackStoreClient::DownloadResult> result;
    client.download(token, slug, [&result](PackStoreClient::DownloadResult r) { result = std::move(r); });
    pump([&result] { return result.has_value(); });
    return result;
}

juce::String catalogJson(const juce::String& testVersion, const juce::String& testSha)
{
    return "{\"version\":1,\"generated_at\":\"2026-10-18T00:00:00Z\",\"packs\":["
           "{\"slug\":\"chordengine-test-pack\",\"name\":\"ChordEngine Test Pack\",\"description\":\"t\","
           "\"version\":\"" + testVersion + "\",\"preset_count\":4,\"is_stock\":false,\"size_bytes\":2000,"
           "\"sha256\":\"" + testSha + "\",\"sort_order\":20},"
           "{\"slug\":\"chordengine-stock\",\"name\":\"ChordEngine Stock Pack\",\"description\":\"s\","
           "\"version\":\"1.0.0\",\"preset_count\":12,\"is_stock\":true,\"size_bytes\":9000,"
           "\"sha256\":\"" + juce::String::repeatedString("a", 64) + "\",\"sort_order\":10,"
           "\"artwork_url\":\"http://evil.example/x.png\"},"
           "{\"slug\":\"Bad Slug\",\"name\":\"x\",\"version\":\"1.0.0\",\"preset_count\":1,"
           "\"size_bytes\":1,\"sha256\":\"" + juce::String::repeatedString("b", 64) + "\"}]}";
}

// =================================================================== e2e
int runEndToEnd(const juce::String& origin)
{
    const juce::ScopedJuceInitialiser_GUI guiInitialiser;
    require(origin.startsWith("http://127.0.0.1:") || origin.startsWith("http://localhost:"),
            "--e2e only accepts a local origin");

    ScratchDir scratch;
    packs::PackLibrary library(scratch.dir.getChildFile("Packs"));
    library.scan();
    packs::HttpPackStoreTransport transport;
    PackStoreClient client(transport, library, origin);

    std::optional<PackStoreClient::CatalogResult> catalog;
    client.fetchCatalog([&catalog](PackStoreClient::CatalogResult r) { catalog = std::move(r); });
    pump([&catalog] { return catalog.has_value(); }, 15000);
    require(catalog && catalog->ok, "e2e catalog: " + (catalog ? catalog->error : juce::String("timeout")));
    require(catalog->catalog.find(packs::stockSlug) != nullptr
                && catalog->catalog.find("chordengine-test-pack") != nullptr,
            "e2e catalog lists the stock and the test pack");
    casePassed("e2e: catalog fetched over HTTP");

    const auto before = packs::mergeListings(&catalog->catalog, library, { true, true });
    require(before.size() == 2 && before[0].status == PackStatus::builtIn
                && before[1].status == PackStatus::available,
            "e2e: before download the test pack is 'Click to download'");

    auto denied = runDownload(client, "nonmember-token", "chordengine-test-pack");
    require(denied && denied->outcome == Outcome::membershipRequired && denied->unlockUrl.isNotEmpty(),
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
            "e2e member-token installs the pack: " + (granted ? granted->message : juce::String("timeout")));
    const auto installedFile = library.fileFor("chordengine-test-pack");
    juce::MemoryBlock onDisk;
    installedFile.loadFileAsData(onDisk);
    require(packs::sha256Hex(onDisk) == catalog->catalog.find("chordengine-test-pack")->sha256,
            "e2e: the installed file hashes to the catalog digest");
    packs::PackLibrary reloaded(scratch.dir.getChildFile("Packs"));
    reloaded.scan();
    require(reloaded.find("chordengine-test-pack") != nullptr
                && reloaded.find("chordengine-test-pack")->presets.size() == 4,
            "e2e: a fresh scan finds the installed pack");
    const auto after = packs::mergeListings(&catalog->catalog, library, { true, true });
    require(after[1].status == PackStatus::installed
                && after[1].statusText == juce::String(juce::CharPointer_UTF8("Installed \xc2\xb7 4 presets")),
            "e2e: after download the test pack is 'Installed (dot) 4 presets'");
    casePassed("e2e: member-token -> catalog -> download -> install");

    std::cout << "ChordEnginePackTests --e2e: " << casesPassed << " end-to-end cases passed against "
              << origin << '\n';
    return EXIT_SUCCESS;
}
} // namespace

int main(int argc, char** argv)
{
    const juce::StringArray args(argv + 1, argc - 1);
    if (args.size() == 2 && args[0] == "--emit-stock")
    {
        const auto ok = juce::File::getCurrentWorkingDirectory().getChildFile(args[1])
                            .replaceWithText(packs::toJson(packs::makeStockPack()), false, false, "\n");
        std::cout << (ok ? "wrote " : "could not write ") << args[1] << '\n';
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (args.size() == 2 && args[0] == "--e2e")
        return runEndToEnd(args[1]);

    const juce::ScopedJuceInitialiser_GUI guiInitialiser;

    // =================================================================
    // 1. The shipped test pack parses, is canonical and is musically
    //    distinct.
    // =================================================================
    {
        const auto text = loadText("Resources/Packs/chordengine-test-pack.json");
        juce::String error;
        const auto pack = packs::parse(text, error);
        require(pack.has_value(), "test pack valid: " + error);
        require(error.isEmpty(), "a successful parse clears the error");
        require(pack->slug == "chordengine-test-pack" && pack->name == "ChordEngine Test Pack"
                    && pack->version == "1.0.0" && pack->author == "Music-Prod" && !pack->isStock,
                "test pack header");
        require(pack->presets.size() == 4, "test pack has four presets");
        const juce::StringArray names { "Test Gospel", "Test Jazz Ninths", "Test Sus Pads", "Test Power" };
        for (int index = 0; index < 4; ++index)
            require(pack->presets[static_cast<std::size_t>(index)].name == names[index],
                    "test preset name " + names[index]);
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
                            pack->presets[a].name + " resolves note " + juce::String(note));
        }
        casePassed("test pack: valid, canonical, distinct, every note resolves in every scale");
    }

    // =================================================================
    // 2. The validator rejects every malformed pack with a reason.
    // =================================================================
    {
        const auto longName = juce::String::repeatedString("x", 41);
        const std::vector<std::pair<const char*, juce::String>> bad {
            { "not JSON", "{ nope" },
            { "array root", "[]" },
            { "empty", "" },
            { "wrong format", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("format", "vyre-pack"); }) },
            { "missing format", mutated([](juce::var& r) { r.getDynamicObject()->removeProperty("format"); }) },
            { "formatVersion 2", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("formatVersion", 2); }) },
            { "formatVersion string", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("formatVersion", "1"); }) },
            { "missing slug", mutated([](juce::var& r) { r.getDynamicObject()->removeProperty("slug"); }) },
            { "uppercase slug", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("slug", "Test-Pack"); }) },
            { "leading dash slug", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("slug", "-test"); }) },
            { "one-char slug", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("slug", "a"); }) },
            { "64-char slug", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("slug", juce::String::repeatedString("a", 64)); }) },
            { "underscore slug", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("slug", "test_pack"); }) },
            { "slug number", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("slug", 42); }) },
            { "empty pack name", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("name", ""); }) },
            { "bad version", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("version", "1.0"); }) },
            { "version with suffix", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("version", "1.0.0-beta"); }) },
            { "presets not array", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("presets", "x"); }) },
            { "no presets", mutated([](juce::var& r) { r.getDynamicObject()->setProperty("presets", juce::Array<juce::var>()); }) },
            { "65 presets", mutated([](juce::var& r)
                {
                    juce::Array<juce::var> many;
                    for (int i = 0; i < 65; ++i)
                    {
                        auto preset = clone(presetAt(r, 0));
                        preset.getDynamicObject()->setProperty("name", "P" + juce::String(i));
                        many.add(preset);
                    }
                    r.getDynamicObject()->setProperty("presets", many);
                }) },
            { "preset not object", mutated([](juce::var& r) { r["presets"].getArray()->set(0, 5); }) },
            { "empty preset name", mutated([](juce::var& r) { presetAt(r, 0).getDynamicObject()->setProperty("name", ""); }) },
            { "41-char preset name", mutated([&longName](juce::var& r) { presetAt(r, 0).getDynamicObject()->setProperty("name", longName); }) },
            { "control char in name", mutated([](juce::var& r) { presetAt(r, 0).getDynamicObject()->setProperty("name", "Bad\tName"); }) },
            { "leading space in name", mutated([](juce::var& r) { presetAt(r, 0).getDynamicObject()->setProperty("name", " Gospel"); }) },
            { "duplicate names (case)", mutated([](juce::var& r) { presetAt(r, 1).getDynamicObject()->setProperty("name", "test GOSPEL"); }) },
            { "unknown voicing", mutated([](juce::var& r) { presetAt(r, 0).getDynamicObject()->setProperty("voicing", "wide"); }) },
            { "missing voicing", mutated([](juce::var& r) { presetAt(r, 0).getDynamicObject()->removeProperty("voicing"); }) },
            { "missing map", mutated([](juce::var& r) { presetAt(r, 0).getDynamicObject()->removeProperty("triggerMap"); }) },
            { "72 codes", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->removeLast(); }) },
            { "74 codes", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->add(0); }) },
            { "code 70", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->set(10, 70); }) },
            { "code -1", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->set(10, -1); }) },
            { "code 3.5", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->set(10, 3.5); }) },
            { "code string", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->set(10, "3"); }) },
            { "code bool", mutated([](juce::var& r) { presetAt(r, 0)["triggerMap"].getArray()->set(10, true); }) },
            { "over 256 KiB", mutated([](juce::var& r)
                { r.getDynamicObject()->setProperty("padding", juce::String::repeatedString("x", 262144)); }) },
        };
        for (const auto& [label, json] : bad)
        {
            juce::String error;
            require(!packs::parse(json, error).has_value(), juce::String("rejects ") + label);
            require(error.isNotEmpty(), juce::String("gives a reason for ") + label);
        }

        // Invalid UTF-8 bytes are refused before JSON parsing.
        juce::MemoryBlock invalidUtf8 = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        static_cast<char*>(invalidUtf8.getData())[40] = static_cast<char>(0xff);
        juce::String error;
        require(!packs::parse(invalidUtf8, error).has_value() && error.contains("UTF-8"),
                "rejects invalid UTF-8");

        // Accepted edge cases: unknown keys ignored, author optional, a
        // 63-char slug, a 40-char name, code 69, 64 presets.
        const auto accepted = mutated([](juce::var& r)
        {
            auto* object = r.getDynamicObject();
            object->setProperty("futureKey", juce::var(new juce::DynamicObject()));
            object->removeProperty("author");
            object->setProperty("slug", "a" + juce::String::repeatedString("-", 61) + "z");
            presetAt(r, 0).getDynamicObject()->setProperty("name", juce::String::repeatedString("y", 40));
            presetAt(r, 0).getDynamicObject()->setProperty("extra", 1);
            presetAt(r, 0)["triggerMap"].getArray()->set(0, 69);
            juce::Array<juce::var> many;
            for (int i = 0; i < 64; ++i)
            {
                auto preset = clone(presetAt(r, 0));
                preset.getDynamicObject()->setProperty("name", "Preset " + juce::String(i));
                many.add(preset);
            }
            object->setProperty("presets", many);
        });
        const auto edge = packs::parse(accepted, error);
        require(edge.has_value(), "accepts the edge-case pack: " + error);
        require(edge->author == "Music-Prod" && edge->presets.size() == 64
                    && edge->presets[0].triggerMap[0] == 69,
                "edge-case pack fields");
        require(packs::parse(withPreset(juce::String(juce::CharPointer_UTF8("Caf\xc3\xa9 \xe2\x99\xaf")), "open", 3),
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
            require(preset.name == juce::String(std::string(builtIn->name)), "stock name " + preset.name);
            require(juce::String(packs::toString(preset.voicing)) == juce::String(std::string(builtIn->voicingStyle)),
                    "stock voicing " + preset.name);
            for (std::size_t slot = 0; slot < packs::triggerCount; ++slot)
                require(preset.triggerMap[slot] == builtIn->triggerMap[slot], "stock map " + preset.name);
        }

        const auto text = loadText("Resources/Packs/chordengine-stock.json");
        require(text == packs::toJson(stock),
                "Resources/Packs/chordengine-stock.json is byte-identical to toJson(makeStockPack()) "
                "(regenerate with --emit-stock)");
        juce::String error;
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
            require(packs::isAllowedStoreUrl(url) == allowed, juce::String("URL policy for ") + url);
        casePassed("versions and download URL policy");
    }

    // =================================================================
    // 5. Library: install, checksum, slug, size, atomic replace, remove.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir.getChildFile("Packs"));
        library.scan();
        require(library.list().size() == 1 && library.list()[0].isStock, "an empty library holds only stock");

        const auto bytes = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        const auto sha = packs::sha256Hex(bytes);
        const auto file = library.fileFor("chordengine-test-pack");

        require(library.install("chordengine-test-pack", bytes, juce::String::repeatedString("0", 64))
                    == "checksum mismatch",
                "wrong sha is refused");
        require(library.install("chordengine-test-pack", bytes, "abc") == "missing or malformed checksum",
                "malformed sha is refused");
        require(library.install("other-pack", bytes, sha) == "pack id mismatch", "slug mismatch is refused");
        require(library.install(packs::stockSlug, bytes, sha).isNotEmpty(), "the stock slug cannot be installed");
        juce::MemoryBlock huge(packs::maxPackBytes + 1, true);
        require(library.install("chordengine-test-pack", huge, packs::sha256Hex(huge)).isNotEmpty(),
                "an oversize pack is refused");
        const auto garbage = bytesOf("{\"format\":\"chordengine-pack\"}");
        require(library.install("chordengine-test-pack", garbage, packs::sha256Hex(garbage)).startsWith("invalid pack"),
                "a correctly hashed but invalid pack is refused");
        require(!file.exists() && library.list().size() == 1, "refusals leave nothing behind");

        require(library.install("chordengine-test-pack", bytes, sha.toUpperCase()).isEmpty(),
                "a verified pack installs (digest case-insensitive)");
        require(file.existsAsFile() && library.list().size() == 2 && library.list()[0].isStock
                    && library.find("chordengine-test-pack") != nullptr,
                "installed pack listed after stock");

        // Atomic replace with a newer version.
        auto newer = testPackVar();
        newer.getDynamicObject()->setProperty("version", "1.1.0");
        juce::String parseError;
        const auto newerPack = *packs::parse(juce::JSON::toString(newer), parseError);
        const auto newerBytes = bytesOf(packs::toJson(newerPack));
        require(library.install("chordengine-test-pack", newerBytes, packs::sha256Hex(newerBytes)).isEmpty(),
                "replacing with a newer version");
        juce::MemoryBlock onDisk;
        file.loadFileAsData(onDisk);
        require(onDisk == newerBytes && library.find("chordengine-test-pack")->version == "1.1.0"
                    && library.list().size() == 2,
                "the replace swapped the file and the in-memory pack");
        require(scratch.dir.getChildFile("Packs").findChildFiles(juce::File::findFiles, false, "*.tmp").isEmpty(),
                "no temporary file survives an install");

        // A failed install never disturbs the installed one.
        require(library.install("chordengine-test-pack", bytes, juce::String::repeatedString("f", 64)).isNotEmpty(),
                "bad update refused");
        onDisk.reset(); // loadFileAsData appends
        file.loadFileAsData(onDisk);
        require(onDisk == newerBytes && library.find("chordengine-test-pack")->version == "1.1.0",
                "a refused update keeps the previous pack");

        require(library.usablePacks(false).size() == 1 && library.usablePacks(true).size() == 2,
                "downloaded packs are usable only for members; stock always");

        require(library.remove(packs::stockSlug).isNotEmpty(), "stock cannot be removed");
        require(library.remove("chordengine-test-pack").isEmpty() && !file.exists()
                    && library.list().size() == 1,
                "remove deletes the file and the entry");
        require(library.remove("chordengine-test-pack").isNotEmpty(), "removing twice reports it");
        casePassed("library: checksum, slug, size, atomic replace, remove");
    }

    // =================================================================
    // 6. Library scan skips corrupt and misplaced files.
    // =================================================================
    {
        ScratchDir scratch;
        const auto dir = scratch.dir.getChildFile("Packs");
        dir.createDirectory();
        const auto good = loadText("Resources/Packs/chordengine-test-pack.json");
        dir.getChildFile("chordengine-test-pack.chordpack").replaceWithText(good);
        dir.getChildFile("broken.chordpack").replaceWithText("{ not json");
        dir.getChildFile("wrong-name.chordpack").replaceWithText(good); // slug mismatch
        dir.getChildFile("chordengine-stock.chordpack")
            .replaceWithText(packs::toJson(packs::makeStockPack())); // the stock pack is virtual only
        dir.getChildFile(".install-chordengine-test-pack.tmp").replaceWithText("partial");
        dir.getChildFile("notes.txt").replaceWithText("ignored");

        packs::PackLibrary library(dir);
        library.scan();
        require(library.list().size() == 2 && library.list()[0].isStock
                    && library.list()[1].slug == "chordengine-test-pack",
                "scan loads stock + the one valid pack");
        require(library.scanErrors().size() == 3, "scan reports the three bad .chordpack files");
        require(!dir.getChildFile(".install-chordengine-test-pack.tmp").exists(),
                "scan removes a stale temp file");

        packs::PackLibrary missing(scratch.dir.getChildFile("does-not-exist"));
        missing.scan();
        require(missing.list().size() == 1 && missing.scanErrors().isEmpty(),
                "a missing folder is an empty library");
        casePassed("library scan: corrupt, misplaced, stock-on-disk and temp files skipped");
    }

    // =================================================================
    // 7. Store client: catalog.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir.getChildFile("Packs"));
        FakeTransport transport;

        {
            PackStoreClient defaultClient(transport, library);
            require(defaultClient.functionUrl()
                        == juce::String(chordengine::licensing::authOrigin) + "functions/v1/chordengine-packs",
                    "the default origin is the licensing origin");
        }
        PackStoreClient noSlash(transport, library, "http://127.0.0.1:9");
        require(noSlash.catalogUrl() == "http://127.0.0.1:9/functions/v1/chordengine-packs?action=catalog",
                "origin override, slash appended");

        PackStoreClient client(transport, library, fakeOrigin);
        const auto sha = juce::String::repeatedString("c", 64);
        transport.script(client.catalogUrl(), jsonReply(200, catalogJson("1.0.0", sha)));

        std::optional<PackStoreClient::CatalogResult> result;
        client.fetchCatalog([&result](PackStoreClient::CatalogResult r) { result = std::move(r); });
        require(result && result->ok, "catalog 200 parses");
        require(transport.requests.back().method == "GET"
                    && transport.requests.back().url
                           == juce::String(fakeOrigin) + "functions/v1/chordengine-packs?action=catalog",
                "catalog is a GET of ?action=catalog");
        const auto& catalog = result->catalog;
        require(catalog.packs.size() == 2 && catalog.skipped.size() == 1, "a malformed entry is skipped");
        require(catalog.packs[0].slug == packs::stockSlug && catalog.packs[1].slug == "chordengine-test-pack",
                "entries are ordered by sort_order");
        require(catalog.packs[0].isStock && catalog.packs[0].presetCount == 12 && catalog.packs[1].sha256 == sha,
                "entry fields");
        require(catalog.packs[0].artworkUrl.isEmpty(), "an insecure artwork URL is dropped");

        for (const auto& [label, reply] : std::vector<std::pair<const char*, StoreReply>> {
                 { "500", jsonReply(500, "{\"error\":\"server_error\"}") },
                 { "not JSON", jsonReply(200, "<html>") },
                 { "version 2", jsonReply(200, "{\"version\":2,\"packs\":[]}") },
                 { "transport failure", StoreReply {} } })
        {
            transport.script(client.catalogUrl(), reply);
            result.reset();
            client.fetchCatalog([&result](PackStoreClient::CatalogResult r) { result = std::move(r); });
            require(result && !result->ok && result->error.isNotEmpty(), juce::String("catalog fails on ") + label);
        }
        casePassed("store client: catalog 200 / skipped entry / 500 / malformed / offline");
    }

    // =================================================================
    // 8. Store client: download outcomes.
    // =================================================================
    {
        ScratchDir scratch;
        packs::PackLibrary library(scratch.dir.getChildFile("Packs"));
        library.scan();
        FakeTransport transport;
        PackStoreClient client(transport, library, fakeOrigin);
        const juce::String slug = "chordengine-test-pack";
        const auto bytes = bytesOf(loadText("Resources/Packs/chordengine-test-pack.json"));
        const auto sha = packs::sha256Hex(bytes);

        // 401 / 403 / 404 / 500 / offline: one request, nothing installed.
        transport.script(fakeFunction, jsonReply(401, "{\"error\":\"unauthorized\"}"));
        auto r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::unauthorized, "401 -> unauthorized");
        const auto& post = transport.requests.back();
        juce::var sent;
        juce::JSON::parse(post.body, sent);
        require(post.method == "POST" && post.url == fakeFunction && sent["action"] == "download"
                    && sent["token"] == "tok" && sent["slug"] == slug,
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
        static_cast<char*>(tampered.getData())[20] ^= 0x01;
        transport.script(fakeFunction, jsonReply(200, grantJson(slug, sha, fakeFileUrl)));
        transport.script(fakeFileUrl, bytesReply(200, tampered));
        r = runDownload(client, "tok", slug);
        require(r && r->outcome == Outcome::installFailed && r->message.contains("checksum mismatch"),
                "tampered bytes -> installFailed (checksum mismatch)");
        require(library.find(slug) == nullptr, "tampered bytes install nothing");

        // Oversize body.
        juce::MemoryBlock huge(packs::maxPackBytes + 10, true);
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
        require(library.find(slug) != nullptr && library.fileFor(slug).existsAsFile(), "the pack is on disk");

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
        packs::PackLibrary library(scratch.dir.getChildFile("Packs"));
        library.scan();
        const auto dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));

        juce::String error;
        auto catalog = *packs::parseCatalog(catalogJson("1.0.0", juce::String::repeatedString("c", 64)), error);

        struct Row
        {
            const char* label;
            packs::StoreAccess access;
            bool installed;
            const char* catalogVersion;
            PackStatus expected;
            juce::String text;
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
            require(listings.size() == 2, juce::String(row.label) + ": stock + test pack");
            require(listings[0].status == PackStatus::builtIn && listings[0].usable
                        && listings[0].statusText == "Built in" + dot + "12 presets",
                    juce::String(row.label) + ": the stock pack is always built in and usable");
            const auto& test = listings[1];
            require(test.status == row.expected, juce::String(row.label) + ": status");
            require(test.statusText == row.text, juce::String(row.label) + ": text was \"" + test.statusText + "\"");
            require(test.usable == row.usable && test.canDownload == row.canDownload,
                    juce::String(row.label) + ": usable/canDownload");
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
            "every case must run: expected " + juce::String(expectedCaseCount) + ", executed "
                + juce::String(casesPassed));
    std::cout << "ChordEnginePackTests: " << casesPassed << "/" << expectedCaseCount << " cases passed\n";
    return EXIT_SUCCESS;
}
