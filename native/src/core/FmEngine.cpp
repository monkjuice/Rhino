#include "FmEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;
// ln(1000): a decay or release of t seconds closes 60 dB in t.
constexpr double sixtyDecibels = 6.907755278982137;
constexpr int tableBits = 12;
constexpr int tableSize = 1 << tableBits;
constexpr int fractionBits = 32 - tableBits;
constexpr float fractionScale = 1.0f / static_cast<float>(1u << fractionBits);

// One cycle of a sine, with a guard point so interpolation never wraps. With
// 4096 points and linear interpolation the error is below -140 dB.
const std::array<float, tableSize + 1>& sineTable()
{
    static const auto table = []
    {
        std::array<float, tableSize + 1> points {};
        for (int i = 0; i <= tableSize; ++i)
            points[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * pi * i / tableSize));
        return points;
    }();
    return table;
}

inline float sineAt(const float* table, std::uint32_t phase) noexcept
{
    const auto index = phase >> fractionBits;
    const auto fraction = static_cast<float>(phase & ((1u << fractionBits) - 1u)) * fractionScale;
    return table[index] + (table[index + 1] - table[index]) * fraction;
}

// Bit n of a mask is operator n, 0-based. modulators[op] names the operators
// that feed op; every such bit is above op's own.
struct Routing
{
    std::array<std::uint8_t, FmEngine::operators> modulators;
    std::uint8_t carriers;
    const char* name;
};

// The eight classic four-operator layouts. Read "4>3" as "4 modulates 3", "+"
// as modulators summed into one input, and "|" as outputs heard side by side.
constexpr std::array<Routing, FmEngine::algorithms> routings { {
    { { 0b0010, 0b0100, 0b1000, 0 }, 0b0001, "4>3>2>1" },
    { { 0b0010, 0b1100, 0, 0 }, 0b0001, "(4+3)>2>1" },
    { { 0b0110, 0, 0b1000, 0 }, 0b0001, "(4>3+2)>1" },
    { { 0b1110, 0, 0, 0 }, 0b0001, "(4+3+2)>1" },
    { { 0b0010, 0, 0b1000, 0 }, 0b0101, "4>3 | 2>1" },
    { { 0b1000, 0b1000, 0b1000, 0 }, 0b0111, "4>(3|2|1)" },
    { { 0, 0, 0b1000, 0 }, 0b0111, "4>3 | 2 | 1" },
    { { 0, 0, 0, 0 }, 0b1111, "4 | 3 | 2 | 1" },
} };

const Routing& routingFor(int algorithm)
{
    return routings[static_cast<size_t>(std::clamp(algorithm, 0, FmEngine::algorithms - 1))];
}

float coefficientFor(float seconds, double sampleRate)
{
    return seconds <= 0.0f ? 0.0f
                           : static_cast<float>(std::exp(-sixtyDecibels / (static_cast<double>(seconds) * sampleRate)));
}
}

// A decay keeps following the sustain level after it arrives, so moving
// Sustain while a note is held glides rather than jumps.
float FmEngine::stepEnvelope(Stage& stage, float& level, const Shape& shape) noexcept
{
    switch (stage)
    {
        case Stage::attack:
            level += shape.attackStep;
            if (level >= 1.0f)
            {
                level = 1.0f;
                stage = Stage::decay;
            }
            break;
        case Stage::decay:
            level = shape.sustain + (level - shape.sustain) * shape.decayCoefficient;
            break;
        case Stage::release:
            level *= shape.releaseCoefficient;
            if (level < 1.0e-5f)
            {
                level = 0.0f;
                stage = Stage::idle;
            }
            break;
        case Stage::idle:
            break;
    }
    return level;
}

bool FmEngine::modulates(int algorithm, int from, int to)
{
    if (from < 0 || from >= operators || to < 0 || to >= operators)
        return false;
    return (routingFor(algorithm).modulators[static_cast<size_t>(to)] & (1u << from)) != 0;
}

bool FmEngine::isCarrier(int algorithm, int op)
{
    return op >= 0 && op < operators && (routingFor(algorithm).carriers & (1u << op)) != 0;
}

