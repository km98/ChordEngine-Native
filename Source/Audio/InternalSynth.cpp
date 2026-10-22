#include "InternalSynth.h"

#include <algorithm>
#include <cmath>

namespace chordengine::audio
{
namespace
{
// The float value of pi the 0.5 synth used for its filter coefficients.
constexpr float kPi = static_cast<float>(3.141592653589793238L);
} // namespace

namespace
{
constexpr float twoPi = 6.283185307179586f;
constexpr float silenceFloor = 1.0e-4f;    // -80 dB: a voice below this is done
constexpr float stealFadeSeconds = 0.003f; // voice stealing / retrigger fade
constexpr float soundOffFadeSeconds = 0.005f;
constexpr float detuneRatio = 1.0040508f;  // +7 cents

float noteToFrequency(int note) noexcept
{
    return 440.0f * std::exp2((static_cast<float>(note) - 69.0f) / 12.0f);
}

float nextNoise(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

float wrap01(float phase) noexcept
{
    return phase >= 1.0f ? phase - 1.0f : phase;
}
} // namespace

// ------------------------------------------------------------- primitives
float InternalSynth::Envelope::next() noexcept
{
    switch (stage)
    {
        case Stage::off:
            value = 0.0f;
            break;
        case Stage::attack:
            value += attackStep;
            if (value >= 1.0f)
            {
                value = 1.0f;
                stage = Stage::decay;
            }
            break;
        case Stage::decay:
            value = sustain + (value - sustain) * decayCoeff;
            if (std::abs(value - sustain) < 1.0e-5f)
            {
                value = sustain;
                stage = Stage::sustain;
            }
            break;
        case Stage::sustain:
            value = sustain;
            break;
        case Stage::release:
            value *= releaseCoeff;
            if (value < silenceFloor)
            {
                value = 0.0f;
                stage = Stage::off;
            }
            break;
    }
    return value;
}

float InternalSynth::Svf::lowpass(float input, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

float InternalSynth::polyBlep(float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// Output stage. The voices are summed at a fixed headroom gain (about
// -4.4 dB) so a full chord sits well below full scale, then a peak limiter
// holds every sample at or under -3 dBFS. The limiter reacts instantly (no
// look-ahead, so no added latency) and recovers over 150 ms, which keeps
// dense chords clean instead of folding them into a saturating shoulder.
namespace
{
constexpr float outputHeadroom = 0.6f;
constexpr float limiterCeiling = 0.7079f; // -3 dBFS
constexpr float limiterReleaseSeconds = 0.15f;
}

float InternalSynth::limit(float x) noexcept
{
    const float magnitude = std::abs(x * outputHeadroom);
    const float target = magnitude > limiterCeiling ? limiterCeiling / magnitude : 1.0f;
    if (target < limiterGain_)
        limiterGain_ = target;
    else
        limiterGain_ = target + (limiterGain_ - target) * limiterRelease_;
    return std::clamp(x * outputHeadroom * limiterGain_, -limiterCeiling, limiterCeiling);
}

// Multiplier per sample that decays by 80 dB over the given time.
float InternalSynth::coeffForTime(float seconds) const noexcept
{
    return std::exp(std::log(silenceFloor) / std::max(1.0f, seconds * sampleRateF_));
}

// ------------------------------------------------------------- lifecycle
void InternalSynth::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    sampleRateF_ = static_cast<float>(sampleRate_);
    scratch_.assign(static_cast<std::size_t>(std::max(maxBlockSize, 32)), 0.0f);
    levelSmoothed_.reset(sampleRate_, 0.03);
    limiterRelease_ = std::exp(-1.0f / (limiterReleaseSeconds * sampleRateF_));
    for (std::size_t index = 0; index < voices_.size(); ++index)
        voices_[index].noise = 0x9E3779B9u * static_cast<std::uint32_t>(index + 1);
    reset();
}

void InternalSynth::reset() noexcept
{
    for (auto& voice : voices_)
    {
        const auto seed = voice.noise;
        voice = Voice{};
        voice.noise = seed;
    }
    eventCount_ = 0;
    held_ = {};
    heldCount_ = {};
    struckThisBatch_ = {};
    monoDirty_ = false;
    monoVoice_ = -1;
    limiterGain_ = 1.0f;
    levelSmoothed_.setCurrentAndTargetValue(level_.load(std::memory_order_relaxed));
}

void InternalSynth::setSound(Sound sound) noexcept
{
    sound_.store(sound, std::memory_order_relaxed);
}

void InternalSynth::setLevel(float linearGain) noexcept
{
    level_.store(std::isfinite(linearGain) ? std::clamp(linearGain, 0.0f, 1.0f) : 0.0f,
                 std::memory_order_relaxed);
}

void InternalSynth::setBassLowestNoteOnly(bool enabled) noexcept
{
    bassLowestOnly_.store(enabled, std::memory_order_relaxed);
}

bool InternalSynth::isSilent() const noexcept
{
    return eventCount_ == 0 && activeVoiceCount() == 0;
}

int InternalSynth::activeVoiceCount() const noexcept
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& voice)
                                          { return voice.state != Voice::State::idle; }));
}

