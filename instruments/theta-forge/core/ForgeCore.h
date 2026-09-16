#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The reusable sound engine. It deliberately owns no AudioProcessor, UI,
// Tracktion, state tree, filesystem, or allocation in renderSample().
//
// Forge is a synthesiser only. Effects live outside this file and, until the
// FX rack milestone, do not exist at all. See PLAN.md.
namespace theta::forge
{
// Field order is the order Processor::patch() builds them in; that initialiser
// is positional, so keep the two in step.
struct Patch
{
    float oscAEnable = 1.0f, oscAPosition = 0.55f, unison = 2.0f, detune = 0.18f;
    float oscBEnable = 1.0f, oscBPosition = 0.18f, oscBLevel = 0.25f, oscBTune = 7.0f;
    float subEnable = 1.0f, subLevel = 0.12f;
    float noiseEnable = 0.0f, noiseLevel = 0.0f;
    float filterEnable = 1.0f, cutoff = 7800.0f, resonance = 0.12f, drive = 0.08f;
    float attack = 0.01f, decay = 0.24f, sustain = 0.75f, release = 0.35f;
    float lfoRate = 0.5f, lfoCutoff = 0.0f, lfoPosition = 0.0f, lfoPitch = 0.0f;
    float polyphony = 8.0f, mono = 0.0f, legato = 1.0f, glide = 0.08f;
    float output = 0.75f;
};

inline bool on(float enable) { return enable >= 0.5f; }

class Core final
{
public:
    void initialise(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        reset();
    }

    void reset()
    {
        voices = {};
        nextVoice = 0;
        lfoPhase = 0.0f;
        noiseState = 0x9e3779b9u;
        heldCount = 0;
        monoMode = false;
    }

    void noteOn(int note, float velocity)
    {
        noteOn(note, velocity, Patch {});
    }

    void noteOn(int note, float velocity, const Patch& patch)
    {
        monoMode = patch.mono >= 0.5f;
        if (monoMode)
        {
            hold(note);
            auto& voice = voices[0];
            const auto continueEnvelope = voice.active && patch.legato >= 0.5f;
            if (!continueEnvelope)
                startVoice(voice, note, velocity);
            else
            {
                voice.note = note;
                voice.targetHz = noteFrequency(note);
                voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
            }
            if (patch.glide <= 0.0001f) voice.currentHz = voice.targetHz;
            return;
        }

        const auto voiceCount = static_cast<size_t>(juce::jlimit(1, static_cast<int>(voices.size()), juce::roundToInt(patch.polyphony)));
        auto& voice = voices[nextVoice++ % voiceCount];
        startVoice(voice, note, velocity);
    }

    void noteOff(int note)
    {
        if (monoMode)
        {
            releaseHeld(note);
            auto& voice = voices[0];
            if (voice.active && voice.note == note && heldCount > 0)
            {
                voice.note = heldNotes[static_cast<size_t>(heldCount - 1)];
                voice.targetHz = noteFrequency(voice.note);
                return;
            }
        }
        for (auto& voice : voices)
            if (voice.active && voice.note == note && voice.ampStage != EnvelopeStage::release)
            {
                voice.ampStage = EnvelopeStage::release;
                voice.ampReleaseStart = voice.ampEnvelope;
            }
    }

    void allNotesOff() { reset(); }

    void renderSample(const Patch& patch, float& left, float& right)
    {
        left = right = 0.0f;
        const auto lfo = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
        lfoPhase = wrap(lfoPhase + juce::jlimit(0.01f, 40.0f, patch.lfoRate) / static_cast<float>(sampleRate));

        // LFO 1 is the only thing that moves the cutoff, by design: Forge has a
        // single envelope and it is hardwired to amplitude. Everything else
        // reaches the cutoff once the modulation matrix exists.
        const auto lfoOctaves = juce::jlimit(-1.0f, 1.0f, patch.lfoCutoff) * lfo * 3.0f;
        const auto cutoff = patch.cutoff * std::pow(2.0f, lfoOctaves);

        for (auto& voice : voices)
        {
            if (!voice.active) continue;
            updateEnvelope(voice.ampEnvelope, voice.ampStage, voice.ampReleaseStart,
                           patch.attack, patch.decay, patch.sustain, patch.release);
            if (voice.ampStage == EnvelopeStage::idle)
            {
                voice.active = false;
                continue;
            }

            float dryLeft = 0.0f, dryRight = 0.0f;
            renderOscillators(voice, patch, lfo, dryLeft, dryRight);
            if (on(patch.filterEnable))
            {
                // Keep running the filter state even when nothing reaches it,
                // so switching the filter back on does not click.
                left += filter(dryLeft, voice.lowLeft, voice.bandLeft, cutoff, patch.resonance);
                right += filter(dryRight, voice.lowRight, voice.bandRight, cutoff, patch.resonance);
            }
            else
            {
                filter(dryLeft, voice.lowLeft, voice.bandLeft, cutoff, patch.resonance);
                filter(dryRight, voice.lowRight, voice.bandRight, cutoff, patch.resonance);
                left += dryLeft;
                right += dryRight;
            }
        }

        const auto driveGain = 1.0f + juce::jlimit(0.0f, 1.0f, patch.drive) * 12.0f;
        const auto compensation = 1.0f / std::tanh(driveGain);
        const auto gain = juce::jlimit(0.0f, 1.25f, patch.output) * 0.28f;
        left = std::tanh(left * driveGain) * compensation * gain;
        right = std::tanh(right * driveGain) * compensation * gain;
    }

private:
    enum class EnvelopeStage { idle, attack, decay, sustain, release };

