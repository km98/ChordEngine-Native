#include "PackFormat.h"

#include "Platform/Platform.h"

#include <algorithm>
#include <sstream>

namespace chordengine::packs
{
namespace
{
bool isPrintableText(const std::string& text) noexcept
{
    for (const auto character : text::codePoints(text))
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f))
            return false;
    return true;
}

// A display name: non-empty, bounded, printable and without surrounding
// whitespace (so "Jazz" and "Jazz " can never be two different presets).
bool isValidName(const std::string& name, int maxLength) noexcept
{
    const auto length = text::utf8Length(name);
    return length >= 1 && length <= static_cast<std::size_t>(maxLength) && isPrintableText(name)
        && name == text::trim(name);
}

bool readString(const platform::Json& object, const char* key, bool required,
                std::string& out, std::string& error)
{
    const auto& value = object[key];
    if (value.isNull())
    {
        if (required)
            error = std::string(key) + ": missing";
        return !required;
    }
    if (!value.isString())
    {
        error = std::string(key) + ": must be a string";
        return false;
    }
    out = value.stringRef();
    return true;
}

bool isInteger(const platform::Json& value) noexcept
{
    return value.isInteger();
}

std::optional<PackPreset> parsePreset(const platform::Json& value, int index, std::string& error)
{
    const auto where = "presets[" + std::to_string(index) + "]";
    if (!value.isObject())
    {
        error = where + ": must be an object";
        return std::nullopt;
    }

    PackPreset preset;
    std::string fieldError;
    if (!readString(value, "name", true, preset.name, fieldError))
    {
        error = where + "." + fieldError;
        return std::nullopt;
    }
    if (!isValidName(preset.name, maxPresetNameLength))
    {
        error = where + ".name: must be 1.." + std::to_string(maxPresetNameLength)
              + " printable characters without surrounding spaces";
        return std::nullopt;
    }

    std::string voicing;
    if (!readString(value, "voicing", true, voicing, fieldError))
    {
        error = where + "." + fieldError;
        return std::nullopt;
    }
    const auto style = voicingFromString(voicing);
    if (!style)
    {
        error = where + ".voicing: must be close, spread or open";
        return std::nullopt;
    }
    preset.voicing = *style;

    const auto& map = value["triggerMap"];
    const auto* codes = map.isArray() ? &map.items() : nullptr;
    if (codes == nullptr)
    {
        error = where + ".triggerMap: must be an array";
        return std::nullopt;
    }
    if (static_cast<std::size_t>(codes->size()) != triggerCount)
    {
        error = where + ".triggerMap: must hold exactly " + std::to_string(triggerCount)
              + " codes";
        return std::nullopt;
    }
    for (std::size_t slot = 0; slot < codes->size(); ++slot)
    {
        const auto& code = (*codes)[slot];
        const auto number = isInteger(code) ? code.asInt() : -1;
        if (number < 0 || number > maxTriggerCode)
        {
            error = where + ".triggerMap[" + std::to_string(slot) + "]: must be an integer 0.."
                  + std::to_string(maxTriggerCode);
            return std::nullopt;
        }
        preset.triggerMap[slot] = static_cast<std::uint8_t>(number);
    }
    return preset;
}

std::string quoted(const std::string& text)
{
    std::string out;
    platform::appendJsonString(out, text);
    return out;
}
} // namespace

const char* toString(VoicingStyle style) noexcept
{
    switch (style)
    {
        case VoicingStyle::spread: return "spread";
        case VoicingStyle::open: return "open";
        case VoicingStyle::close: break;
    }
    return "close";
}

std::optional<VoicingStyle> voicingFromString(const std::string& text) noexcept
{
    if (text == "close")
        return VoicingStyle::close;
    if (text == "spread")
        return VoicingStyle::spread;
    if (text == "open")
        return VoicingStyle::open;
    return std::nullopt;
}

bool PackPreset::operator==(const PackPreset& other) const noexcept
{
    return name == other.name && voicing == other.voicing && triggerMap == other.triggerMap;
}

bool PackDefinition::operator==(const PackDefinition& other) const noexcept
{
    return slug == other.slug && name == other.name && version == other.version
        && author == other.author && description == other.description
        && presets == other.presets && isStock == other.isStock;
}