// ------------------------------------------------------------- events
void InternalSynth::noteOn(int channel, int note, float velocity01, int sampleOffset) noexcept
{
    if (note < 0 || note > 127)
        return;
    if (!(velocity01 > 0.0f))
    {
        noteOff(channel, note, sampleOffset);
        return;
    }
    Event event;
    event.kind = EventKind::noteOn;
    event.channel = static_cast<std::uint8_t>(std::clamp(channel, 1, 16) - 1);
    event.note = static_cast<std::uint8_t>(note);
    event.velocity = std::min(velocity01, 1.0f);
    event.offset = std::max(sampleOffset, 0);
    enqueue(event);
}

void InternalSynth::noteOff(int channel, int note, int sampleOffset) noexcept
{
    if (note < 0 || note > 127)
        return;
    Event event;
    event.kind = EventKind::noteOff;
    event.channel = static_cast<std::uint8_t>(std::clamp(channel, 1, 16) - 1);
    event.note = static_cast<std::uint8_t>(note);
    event.offset = std::max(sampleOffset, 0);
    enqueue(event);
}

void InternalSynth::allNotesOff() noexcept
{
    eventCount_ = 0;
    releaseAll();
}

// Keeps the queue ordered by offset; equal offsets stay in arrival order.
void InternalSynth::enqueue(const Event& event) noexcept
{
    if (eventCount_ >= maxEvents)
    {
        // A dropped note-off would hang a note; apply it early instead.
        if (event.kind != EventKind::noteOn)
            applyEvent(event);
        return;
    }
    int index = eventCount_++;
    while (index > 0 && events_[static_cast<std::size_t>(index - 1)].offset > event.offset)
    {
        events_[static_cast<std::size_t>(index)] = events_[static_cast<std::size_t>(index - 1)];
        --index;
    }
    events_[static_cast<std::size_t>(index)] = event;
}

void InternalSynth::applyEvent(const Event& event) noexcept
{
    switch (event.kind)
    {
        case EventKind::noteOn: handleNoteOn(event.channel, event.note, event.velocity); break;
        case EventKind::noteOff: handleNoteOff(event.channel, event.note); break;
        case EventKind::allNotesOff: releaseAll(); break;
        case EventKind::allSoundOff: fadeAll(); break;
    }
}

void InternalSynth::handleNoteOn(int channel, int note, float velocity) noexcept
{
    const auto c = static_cast<std::size_t>(channel);
    const auto n = static_cast<std::size_t>(note);
    if (!held_[c][n])
    {
        held_[c][n] = true;
        ++heldCount_[n];
    }
    heldVelocity_[n] = velocity;

    if (blockSound_ == Sound::bass && blockBassMono_)
    {
        struckThisBatch_[n] = true;
        monoDirty_ = true;
        return;
    }

    // Re-striking a sounding key releases the old voice and starts a new one.
    for (auto& voice : voices_)
        if (voice.state == Voice::State::held && !voice.mono
            && voice.channel == channel && voice.note == note)
            releaseVoice(voice);
    triggerVoice(channel, note, velocity, blockSound_, false);
}