int FmEngine::carrierCount(int algorithm)
{
    int count = 0;
    for (int op = 0; op < operators; ++op)
        count += isCarrier(algorithm, op) ? 1 : 0;
    return count;
}

const char* FmEngine::algorithmName(int algorithm)
{
    return routingFor(algorithm).name;
}

void FmEngine::picture(const Settings& source, float* peak, float* held)
{
    // 128 samples to a cycle of the note, so the picture is exactly two. At
    // this rate the taper and the guard both sit far above anything an A4
    // reaches, even at ratio 16.
    constexpr int cycle = pictureLength / 2;
    constexpr double rate = 440.0 * cycle;
    const auto play = [&source] (bool open, float* out)
    {
        auto settings = source;
        settings.gain = 1.0f;
        settings.mono = false;
        for (auto& op : settings.ops)
        {
            // With no attack and no decay, every envelope stands where it
            // will for the rest of the note within two samples.
            op.attack = 0.0f;
            op.decay = 0.0f;
            if (open)
                op.sustain = 1.0f;
        }
        FmEngine engine;
        engine.prepare(rate);
        engine.setSettings(settings);
        engine.noteOn(69, 1.0f);
        // Four cycles, for operator 4's feedback to settle.
        std::array<float, cycle * 4> settling {};
        engine.render(settling.data(), nullptr, static_cast<int>(settling.size()));
        std::fill(out, out + pictureLength, 0.0f);
        engine.render(out, nullptr, pictureLength);
    };
    play(true, peak);
    play(false, held);
}

void FmEngine::prepare(double rate)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    table = sineTable().data();
    gainSmoothing = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate)));
    taperFrequency = static_cast<float>(sampleRate / 24.0);
    nyquistGuard = 0.45 * sampleRate;
    gainPrimed = false;
    reset();
    applySettings();
}

void FmEngine::reset()
{
    for (auto& voice : voices)
        voice = Voice {};
    heldCount = 0;
    sustainDown = false;
    pitchBend = 0.0f;
    noteCounter = 0;
}

void FmEngine::setSettings(const Settings& next)
{
    // The first settings after prepare set the gain outright; later ones
    // glide to it, so a level change does not click.
    if (!gainPrimed)
    {
        gainNow = next.gain;
        gainPrimed = true;
    }
    // The device hands the same settings over at every stretch, and working
    // them out again costs a dozen exponentials and every voice's increments.
    if (next == settings)
        return;
    if (next.mono != settings.mono)
        releaseAll();
    settings = next;
    applySettings();
}

void FmEngine::applySettings()
{
    const auto& routing = routingFor(settings.algorithm);
    modulatorMask = routing.modulators;
    carrierMask = routing.carriers;
    carrierNorm = 1.0f / static_cast<float>(std::max(1, carrierCount(settings.algorithm)));
    feedbackAmount = std::clamp(settings.feedback, 0.0f, 1.0f) * feedbackCycles;
    glideStep = settings.glide > 0.0f
        ? static_cast<float>(1.0 - std::exp(-1.0 / (static_cast<double>(settings.glide) * sampleRate))) : 1.0f;
    const auto depth = std::clamp(settings.depth, 0.0f, 2.0f);
    for (int op = 0; op < operators; ++op)
    {
        const auto& source = settings.ops[static_cast<size_t>(op)];
        auto& shape = shapes[static_cast<size_t>(op)];
        const auto level = std::clamp(source.level, 0.0f, 1.0f);
        shape.attackStep = source.attack > 0.0f
            ? static_cast<float>(1.0 / (static_cast<double>(source.attack) * sampleRate)) : 1.0f;
        shape.decayCoefficient = coefficientFor(source.decay, sampleRate);
        shape.releaseCoefficient = coefficientFor(source.release, sampleRate);
        shape.sustain = std::clamp(source.sustain, 0.0f, 1.0f);
        shape.amplitude = level * level * (isCarrier(settings.algorithm, op) ? 1.0f : depth);
        shape.frequencyFactor = std::max(0.0f, source.ratio) * std::exp2(static_cast<double>(source.detuneCents) / 1200.0);
    }
    // Ratios, detune and the routing all reach a voice through its
    // increments and per-operator scales, so each works them out again.
    for (auto& voice : voices)
        voice.incrementsValid = false;
}

