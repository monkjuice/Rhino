#pragma once
#include "PitchTracker.h"
#include "PsolaShifter.h"
#include "ScaleQuantizer.h"
#include <atomic>
#include <vector>

namespace rhino
{
// Vocal pitch correction: what the tracker heard, what the scale says it
// should have been, and how firmly to insist on the difference.
//
// The control set is Ableton's Auto Shift -- strength, smoothing, scale,
// pitch, formants, vibrato, dry/wet -- with two ideas taken from Antares
// that Auto Shift has no equivalent of:
//
//   Flex  backs the correction off as a note moves away from its target, so a
//         scoop or a slide between two notes reads as a performance instead
//         of being flattened into a staircase.
//   Human lengthens the retune on a note that is being held.  A phrase still
//         lands in tune, but the part a listener hears as expression -- the
//         middle of a long note -- is left alone.
//
// Correction is smoothed on the *offset* rather than on the target, and that
// is the whole reason vibrato survives a slow retune.  A singer's vibrato
// moves the detected pitch and the target stays put, so the offset swings at
// the vibrato rate; a slow filter cannot follow that swing and passes it
// through, while the drift underneath it -- which moves slowly -- is taken
// out.  Smoothing the target instead would chase the vibrato and iron it flat.
//
// This owns no engine and no UI types, so a test can push a detuned tone
// through it and measure what comes out.
class AutoTuneEngine
{
public:
    struct Settings
    {
        float inputGainDb = 0.0f;
        PitchTracker::Range range = PitchTracker::Range::Mid;
        bool liveMode = false;

        PitchClassMask mask { true, true, true, true, true, true, true, true, true, true, true, true };
        int scaleDegreeShift = 0;

        float strength = 1.0f;       // 0..1
        float retuneMs = 40.0f;      // 0..200
        float flex = 0.0f;           // 0..1
        float humanize = 0.0f;       // 0..1

        float pitchSemitones = 0.0f;
        float fineCents = 0.0f;
        float formantPercent = 0.0f; // -100..100, an octave either way
        float formantFollow = 0.0f;  // 0..1

        float vibratoCents = 0.0f;   // 0..200
        float vibratoRate = 6.0f;    // 2..15 Hz
        float vibratoFadeMs = 0.0f;  // 0..5000
        bool naturalVibrato = false;

        float dryWet = 1.0f;         // 0..1; a half is the doubler
    };

    // What the editor draws.  Written from the audio thread and read from the
    // message thread, so every field is its own relaxed atomic: a meter that
    // reads one field from this frame and one from the last is not a bug
    // worth a lock on the audio thread.
    struct Readout
    {
        float detectedNote = 0.0f;   // fractional MIDI note, 0 when unvoiced
        float targetNote = 0.0f;
        float correctionCents = 0.0f;
        float clarity = 0.0f;
        bool voiced = false;
        int band = -1;               // which range toggle's LED should light
        float latencyMs = 0.0f;
    };

    void prepare(double sampleRate, int channels, int maxBlockSize);
    void reset();

    // Called at the top of a block, from the audio thread.
    void setSettings(const Settings&);

    void process(float* const* data, int channels, int count);

    int latencySamples() const { return latency; }
    double latencySeconds() const { return sampleRate > 0.0 ? latency / sampleRate : 0.0; }
    Readout readout() const;

    static int latencyFor(PitchTracker::Range, bool liveMode, double sampleRate);

private:
    void applyRange(PitchTracker::Range, bool liveMode);
    void updateControl(int samplesInChunk);

    double sampleRate = 48000.0;
    int channelCount = 2, latency = 0, preparedBlock = 512;

    PitchTracker trackers[PitchTracker::rangeCount];
    PsolaShifter shifter;
    PitchTracker::Range activeRange = PitchTracker::Range::Mid;
    bool activeLiveMode = false, configured = false;

    std::vector<std::vector<float>> dry;   // one delay line per channel
    std::vector<float> monoScratch;
    int dryMask = 0;
    long long dryWritten = 0;

    Settings settings;
    float smoothedCorrection = 0.0f, stability = 0.0f, lastNote = 0.0f;
    float vibratoPhase = 0.0f, vibratoDrift = 0.0f, vibratoDriftTarget = 0.0f, onsetSeconds = 0.0f;
    float lastPeriod = 240.0f, settleGain = 0.0f;
    bool wasVoiced = false;
    unsigned int noise = 0x9e3779b9u;

    std::atomic<float> readDetected {0.0f}, readTarget {0.0f}, readCents {0.0f}, readClarity {0.0f};
    std::atomic<int> readVoiced {0}, readBand {-1};
};
}