    struct Voice
    {
        bool active = false;
        int note = 0;
        float velocity = 0.0f;
        std::array<float, 8> phaseA {}, phaseB {};
        float phaseSub = 0.0f;
        float currentHz = 0.0f, targetHz = 0.0f;
        float ampEnvelope = 0.0f, ampReleaseStart = 0.0f;
        float lowLeft = 0.0f, bandLeft = 0.0f, lowRight = 0.0f, bandRight = 0.0f;
        EnvelopeStage ampStage = EnvelopeStage::idle;
    };

    static float wrap(float phase) { return phase - std::floor(phase); }
    static float noteFrequency(int note) { return static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note)); }

    void startVoice(Voice& voice, int note, float velocity)
    {
        voice = {};
        voice.active = true;
        voice.note = note;
        voice.velocity = juce::jlimit(0.0f, 1.0f, velocity);
        voice.currentHz = voice.targetHz = noteFrequency(note);
        voice.ampStage = EnvelopeStage::attack;
        for (size_t i = 0; i < voice.phaseA.size(); ++i)
        {
            const auto offset = static_cast<float>(i) / static_cast<float>(voice.phaseA.size());
            voice.phaseA[i] = std::fmod(offset * 0.37f + static_cast<float>(note) * 0.013f, 1.0f);
            voice.phaseB[i] = std::fmod(offset * 0.61f + static_cast<float>(note) * 0.019f, 1.0f);
        }
    }

    void hold(int note)
    {
        releaseHeld(note);
        if (heldCount < static_cast<int>(heldNotes.size())) heldNotes[static_cast<size_t>(heldCount++)] = note;
    }

    void releaseHeld(int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (heldNotes[static_cast<size_t>(i)] == note)
            {
                for (int j = i; j + 1 < heldCount; ++j) heldNotes[static_cast<size_t>(j)] = heldNotes[static_cast<size_t>(j + 1)];
                --heldCount;
                return;
            }
    }

    static float morph(float phase, float position)
    {
        phase = wrap(phase);
        const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
        const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
        const auto saw = phase * 2.0f - 1.0f;
        const auto square = phase < 0.5f ? 1.0f : -1.0f;
        const float frames[] {sine, triangle, saw, square};
        const auto scaled = juce::jlimit(0.0f, 1.0f, position) * 3.0f;
        const auto index = std::min(2, static_cast<int>(scaled));
        return juce::jmap(scaled - static_cast<float>(index), frames[index], frames[index + 1]);
    }

    void updateEnvelope(float& value, EnvelopeStage& stage, float releaseStart,
                        float attack, float decay, float sustain, float release) const
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        switch (stage)
        {
            case EnvelopeStage::attack:
                value += dt / std::max(0.001f, attack);
                if (value >= 1.0f) { value = 1.0f; stage = EnvelopeStage::decay; }
                break;
            case EnvelopeStage::decay:
                value -= (1.0f - juce::jlimit(0.0f, 1.0f, sustain)) * dt / std::max(0.001f, decay);
                if (value <= sustain) { value = sustain; stage = EnvelopeStage::sustain; }
                break;
            case EnvelopeStage::sustain: value = sustain; break;
            case EnvelopeStage::release:
                value -= releaseStart * dt / std::max(0.001f, release);
                if (value <= 0.0001f) { value = 0.0f; stage = EnvelopeStage::idle; }
                break;
            case EnvelopeStage::idle: value = 0.0f; break;
        }
    }

    float noise()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<float>(noiseState & 0xffffu) / 32767.5f - 1.0f;
    }

    void renderOscillators(Voice& voice, const Patch& patch, float lfo, float& left, float& right)
    {
        const auto dt = static_cast<float>(1.0 / sampleRate);
        const auto glide = juce::jlimit(0.0f, 2.0f, patch.glide);
        if (glide <= 0.0001f) voice.currentHz = voice.targetHz;
        else voice.currentHz += (voice.targetHz - voice.currentHz)
            * (1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * glide)));
        const auto hz = voice.currentHz;
        const auto pitchRatio = std::pow(2.0f, juce::jlimit(-12.0f, 12.0f, patch.lfoPitch) * lfo / 12.0f);
        const auto hzB = hz * std::pow(2.0f, patch.oscBTune / 12.0f);
        const auto positionA = juce::jlimit(0.0f, 1.0f, patch.oscAPosition + lfo * patch.lfoPosition * 0.5f);
        const auto positionB = juce::jlimit(0.0f, 1.0f, patch.oscBPosition + lfo * patch.lfoPosition * 0.5f);
        const auto count = juce::jlimit(1, 8, juce::roundToInt(patch.unison));
        const auto oscA = on(patch.oscAEnable), oscB = on(patch.oscBEnable);
        // With both oscillators live, B's level crossfades against A, as it
        // always has. Switching one off gives the survivor the whole voice
        // rather than leaving a hole where the crossfade partner was. The
        // per-oscillator levels that make this unnecessary arrive with M3.
        const auto levelA = oscA ? (oscB ? 1.0f - patch.oscBLevel : 1.0f) : 0.0f;
        const auto levelB = oscB ? (oscA ? patch.oscBLevel : 1.0f) : 0.0f;
        left = right = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto spread = count == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1) - 0.5f;
            const auto detuneSemitones = spread * juce::jlimit(0.0f, 1.0f, patch.detune) * 0.7f;
            const auto ratio = std::pow(2.0f, detuneSemitones / 12.0f);
            const auto oscillator = morph(voice.phaseA[static_cast<size_t>(i)], positionA) * levelA
                + morph(voice.phaseB[static_cast<size_t>(i)], positionB) * levelB;
            const auto pan = spread * juce::jlimit(0.0f, 1.0f, patch.detune) * 1.6f;
            left += oscillator * std::sqrt(0.5f * (1.0f - pan));
            right += oscillator * std::sqrt(0.5f * (1.0f + pan));
            voice.phaseA[static_cast<size_t>(i)] = wrap(voice.phaseA[static_cast<size_t>(i)] + hz * pitchRatio * ratio * dt);
            voice.phaseB[static_cast<size_t>(i)] = wrap(voice.phaseB[static_cast<size_t>(i)] + hzB * pitchRatio * ratio * dt);
        }
        // The sub and the noise generator are their own sources: each is silent
        // unless its own module is on, whatever its level knob reads.
        const auto sub = on(patch.subEnable) ? std::sin(voice.phaseSub * juce::MathConstants<float>::twoPi) * patch.subLevel : 0.0f;
        const auto hiss = on(patch.noiseEnable) ? noise() * patch.noiseLevel : 0.0f;
        const auto centre = sub + hiss;
        const auto level = voice.ampEnvelope * voice.velocity / std::sqrt(static_cast<float>(count));
        left = (left + centre) * level;
        right = (right + centre) * level;
        voice.phaseSub = wrap(voice.phaseSub + hz * pitchRatio * 0.5f * dt);
    }

    float filter(float input, float& low, float& band, float cutoff, float resonance) const
    {
        const auto limitedCutoff = juce::jlimit(25.0f, static_cast<float>(sampleRate * 0.3), cutoff);
        const auto g = std::tan(juce::MathConstants<float>::pi * limitedCutoff / static_cast<float>(sampleRate));
        const auto damping = 1.0f / (1.0f + juce::jlimit(0.0f, 1.0f, resonance) * 15.0f);
        const auto high = (input - 2.0f * damping * band - low) / (1.0f + 2.0f * damping * g + g * g);
        band += g * high;
        low += g * band;
        return low;
    }

    std::array<Voice, 16> voices {};
    double sampleRate = 48000.0;
    size_t nextVoice = 0;
    float lfoPhase = 0.0f;
    std::uint32_t noiseState = 0x9e3779b9u;
    std::array<int, 16> heldNotes {};
    int heldCount = 0;
    bool monoMode = false;
};
}
