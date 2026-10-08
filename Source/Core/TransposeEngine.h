#pragma once

#include "ChordPresetSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace chordengine::core
{
enum class TransposeTarget : std::uint8_t
{
    whole, lowest, highest
};

struct TransposeConfiguration
{
    TransposeTarget target = TransposeTarget::whole;
    int octaveSteps = 0;
};

struct NoteSet
{
    std::array<std::uint8_t, 6> notes{};
    std::size_t size = 0;
};

class TransposeEngine
{
public:
    static constexpr int minimumOctaveSteps = -2;
    static constexpr int maximumOctaveSteps = 2;

    static bool applyToNotes(const TransposeConfiguration& configuration,
                             std::array<std::int64_t, 6>& notes,
                             std::size_t noteCount) noexcept;
    static std::optional<int> supportedOctaveRange() noexcept;
};
} // namespace chordengine::core
