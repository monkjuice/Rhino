#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

// The arpeggiator: what turns the keys being held down into a sequence.
//
// It sits upstream of every voice rather than inside one, so it is not part of
// Core and Core knows nothing about it. The Processor feeds it the notes the
// host and the panel's keyboard send, and it hands notes back through two
// callbacks — which is how it reaches the voices without this file having to
// know what a voice is, and what lets the whole of it be tested against a plain
// pair of lambdas with no Processor, no Core and no audio device.
//
// Nothing here allocates. The held keys, the order they are visited in and the
// notes currently sounding are all fixed arrays, because `advance` is called
// once per sample on the audio thread.
namespace rhino::forge
{
// How many keys the arp will hold at once. A chord larger than this drops its
// oldest key rather than growing an array on the audio thread.
inline constexpr int arpMaxHeld = 16;

// The longest order a shape can produce from that many keys. UP+DOWN visits
// every key twice and is the longest of them, so twice the held count is the
// bound for every shape here.
inline constexpr int arpMaxSteps = arpMaxHeld * 2;

// How many times a pattern may be transposed, and the longest stage order that
// can produce — the same doubling, for the same reason.
inline constexpr int arpMaxRange = 8;
inline constexpr int arpMaxStages = arpMaxRange * 2;

// A note the arp has started and has still to stop. One per step in flight:
// a gate longer than a step means the previous note is still sounding when the
// next one starts, so this cannot be a single slot.
inline constexpr int arpMaxSounding = 32;

// The shapes, in the order the panel lists them and the order a preset stores.
// Serum's SHAPE field and its transpose-range menu offer the same vocabulary
// and mean the same thing by it — an order to visit a set of things in — so
// this one list serves both, and `arpOrder` is called twice with it: once for
// the keys held down, once for the transposition stages.
//
// Appending is safe; reordering is not, because the index is what a preset and
// a host automation lane store.
enum class ArpShape
{
    up, down, upDown, downUp, upAndDown, downAndUp,
    thumbUp, thumbUD, pinkyUp, pinkyUD,
    converge, diverge, conDiverge, chord,
    random, randomNoDup, randomDrift, randomOnce
};

inline constexpr int arpShapeCount = 18;

inline const char* arpShapeName(int index)
{
    switch (static_cast<ArpShape>(index))
    {
        case ArpShape::up:          return "Up";
        case ArpShape::down:        return "Down";
        case ArpShape::upDown:      return "Up/Down";
        case ArpShape::downUp:      return "Down/Up";
        case ArpShape::upAndDown:   return "Up+Down";
        case ArpShape::downAndUp:   return "Down+Up";
        case ArpShape::thumbUp:     return "Thumb Up";
        case ArpShape::thumbUD:     return "Thumb UD";
        case ArpShape::pinkyUp:     return "Pinky Up";
        case ArpShape::pinkyUD:     return "Pinky UD";
        case ArpShape::converge:    return "Converge";
        case ArpShape::diverge:     return "Diverge";
        case ArpShape::conDiverge:  return "Con+Diverge";
        case ArpShape::chord:       return "Chord";
        case ArpShape::random:      return "Random";
        case ArpShape::randomNoDup: return "Rnd.NoDup";
        case ArpShape::randomDrift: return "Rnd.Drift";
        case ArpShape::randomOnce:  return "Rnd.Once";
    }
    return "Up";
}

// Whether a shape picks its next position rather than following a fixed order.
// The four of them share `arpOrder`'s identity order and are resolved a step at
// a time instead, because "random" is a decision made when the step fires and
// not a list worked out in advance — except Rnd.Once, which is exactly a list
// worked out in advance and is shuffled when the pattern restarts.
inline constexpr bool arpShapeIsRandom(ArpShape shape)
{
    return shape == ArpShape::random || shape == ArpShape::randomNoDup
        || shape == ArpShape::randomDrift || shape == ArpShape::randomOnce;
}

// CHORD is not an order at all: every key sounds on every step. It is in the
// same list because that is where Serum puts it, and because a patch reaches
// for it in the same place it would reach for UP.
inline constexpr bool arpShapePlaysChord(ArpShape shape) { return shape == ArpShape::chord; }

// The order a shape visits `count` things in, written into `out`, returning how
// many entries it wrote.
//
// Everything here is an index into a set of `count` things, never a note: that
// is what lets the same function order the keys of a chord and the stages of a
// transposition without knowing which it is doing.
inline int arpOrder(ArpShape shape, int count, std::array<int, arpMaxSteps>& out)
{
    count = juce::jlimit(0, arpMaxHeld, count);
    if (count <= 0) return 0;

    auto length = 0;
    const auto push = [&out, &length] (int index)
    {
        if (length < arpMaxSteps) out[static_cast<size_t>(length++)] = index;
    };

    // One key makes every shape the same shape, and several of the rules below
    // divide by count - 1. Answering here keeps that out of all of them.
    if (count == 1) { push(0); return length; }

    switch (shape)
    {
        case ArpShape::up:
            for (int i = 0; i < count; ++i) push(i);
            break;
        case ArpShape::down:
            for (int i = count - 1; i >= 0; --i) push(i);
            break;
        // Up then back, without sounding either end twice: the top and the
        // bottom are the turning points rather than beats of their own.
        case ArpShape::upDown:
            for (int i = 0; i < count; ++i) push(i);
            for (int i = count - 2; i >= 1; --i) push(i);
            break;
        case ArpShape::downUp:
            for (int i = count - 1; i >= 0; --i) push(i);
            for (int i = 1; i <= count - 2; ++i) push(i);
            break;
        // The same journey with the ends held: an even pattern where Up/Down is
        // an odd one, which is the whole difference between the two pairs.
        case ArpShape::upAndDown:
            for (int i = 0; i < count; ++i) push(i);
            for (int i = count - 1; i >= 0; --i) push(i);
            break;
        case ArpShape::downAndUp:
            for (int i = count - 1; i >= 0; --i) push(i);
            for (int i = 0; i < count; ++i) push(i);
            break;
        // The thumb is the lowest key and it sounds between every other one.
        case ArpShape::thumbUp:
            for (int i = 1; i < count; ++i) { push(0); push(i); }
            break;
        case ArpShape::thumbUD:
            for (int i = 1; i < count; ++i) { push(0); push(i); }
            for (int i = count - 2; i >= 1; --i) { push(0); push(i); }
            break;
        // The pinky is the highest key, and the mirror of the thumb.
        case ArpShape::pinkyUp:
            for (int i = 0; i < count - 1; ++i) { push(i); push(count - 1); }
            break;
        case ArpShape::pinkyUD:
            for (int i = 0; i < count - 1; ++i) { push(i); push(count - 1); }
            for (int i = count - 2; i >= 1; --i) { push(i); push(count - 1); }
            break;
        // In from both ends towards the middle, and out from the middle to both
        // ends. Diverge is Converge read backwards, which is what makes
        // Con+Diverge the two of them end to end.
        case ArpShape::converge:
            for (int low = 0, high = count - 1; low <= high; ++low, --high)
            {
                push(low);
                if (low != high) push(high);
            }
            break;
        case ArpShape::diverge:
        {
            std::array<int, arpMaxSteps> inward {};
            const auto inwardLength = arpOrder(ArpShape::converge, count, inward);
            for (int i = inwardLength - 1; i >= 0; --i) push(inward[static_cast<size_t>(i)]);
            break;
        }
        case ArpShape::conDiverge:
        {
            std::array<int, arpMaxSteps> inward {};
            const auto inwardLength = arpOrder(ArpShape::converge, count, inward);
            for (int i = 0; i < inwardLength; ++i) push(inward[static_cast<size_t>(i)]);
            // From the second entry back, so the note the two halves meet on is
            // not struck twice in a row.
            for (int i = inwardLength - 2; i >= 0; --i) push(inward[static_cast<size_t>(i)]);
            break;
        }
        // CHORD sounds everything at once, so its order is one step long and
        // the step is what fans out. Random shapes carry the identity order and
        // choose a position when the step fires instead.
        case ArpShape::chord:
            push(0);
            break;
        case ArpShape::random:
        case ArpShape::randomNoDup:
        case ArpShape::randomDrift:
        case ArpShape::randomOnce:
            for (int i = 0; i < count; ++i) push(i);
            break;
    }
    return length;
}

// Tempo-synced arp rates, as the length of one step in beats, where a beat is a
// quarter note. Straight divisions only: TRIP and DOT are separate switches
// that scale whichever of these is chosen, exactly as Serum's are, which is
// what keeps this a list of seven rather than one of twenty-one.
struct ArpDivision
{
    const char* label;
    float beats;
};

inline const std::array<ArpDivision, 7>& arpDivisions()
{
    static const std::array<ArpDivision, 7> table {{
        {"1/1", 4.0f},   {"1/2", 2.0f},    {"1/4", 1.0f},    {"1/8", 0.5f},
        {"1/16", 0.25f}, {"1/32", 0.125f}, {"1/64", 0.0625f},
    }};
    return table;
}

inline constexpr int arpDivisionCount = 7;

// 1/16 is where an arpeggio is reached for before it is adjusted, and it is
// what Serum opens on.
inline constexpr int arpDefaultDivision = 4;

// Everything the arp reads, gathered once per block the way the Patch is. Held
// as plain values rather than parameter pointers so `advance` touches no
// parameter object on the audio thread.
struct ArpSettings
{
    bool enabled = false;
    ArpShape shape = ArpShape::up;
    // The length of one step in seconds, already resolved against the tempo and
    // the triplet and dotted switches. Doing that in the Processor is what
    // keeps the tempo out of here, the same division of labour the LFOs use.
    float stepSeconds = 0.125f;
    // Rotates where in the order the pattern starts.
    int offset = 0;
    // How many complete passes before the arp falls silent. Zero is forever,
    // which is what the panel reads as "inf" and what a patch nearly always
    // wants.
    int repeats = 0;
    // The note's length against the step: 1.0 is exactly a step, and anything
    // above it means a note is still sounding when the next one starts.
    float gate = 1.0f;
    float chance = 1.0f;
    // Whether a note lost to CHANCE still advances the pattern. Serum's Pre
    // applies the roll before the shape steps, so the note that was skipped is
    // the one played next time rather than being missed altogether.
    bool chancePre = false;
    bool latch = false;
    bool thru = false;
    int shift = 0;
    int range = 1;
    ArpShape rangeShape = ArpShape::up;
    bool retriggerOnNote = false;
    bool retriggerFirstOnly = false;
    bool retriggerOnRate = false;
    float retriggerSeconds = 2.0f;
    // The interval a started arp waits for before its first step, in beats.
    // Zero is OFF, which starts it the moment the key goes down.
    float launchQuantBeats = 0.0f;
    bool velocityOn = false;
    bool velocityRetrigger = false;
    // How far towards the target one step moves the velocity. Zero holds the
    // incoming velocity; one arrives at the target on the first step.
    float velocityDecay = 0.15f;
    float velocityTarget = 0.0f;
};

// One arpeggiator. Fed notes, advanced a sample at a time, and emitting through
// callbacks.
class Arp
{
public:
    void reset()
    {
        heldCount = 0;
        soundingCount = 0;
        physical.fill(false);
        physicalCount = 0;
        sustainDown = false;
        stepTimer = 0.0;
        step = 0;
        stage = 0;
        pass = 0;
        velocity = -1.0f;
        retriggerTimer = 0.0;
        orderDirty = true;
    }

