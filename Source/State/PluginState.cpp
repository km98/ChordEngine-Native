#include "PluginState.h"

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

static_assert(scaleTokens.size() == chordengine::core::ScaleSystem::scaleCount,
              "every ScaleId needs a persisted token");

template <std::size_t N>
std::optional<std::size_t> tokenIndex(const std::array<const char*, N>& tokens,
                                      const juce::String& value)
{
    for (std::size_t index = 0; index < N; ++index)
        if (value == tokens[index])
            return index;
    return std::nullopt;
}

// Strict integer: optional leading minus, digits only, at most four characters.
// Rejects empty text, whitespace and trailing junk instead of guessing.
std::optional<int> parseInteger(const juce::String& value)
{
    const int length = value.length();
    if (length == 0 || length > 4)
        return std::nullopt;

    const int firstDigit = value[0] == '-' ? 1 : 0;
    if (firstDigit == length)
        return std::nullopt;

    for (int index = firstDigit; index < length; ++index)
        if (value[index] < '0' || value[index] > '9')
            return std::nullopt;

    return value.getIntValue();
}

// Known-property reader. An absent property keeps the default already in
// place. A present property must parse and be accepted by `apply`; otherwise
// the whole payload is rejected.
template <typename Apply>
bool readField(const juce::XmlElement& root, const char* name, Apply&& apply)
{
    if (!root.hasAttribute(name))
        return true;
    return apply(root.getStringAttribute(name));
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

juce::String serialize(const PluginState& state)
{
    const auto& configuration = state.configuration;

    juce::XmlElement root(rootTag);
    root.setAttribute("version", currentFormatVersion);
    root.setAttribute("keyIndex", configuration.keyIndex);
    root.setAttribute("scale", scaleTokens[static_cast<std::size_t>(configuration.scale)]);
    root.setAttribute("chordPreset", presetTokens[static_cast<std::size_t>(configuration.preset)]);
    root.setAttribute("transposeTarget", targetTokens[static_cast<std::size_t>(configuration.transpose.target)]);
    root.setAttribute("transposeOctave", configuration.transpose.octaveSteps);
    root.setAttribute("velocityMode", velocityTokens[static_cast<std::size_t>(configuration.velocityMode)]);
    root.setAttribute("fixedVelocity", static_cast<int>(configuration.fixedVelocity));
    return root.toString();
}

std::optional<PluginState> deserialize(const void* data, std::size_t sizeInBytes)
{
    if (data == nullptr || sizeInBytes == 0 || sizeInBytes > maximumSerializedBytes)
        return std::nullopt;

    const auto text = juce::String::fromUTF8(static_cast<const char*>(data),
                                             static_cast<int>(sizeInBytes));
    // The parser accepts some truncated documents (for example an unterminated
    // start tag at end of input) as partial elements. A payload must therefore
    // be complete: the root must close, either self-closing or with its end tag.
    const auto trimmed = text.trim();
    const bool complete = trimmed.endsWith("/>") || trimmed.endsWith(juce::String("</") + rootTag + ">");
    if (!complete)
        return std::nullopt;

    juce::XmlDocument document(text);
    const auto root = document.getDocumentElement();
    if (root == nullptr || document.getLastParseError().isNotEmpty()
        || root->getTagName() != rootTag)
        return std::nullopt;

    // Unversioned or incompatible payloads are rejected, never guessed at.
    const auto version = parseInteger(root->getStringAttribute("version"));
    if (!version.has_value() || *version != currentFormatVersion)
        return std::nullopt;

    PluginState state;
    auto& configuration = state.configuration;

    if (!readField(*root, "keyIndex", [&](const juce::String& value)
        {
            const auto parsed = parseInteger(value);
            if (!parsed.has_value()) return false;
            configuration.keyIndex = *parsed;
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "scale", [&](const juce::String& value)
        {
            const auto index = tokenIndex(scaleTokens, value);
            if (!index.has_value()) return false;
            configuration.scale = static_cast<ScaleId>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "chordPreset", [&](const juce::String& value)
        {
            const auto index = tokenIndex(presetTokens, value);
            if (!index.has_value()) return false;
            configuration.preset = static_cast<ChordPresetId>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "transposeTarget", [&](const juce::String& value)
        {
            const auto index = tokenIndex(targetTokens, value);
            if (!index.has_value()) return false;
            configuration.transpose.target = static_cast<TransposeTarget>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "transposeOctave", [&](const juce::String& value)
        {
            const auto parsed = parseInteger(value);
            if (!parsed.has_value()) return false;
            configuration.transpose.octaveSteps = *parsed;
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "velocityMode", [&](const juce::String& value)
        {
            const auto index = tokenIndex(velocityTokens, value);
            if (!index.has_value()) return false;
            configuration.velocityMode = static_cast<VelocityMode>(*index);
            return true;
        }))
        return std::nullopt;

    if (!readField(*root, "fixedVelocity", [&](const juce::String& value)
        {
            const auto parsed = parseInteger(value);
            // Range-check before narrowing so an out-of-range value cannot wrap.
            if (!parsed.has_value() || *parsed < 1 || *parsed > 127) return false;
            configuration.fixedVelocity = static_cast<std::uint8_t>(*parsed);
            return true;
        }))
        return std::nullopt;

    // Unknown attributes and child elements are ignored for forward compatibility.
    if (!isValid(configuration))
        return std::nullopt;

    return state;
}
} // namespace chordengine::state
