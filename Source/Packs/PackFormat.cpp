#include "PackFormat.h"

#include <juce_cryptography/juce_cryptography.h>

namespace chordengine::packs
{
namespace
{
bool isPrintableText(const juce::String& text) noexcept
{
    for (auto pointer = text.getCharPointer(); !pointer.isEmpty();)
    {
        const auto character = pointer.getAndAdvance();
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f))
            return false;
    }
    return true;
}

// A display name: non-empty, bounded, printable and without surrounding
// whitespace (so "Jazz" and "Jazz " can never be two different presets).
bool isValidName(const juce::String& name, int maxLength) noexcept
{
    const auto length = name.length();
    return length >= 1 && length <= maxLength && isPrintableText(name)
        && name == name.trim();
}

bool readString(const juce::var& object, const char* key, bool required,
                juce::String& out, juce::String& error)
{
    const auto value = object.getProperty(key, juce::var());
    if (value.isVoid() || value.isUndefined())
    {
        if (required)
            error = juce::String(key) + ": missing";
        return !required;
    }
    if (!value.isString())
    {
        error = juce::String(key) + ": must be a string";
        return false;
    }
    out = value.toString();
    return true;
}

bool isInteger(const juce::var& value) noexcept
{
    return value.isInt() || value.isInt64();
}