    // A key going down. The held set is kept sorted by pitch, because every
    // shape here is expressed as "the lowest", "the highest" or a position
    // between them — an order of arrival would make all of them wrong.
    void noteOn(int note, float noteVelocity, const ArpSettings& settings)
    {
        // Latched, a key pressed while nothing is physically down starts a new
        // chord; one pressed while a key is still down joins the chord being
        // held. Asking whether any key is down is what tells the two apart —
        // lifting one finger of a three-note chord must not replace it.
        if (latching(settings) && physicalCount == 0)
        {
            heldCount = 0;
            orderDirty = true;
        }

        if (!physical[static_cast<size_t>(juce::jlimit(0, 127, note))])
        {
            physical[static_cast<size_t>(juce::jlimit(0, 127, note))] = true;
            ++physicalCount;
        }

        const auto wasEmpty = heldCount == 0;
        insertHeld(note, noteVelocity);

        if (settings.retriggerOnNote && (wasEmpty || !settings.retriggerFirstOnly)) retrigger(settings);
        // Whatever the retrigger settings say, the first key of a new chord
        // starts the sequence at its beginning: an arp that began mid-pattern
        // because the last chord ended there reads as a fault rather than as a
        // feature.
        else if (wasEmpty) retrigger(settings);
    }

    void noteOff(int note, const ArpSettings& settings)
    {
        if (physical[static_cast<size_t>(juce::jlimit(0, 127, note))])
        {
            physical[static_cast<size_t>(juce::jlimit(0, 127, note))] = false;
            physicalCount = juce::jmax(0, physicalCount - 1);
        }
        // Latched, a key lifting leaves the chord sounding. The key is gone
        // from `physical` either way, which is what the latch being switched
        // off later reads to decide what to drop.
        if (latching(settings)) return;
        removeHeld(note);
    }

