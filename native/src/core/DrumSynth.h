#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <optional>

namespace rhino
{
// The drums the Drum Rack synthesises rather than reads from a file: what a pad
// plays when it holds a synth instead of a sample.
//
// Each model is the idea behind an analog drum machine's voice, kept small. A
// kick is a sine whose pitch falls as it starts, a snare two tuned drumheads
// under a burst of filtered noise, a hat six square waves at unrelated pitches
// through a high-pass, a clap three quick bursts of band-passed noise and a
// tail. Three controls reach every model, the same three a sample pad has:
// Tune moves its pitch, Decay is the time it takes to fall 60 dB, and Tone is
// how bright it is.
//
// Pure C++ in RhinoCore, so a test can strike one and measure what comes out.
enum class DrumModel : int
{
    Kick,
    Snare,
    Tom,
    ClosedHat,
    OpenHat,
    Clap,
    Rim,
    Cowbell
};
inline constexpr int drumModelCount = 8;

struct DrumModelInfo
{
    // What a file stores. Never renamed.
    const char* id;
    // What the face and the browser call it.
    const char* name;
    // The kind of drum it is, as the browser files sounds
    // (ContentLibrary::drumTypes).
    const char* type;
    // Where a pad switched to the model starts its Decay and Tone.
    float decay;
    float tone;
};

const DrumModelInfo& drumModelInfo(DrumModel);
std::optional<DrumModel> drumModelFromId(const juce::String&);

// One strike of a model. It allocates nothing, so the audio thread plays it,
// and anything that wants to picture a pad can play one of its own.
class DrumSynthVoice
{
public:
    // tuneRatio is two to the power of the semitones over twelve, decaySeconds
    // the time to fall 60 dB, tone 0 to 1. The seed makes a strike's noise
    // repeat exactly, so a render does not depend on what played before it.
    void start(DrumModel, double sampleRate, float tuneRatio, float decaySeconds, float tone,
               std::uint32_t seed) noexcept;
    // The next sample, or silence once the strike has died away.
    float next() noexcept;
    bool active() const noexcept { return running; }

private:
    // A state-variable filter in Zavalishin's form, which stays stable while
    // its cutoff is anywhere below Nyquist.
    struct Filter
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        float low = 0.0f, band = 0.0f, high = 0.0f;
        void set(double sampleRate, float frequency, float q) noexcept;
        void run(float input) noexcept;
    };

    float noise() noexcept;
    float clampFrequency(float hertz) const noexcept;

    DrumModel model = DrumModel::Kick;
    bool running = false;
    double rate = 48000.0;
    float step = 1.0f / 48000.0f;
    int age = 0;
    int limit = 0;
    std::uint32_t seed = 1;

    // Up to six oscillators, each a phase in cycles and a frequency in Hz.
    std::array<float, 6> phase {}, frequency {};
    // The model's envelopes, each multiplied down a sample at a time.
    float body = 0.0f, bodyFactor = 1.0f;
    float second = 0.0f, secondFactor = 1.0f;
    float sweep = 0.0f, sweepFactor = 1.0f, sweepDepth = 0.0f;
    float click = 0.0f, clickFactor = 1.0f, clickLevel = 0.0f;
    float drive = 1.0f, driveNormal = 1.0f;
    float bodyLevel = 1.0f, noiseLevel = 0.0f;
    float noiseState = 0.0f, noiseCoefficient = 0.0f;
    // The clap's bursts and its tail.
    std::array<int, 3> bursts {};
    int tailStart = 0;
    Filter filter;
};
}
