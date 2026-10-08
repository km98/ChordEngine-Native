#include "TransposeEngine.h"

#include <algorithm>

namespace chordengine::core
{
std::optional<int> TransposeEngine::supportedOctaveRange() noexcept
{
    return maximumOctaveSteps;
}

bool TransposeEngine::applyToNotes(const TransposeConfiguration& configuration,
                                   std::array<std::int64_t, 6>& notes,
                                   std::size_t noteCount) noexcept
{
    if (noteCount == 0 || noteCount > notes.size()
        || configuration.octaveSteps < minimumOctaveSteps
        || configuration.octaveSteps > maximumOctaveSteps)
        return false;

    const auto shift = static_cast<std::int64_t>(configuration.octaveSteps) * 12;
    if (configuration.target == TransposeTarget::whole)
    {
        for (std::size_t index = 0; index < noteCount; ++index)
            notes[index] += shift;
        return true;
    }

    if (configuration.target != TransposeTarget::lowest
        && configuration.target != TransposeTarget::highest)
        return false;

    auto first = notes.begin();
    const auto last = first + static_cast<std::ptrdiff_t>(noteCount);
    const auto selected = configuration.target == TransposeTarget::lowest
        ? std::min_element(first, last) : std::max_element(first, last);
    *selected = std::clamp<std::int64_t>(*selected + shift, 0, 127);
    return true;
}
} // namespace chordengine::core