    void allNotesOff()
    {
        heldCount = 0;
        physical.fill(false);
        physicalCount = 0;
        orderDirty = true;
    }

    // Everything the latch was holding that no finger is on any more. Called
    // when the latch is switched off and when the pedal comes up, which is the
    // one moment a latched chord is meant to stop.
    void releaseLatch()
    {
        for (auto i = heldCount - 1; i >= 0; --i)
            if (!physical[static_cast<size_t>(juce::jlimit(0, 127, held[static_cast<size_t>(i)].note))])
                removeHeldAt(i);
    }

    // While the arp is on, CC64 works the latch rather than sustaining notes,
    // which is what the manual says Serum does and what a pedal under an
    // arpeggio is actually wanted for.
    void sustain(bool down)
    {
        sustainDown = down;
        if (!down) releaseLatch();
    }

    // Everything the arp has started and not yet stopped, let go of at once.
    // The one thing that has to happen when the arp is switched off mid-phrase:
    // its notes are held by its own gate clock, and nothing else would ever
    // come back to stop them.
    template <typename StopNote>
    void flush(StopNote&& stopNote)
    {
        for (auto i = 0; i < soundingCount; ++i) stopNote(sounding[static_cast<size_t>(i)].note);
        soundingCount = 0;
    }

    // True while there is something for the arp to play, which is what decides
    // whether the notes arriving are consumed or passed straight through.
    bool isPlaying() const { return heldCount > 0; }

