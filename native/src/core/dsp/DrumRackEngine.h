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
    double seconds() const noexcept { return sampleRate > 0.0 ? length() / sampleRate : 0.0; }

    // How many voices are playing it, so that a sample a pad has let go of is
    // freed only once the last of them has rung out. The audio thread counts
    // and the message thread reads.
    mutable std::atomic<int> playing { 0 };
};

// The Drum Rack's sound: a pad on every MIDI note, as in Live, each a sample or
// a synthesised drum shaped by its own six controls, and the voices that play
// them. A pad is addressed by its note.
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
    // One pad for each MIDI note.
    static constexpr int padCount = 128;
    // The sixteen pads a new rack shows, C2 to D#3: where the kits sit, and
    // the rows the drum patterns are written on.
    static constexpr int defaultFirstNote = 48;
    static constexpr int voiceCount = 32;
    static constexpr int chokeGroupCount = 4;
    // Decay at its top lets a sample play out to its own end.
    static constexpr float fullDecay = 10.0f;
    // Level at its bottom is silence.
    static constexpr float silentLevel = -48.0f;
    // The longest a sample file is read for. Past it, the sample ends in a
    // short fade rather than a cut.
    static constexpr double longestSampleSeconds = 10.0;
    // How long a fade, an attack or a release can be.
    static constexpr float longestFade = 2.0f;
    static constexpr float longestRelease = 10.0f;

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

    // How a sample pad plays its file, the sample editor's settings. A synth
    // pad ignores them: a synthesised drum is always struck and left to ring.
    //
    // - One-shot plays the part from start to end whatever the key does, faded
    //   in and out at its edges, as a drum sounds.
    // - Classic follows the key: an attack, a fall towards Sustain at the
    //   pad's Decay while the key is held, and a Release once it is let go.
    //   With Loop on, the part repeats while the sound lasts.
    // - Slice plays as one-shot does. It is the mode a pad is cut into slices
    //   in, before the slices are spread across pads of their own.
    enum class PlayMode : int { oneShot, classic, slice };
    // Where Slice cuts: at the sample's transients, or into equal parts.
    enum class SliceBy : int { transients, divisions };
    struct Playback
    {
        PlayMode mode = PlayMode::oneShot;
        // The part of the file a strike plays, as fractions of its length.
        float start = 0.0f, end = 1.0f;
        // One-shot and Slice: fades at the part's two ends, in seconds.
        float fadeIn = 0.0f, fadeOut = 0.0f;
        // Classic: seconds to rise, the level the sound falls to while the
        // key is held (a gain, 0 to 1), and seconds to fall 60 dB once it is
        // let go.
        float attack = 0.0f, sustain = 1.0f, release = 0.05f;
        bool loop = false;
        // Slice: how the part is cut. Playing ignores these.
        SliceBy sliceBy = SliceBy::transients;
        int divisions = 8;
        float sensitivity = 0.5f;
        friend bool operator==(const Playback&, const Playback&) = default;
        // Within every range, with the part at least a sliver long.
        Playback clamped() const;
    };

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
    void setPadPlayback(int pad, const Playback&);
    // 0 is no group. A pad struck cuts off every other pad in its group.
    void setPadChoke(int pad, int group);
    void setPadMuted(int pad, bool);
    void setPadSoloed(int pad, bool);
    Source padSource(int pad) const;
    DrumModel padModel(int pad) const;
    Playback padPlayback(int pad) const;
    // What a sample pad is playing. Message thread only, because that is the
    // thread that frees samples.
    const DrumSample* padSample(int pad) const;
    // Frees the samples pads have let go of that no voice is playing any
    // more. Returns how many are still waiting.
    int collect();
    double sampleRate() const noexcept { return rate; }

    // ---- any thread -------------------------------------------------------
    // A pad struck from the face rather than by a note. It sounds at the start
    // of the next render. A classic pad is held for a moment and let go.
    void previewPad(int pad);
    // One part of a pad's sample, struck as a one-shot: a slice auditioned
    // from the face. Fractions of the file, as Playback's are.
    void previewPart(int pad, float start, float end);
    // How many times a pad has been struck, so a face can flash it.
    std::uint32_t strikes(int pad) const;
    // How many note-ons have arrived for a note, whether or not its pad holds
    // anything, so a face can show which keys are being played.
    std::uint32_t notesReceived(int note) const;
    // Where in its file the latest voice on a pad is, as a fraction, or a
    // negative number while nothing plays it.
    float playhead(int pad) const;

    // ---- audio thread -----------------------------------------------------
    // Stops everything at once.
    void clear();
    // Lets everything sounding fall away in a few milliseconds.
    void releaseAll();
    void noteOn(int note, float velocity, const Settings&);
    // Lets go of a classic pad's held voices. A one-shot ignores it.
    void noteOff(int note);
    // Adds the next numSamples to left, and to right unless it is null.
    void render(float* left, float* right, int numSamples, const Settings&);
    int activeVoices() const;

    // One strike of one pad, alone, from its start to its end or to
    // maxFrames, into left and right (either may be null, and both are
    // cleared first). It plays through a voice of its own and touches nothing
    // the audio thread uses, so a face can picture a pad with exactly the
    // sound the pad makes. A classic pad is held for holdFrames, or
    // throughout when that is negative. Returns the frames the strike lasted.
    static int renderStrike(Source, DrumModel, const DrumSample*, const PadSettings&, const Playback&, float velocity,
                            double sampleRate, float* left, float* right, int maxFrames, int holdFrames = -1);