void InternalSynth::handleNoteOff(int channel, int note) noexcept
{
    const auto c = static_cast<std::size_t>(channel);
    const auto n = static_cast<std::size_t>(note);
    if (held_[c][n])
    {
        held_[c][n] = false;
        if (heldCount_[n] > 0)
            --heldCount_[n];
    }

    for (auto& voice : voices_)
    {
        if (voice.state == Voice::State::held && !voice.mono
            && voice.channel == channel && voice.note == note)
            releaseVoice(voice);
        else if (voice.state == Voice::State::stealing && voice.hasPending && !voice.pendingMono
                 && voice.pending.channel == channel && voice.pending.note == note)
            voice.hasPending = false; // released before it could start
    }
    if (monoVoice_ >= 0)
        monoDirty_ = true;
}

void InternalSynth::releaseAll() noexcept
{
    for (auto& voice : voices_)
        releaseVoice(voice);
    held_ = {};
    heldCount_ = {};
    struckThisBatch_ = {};
    monoDirty_ = false;
    monoVoice_ = -1;
}

void InternalSynth::fadeAll() noexcept
{
    const float step = 1.0f / std::max(1.0f, soundOffFadeSeconds * sampleRateF_);
    for (auto& voice : voices_)
    {
        if (voice.state == Voice::State::idle)
            continue;
        voice.state = Voice::State::stealing;
        voice.hasPending = false;
        voice.stealStep = std::max(voice.stealStep, step);
    }
    held_ = {};
    heldCount_ = {};
    struckThisBatch_ = {};
    monoDirty_ = false;
    monoVoice_ = -1;
}

int InternalSynth::lowestHeldNote() const noexcept
{
    for (int note = 0; note < 128; ++note)
        if (heldCount_[static_cast<std::size_t>(note)] > 0)
            return note;
    return -1;
}

// Runs once all events of one sample position are applied, so a chord change
// (offs + ons at the same offset) retriggers the bass once instead of gliding
// through intermediate notes.
void InternalSynth::resolveMono() noexcept
{
    monoDirty_ = false;
    const int lowest = lowestHeldNote();
    const bool startAllowed = blockSound_ == Sound::bass && blockBassMono_;
    bool anyStruck = false;
    for (bool struck : struckThisBatch_)
        anyStruck = anyStruck || struck;

    if (!startAllowed && monoVoice_ >= 0)
    {
        // The sound moved away from mono bass: the old bass note no longer
        // follows other keys and simply ends with its own key.
        auto& voice = voices_[static_cast<std::size_t>(monoVoice_)];
        const bool pendingStart = voice.state == Voice::State::stealing && voice.hasPending;
        const int current = pendingStart ? voice.pending.note : voice.note;
        if (heldCount_[static_cast<std::size_t>(current)] == 0)
        {
            releaseVoice(voice);
            monoVoice_ = -1;
        }
    }
    else if (lowest < 0)
    {
        if (monoVoice_ >= 0)
            releaseVoice(voices_[static_cast<std::size_t>(monoVoice_)]);
        monoVoice_ = -1;
    }
    else if (monoVoice_ >= 0)
    {
        auto& voice = voices_[static_cast<std::size_t>(monoVoice_)];
        const bool pendingStart = voice.state == Voice::State::stealing && voice.hasPending;
        const int current = pendingStart ? voice.pending.note : voice.note;
        if (startAllowed && struckThisBatch_[static_cast<std::size_t>(lowest)])
        {
            if (pendingStart)
            {
                voice.pending.note = static_cast<std::uint8_t>(lowest);
                voice.pending.velocity = heldVelocity_[static_cast<std::size_t>(lowest)];
            }
            else
            {
                releaseVoice(voice);
                monoVoice_ = triggerVoice(0, lowest, heldVelocity_[static_cast<std::size_t>(lowest)],
                                          Sound::bass, true);
            }
        }
        else if (lowest != current)
        {
            if (pendingStart)
            {
                voice.pending.note = static_cast<std::uint8_t>(lowest);
            }
            else
            {
                voice.note = static_cast<std::uint8_t>(lowest);
                voice.targetFrequency = noteToFrequency(lowest);
                voice.filterEnv = std::max(voice.filterEnv, 0.5f);
            }
        }
    }
    else if (startAllowed && anyStruck)
    {
        monoVoice_ = triggerVoice(0, lowest, heldVelocity_[static_cast<std::size_t>(lowest)],
                                  Sound::bass, true);
    }
    struckThisBatch_ = {};
}