    // Start the pattern again from its beginning. What the LAUNCH retrigger
    // does when the arp is switched on, and the reason `retrigger` — which is
    // the same thing reached from inside — is private: everything else that
    // restarts the pattern is a rule this class already owns.
    void restart(const ArpSettings& settings) { retrigger(settings); }

    // Hold the first step for this many samples. The arp counts in steps and
    // knows nothing about bars, so LAUNCH QUANT is worked out by the Processor
    // against the host's timeline and handed here as a plain delay — the same
    // division of labour that keeps the tempo out of `stepSeconds`.
    void delayFirstStep(double samples) { stepTimer = juce::jmax(0.0, samples); }

    int currentStep() const { return step; }

    // Advances one sample, emitting through `startNote(note, velocity)` and
    // `stopNote(note)`.
    template <typename StartNote, typename StopNote>
    void advance(const ArpSettings& settings, double sampleRate,
                 StartNote&& startNote, StopNote&& stopNote)
    {
        const auto stepSamples = juce::jmax(1.0, static_cast<double>(settings.stepSeconds) * sampleRate);

        // Notes already sounding are stopped on their own clock, because a gate
        // above one step means several of them overlap.
        for (auto i = soundingCount - 1; i >= 0; --i)
        {
            auto& note = sounding[static_cast<size_t>(i)];
            if (--note.samplesLeft > 0) continue;
            stopNote(note.note);
            note = sounding[static_cast<size_t>(--soundingCount)];
        }

        if (!settings.enabled) return;

        if (settings.retriggerOnRate)
        {
            retriggerTimer -= 1.0;
            if (retriggerTimer <= 0.0)
            {
                retriggerTimer = juce::jmax(1.0, static_cast<double>(settings.retriggerSeconds) * sampleRate);
                retrigger(settings);
            }
        }

        if (heldCount <= 0) return;
        // Zero repeats is forever; anything else stops the arp once that many
        // complete passes have been played, and holding the keys down does not
        // start it again.
        if (settings.repeats > 0 && pass >= settings.repeats) return;

        stepTimer -= 1.0;
        if (stepTimer > 0.0) return;
        // Carried rather than reset, so a step length that is not a whole
        // number of samples does not drift the arp off the beat over a bar.
        stepTimer += stepSamples;

        fireStep(settings, stepSamples, startNote, stopNote);
    }

private:
    struct HeldNote { int note; float velocity; };
    struct SoundingNote { int note; int samplesLeft; };