void FmEngine::pushHeld(int note)
{
    removeHeld(note);
    if (heldCount == static_cast<int>(held.size()))
    {
        std::move(held.begin() + 1, held.end(), held.begin());
        --heldCount;
    }
    held[static_cast<size_t>(heldCount++)] = note;
}

void FmEngine::removeHeld(int note)
{
    const auto end = held.begin() + heldCount;
    const auto found = std::find(held.begin(), end, note);
    if (found == end)
        return;
    std::move(found + 1, end, found);
    --heldCount;
}

void FmEngine::trigger(Voice& voice, int note, float velocity)
{
    const auto wasActive = voice.active;
    voice.active = true;
    voice.held = true;
    voice.sustained = false;
    voice.note = note;
    voice.targetPitch = static_cast<float>(note);
    voice.pitch = voice.targetPitch;
    const auto sensitivity = std::clamp(settings.velocity, 0.0f, 1.0f);
    voice.velocityScale = 1.0f - sensitivity + sensitivity * std::clamp(velocity, 0.0f, 1.0f);
    voice.started = ++noteCounter;
    voice.incrementsValid = false;
    for (int op = 0; op < operators; ++op)
    {
        voice.stage[static_cast<size_t>(op)] = Stage::attack;
        // A voice taken over while it sounds keeps its phase and rises from
        // where its envelope stood, so the handover does not click.
        if (!wasActive)
        {
            voice.envelope[static_cast<size_t>(op)] = 0.0f;
            voice.phase[static_cast<size_t>(op)] = 0;
        }
    }
    if (!wasActive)
        voice.feedbackA = voice.feedbackB = 0.0f;
}

void FmEngine::release(Voice& voice)
{
    voice.held = false;
    voice.sustained = false;
    for (auto& stage : voice.stage)
        if (stage != Stage::idle)
            stage = Stage::release;
}

void FmEngine::noteOn(int note, float velocity)
{
    if (settings.mono)
    {
        const auto legato = heldCount > 0 && voices[0].active && voices[0].held;
        pushHeld(note);
        auto& voice = voices[0];
        if (legato)
        {
            // Overlapping notes glide and keep the envelopes running.
            voice.note = note;
            voice.targetPitch = static_cast<float>(note);
            return;
        }
        trigger(voice, note, velocity);
        return;
    }

    // The same key struck again while it sounds takes its own voice back.
    for (auto& voice : voices)
        if (voice.active && voice.note == note && (voice.held || voice.sustained))
        {
            trigger(voice, note, velocity);
            return;
        }
    for (auto& voice : voices)
        if (!voice.active)
        {
            trigger(voice, note, velocity);
            return;
        }
    // Every voice is busy: take the oldest released one, or else the oldest.
    Voice* steal = nullptr;
    for (auto& voice : voices)
    {
        const auto released = !voice.held && !voice.sustained;
        const auto stealReleased = steal != nullptr && !steal->held && !steal->sustained;
        if (steal == nullptr || (released && !stealReleased)
            || (released == stealReleased && voice.started < steal->started))
            steal = &voice;
    }
    trigger(*steal, note, velocity);
}

void FmEngine::noteOff(int note)
{
    if (settings.mono)
    {
        removeHeld(note);
        auto& voice = voices[0];
        if (!voice.active || !voice.held)
            return;
        if (heldCount > 0)
        {
            // Back to the key still down, without a new attack.
            voice.note = held[static_cast<size_t>(heldCount - 1)];
            voice.targetPitch = static_cast<float>(voice.note);
            return;
        }
        if (sustainDown)
        {
            voice.held = false;
            voice.sustained = true;
        }
        else
        {
            release(voice);
        }
        return;
    }

    for (auto& voice : voices)
        if (voice.active && voice.held && voice.note == note)
        {
            if (sustainDown)
            {
                voice.held = false;
                voice.sustained = true;
            }
            else
            {
                release(voice);
            }
        }
}