bool isValidSlug(const std::string& slug) noexcept
{
    const auto length = slug.size();
    if (length < 2 || length > 63)
        return false;
    for (std::size_t index = 0; index < length; ++index)
    {
        const auto character = slug[index];
        const bool alnum = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (!alnum && (index == 0 || character != '-'))
            return false;
    }
    return true;
}

bool isValidVersion(const std::string& version) noexcept
{
    const auto parts = text::split(version, '.');
    if (parts.size() != 3 || version.size() > 32)
        return false;
    for (const auto& part : parts)
        if (part.empty() || !text::containsOnly(part, "0123456789"))
            return false;
    return true;
}

int compareVersions(const std::string& a, const std::string& b) noexcept
{
    const bool validA = isValidVersion(a);
    const bool validB = isValidVersion(b);
    if (!validA || !validB)
        return static_cast<int>(validA) - static_cast<int>(validB);

    const auto partsA = text::split(a, '.');
    const auto partsB = text::split(b, '.');
    for (std::size_t index = 0; index < 3; ++index)
    {
        // At most 32 characters in all, so a component can exceed 64 bits;
        // compare by length first, then digit by digit, after leading zeros.
        auto strip = [](const std::string& digits)
        {
            const auto first = digits.find_first_not_of('0');
            return first == std::string::npos ? std::string("0") : digits.substr(first);
        };
        const auto numberA = strip(partsA[index]);
        const auto numberB = strip(partsB[index]);
        if (numberA.size() != numberB.size())
            return numberA.size() < numberB.size() ? -1 : 1;
        if (numberA != numberB)
            return numberA < numberB ? -1 : 1;
    }
    return 0;
}

std::optional<PackDefinition> parse(const std::string& json, std::string& error)
{
    error.clear();
    if (json.size() > maxPackBytes)
    {
        error = "pack exceeds " + std::to_string(maxPackBytes / 1024) + " KiB";
        return std::nullopt;
    }

    const auto document = platform::Json::parse(json);
    if (!document.has_value() || !document->isObject())
    {
        error = "not a JSON object";
        return std::nullopt;
    }
    const auto& root = *document;

    std::string format;
    if (!readString(root, "format", true, format, error))
        return std::nullopt;
    if (format != formatName)
    {
        error = "format: must be " + std::string(formatName);
        return std::nullopt;
    }

    const auto& versionValue = root["formatVersion"];
    if (!isInteger(versionValue) || versionValue.asInt() != formatVersion)
    {
        error = "formatVersion: must be " + std::to_string(formatVersion);
        return std::nullopt;
    }

    PackDefinition pack;
    pack.author.clear();
    if (!readString(root, "slug", true, pack.slug, error)
        || !readString(root, "name", true, pack.name, error)
        || !readString(root, "version", true, pack.version, error)
        || !readString(root, "author", false, pack.author, error)
        || !readString(root, "description", false, pack.description, error))
        return std::nullopt;

    if (!isValidSlug(pack.slug))
    {
        error = "slug: must match ^[a-z0-9][a-z0-9-]{1,62}$";
        return std::nullopt;
    }
    if (!isValidName(pack.name, maxPackNameLength))
    {
        error = "name: must be 1.." + std::to_string(maxPackNameLength) + " printable characters";
        return std::nullopt;
    }
    if (!isValidVersion(pack.version))
    {
        error = "version: must be MAJOR.MINOR.PATCH";
        return std::nullopt;
    }
    if (pack.author.empty())
        pack.author = defaultAuthor;
    if (!isValidName(pack.author, maxAuthorLength))
    {
        error = "author: must be 1.." + std::to_string(maxAuthorLength) + " printable characters";
        return std::nullopt;
    }
    if (text::utf8Length(pack.description) > static_cast<std::size_t>(maxDescriptionLength))
    {
        error = "description: longer than " + std::to_string(maxDescriptionLength) + " characters";
        return std::nullopt;
    }

    const auto& presetsValue = root["presets"];
    const auto* presets = presetsValue.isArray() ? &presetsValue.items() : nullptr;
    if (presets == nullptr)
    {
        error = "presets: must be an array";
        return std::nullopt;
    }
    if (presets->size() < static_cast<std::size_t>(minPresets) || presets->size() > static_cast<std::size_t>(maxPresets))
    {
        error = "presets: must hold " + std::to_string(minPresets) + ".." + std::to_string(maxPresets)
              + " presets";
        return std::nullopt;
    }

    std::vector<std::string> seenNames;
    for (std::size_t index = 0; index < presets->size(); ++index)
    {
        auto preset = parsePreset((*presets)[index], static_cast<int>(index), error);
        if (!preset)
            return std::nullopt;
        // Names identify presets in saved host state, so they are unique
        // ignoring case.
        const auto folded = text::toLower(preset->name);
        if (std::find(seenNames.begin(), seenNames.end(), folded) != seenNames.end())
        {
            error = "presets[" + std::to_string(index) + "].name: duplicate \"" + preset->name + "\"";
            return std::nullopt;
        }
        seenNames.push_back(folded);
        pack.presets.push_back(std::move(*preset));
    }

    pack.isStock = pack.slug == stockSlug;
    return pack;
}