    bool latching(const ArpSettings& settings) const { return settings.latch || sustainDown; }

    void removeHeldAt(int index)
    {
        for (auto j = index + 1; j < heldCount; ++j)
            held[static_cast<size_t>(j - 1)] = held[static_cast<size_t>(j)];
        --heldCount;
        orderDirty = true;
    }

    void insertHeld(int note, float noteVelocity)
    {
        for (auto i = 0; i < heldCount; ++i)
            if (held[static_cast<size_t>(i)].note == note) return;

        // A chord larger than the arp holds drops its lowest key rather than
        // reaching for memory on the audio thread.
        if (heldCount >= arpMaxHeld)
        {
            for (auto i = 1; i < heldCount; ++i) held[static_cast<size_t>(i - 1)] = held[static_cast<size_t>(i)];
            --heldCount;
        }

        auto at = heldCount;
        while (at > 0 && held[static_cast<size_t>(at - 1)].note > note)
        {
            held[static_cast<size_t>(at)] = held[static_cast<size_t>(at - 1)];
            --at;
        }
        held[static_cast<size_t>(at)] = {note, noteVelocity};
        ++heldCount;
        orderDirty = true;
    }

    void removeHeld(int note)
    {
        for (auto i = 0; i < heldCount; ++i)
            if (held[static_cast<size_t>(i)].note == note) { removeHeldAt(i); return; }
    }

    void retrigger(const ArpSettings& settings)
    {
        step = 0;
        stage = 0;
        pass = 0;
        stepTimer = 0.0;
        driftPosition = 0;
        if (settings.velocityRetrigger || !settings.velocityOn) velocity = -1.0f;
        orderDirty = true;
    }

    void rebuildOrder(const ArpSettings& settings)
    {
        orderLength = arpOrder(settings.shape, heldCount, order);
        stageLength = arpOrder(settings.rangeShape,
                               juce::jlimit(1, arpMaxRange, settings.range), stages);
        if (settings.shape == ArpShape::randomOnce) shuffle(order, orderLength);
        orderDirty = false;
        shapeBuilt = settings.shape;
        rangeShapeBuilt = settings.rangeShape;
        heldBuilt = heldCount;
        rangeBuilt = settings.range;
    }

    void shuffle(std::array<int, arpMaxSteps>& values, int length)
    {
        for (auto i = length - 1; i > 0; --i)
        {
            const auto j = random.nextInt(i + 1);
            std::swap(values[static_cast<size_t>(i)], values[static_cast<size_t>(j)]);
        }
    }

    // Which key this step sounds. The deterministic shapes read their order;
    // the random ones decide here, because that is what makes them random per
    // step rather than a fixed order that happens to have been shuffled.
    int keyForStep(const ArpSettings& settings)
    {
        if (!arpShapeIsRandom(settings.shape) || settings.shape == ArpShape::randomOnce)
            return order[static_cast<size_t>((step + settings.offset) % juce::jmax(1, orderLength))];

        if (settings.shape == ArpShape::randomDrift)
        {
            // A walk rather than a jump: one step either way, turned back at
            // the ends, which is what keeps it recognisably the same chord.
            driftPosition += random.nextBool() ? 1 : -1;
            driftPosition = juce::jlimit(0, heldCount - 1, driftPosition);
            return driftPosition;
        }
        if (settings.shape == ArpShape::randomNoDup && heldCount > 1)
        {
            auto pick = lastRandom;
            while (pick == lastRandom) pick = random.nextInt(heldCount);
            lastRandom = pick;
            return pick;
        }
        lastRandom = random.nextInt(juce::jmax(1, heldCount));
        return lastRandom;
    }