// ------------------------------------------------------------- voices
int InternalSynth::allocateVoice() noexcept
{
    int best = -1;
    for (int index = 0; index < maxVoices; ++index)
        if (voices_[static_cast<std::size_t>(index)].state == Voice::State::idle)
            return index;

    // Quietest releasing voice, then the oldest held one, then the oldest
    // voice that is already fading out for another steal.
    float quietest = 2.0f;
    for (int index = 0; index < maxVoices; ++index)
    {
        const auto& voice = voices_[static_cast<std::size_t>(index)];
        if (voice.state == Voice::State::released && voice.amp.value < quietest)
        {
            quietest = voice.amp.value;
            best = index;
        }
    }
    if (best >= 0)
        return best;

    for (const auto wanted : { Voice::State::held, Voice::State::stealing })
    {
        std::uint64_t oldest = UINT64_MAX;
        for (int index = 0; index < maxVoices; ++index)
        {
            const auto& voice = voices_[static_cast<std::size_t>(index)];
            if (voice.state == wanted && voice.age < oldest)
            {
                oldest = voice.age;
                best = index;
            }
        }
        if (best >= 0)
            return best;
    }
    return 0;
}

int InternalSynth::triggerVoice(int channel, int note, float velocity, Sound sound, bool mono) noexcept
{
    const int index = allocateVoice();
    auto& voice = voices_[static_cast<std::size_t>(index)];
    if (index == monoVoice_)
        monoVoice_ = -1;

    if (voice.state == Voice::State::idle)
    {
        startVoice(voice, channel, note, velocity, sound, mono);
        return index;
    }

    // Steal: fade the old sound out quickly, then start the new note.
    if (voice.state != Voice::State::stealing)
    {
        voice.state = Voice::State::stealing;
        voice.stealStep = 1.0f / std::max(1.0f, stealFadeSeconds * sampleRateF_);
    }
    voice.hasPending = true;
    voice.pending.channel = static_cast<std::uint8_t>(channel);
    voice.pending.note = static_cast<std::uint8_t>(note);
    voice.pending.velocity = velocity;
    voice.pendingSound = sound;
    voice.pendingMono = mono;
    voice.age = ++ageCounter_;
    return index;
}

void InternalSynth::releaseVoice(Voice& voice) noexcept
{
    if (voice.state == Voice::State::held)
    {
        voice.state = Voice::State::released;
        if (voice.amp.stage != Envelope::Stage::off)
            voice.amp.stage = Envelope::Stage::release;
    }
    else if (voice.state == Voice::State::stealing)
    {
        voice.hasPending = false;
    }
}

