#pragma once
#include "DrumSynth.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace rhino
{
// A pad's sound when it plays a file: decoded once, on the message thread, and
// never written again. The device reads the file; the engine plays it.
struct DrumSample
{
    std::vector<float> left, right;     // right is empty for a mono file
    double sampleRate = 44100.0;

    int length() const noexcept { return static_cast<int>(left.size()); }
    bool stereo() const noexcept { return !right.empty(); }

    // How many voices are playing it, so that a sample a pad has let go of is
    // freed only once the last of them has rung out. The audio thread counts
    // and the message thread reads.
    mutable std::atomic<int> playing { 0 };
};

// The Drum Rack's sound: sixteen pads on the notes from C2, each a sample or a
// synthesised drum shaped by its own six controls, and the voices that play
// them.
//
// Pure C++ in RhinoCore, as FmEngine is, so a test can strike pads and measure
// what comes back without an Edit or a graph. The device is a shell that turns
// its controls into Settings and its MIDI into calls.
//
// What a pad holds is set on the message thread and read by the audio thread
// through atomics, so neither ever waits for the other. The hard part is
// letting go of a sample nobody can hear any more without the audio thread
// still reading it. Every stretch of audio-thread work that can pick a sample
// up (a strike, a render) is counted on the way in and on the way out, and
// every voice counts itself onto the sample it plays. A sample a pad has let
// go of is freed once every stretch that began before the pad let go has
// finished -- nothing later can find it -- and its count of voices is back to
// zero, so nothing is still reading it.
class DrumRackEngine
{
public:
    static constexpr int padCount = 16;
    // The note the first pad plays: C2, the note editor's lowest row, so its
    // sixteen rows are the sixteen pads.
    static constexpr int lowestNote = 48;
    static constexpr int voiceCount = 32;
    static constexpr int chokeGroupCount = 4;
    // Decay at its top lets a sample play out to its own end.
    static constexpr float fullDecay = 10.0f;
    // Level at its bottom is silence.
    static constexpr float silentLevel = -48.0f;
    // The longest a sample file is read for. Past it, the sample ends in a
    // short fade rather than a cut.
    static constexpr double longestSampleSeconds = 10.0;

    enum class Source : int { empty, sample, synth };

    // A pad's six controls, as the device reads them.
    struct PadSettings
    {
        float tune = 0.0f;              // semitones
        float decay = fullDecay;        // seconds to fall 60 dB
        float tone = 1.0f;              // 0 dark to 1 open
        float velocity = 1.0f;          // how much quieter a soft note is, 0 to 1
        float level = 0.0f;             // dB
        float pan = 0.0f;               // -1 left to 1 right
        friend bool operator==(const PadSettings&, const PadSettings&) = default;
    };
    using Settings = std::array<PadSettings, padCount>;

    DrumRackEngine();
    ~DrumRackEngine();

    // ---- message thread ---------------------------------------------------
    // Never while rendering. Also frees every sample a pad has let go of.
    void prepare(double sampleRate);
    // What a pad plays. The engine owns a sample from here on, and a voice
    // still ringing on what the pad played before rings out.
    void setPadSample(int pad, std::unique_ptr<DrumSample>);
    void setPadSynth(int pad, DrumModel);
    void clearPad(int pad);
    // 0 is no group. A pad struck cuts off every other pad in its group.
    void setPadChoke(int pad, int group);
    void setPadMuted(int pad, bool);
    void setPadSoloed(int pad, bool);
    Source padSource(int pad) const;
    DrumModel padModel(int pad) const;
    // What a sample pad is playing. Message thread only, because that is the
    // thread that frees samples.
    const DrumSample* padSample(int pad) const;
    // Frees the samples pads have let go of that no voice is playing any
    // more. Returns how many are still waiting.
    int collect();
    double sampleRate() const noexcept { return rate; }

    // ---- any thread -------------------------------------------------------
    // A pad struck from the face rather than by a note. It sounds at the start
    // of the next render.
    void previewPad(int pad);
    // How many times a pad has been struck, so a face can flash it.
    std::uint32_t strikes(int pad) const;

    // ---- audio thread -----------------------------------------------------
    // Stops everything at once.
    void clear();
    // Lets everything sounding fall away in a few milliseconds.
    void releaseAll();
    void noteOn(int note, float velocity, const Settings&);
    // Adds the next numSamples to left, and to right unless it is null.
    void render(float* left, float* right, int numSamples, const Settings&);
    int activeVoices() const;

    // One strike of one pad, alone, from its start to its end or to
    // maxFrames, into left and right (either may be null, and both are
    // cleared first). It plays through a voice of its own and touches nothing
    // the audio thread uses, so a face can picture a pad with exactly the
    // sound the pad makes. Returns the frames the strike lasted.
    static int renderStrike(Source, DrumModel, const DrumSample*, const PadSettings&, float velocity,
                            double sampleRate, float* left, float* right, int maxFrames);

private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        Source source = Source::empty;
        const DrumSample* sample = nullptr;
        double position = 0.0, increment = 1.0;
        DrumSynthVoice synth;
        // How a sample voice is shaped: the Decay envelope and the Tone
        // low-pass. A synth voice shapes itself.
        bool decaying = false;
        float envelope = 1.0f, envelopeFactor = 1.0f;
        bool filtered = false;
        float k = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        std::array<float, 2> ic1 {}, ic2 {};
        // The strike's velocity as the pad hears it, and the gains that
        // follow the pad's level, pan, mute and solo.
        float strength = 1.0f;
        float gainLeft = 0.0f, gainRight = 0.0f;
        // A voice cut off -- by a choke, by all-notes-off -- fades rather than
        // clicking.
        float fade = 1.0f, fadeStep = 0.0f;
        std::uint64_t order = 0;

        void startSample(const DrumSample&, const PadSettings&, double rate);
        void startSynth(DrumModel, const PadSettings&, double rate, std::uint32_t seed);
        // Adds to the buffers, gliding its gains towards the targets, and
        // returns how many samples it played: fewer than asked once it has
        // finished.
        int render(float* left, float* right, int numSamples, float targetLeft, float targetRight,
                   float smoothing) noexcept;
    };

    struct Pad
    {
        std::atomic<const DrumSample*> sample { nullptr };
        std::atomic<int> source { static_cast<int>(Source::empty) };
        std::atomic<int> model { 0 };
        std::atomic<int> choke { 0 };
        std::atomic<bool> muted { false }, soloed { false };
        std::atomic<std::uint32_t> struck { 0 };
    };

    struct Retired
    {
        const DrumSample* sample = nullptr;
        // How many stretches had begun when the pad let go of it.
        std::uint64_t after = 0;
    };

    // Counts a stretch of audio-thread work in and out (see the class note).
    struct Stretch
    {
        explicit Stretch(DrumRackEngine& owner) : engine(owner) { engine.stretchesBegun.fetch_add(1); }
        ~Stretch() { engine.stretchesEnded.fetch_add(1); }
        DrumRackEngine& engine;
    };

    void strike(int pad, float velocity, const Settings&);
    // Not muted, and soloed whenever any pad is.
    bool audible(int pad) const;
    void finish(Voice&);
    void retire(const DrumSample*);
    float fadeStepFor() const;

    // Long enough ago that no first strike is ever taken for a repeat.
    static constexpr juce::int64 never = std::numeric_limits<juce::int64>::min() / 2;

    double rate = 48000.0;
    float smoothing = 1.0f;
    juce::int64 guardFrames = 48;
    juce::int64 framesRendered = 0;
    std::uint64_t voiceOrder = 0;
    std::uint32_t strikeCount = 0;
    std::array<Pad, padCount> pads;
    std::array<Voice, voiceCount> voices;
    // When each pad was last struck, on the engine's own frame count.
    std::array<juce::int64, padCount> lastStruck {};
    std::atomic<std::uint32_t> previews { 0 };
    std::atomic<std::uint64_t> stretchesBegun { 0 }, stretchesEnded { 0 };
    // Message thread: every sample the engine owns, and those let go of.
    std::vector<std::unique_ptr<DrumSample>> owned;
    std::vector<Retired> retired;
};
}