    template <typename StartNote, typename StopNote>
    void fireStep(const ArpSettings& settings, double stepSamples,
                  StartNote&& startNote, StopNote&& stopNote)
    {
        if (orderDirty || shapeBuilt != settings.shape || rangeShapeBuilt != settings.rangeShape
            || heldBuilt != heldCount || rangeBuilt != settings.range)
            rebuildOrder(settings);
        if (orderLength <= 0 || stageLength <= 0) return;

        const auto advancePattern = [this, &settings]
        {
            if (++step < orderLength) return;
            step = 0;
            if (++stage < stageLength) return;
            stage = 0;
            ++pass;
            if (settings.shape == ArpShape::randomOnce) shuffle(order, orderLength);
        };

        // CHANCE decides whether this step sounds. Pre applies the roll before
        // the pattern moves on, so the note that was skipped is the one played
        // next time; the default applies it after, so a skipped step is a hole
        // in the sequence rather than a stumble in it.
        const auto plays = settings.chance >= 1.0f || random.nextFloat() < settings.chance;
        if (!plays)
        {
            if (!settings.chancePre) advancePattern();
            return;
        }

        const auto transpose = settings.shift * stages[static_cast<size_t>(stage)];
        const auto gateSamples = juce::jmax(1, juce::roundToInt(
            stepSamples * juce::jlimit(0.01f, 4.0f, settings.gate)));

        // The velocity of the key this step struck, which is where the ramp
        // starts from when VELOCITY is on and what every note sounds at when it
        // is off.
        auto struck = 1.0f;

        const auto sound = [&] (int index)
        {
            const auto& key = held[static_cast<size_t>(juce::jlimit(0, heldCount - 1, index))];
            struck = key.velocity;
            const auto note = juce::jlimit(0, 127, key.note + transpose);
            // A note already sounding at this pitch is stopped first: a gate
            // over 100% with a repeating pattern would otherwise start the same
            // pitch twice and only ever stop it once.
            stopSounding(note, stopNote);
            startNote(note, velocityFor(settings, key.velocity));
            if (soundingCount < arpMaxSounding)
                sounding[static_cast<size_t>(soundingCount++)] = {note, gateSamples};
        };

        if (arpShapePlaysChord(settings.shape))
            for (auto i = 0; i < heldCount; ++i) sound(i);
        else
            sound(keyForStep(settings));

        stepVelocity(settings, struck);
        advancePattern();
    }

    template <typename StopNote>
    void stopSounding(int note, StopNote&& stopNote)
    {
        for (auto i = soundingCount - 1; i >= 0; --i)
        {
            if (sounding[static_cast<size_t>(i)].note != note) continue;
            stopNote(note);
            sounding[static_cast<size_t>(i)] = sounding[static_cast<size_t>(--soundingCount)];
        }
    }

    // The velocity this step sounds at. With VELOCITY off it is the velocity
    // the key was struck at and nothing moves it; with it on, each step walks
    // the level a share of the way towards the target, which is the ramp the
    // manual describes rather than a second envelope.
    float velocityFor(const ArpSettings& settings, float keyVelocity) const
    {
        if (!settings.velocityOn) return keyVelocity;
        return velocity < 0.0f ? keyVelocity : velocity;
    }

    void stepVelocity(const ArpSettings& settings, float struck)
    {
        if (!settings.velocityOn) { velocity = -1.0f; return; }
        if (velocity < 0.0f) velocity = struck;
        velocity += (settings.velocityTarget - velocity) * juce::jlimit(0.0f, 1.0f, settings.velocityDecay);
    }

    std::array<HeldNote, arpMaxHeld> held {};
    int heldCount = 0;
    std::array<SoundingNote, arpMaxSounding> sounding {};
    int soundingCount = 0;

    std::array<int, arpMaxSteps> order {};
    int orderLength = 0;
    std::array<int, arpMaxSteps> stages {};
    int stageLength = 0;
    bool orderDirty = true;
    ArpShape shapeBuilt = ArpShape::up;
    ArpShape rangeShapeBuilt = ArpShape::up;
    int heldBuilt = 0;
    int rangeBuilt = 1;

    double stepTimer = 0.0;
    double retriggerTimer = 0.0;
    int step = 0, stage = 0, pass = 0;
    int driftPosition = 0, lastRandom = -1;
    // Negative until the first step of a run, which is what lets the ramp start
    // from the velocity the key was actually struck at.
    float velocity = -1.0f;
    // Which keys a finger is actually on, as against which the arp is playing.
    // The two differ exactly while the latch is holding a chord nobody is
    // touching, and telling them apart is what makes the latch behave.
    std::array<bool, 128> physical {};
    int physicalCount = 0;
    bool sustainDown = false;
    juce::Random random;
};
}