void InternalSynth::startVoice(Voice& voice, int channel, int note, float velocity,
                               Sound sound, bool mono) noexcept
{
    const auto seed = voice.noise;
    voice = Voice{};
    voice.noise = seed;
    voice.state = Voice::State::held;
    voice.sound = sound;
    voice.mono = mono;
    voice.channel = static_cast<std::uint8_t>(channel);
    voice.note = static_cast<std::uint8_t>(note);
    voice.velocity = velocity;
    voice.age = ++ageCounter_;
    voice.frequency = voice.targetFrequency = noteToFrequency(note);
    for (auto& phase : voice.phase)
        phase = 0.5f * (nextNoise(voice.noise) + 1.0f) * 0.999f;

    const float f = voice.frequency;
    auto& amp = voice.amp;
    amp.stage = Envelope::Stage::attack;
    amp.value = 0.0f;

    switch (sound)
    {
        case Sound::synth:
        {
            amp.attackStep = 1.0f / std::max(1.0f, 0.012f * sampleRateF_);
            amp.decayCoeff = coeffForTime(0.8f);
            amp.sustain = 0.72f;
            amp.releaseCoeff = coeffForTime(0.45f);
            voice.filterEnv = 1.0f;
            voice.filterEnvCoeff = coeffForTime(1.6f);
            voice.cutoffBase = 650.0f + 1.5f * f + 1400.0f * velocity;
            voice.cutoffDepth = 900.0f + 2600.0f * velocity;
            voice.gain = 0.18f * (0.35f + 0.65f * velocity);
            break;
        }
        case Sound::bass:
        {
            amp.attackStep = 1.0f / std::max(1.0f, 0.004f * sampleRateF_);
            amp.decayCoeff = coeffForTime(0.7f);
            amp.sustain = 0.8f;
            amp.releaseCoeff = coeffForTime(0.14f);
            voice.phase[0] = 0.0f;
            voice.filterEnv = 1.0f;
            voice.filterEnvCoeff = coeffForTime(0.6f);
            voice.cutoffBase = 120.0f + 2.0f * f;
            voice.cutoffDepth = 350.0f + 2800.0f * velocity;
            voice.gain = (mono ? 0.42f : 0.24f) * (0.5f + 0.5f * velocity);
            break;
        }
        case Sound::piano:
        {
            amp.attackStep = 1.0f / std::max(1.0f, 0.0015f * sampleRateF_);
            amp.decayCoeff = 0.0f;
            amp.sustain = 1.0f;
            const float damper = std::clamp(0.55f - 0.006f * static_cast<float>(note - 36), 0.22f, 0.55f);
            amp.releaseCoeff = coeffForTime(damper);

            // Lower strings ring longer; upper partials die faster than the
            // fundamental and velocity opens up the spectrum.
            const float ringTime = std::clamp(9.0f * std::exp2(-static_cast<float>(note - 36) / 22.0f), 1.0f, 10.0f);
            const float exponent = 2.5f - 1.4f * velocity;
            const float inharmonicity = 0.00015f * std::exp2(static_cast<float>(note - 60) / 24.0f);
            const float nyquistGuard = std::min(0.42f * sampleRateF_, 16000.0f);
            float sum = 0.0f;
            int count = 0;
            for (int k = 1; k <= pianoPartials; ++k)
            {
                const auto kf = static_cast<float>(k);
                const float fk = f * kf * std::sqrt(1.0f + inharmonicity * kf * kf);
                if (fk >= nyquistGuard)
                    break;
                const float rolloff = 1.0f / (1.0f + (fk / 4500.0f) * (fk / 4500.0f));
                const float a = std::pow(kf, -exponent) * rolloff;
                const float slowTime = ringTime / (1.0f + 0.45f * (kf - 1.0f));
                const auto i = static_cast<std::size_t>(count);
                const float w = twoPi * fk / sampleRateF_;
                voice.rotC[i] = std::cos(w);
                voice.rotS[i] = std::sin(w);
                voice.re[i] = 1.0f;
                voice.im[i] = 0.0f;
                voice.fastAmp[i] = 0.55f * a;
                voice.slowAmp[i] = 0.45f * a;
                voice.fastCoeff[i] = coeffForTime(0.12f * slowTime);
                voice.slowCoeff[i] = coeffForTime(slowTime);
                sum += a;
                ++count;
            }
            voice.activePartials = count;
            const float normalise = sum > 0.0f ? 1.3f / sum : 0.0f;
            for (int i = 0; i < count; ++i)
            {
                voice.fastAmp[static_cast<std::size_t>(i)] *= normalise;
                voice.slowAmp[static_cast<std::size_t>(i)] *= normalise;
            }
            voice.hammer = 0.6f * velocity * velocity + 0.1f;
            voice.hammerCoeff = coeffForTime(0.035f);
            voice.hammerLpCoeff = 1.0f - std::exp(-twoPi * std::min(1200.0f + 3800.0f * velocity, 0.4f * sampleRateF_) / sampleRateF_);
            voice.gain = 0.21f * (0.1f + 0.9f * std::pow(velocity, 1.3f));
            break;
        }
    }
}

