#include "PackLibrary.h"

#include "Platform/Platform.h"

#include <algorithm>
#include <fstream>

namespace chordengine::packs
{
namespace
{
constexpr const char* tempPrefix = ".install-";
constexpr const char* tempSuffix = ".tmp";
} // namespace

PackLibrary::PackLibrary(std::filesystem::path packsDirectory)
    : directory_(std::move(packsDirectory))
{
    packs_.push_back(makeStockPack());
}

std::filesystem::path PackLibrary::defaultPacksDirectory()
{
    // Beside the licensing state: Application Support/Music-Prod/ChordEngine.
    return platform::userApplicationDataDirectory() / "Music-Prod" / "ChordEngine" / "Packs";
}

std::filesystem::path PackLibrary::fileFor(const std::string& slug) const
{
    return directory_ / (slug + fileExtension);
}

void PackLibrary::scan()
{
    packs_.clear();
    scanErrors_.clear();
    packs_.push_back(makeStockPack());

    std::error_code error;
    if (!std::filesystem::is_directory(directory_, error))
        return;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error))
    {
        if (!entry.is_regular_file(error))
            continue;
        const auto name = entry.path().filename().string();
        if (text::startsWith(name, tempPrefix) && text::endsWith(name, tempSuffix))
        {
            std::filesystem::remove(entry.path(), error);
            continue;
        }
        if (entry.path().extension() == fileExtension)
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files)
    {
        const auto fileName = file.filename().string();
        const auto fileSlug = file.stem().string();
        std::error_code sizeError;
        if (std::filesystem::file_size(file, sizeError) > static_cast<std::uintmax_t>(maxPackBytes))
        {
            scanErrors_.push_back(fileName + ": larger than the pack size limit");
            continue;
        }

        const auto stored = platform::readFile(file, maxPackBytes);
        if (!stored.has_value())
        {
            scanErrors_.push_back(fileName + ": unreadable");
            continue;
        }

        std::string parseError;
        auto pack = parse(platform::bytesOf(*stored), parseError);
        if (!pack)
        {
            scanErrors_.push_back(fileName + ": " + parseError);
            continue;
        }
        if (pack->slug != fileSlug || pack->isStock)
        {
            scanErrors_.push_back(fileName + ": slug \"" + pack->slug + "\" does not belong in this file");
            continue;
        }
        packs_.push_back(std::move(*pack));
    }

    sortAndPinStock();
}

std::string PackLibrary::install(const std::string& slug, const platform::Bytes& bytes,
                                  const std::string& expectedSha256)
{
    if (!isValidSlug(slug))
        return "invalid pack id";
    if (slug == stockSlug)
        return "the stock pack is built in";
    if (bytes.size() > maxPackBytes)
        return "pack is larger than the size limit";

    // The digest comes from the store's signed answer; the bytes from a URL.
    // Only bytes that hash to the published digest are ever parsed.
    const auto expected = text::toLower(text::trim(expectedSha256));
    if (!isValidSha256Hex(expected))
        return "missing or malformed checksum";
    if (sha256Hex(bytes) != expected)
        return "checksum mismatch";

    std::string error;
    auto pack = parse(bytes, error);
    if (!pack)
        return "invalid pack: " + error;
    if (pack->slug != slug)
        return "pack id mismatch";

    std::error_code fsError;
    std::filesystem::create_directories(directory_, fsError);
    if (!std::filesystem::is_directory(directory_, fsError))
        return "cannot create the packs folder";

    // Atomic replace: write the complete file beside the target, then rename
    // it over the old one. A crash leaves either the old pack or the new one,
    // never a half-written file (and scan() removes an orphaned temp file).
    const auto temp = directory_ / (std::string(tempPrefix) + slug + "-" + platform::randomHex(4) + tempSuffix);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (out)
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out)
        {
            out.close();
            std::filesystem::remove(temp, fsError);
            return "cannot write the pack file";
        }
    }
    std::filesystem::rename(temp, fileFor(slug), fsError);
    if (fsError)
    {
        std::filesystem::remove(temp, fsError);
        return "cannot replace the pack file";
    }

    packs_.erase(std::remove_if(packs_.begin(), packs_.end(),
                                [&slug](const PackDefinition& existing) { return existing.slug == slug; }),
                 packs_.end());
    packs_.push_back(std::move(*pack));
    sortAndPinStock();
    return {};
}

std::string PackLibrary::remove(const std::string& slug)
{
    if (slug == stockSlug)
        return "the stock pack is built in";

    const auto it = std::find_if(packs_.begin(), packs_.end(),
                                 [&slug](const PackDefinition& pack) { return pack.slug == slug; });
    if (it == packs_.end())
        return "pack is not installed";

    const auto file = fileFor(slug);
    std::error_code error;
    if (std::filesystem::exists(file, error) && !std::filesystem::remove(file, error))
        return "cannot delete the pack file";

    packs_.erase(it);
    return {};
}

const PackDefinition* PackLibrary::find(const std::string& slug) const noexcept
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
                         const auto nameA = text::toLower(a.name);
                         const auto nameB = text::toLower(b.name);
                         return nameA != nameB ? nameA < nameB : a.slug < b.slug;
                     });
}
} // namespace chordengine::packs