private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        Source source = Source::empty;
        const DrumSample* sample = nullptr;
        double position = 0.0, increment = 1.0;
        DrumSynthVoice synth;
        // The part of the file played, in its frames, and the crossfade a
        // loop makes from the part's end back into its start.
        double partStart = 0.0, partEnd = 0.0, crossfade = 0.0;
        bool looping = false;
        // How a sample voice is shaped: the Decay envelope, which falls
        // towards Sustain (zero for a one-shot), the fades or attack at the
        // part's edges, the release once a classic key is let go, and the Tone
        // low-pass. A synth voice shapes itself.
        bool decaying = false;
        float envelope = 1.0f, envelopeFactor = 1.0f, sustain = 0.0f;
        float rise = 1.0f, riseStep = 0.0f;
        double fadeOutFrames = 0.0;
        bool classic = false, held = false, releasing = false;
        // A classic voice struck from the face lets go of itself after this
        // many frames; one struck by a note waits for its note-off (-1).
        int holdRemaining = -1;
        float releaseGain = 1.0f, releaseFactor = 1.0f;
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

        void startSample(const DrumSample&, const PadSettings&, const Playback&, double rate, int holdFrames);
        void startSynth(DrumModel, const PadSettings&, double rate, std::uint32_t seed);
        void letGo();
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
        std::atomic<std::uint32_t> struck { 0 }, received { 0 };
        // The pad's Playback, a field at a time. A strike that lands while
        // they are being written plays a mix of old and new for one note.
        std::atomic<int> mode { 0 };
        std::atomic<float> start { 0.0f }, end { 1.0f }, fadeIn { 0.0f }, fadeOut { 0.0f };
        std::atomic<float> attack { 0.0f }, sustain { 1.0f }, release { 0.05f };
        std::atomic<bool> loop { false };
        std::atomic<float> playhead { -1.0f };
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

    // A part of a pad's file struck in place of the pad's own.
    struct Part
    {
        float start = 0.0f, end = 1.0f;
    };

    void strike(int pad, float velocity, const Settings&, bool fromFace, const Part* part = nullptr);
    // Not muted, and soloed whenever any pad is.
    bool audible(int pad) const;
    void finish(Voice&);
    void retire(const DrumSample*);
    float fadeStepFor() const;
    Playback playbackOf(const Pad&) const;

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
    // When each pad was last struck, on the engine's own frame count, and
    // which of its voices is the newest. Audio thread.
    std::array<juce::int64, padCount> lastStruck {};
    std::array<std::uint64_t, padCount> newestVoice {};
    // Pads waiting to be struck from the face, a bit each, and one part.
    std::array<std::atomic<std::uint64_t>, padCount / 64> previews {};
    std::atomic<int> partPreviewPad { -1 };
    std::atomic<float> partPreviewStart { 0.0f }, partPreviewEnd { 1.0f };
    // How many pads are soloed, so a voice need not ask every pad.
    std::atomic<int> soloCount { 0 };
    std::atomic<std::uint64_t> stretchesBegun { 0 }, stretchesEnded { 0 };
    // Message thread: every sample the engine owns, and those let go of.
    std::vector<std::unique_ptr<DrumSample>> owned;
    std::vector<Retired> retired;
};
}
