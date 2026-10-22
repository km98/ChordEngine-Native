#include "PluginState.h"
#include "XmlLite.h"

#include "../Platform/Text.h"

#include "../Core/ChordPresetSystem.h"
#include "../Core/ScaleSystem.h"
#include "../Core/TransposeEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace chordengine::state
{
namespace
{
using chordengine::core::ChordPresetId;
using chordengine::core::CoreConfiguration;
using chordengine::core::ScaleId;
using chordengine::core::TransposeTarget;
using chordengine::core::VelocityMode;

constexpr const char* rootTag = "CHORDENGINE_STATE";

// Stable persisted tokens. They are part of the on-disk format: never rename or
// reorder them. New values may only be appended, under a new format version.
constexpr std::array<const char*, 8> scaleTokens {
    "major", "minor", "dorian", "phrygian", "lydian", "mixolydian", "locrian", "harmonicMinor" };
constexpr std::array<const char*, 12> presetTokens {
    "basic", "pop", "piano", "emotional", "dreamy", "cinematic",
    "rAndB", "neoSoul", "loFi", "house", "deep", "ambient" };
constexpr std::array<const char*, 3> targetTokens { "whole", "lowest", "highest" };
constexpr std::array<const char*, 3> velocityTokens { "dynamic", "maximum", "fixed" };
constexpr std::array<const char*, 3> voicingTokens { "close", "spread", "open" };
constexpr std::array<const char*, 3> soundTokens { "synth", "piano", "bass" };
constexpr std::array<const char*, 2> switchTokens { "off", "on" };
constexpr std::array<const char*, 12> quantizeTokens {
    "none", "sixthStep", "quarterStep", "thirdStep", "halfStep", "step",
    "sixthBeat", "quarterBeat", "thirdBeat", "halfBeat", "beat", "bar" };
static_assert(quantizeTokens.size() == chordengine::midi::takeQuantizeCount,
              "every TakeQuantize needs a persisted token");

static_assert(scaleTokens.size() == chordengine::core::ScaleSystem::scaleCount,
              "every ScaleId needs a persisted token");

template <std::size_t N>
std::optional<std::size_t> tokenIndex(const std::array<const char*, N>& tokens,
                                      const std::string& value)
{
    for (std::size_t index = 0; index < N; ++index)
        if (value == tokens[index])
            return index;
    return std::nullopt;
}

// Strict integer: optional leading minus, digits only, at most four characters.
// Rejects empty text, whitespace and trailing junk instead of guessing.
std::optional<int> parseInteger(const std::string& value)
{
    const auto length = value.size();
    if (length == 0 || length > 4)
        return std::nullopt;

    const std::size_t firstDigit = value[0] == '-' ? 1 : 0;
    if (firstDigit == length)
        return std::nullopt;

    for (std::size_t index = firstDigit; index < length; ++index)
        if (value[index] < '0' || value[index] > '9')
            return std::nullopt;

    return std::stoi(value);
}

// Known-property reader. An absent property keeps the default already in
// place. A present property must parse and be accepted by `apply`; otherwise
// the whole payload is rejected.
template <typename Apply>
bool readField(const XmlElementLite& root, const char* name, Apply&& apply)
{
    if (!root.has(name))
        return true;
    return apply(root.get(name));
}

// Pack identity fields: a slug as the store issues them, and display names
// of printable text that fit the editor.
bool validSlug(const std::string& value)
{
    if (value.size() < 2 || value.size() > 63 || value[0] == '-')
        return false;
    return text::containsOnly(value, "abcdefghijklmnopqrstuvwxyz0123456789-");
}

bool validName(const std::string& value)
{
    const auto characters = text::codePoints(value);
    if (characters.empty() || characters.size() > 64)
        return false;
    for (const auto character : characters)
        if (character < 0x20 || character == 0x7f)
            return false;
    return true;
}

// The trigger map is persisted as two lower-case hex digits per code.
std::string encodeTriggerMap(const chordengine::core::CustomChordPreset& preset)
{
    std::string encoded;
    for (const auto code : preset.triggerMap)
        encoded += text::hex2(code);
    return encoded;
}

bool decodeTriggerMap(const std::string& encoded, chordengine::core::CustomChordPreset& preset)
{
    if (encoded.size() != 2 * preset.triggerMap.size()
        || !text::containsOnly(encoded, "0123456789abcdef"))
        return false;
    for (std::size_t index = 0; index < preset.triggerMap.size(); ++index)
        preset.triggerMap[index] = static_cast<std::uint8_t>(
            text::hexDigit(encoded[2 * index]) * 16 + text::hexDigit(encoded[2 * index + 1]));
    return true;
}

bool isValid(const CoreConfiguration& configuration) noexcept
{
    return chordengine::core::ChordPresetSystem::get(configuration.preset) != nullptr
        && chordengine::core::ScaleSystem::find(configuration.scale) != nullptr
        && chordengine::core::ScaleSystem::keyOffset(configuration.keyIndex).has_value()
        && configuration.transpose.octaveSteps >= chordengine::core::TransposeEngine::minimumOctaveSteps
        && configuration.transpose.octaveSteps <= chordengine::core::TransposeEngine::maximumOctaveSteps
        && configuration.fixedVelocity >= 1 && configuration.fixedVelocity <= 127;
}
} // namespace

std::string serialize(const PluginState& state)
{
    const auto& configuration = state.configuration;

    XmlElementLite root;
    root.tag = rootTag;
    root.set("version", std::to_string(currentFormatVersion));
    root.set("keyIndex", std::to_string(configuration.keyIndex));
    root.set("scale", scaleTokens[static_cast<std::size_t>(configuration.scale)]);
    root.set("chordPreset", presetTokens[static_cast<std::size_t>(configuration.preset)]);
    root.set("transposeTarget", targetTokens[static_cast<std::size_t>(configuration.transpose.target)]);
    root.set("transposeOctave", std::to_string(configuration.transpose.octaveSteps));
    root.set("velocityMode", velocityTokens[static_cast<std::size_t>(configuration.velocityMode)]);
    root.set("fixedVelocity", std::to_string(static_cast<int>(configuration.fixedVelocity)));
    if (state.packPreset.has_value())
    {
        const auto& pack = *state.packPreset;
        root.set("packSlug", pack.packSlug);
        root.set("packName", pack.packName);
        root.set("packPreset", pack.presetName);
        root.set("packVoicing", std::string(pack.preset.voicingStyle.data(), pack.preset.voicingStyle.size()));
        root.set("packMap", encodeTriggerMap(pack.preset));
    }
    if (state.internalAudioEnabled.has_value())
        root.set("internalAudio", switchTokens[*state.internalAudioEnabled ? 1 : 0]);
    if (state.internalSound.has_value())
        root.set("internalSound", soundTokens[static_cast<std::size_t>(*state.internalSound)]);
    if (state.recordQuantize.has_value())
        root.set("recordQuantize", quantizeTokens[static_cast<std::size_t>(*state.recordQuantize)]);
    if (state.followHostRecord.has_value())
        root.set("followHostRecord", switchTokens[*state.followHostRecord ? 1 : 0]);
    if (state.hostBypass.has_value() && *state.hostBypass)
        root.set("hostBypass", switchTokens[1]);
    return writeXmlDocument(root);
}

std::optional<PluginState> deserialize(const void* data, std::size_t sizeInBytes)
{
    if (data == nullptr || sizeInBytes == 0 || sizeInBytes > maximumSerializedBytes)
        return std::nullopt;

    // 0.5.x builds wrote a terminating NUL in some hosts' copies: text ends at
    // the first NUL, as it did for the 0.5 reader.
    std::string document(static_cast<const char*>(data), sizeInBytes);
    if (const auto nul = document.find('\0'); nul != std::string::npos)
        document.resize(nul);
    // A payload must be complete: the root must close, either self-closing or
    // with its end tag.
    const auto trimmed = text::trim(document);
    const bool complete = text::endsWith(trimmed, "/>")
        || text::endsWith(trimmed, std::string("</") + rootTag + ">");
    if (!complete)
        return std::nullopt;

    const auto root = parseXmlRoot(document);
    if (!root.has_value() || root->tag != rootTag)
        return std::nullopt;

    // Unversioned or incompatible payloads are rejected, never guessed at.
    const auto version = parseInteger(root->get("version"));
    if (!version.has_value() || *version != currentFormatVersion)
        return std::nullopt;

    PluginState state;
    auto& configuration = state.configuration;

    if (!readField(*root, "keyIndex", [&](const std::string& value)
        {
            const auto parsed = parseInteger(value);
            if (!parsed.has_value()) return false;
            configuration.keyIndex = *parsed;
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "scale", [&](const std::string& value)
        {
            const auto index = tokenIndex(scaleTokens, value);
            if (!index.has_value()) return false;
            configuration.scale = static_cast<ScaleId>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "chordPreset", [&](const std::string& value)
        {
            const auto index = tokenIndex(presetTokens, value);
            if (!index.has_value()) return false;
            configuration.preset = static_cast<ChordPresetId>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "transposeTarget", [&](const std::string& value)
        {
            const auto index = tokenIndex(targetTokens, value);
            if (!index.has_value()) return false;
            configuration.transpose.target = static_cast<TransposeTarget>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "transposeOctave", [&](const std::string& value)
        {
            const auto parsed = parseInteger(value);
            if (!parsed.has_value()) return false;
            configuration.transpose.octaveSteps = *parsed;
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "velocityMode", [&](const std::string& value)
        {
            const auto index = tokenIndex(velocityTokens, value);
            if (!index.has_value()) return false;
            configuration.velocityMode = static_cast<VelocityMode>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "fixedVelocity", [&](const std::string& value)
        {
            const auto parsed = parseInteger(value);
            // Range-check before narrowing so an out-of-range value cannot wrap.
            if (!parsed.has_value() || *parsed < 1 || *parsed > 127) return false;
            configuration.fixedVelocity = static_cast<std::uint8_t>(*parsed);
            return true;
        }))
        return std::nullopt;

    // A pack preset is all-or-nothing: either every pack field is present and
    // valid, or none is.
    constexpr std::array<const char*, 5> packFields {
        "packSlug", "packName", "packPreset", "packVoicing", "packMap" };
    int packFieldCount = 0;
    for (const auto* field : packFields)
        packFieldCount += root->has(field) ? 1 : 0;
    if (packFieldCount == static_cast<int>(packFields.size()))
    {
        PackPresetState pack;
        pack.packSlug = root->get("packSlug");
        pack.packName = root->get("packName");
        pack.presetName = root->get("packPreset");
        const auto voicing = tokenIndex(voicingTokens, root->get("packVoicing"));
        if (!validSlug(pack.packSlug) || !validName(pack.packName) || !validName(pack.presetName)
            || !voicing.has_value() || !decodeTriggerMap(root->get("packMap"), pack.preset))
            return std::nullopt;
        pack.preset.voicingStyle = voicingTokens[*voicing];
        if (!pack.preset.valid())
            return std::nullopt;
        state.packPreset = pack;
    }
    else if (packFieldCount != 0)
        return std::nullopt;

    if (!readField(*root, "internalAudio", [&](const std::string& value)
        {
            const auto index = tokenIndex(switchTokens, value);
            if (!index.has_value()) return false;
            state.internalAudioEnabled = *index == 1;
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "internalSound", [&](const std::string& value)
        {
            const auto index = tokenIndex(soundTokens, value);
            if (!index.has_value()) return false;
            state.internalSound = static_cast<InternalSound>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "recordQuantize", [&](const std::string& value)
        {
            const auto index = tokenIndex(quantizeTokens, value);
            if (!index.has_value()) return false;
            state.recordQuantize = static_cast<chordengine::midi::TakeQuantize>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "followHostRecord", [&](const std::string& value)
        {
            const auto index = tokenIndex(switchTokens, value);
            if (!index.has_value()) return false;
            state.followHostRecord = *index == 1;
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "hostBypass", [&](const std::string& value)
        {
            const auto index = tokenIndex(switchTokens, value);
            if (!index.has_value()) return false;
            state.hostBypass = *index == 1;
            return true;
        }))
        return std::nullopt;

    // Unknown attributes and child elements are ignored for forward compatibility.
    if (!isValid(configuration))
        return std::nullopt;

    return state;
}
} // namespace chordengine::state