void FmEngine::releaseAll()
{
    heldCount = 0;
    sustainDown = false;
    for (auto& voice : voices)
        if (voice.active)
            release(voice);
}

void FmEngine::setSustainPedal(bool down)
{
    sustainDown = down;
    if (down)
        return;
    for (auto& voice : voices)
        if (voice.active && voice.sustained)
            release(voice);
}

void FmEngine::setPitchBend(float semitones)
{
    pitchBend = semitones;
}

int FmEngine::activeVoices() const
{
    return static_cast<int>(std::count_if(voices.begin(), voices.end(), [] (const Voice& voice) { return voice.active; }));
}

void FmEngine::updateIncrements(Voice& voice, float pitch)
{
    const auto frequency = 440.0 * std::exp2((static_cast<double>(pitch) - 69.0) / 12.0);
    const auto taper = static_cast<float>(std::min(1.0, static_cast<double>(taperFrequency) / frequency));
    for (int op = 0; op < operators; ++op)
    {
        const auto index = static_cast<size_t>(op);
        const auto opFrequency = frequency * shapes[index].frequencyFactor;
        const auto audible = opFrequency < nyquistGuard;
        voice.increment[index] = audible ? static_cast<std::uint32_t>(opFrequency / sampleRate * 4294967296.0) : 0u;
        const auto role = (carrierMask & (1u << op)) != 0 ? 1.0f : taper;
        voice.amplitudeScale[index] = audible ? voice.velocityScale * role : 0.0f;
    }
    voice.incrementPitch = pitch;
    voice.incrementsValid = true;
}

float FmEngine::renderVoice(Voice& voice)
{
    if (voice.pitch != voice.targetPitch)
    {
        voice.pitch += (voice.targetPitch - voice.pitch) * glideStep;
        if (std::abs(voice.targetPitch - voice.pitch) < 1.0e-4f)
            voice.pitch = voice.targetPitch;
    }
    const auto pitch = voice.pitch + pitchBend;
    if (!voice.incrementsValid || pitch != voice.incrementPitch)
        updateIncrements(voice, pitch);

    std::array<float, operators> output {};
    auto carrierSounding = false;
    auto mix = 0.0f;
    for (int op = operators - 1; op >= 0; --op)
    {
        const auto index = static_cast<size_t>(op);
        const auto& shape = shapes[index];
        auto modulation = 0.0f;
        for (int from = op + 1; from < operators; ++from)
            if ((modulatorMask[index] & (1u << from)) != 0)
                modulation += output[static_cast<size_t>(from)];
        modulation *= modulationCycles;
        if (op == operators - 1)
            modulation += feedbackAmount * 0.5f * (voice.feedbackA + voice.feedbackB);
        voice.phase[index] += voice.increment[index];
        const auto offset = static_cast<std::uint32_t>(static_cast<std::int64_t>(modulation * 4294967296.0f));
        const auto sine = sineAt(table, voice.phase[index] + offset);
        const auto envelope = stepEnvelope(voice.stage[index], voice.envelope[index], shape);
        // Feedback follows the operator's own envelope but not its level or
        // the depth, so it reaches the same strength in every routing.
        if (op == operators - 1)
        {
            voice.feedbackB = voice.feedbackA;
            voice.feedbackA = sine * envelope;
        }
        const auto value = sine * envelope * shape.amplitude * voice.amplitudeScale[index];
        output[index] = value;
        if ((carrierMask & (1u << op)) != 0)
        {
            mix += value;
            carrierSounding = carrierSounding || voice.stage[index] != Stage::idle;
        }
    }
    // Checked every sample, so when a voice comes free cannot depend on where
    // a block happens to end.
    if (!carrierSounding)
        voice.active = false;
    return mix * carrierNorm;
}

void FmEngine::render(float* left, float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        gainNow += (settings.gain - gainNow) * gainSmoothing;
        auto mix = 0.0f;
        for (auto& voice : voices)
            if (voice.active)
                mix += renderVoice(voice);
        const auto out = mix * headroom * gainNow;
        left[i] += out;
        if (right != nullptr)
            right[i] += out;
    }
}
}