float InternalSynth::renderSynth(Voice& voice) noexcept
{
    const float dtUp = voice.frequency * detuneRatio / sampleRateF_;
    const float dtDown = voice.frequency / detuneRatio / sampleRateF_;
    const float dtSub = 0.5f * voice.frequency / sampleRateF_;

    auto& p = voice.phase;
    const float sawA = 2.0f * p[0] - 1.0f - polyBlep(p[0], dtUp);
    const float sawB = 2.0f * p[1] - 1.0f - polyBlep(p[1], dtDown);
    const float sub = std::sin(twoPi * p[2]);
    p[0] = wrap01(p[0] + dtUp);
    p[1] = wrap01(p[1] + dtDown);
    p[2] = wrap01(p[2] + dtSub);

    voice.filterEnv *= voice.filterEnvCoeff;
    const float cutoff = std::clamp(voice.cutoffBase + voice.cutoffDepth * voice.filterEnv,
                                    30.0f, 0.45f * sampleRateF_);
    const float g = std::tan(kPi * cutoff / sampleRateF_);
    return voice.filterA.lowpass(0.5f * (sawA + sawB) + 0.28f * sub, g, 1.2f);
}

float InternalSynth::renderBass(Voice& voice) noexcept
{
    // Short glide for legato moves of the lowest note.
    const float glide = 1.0f - std::exp(-1.0f / (0.025f * sampleRateF_));
    voice.frequency += (voice.targetFrequency - voice.frequency) * glide;
    const float dt = voice.frequency / sampleRateF_;

    float& p = voice.phase[0];
    const float saw = 2.0f * p - 1.0f - polyBlep(p, dt);
    const float square = (p < 0.5f ? 1.0f : -1.0f) + polyBlep(p, dt) - polyBlep(wrap01(p + 0.5f), dt);
    const float sine = std::sin(twoPi * p);
    p = wrap01(p + dt);

    voice.filterEnv *= voice.filterEnvCoeff;
    const float cutoff = std::clamp(voice.cutoffBase + voice.cutoffDepth * voice.filterEnv,
                                    30.0f, 0.45f * sampleRateF_);
    const float g = std::tan(kPi * cutoff / sampleRateF_);
    const float filtered = voice.filterB.lowpass(voice.filterA.lowpass(0.55f * saw + 0.3f * square, g, 1.0f), g, 1.4f);
    return filtered + 0.6f * sine;
}

float InternalSynth::renderPiano(Voice& voice) noexcept
{
    float sum = 0.0f;
    for (int k = 0; k < voice.activePartials; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        const float re = voice.re[i] * voice.rotC[i] - voice.im[i] * voice.rotS[i];
        const float im = voice.re[i] * voice.rotS[i] + voice.im[i] * voice.rotC[i];
        voice.re[i] = re;
        voice.im[i] = im;
        sum += im * (voice.fastAmp[i] + voice.slowAmp[i]);
        voice.fastAmp[i] *= voice.fastCoeff[i];
        voice.slowAmp[i] *= voice.slowCoeff[i];
    }

    // Hammer thump: band-limited noise burst.
    if (voice.hammer > 0.0f)
    {
        const float noise = nextNoise(voice.noise);
        voice.hammerLp += (noise - voice.hammerLp) * voice.hammerLpCoeff;
        voice.hammerLp2 += (voice.hammerLp - voice.hammerLp2) * 0.05f;
        sum += (voice.hammerLp - voice.hammerLp2) * voice.hammer;
        voice.hammer *= voice.hammerCoeff;
        if (voice.hammer < silenceFloor)
            voice.hammer = 0.0f;
    }

    // Keep the phasors on the unit circle and end fully decayed notes.
    if (++voice.renormCounter >= 64)
    {
        voice.renormCounter = 0;
        float remaining = voice.hammer;
        for (int k = 0; k < voice.activePartials; ++k)
        {
            const auto i = static_cast<std::size_t>(k);
            const float scale = 1.5f - 0.5f * (voice.re[i] * voice.re[i] + voice.im[i] * voice.im[i]);
            voice.re[i] *= scale;
            voice.im[i] *= scale;
            remaining += voice.fastAmp[i] + voice.slowAmp[i];
        }
        if (remaining < silenceFloor)
            voice.amp.stage = Envelope::Stage::off;
    }
    return sum;
}