std::optional<PackPreset> parsePreset(const juce::var& value, int index, juce::String& error)
{
    const auto where = "presets[" + juce::String(index) + "]";
    if (!value.isObject())
    {
        error = where + ": must be an object";
        return std::nullopt;
    }

    PackPreset preset;
    juce::String fieldError;
    if (!readString(value, "name", true, preset.name, fieldError))
    {
        error = where + "." + fieldError;
        return std::nullopt;
    }
    if (!isValidName(preset.name, maxPresetNameLength))
    {
        error = where + ".name: must be 1.." + juce::String(maxPresetNameLength)
              + " printable characters without surrounding spaces";
        return std::nullopt;
    }

    juce::String voicing;
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

    const auto map = value.getProperty("triggerMap", juce::var());
    const auto* codes = map.getArray();
    if (codes == nullptr)
    {
        error = where + ".triggerMap: must be an array";
        return std::nullopt;
    }
    if (static_cast<std::size_t>(codes->size()) != triggerCount)
    {
        error = where + ".triggerMap: must hold exactly " + juce::String(static_cast<int>(triggerCount))
              + " codes";
        return std::nullopt;
    }
    for (int slot = 0; slot < codes->size(); ++slot)
    {
        const auto& code = codes->getReference(slot);
        const auto number = isInteger(code) ? static_cast<juce::int64>(code) : -1;
        if (number < 0 || number > maxTriggerCode)
        {
            error = where + ".triggerMap[" + juce::String(slot) + "]: must be an integer 0.."
                  + juce::String(maxTriggerCode);
            return std::nullopt;
        }
        preset.triggerMap[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(number);
    }
    return preset;
}

juce::String quoted(const juce::String& text)
{
    return juce::JSON::toString(juce::var(text), juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::none));
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

std::optional<VoicingStyle> voicingFromString(const juce::String& text) noexcept
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

bool isValidSlug(const juce::String& slug) noexcept
{
    const auto length = slug.length();
    if (length < 2 || length > 63)
        return false;
    for (int index = 0; index < length; ++index)
    {
        const auto character = slug[index];
        const bool alnum = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (!alnum && (index == 0 || character != '-'))
            return false;
    }
    return true;
}

bool isValidVersion(const juce::String& version) noexcept
{
    juce::StringArray parts;
    parts.addTokens(version, ".", "");
    if (parts.size() != 3 || version.length() > 32)
        return false;
    for (const auto& part : parts)
        if (part.isEmpty() || !part.containsOnly("0123456789"))
            return false;
    return true;
}

int compareVersions(const juce::String& a, const juce::String& b) noexcept
{
    const bool validA = isValidVersion(a);
    const bool validB = isValidVersion(b);
    if (!validA || !validB)
        return static_cast<int>(validA) - static_cast<int>(validB);

    juce::StringArray partsA, partsB;
    partsA.addTokens(a, ".", "");
    partsB.addTokens(b, ".", "");
    for (int index = 0; index < 3; ++index)
    {
        const auto numberA = partsA[index].getLargeIntValue();
        const auto numberB = partsB[index].getLargeIntValue();
        if (numberA != numberB)
            return numberA < numberB ? -1 : 1;
    }
    return 0;
}

std::optional<PackDefinition> parse(const juce::String& json, juce::String& error)
{
    error.clear();
    if (json.getNumBytesAsUTF8() > maxPackBytes)
    {
        error = "pack exceeds " + juce::String(static_cast<int>(maxPackBytes / 1024)) + " KiB";
        return std::nullopt;
    }

    juce::var root;
    const auto result = juce::JSON::parse(json, root);
    if (result.failed() || !root.isObject())
    {
        error = "not a JSON object";
        return std::nullopt;
    }

    juce::String format;
    if (!readString(root, "format", true, format, error))
        return std::nullopt;
    if (format != formatName)
    {
        error = "format: must be " + juce::String(formatName);
        return std::nullopt;
    }

    const auto versionValue = root.getProperty("formatVersion", juce::var());
    if (!isInteger(versionValue) || static_cast<juce::int64>(versionValue) != formatVersion)
    {
        error = "formatVersion: must be " + juce::String(formatVersion);
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
        error = "name: must be 1.." + juce::String(maxPackNameLength) + " printable characters";
        return std::nullopt;
    }
    if (!isValidVersion(pack.version))
    {
        error = "version: must be MAJOR.MINOR.PATCH";
        return std::nullopt;
    }
    if (pack.author.isEmpty())
        pack.author = defaultAuthor;
    if (!isValidName(pack.author, maxAuthorLength))
    {
        error = "author: must be 1.." + juce::String(maxAuthorLength) + " printable characters";
        return std::nullopt;
    }
    if (pack.description.length() > maxDescriptionLength)
    {
        error = "description: longer than " + juce::String(maxDescriptionLength) + " characters";
        return std::nullopt;
    }

    const auto* presets = root.getProperty("presets", juce::var()).getArray();
    if (presets == nullptr)
    {
        error = "presets: must be an array";
        return std::nullopt;
    }
    if (presets->size() < minPresets || presets->size() > maxPresets)
    {
        error = "presets: must hold " + juce::String(minPresets) + ".." + juce::String(maxPresets)
              + " presets";
        return std::nullopt;
    }

    juce::StringArray seenNames;
    for (int index = 0; index < presets->size(); ++index)
    {
        auto preset = parsePreset(presets->getReference(index), index, error);
        if (!preset)
            return std::nullopt;
        // Names identify presets in saved host state, so they are unique
        // ignoring case.
        if (seenNames.contains(preset->name, true))
        {
            error = "presets[" + juce::String(index) + "].name: duplicate \"" + preset->name + "\"";
            return std::nullopt;
        }
        seenNames.add(preset->name);
        pack.presets.push_back(std::move(*preset));
    }

    pack.isStock = pack.slug == stockSlug;
    return pack;
}

std::optional<PackDefinition> parse(const juce::MemoryBlock& utf8, juce::String& error)
{
    error.clear();
    if (utf8.getSize() > maxPackBytes)
    {
        error = "pack exceeds " + juce::String(static_cast<int>(maxPackBytes / 1024)) + " KiB";
        return std::nullopt;
    }
    const auto* data = static_cast<const char*>(utf8.getData());
    if (utf8.getSize() > 0
        && !juce::CharPointer_UTF8::isValidString(data, static_cast<int>(utf8.getSize())))
    {
        error = "pack is not valid UTF-8";
        return std::nullopt;
    }
    return parse(juce::String::fromUTF8(data, static_cast<int>(utf8.getSize())), error);
}

juce::String toJson(const PackDefinition& pack)
{
    juce::String out;
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
    return out;
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
        jassert(definition != nullptr);
        if (definition == nullptr)
            continue;

        PackPreset preset;
        preset.name = juce::String::fromUTF8(definition->name.data(),
                                             static_cast<int>(definition->name.size()));
        const auto style = voicingFromString(juce::String::fromUTF8(
            definition->voicingStyle.data(), static_cast<int>(definition->voicingStyle.size())));
        jassert(style.has_value());
        preset.voicing = style.value_or(VoicingStyle::close);
        for (std::size_t slot = 0; slot < triggerCount; ++slot)
            preset.triggerMap[slot] = definition->triggerMap[slot];
        pack.presets.push_back(std::move(preset));
    }
    return pack;
}

juce::String sha256Hex(const juce::MemoryBlock& bytes)
{
    return juce::SHA256(bytes).toHexString().toLowerCase();
}

bool isValidSha256Hex(const juce::String& digest) noexcept
{
    return digest.length() == 64 && digest.containsOnly("0123456789abcdef");
}
} // namespace chordengine::packs
