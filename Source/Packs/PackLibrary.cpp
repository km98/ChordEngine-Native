#include "PackLibrary.h"

#include <algorithm>

namespace chordengine::packs
{
namespace
{
constexpr const char* tempPrefix = ".install-";
constexpr const char* tempSuffix = ".tmp";
} // namespace

PackLibrary::PackLibrary(juce::File packsDirectory)
    : directory_(std::move(packsDirectory))
{
    packs_.push_back(makeStockPack());
}

juce::File PackLibrary::defaultPacksDirectory()
{
    // Same resolution as the editor's licensing storage: on macOS the JUCE
    // user-application-data folder is ~/Library, and Music-Prod state lives in
    // its Application Support folder.
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    if (appData.getFileName() == "Library")
        appData = appData.getChildFile("Application Support");
    return appData.getChildFile("Music-Prod").getChildFile("ChordEngine").getChildFile("Packs");
}

juce::File PackLibrary::fileFor(const juce::String& slug) const
{
    return directory_.getChildFile(slug + fileExtension);
}

void PackLibrary::scan()
{
    packs_.clear();
    scanErrors_.clear();
    packs_.push_back(makeStockPack());

    if (!directory_.isDirectory())
        return;

    for (const auto& stale : directory_.findChildFiles(juce::File::findFiles, false,
                                                       juce::String(tempPrefix) + "*" + tempSuffix))
        stale.deleteFile();

    for (const auto& file : directory_.findChildFiles(juce::File::findFiles, false,
                                                      juce::String("*") + fileExtension))
    {
        const auto fileSlug = file.getFileNameWithoutExtension();
        if (file.getSize() > static_cast<juce::int64>(maxPackBytes))
        {
            scanErrors_.add(file.getFileName() + ": larger than the pack size limit");
            continue;
        }

        juce::MemoryBlock bytes;
        if (!file.loadFileAsData(bytes))
        {
            scanErrors_.add(file.getFileName() + ": unreadable");
            continue;
        }

        juce::String error;
        auto pack = parse(bytes, error);
        if (!pack)
        {
            scanErrors_.add(file.getFileName() + ": " + error);
            continue;
        }
        if (pack->slug != fileSlug || pack->isStock)
        {
            scanErrors_.add(file.getFileName() + ": slug \"" + pack->slug
                            + "\" does not belong in this file");
            continue;
        }
        packs_.push_back(std::move(*pack));
    }

    sortAndPinStock();
}

juce::String PackLibrary::install(const juce::String& slug, const juce::MemoryBlock& bytes,
                                  const juce::String& expectedSha256)
{
    if (!isValidSlug(slug))
        return "invalid pack id";
    if (slug == stockSlug)
        return "the stock pack is built in";
    if (bytes.getSize() > maxPackBytes)
        return "pack is larger than the size limit";

    // The digest comes from the store's signed answer; the bytes from a URL.
    // Only bytes that hash to the published digest are ever parsed.
    const auto expected = expectedSha256.trim().toLowerCase();
    if (!isValidSha256Hex(expected))
        return "missing or malformed checksum";
    if (sha256Hex(bytes) != expected)
        return "checksum mismatch";

    juce::String error;
    auto pack = parse(bytes, error);
    if (!pack)
        return "invalid pack: " + error;
    if (pack->slug != slug)
        return "pack id mismatch";

    if (!directory_.createDirectory())
        return "cannot create the packs folder";

    // Atomic replace: write the complete file beside the target, then rename
    // it over the old one. A crash leaves either the old pack or the new one,
    // never a half-written file (and scan() removes an orphaned temp file).
    const auto temp = directory_.getNonexistentChildFile(tempPrefix + slug, tempSuffix, false);
    if (!temp.replaceWithData(bytes.getData(), bytes.getSize()))
    {
        temp.deleteFile();
        return "cannot write the pack file";
    }
    if (!temp.replaceFileIn(fileFor(slug)))
    {
        temp.deleteFile();
        return "cannot replace the pack file";
    }

    packs_.erase(std::remove_if(packs_.begin(), packs_.end(),
                                [&slug](const PackDefinition& existing) { return existing.slug == slug; }),
                 packs_.end());
    packs_.push_back(std::move(*pack));
    sortAndPinStock();
    return {};
}

juce::String PackLibrary::remove(const juce::String& slug)
{
    if (slug == stockSlug)
        return "the stock pack is built in";

    const auto it = std::find_if(packs_.begin(), packs_.end(),
                                 [&slug](const PackDefinition& pack) { return pack.slug == slug; });
    if (it == packs_.end())
        return "pack is not installed";

    const auto file = fileFor(slug);
    if (file.exists() && !file.deleteFile())
        return "cannot delete the pack file";

    packs_.erase(it);
    return {};
}

const PackDefinition* PackLibrary::find(const juce::String& slug) const noexcept
{
    for (const auto& pack : packs_)
        if (pack.slug == slug)
            return &pack;
    return nullptr;
}

bool PackLibrary::isUsable(const PackDefinition& pack, bool memberActive) noexcept
{
    return pack.isStock || memberActive;
}

std::vector<const PackDefinition*> PackLibrary::usablePacks(bool memberActive) const
{
    std::vector<const PackDefinition*> result;
    for (const auto& pack : packs_)
        if (isUsable(pack, memberActive))
            result.push_back(&pack);
    return result;
}

void PackLibrary::sortAndPinStock()
{
    std::stable_sort(packs_.begin(), packs_.end(),
                     [](const PackDefinition& a, const PackDefinition& b)
                     {
                         if (a.isStock != b.isStock)
                             return a.isStock;
                         const auto byName = a.name.compareIgnoreCase(b.name);
                         return byName != 0 ? byName < 0 : a.slug < b.slug;
                     });
}
} // namespace chordengine::packs