void InternalSynth::renderVoice(Voice& voice, float* output, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (voice.state == Voice::State::idle)
            return;

        const float envelope = voice.amp.next();
        float sample = 0.0f;
        switch (voice.sound)
        {
            case Sound::synth: sample = renderSynth(voice); break;
            case Sound::piano: sample = renderPiano(voice); break;
            case Sound::bass: sample = renderBass(voice); break;
        }
        sample *= envelope * voice.gain;

        if (voice.state == Voice::State::stealing)
        {
            sample *= voice.stealGain;
            voice.stealGain -= voice.stealStep;
            if (voice.stealGain <= 0.0f)
            {
                if (voice.hasPending)
                {
                    const auto pending = voice.pending;
                    startVoice(voice, pending.channel, pending.note, pending.velocity,
                               voice.pendingSound, voice.pendingMono);
                }
                else
                {
                    voice.state = Voice::State::idle;
                }
            }
        }
        else if (voice.amp.stage == Envelope::Stage::off)
        {
            voice.state = Voice::State::idle;
        }
        output[i] += sample;
    }
}

// ------------------------------------------------------------- rendering
void InternalSynth::render(const AudioBlock& buffer, int startSample, int numSamples) noexcept
{
    if (sampleRate_ <= 0.0 || numSamples <= 0 || scratch_.empty())
        return;
    const ScopedNoDenormals noDenormals;

    blockSound_ = sound_.load(std::memory_order_relaxed);
    blockBassMono_ = bassLowestOnly_.load(std::memory_order_relaxed);
    levelSmoothed_.setTargetValue(level_.load(std::memory_order_relaxed));

    const int channels = buffer.numChannels;
    const int capacity = static_cast<int>(scratch_.size());
    int eventIndex = 0;
    int position = 0;
    while (position < numSamples)
    {
        while (eventIndex < eventCount_ && events_[static_cast<std::size_t>(eventIndex)].offset <= position)
            applyEvent(events_[static_cast<std::size_t>(eventIndex++)]);
        if (monoDirty_)
            resolveMono();

        int end = std::min(numSamples, position + capacity);
        if (eventIndex < eventCount_)
            end = std::min(end, events_[static_cast<std::size_t>(eventIndex)].offset);
        const int length = end - position;

        float* mono = scratch_.data();
        std::fill(mono, mono + length, 0.0f);
        for (auto& voice : voices_)
            if (voice.state != Voice::State::idle)
                renderVoice(voice, mono, length);

        if (monoVoice_ >= 0)
        {
            const auto& voice = voices_[static_cast<std::size_t>(monoVoice_)];
            const bool live = (voice.state == Voice::State::held && voice.mono)
                || (voice.state == Voice::State::stealing && voice.hasPending && voice.pendingMono);
            if (!live)
                monoVoice_ = -1;
        }

        for (int i = 0; i < length; ++i)
            mono[i] = limit(mono[i] * levelSmoothed_.getNextValue());
        for (int channel = 0; channel < channels; ++channel)
            buffer.addFrom(channel, startSample + position, mono, length);
        position = end;
    }

    // Events beyond this render belong to the next one.
    int kept = 0;
    for (int index = eventIndex; index < eventCount_; ++index)
    {
        auto event = events_[static_cast<std::size_t>(index)];
        event.offset -= numSamples;
        events_[static_cast<std::size_t>(kept++)] = event;
    }
    eventCount_ = kept;
}

void InternalSynth::process(const AudioBlock& buffer, const midi::MidiEventList& midi) noexcept
{
    const int numSamples = buffer.numSamples;
    if (numSamples <= 0)
        return;

    for (const auto& message : midi)
    {
        const int offset = std::clamp(message.sampleOffset, 0, numSamples - 1);
        if (message.isNoteOn())
        {
            noteOn(message.channel(), message.noteNumber(), message.floatVelocity(), offset);
        }
        else if (message.isNoteOff())
        {
            noteOff(message.channel(), message.noteNumber(), offset);
        }
        else if (message.isAllSoundOff() || message.isAllNotesOff())
        {
            Event event;
            event.kind = message.isAllSoundOff() ? EventKind::allSoundOff : EventKind::allNotesOff;
            event.offset = offset;
            enqueue(event);
        }
    }
    render(buffer, 0, numSamples);
}
} // namespace chordengine::audio
