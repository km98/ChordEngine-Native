#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace chordengine::midi
{
// Recorder take quantization, applied when a take is EXPORTED (the stored take
// is never changed, so the user can pick another grid and drag again).
//
// The grids mirror FL Studio's quantize list. A step is a sixteenth note
// (a quarter of a beat), a beat is a quarter note and a bar follows the host
// time signature. Take ticks are 960 per quarter note.
enum class TakeQuantize : std::uint8_t
{
    none,
    sixthStep,
    quarterStep,
    thirdStep,
    halfStep,
    step,
    sixthBeat,
    quarterBeat,
    thirdBeat,
    halfBeat,
    beat,
    bar
};

inline constexpr int takeQuantizeCount = 12;
inline constexpr std::int64_t takeTicksPerQuarterNote = 960;

// Grid spacing in take ticks; 0 for none.
inline std::int64_t takeQuantizeGridTicks(TakeQuantize grid, std::int64_t barTicks) noexcept
{
    constexpr auto beat = takeTicksPerQuarterNote;
    constexpr auto step = beat / 4;
    switch (grid)
    {
        case TakeQuantize::none: return 0;
        case TakeQuantize::sixthStep: return step / 6;
        case TakeQuantize::quarterStep: return step / 4;
        case TakeQuantize::thirdStep: return step / 3;
        case TakeQuantize::halfStep: return step / 2;
        case TakeQuantize::step: return step;
        case TakeQuantize::sixthBeat: return beat / 6;
        case TakeQuantize::quarterBeat: return beat / 4;
        case TakeQuantize::thirdBeat: return beat / 3;
        case TakeQuantize::halfBeat: return beat / 2;
        case TakeQuantize::beat: return beat;
        case TakeQuantize::bar: return barTicks > 0 ? barTicks : 4 * beat;
    }
    return 0;
}

struct TakeEvent
{
    std::int64_t tick = 0;
    std::uint8_t channel = 1;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    bool noteOn = false;
};

// Where the take sits on the host's musical grid. offsetTicks is the distance
// from the start of the bar that holds the take's first note to that note
// (tick 0 of the take). When the host transport was not running there is no
// musical position, and the grid is anchored to the first note instead.
struct TakeGridAnchor
{
    bool known = false;
    std::int64_t offsetTicks = 0;
    std::int64_t barTicks = 4 * takeTicksPerQuarterNote;
};

// Snaps every note START to the nearest grid line and moves its release by
// the same amount, so note lengths are kept (FL Studio's quick quantize does
// the same). With a known anchor the grid is the host's grid and the result
// starts at the bar line before the first note, so a take dropped on that bar
// in the DAW lines up with the host grid; otherwise the first note stays at
// tick 0. Repeated notes of one pitch never overlap afterwards: an earlier
// note is cut at the next start, and dropped when both land on one grid line.
// TakeQuantize::none returns the events unchanged.
inline std::vector<TakeEvent> quantizeTake(const std::vector<TakeEvent>& events, TakeQuantize grid,
                                           const TakeGridAnchor& anchor)
{
    const auto gridTicks = takeQuantizeGridTicks(grid, anchor.barTicks);
    if (gridTicks <= 0 || events.empty())
        return events;

    struct Note
    {
        std::int64_t start = 0;
        std::int64_t end = 0;
        std::uint8_t channel = 1;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool keep = true;
    };

    // Pair each note-on with the next release of the same channel and pitch.
    std::vector<Note> notes;
    std::vector<bool> used(events.size(), false);
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const auto& on = events[index];
        if (!on.noteOn)
            continue;
        Note note;
        note.start = on.tick;
        note.end = on.tick + 12; // an unreleased note, as the recorder closes one
        note.channel = on.channel;
        note.note = on.note;
        note.velocity = on.velocity;
        for (std::size_t other = index + 1; other < events.size(); ++other)
        {
            const auto& off = events[other];
            if (!used[other] && !off.noteOn && off.channel == on.channel && off.note == on.note)
            {
                used[other] = true;
                note.end = std::max(off.tick, on.tick + 1);
                break;
            }
        }
        notes.push_back(note);
    }
    if (notes.empty())
        return events;

    const auto origin = anchor.known ? std::max<std::int64_t>(0, anchor.offsetTicks) : 0;
    auto snap = [gridTicks](std::int64_t position)
    {
        return ((position + gridTicks / 2) / gridTicks) * gridTicks;
    };
    for (auto& note : notes)
    {
        const auto shift = snap(origin + note.start) - (origin + note.start);
        note.start += origin + shift;
        note.end += origin + shift;
    }

    // Rebase: with a host anchor the file starts at the bar line before the
    // first (quantized) note; without one the first note is tick 0.
    std::int64_t firstStart = notes.front().start;
    for (const auto& note : notes)
        firstStart = std::min(firstStart, note.start);
    const auto barTicks = anchor.barTicks > 0 ? anchor.barTicks : 4 * takeTicksPerQuarterNote;
    const auto rebase = anchor.known ? (firstStart / barTicks) * barTicks : firstStart;
    for (auto& note : notes)
    {
        note.start -= rebase;
        note.end -= rebase;
    }

    std::stable_sort(notes.begin(), notes.end(),
                     [](const Note& a, const Note& b) { return a.start < b.start; });
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        auto& earlier = notes[index];
        for (std::size_t later = index + 1; later < notes.size(); ++later)
        {
            const auto& next = notes[later];
            if (!next.keep || next.channel != earlier.channel || next.note != earlier.note)
                continue;
            if (next.start <= earlier.start)
                earlier.keep = false;
            else if (earlier.end > next.start)
                earlier.end = next.start;
            break;
        }
    }

    std::vector<TakeEvent> result;
    result.reserve(notes.size() * 2);
    for (const auto& note : notes)
    {
        if (!note.keep)
            continue;
        result.push_back({ note.start, note.channel, note.note, note.velocity, true });
        result.push_back({ note.end, note.channel, note.note, 0, false });
    }
    // Releases before starts on a shared tick, so a repeated pitch hands over
    // cleanly.
    std::stable_sort(result.begin(), result.end(), [](const TakeEvent& a, const TakeEvent& b)
    {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return !a.noteOn && b.noteOn;
    });
    return result;
}
} // namespace chordengine::midi