std::optional<PackDefinition> parse(const platform::Bytes& utf8, std::string& error)
{
    error.clear();
    if (utf8.size() > maxPackBytes)
    {
        error = "pack exceeds " + std::to_string(maxPackBytes / 1024) + " KiB";
        return std::nullopt;
    }
    if (!utf8.empty() && !platform::isValidUtf8(utf8.data(), utf8.size()))
    {
        error = "pack is not valid UTF-8";
        return std::nullopt;
    }
    return parse(platform::textOf(utf8), error);
}

std::string toJson(const PackDefinition& pack)
{
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": " << quoted(formatName) << ",\n"
        << "  \"formatVersion\": " << formatVersion << ",\n"
        << "  \"slug\": " << quoted(pack.slug) << ",\n"
        << "  \"name\": " << quoted(pack.name) << ",\n"
        << "  \"version\": " << quoted(pack.version) << ",\n"
        << "  \"author\": " << quoted(pack.author) << ",\n"
        << "  \"description\": " << quoted(pack.description) << ",\n"
        << "  \"presets\": [";

    for (std::size_t index = 0; index < pack.presets.size(); ++index)
    {
        const auto& preset = pack.presets[index];
        out << (index == 0 ? "\n" : ",\n")
            << "    {\n"
            << "      \"name\": " << quoted(preset.name) << ",\n"
            << "      \"voicing\": " << quoted(toString(preset.voicing)) << ",\n"
            << "      \"triggerMap\": [";
        for (std::size_t slot = 0; slot < preset.triggerMap.size(); ++slot)
            out << (slot == 0 ? "" : ", ") << static_cast<int>(preset.triggerMap[slot]);
        out << "]\n    }";
    }

    out << "\n  ]\n}\n";
    return out.str();
}

PackDefinition makeStockPack()
{
    PackDefinition pack;
    pack.slug = stockSlug;
    pack.name = stockName;
    pack.version = stockVersion;
    pack.author = defaultAuthor;
    pack.description = "The 12 presets built into ChordEngine.";
    pack.isStock = true;

    for (int index = 0; index < static_cast<int>(core::ChordPresetSystem::count()); ++index)
    {
        const auto* definition = core::ChordPresetSystem::findByIndex(index);
        if (definition == nullptr)
            continue;

        PackPreset preset;
        preset.name = std::string(definition->name);
        const auto style = voicingFromString(std::string(definition->voicingStyle));
        preset.voicing = style.value_or(VoicingStyle::close);
        for (std::size_t slot = 0; slot < triggerCount; ++slot)
            preset.triggerMap[slot] = definition->triggerMap[slot];
        pack.presets.push_back(std::move(preset));
    }
    return pack;
}

std::string sha256Hex(const platform::Bytes& bytes)
{
    return platform::sha256Hex(bytes.data(), bytes.size());
}

bool isValidSha256Hex(const std::string& digest) noexcept
{
    return digest.size() == 64 && text::containsOnly(digest, "0123456789abcdef");
}
} // namespace chordengine::packs
