#pragma once

#include "ForgeVoiceParts.h"

// The shapes themselves: the ten single-cycle waves POSITION morphs through,
// the six the sub offers, and the built-in tables both are baked into.
//
// A table is built once, on whichever thread prepares the synth, and read from
// there afterwards. Nothing in here allocates on the audio thread.
namespace rhino::forge
{
// --- The oscillator's table ---------------------------------------------------
//
// Ten single-cycle shapes that POSITION morphs through: the table an oscillator
// reads until one is loaded over it.
//
// These formulas are a generator, not the render path. Since M9b-1 they are
// rendered into a real Wavetable once, at startup, and the voice reads that
// table — see ForgeWavetable.h. Writing them as formulas is still the right way
// to author them, but a frame is now sampled data by the time anything plays
// it, which is what lets a frame come from a file or a brush instead.
//
// The order matters as much as the contents: POSITION crossfades whichever two
// frames it falls between, so what sits next to what is what the in-between
// positions sound like. They run from the softest through the pulse family to
// the brightest, with each frame a relative of the one before it.
inline constexpr int waveShapeCount = 10;

inline const char* waveShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "TRI";
        case 2: return "TRAP";
        case 3: return "SQR";
        case 4: return "PULSE";
        case 5: return "THIN";
        case 6: return "SAW";
        case 7: return "HUMP";
        case 8: return "ORGAN";
        case 9: return "VOX";
        default: break;
    }
    return "SINE";
}

// One frame, at a point in its cycle. Every frame is bipolar and reaches full
// scale, so morphing between any two never changes the oscillator's level.
inline float waveShape(int shape, float phase)
{
    const auto cycle = phase * juce::MathConstants<float>::twoPi;
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    switch (shape)
    {
        case 1: return triangle;
        // A triangle driven past full scale and clipped: flat tops with sloped
        // sides, which is what sits between a triangle and a square.
        case 2: return juce::jlimit(-1.0f, 1.0f, triangle * 3.0f);
        case 3: return phase < 0.5f ? 1.0f : -1.0f;
        case 4: return phase < 0.25f ? 1.0f : -1.0f;
        case 5: return phase < 0.1f ? 1.0f : -1.0f;
        // A rising saw with its jump in the middle of the frame rather than at
        // the edge of it. Rotating a saw by half a cycle changes nothing about
        // what it is — same harmonics, same sound — but it is the difference
        // between a display that shows one diagonal and one that shows the edge
        // that makes a saw a saw. Serum stores its own saw the same way round.
        case 6: return phase < 0.5f ? phase * 2.0f : phase * 2.0f - 2.0f;
        // A rectified sine: two humps a cycle, so the even harmonics arrive and
        // it reads an octave up without being one.
        case 7: return 2.0f * std::abs(std::sin(cycle)) - 1.0f;
        // Drawbars: a fundamental with an octave and a twelfth over it. The
        // scaling is the measured peak of that sum, so it fills the range
        // without being clipped into a different shape.
        case 8: return 0.694224f * (std::sin(cycle) + 0.5f * std::sin(2.0f * cycle)
                                                    + 0.33f * std::sin(3.0f * cycle));
        // A formant: a burst of the third harmonic under a raised cosine, which
        // is the shape a vowel makes and sounds like one.
        case 9: return 1.067774f * std::sin(3.0f * cycle) * (0.5f - 0.5f * std::cos(cycle));
        default: break;
    }
    return std::sin(cycle);
}

// The built-in ten, rendered into a real table once and read from then on.
// The formulas above are now a generator rather than the thing the voice calls:
// every oscillator reads a Wavetable, and this is the one it reads until a
// table is loaded over it. Built on first use, which the Processor and Core
// force to happen at construction so that no audio thread is ever the first
// caller — building it allocates and runs a transform per frame.
inline const Wavetable& builtInWavetable()
{
    static const Wavetable table = []
    {
        std::vector<float> samples(static_cast<size_t>(waveShapeCount) * wavetableFrameSize);
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(waveShapeCount));
        for (int shape = 0; shape < waveShapeCount; ++shape)
        {
            names.push_back(waveShapeName(shape));
            for (int i = 0; i < wavetableFrameSize; ++i)
                samples[static_cast<size_t>(shape) * wavetableFrameSize + static_cast<size_t>(i)]
                    = waveShape(shape, static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
        }
        return Wavetable(samples.data(), waveShapeCount, "BASIC SHAPES", std::move(names));
    }();
    return table;
}

// Where a position falls in the built-in table: the frame at or below it, and
// how far past that frame it has travelled.
inline void waveFrameAt(float position, int& frame, float& blend)
{
    wavetableFrameAt(builtInWavetable().frameCount(), position, frame, blend);
}

// One sample of the built-in table at a position, as authored — no band
// limiting, because this is what the panel draws rather than what a note reads.
inline float waveAt(float position, float phase)
{
    return builtInWavetable().sample(position, phase);
}

// What a position is called, for a knob to read out. Exactly on a frame it is
// that frame's name; between two it names both, because a blend of two shapes is
// honestly what it is. Never called from the render.
inline juce::String waveLabel(float position)
{
    const auto& table = builtInWavetable();
    int frame = 0;
    auto blend = 0.0f;
    wavetableFrameAt(table.frameCount(), position, frame, blend);
    if (blend <= 0.01f) return table.frameTitle(frame);
    if (blend >= 0.99f) return table.frameTitle(frame + 1);
    return table.frameTitle(frame) + ">" + table.frameTitle(frame + 1);
}

// --- The sub oscillator's shapes ----------------------------------------------
//
// Six of them, in the order and with the names Serum's sub gives them: a sine,
// a rounded rectangle, a triangle, a saw, a square and a pulse. That order runs
// from the shape with nothing but a fundamental to the ones with the most
// harmonics over it, which is what a picker of six icons wants to read as.
//
// They are frames of a table rather than formulas evaluated per sample, for the
// reason every other shape in Forge is: a saw an octave below the note is a
// harmonic series running past Nyquist, and the band-limited copies a Wavetable
// keeps are what stop the top of it folding back down. The sub was a sine until
// now and a sine has nothing to fold, which is why this is the moment the table
// is needed.
inline constexpr int subShapeCount = 6;

inline const char* subShapeName(int shape)
{
    switch (shape)
    {
        case 1: return "RECT";
        case 2: return "TRI";
        case 3: return "SAW";
        case 4: return "SQR";
        case 5: return "PULSE";
        default: break;
    }
    return "SINE";
}

// One sub frame, at a point in its cycle. Bipolar and full scale, like every
// other frame Forge authors, so changing shape moves the harmonics rather than
// the level.
inline float subShape(int shape, float phase)
{
    const auto cycle = phase * juce::MathConstants<float>::twoPi;
    switch (shape)
    {
        // A sine driven past full scale and clipped: flat tops with the sine's
        // own curve either side of them, which is the rounded rectangle the
        // manual describes -- between a sine and a square, and softer at the
        // corners than the trapezoid the main table carries.
        case 1: return juce::jlimit(-1.0f, 1.0f, std::sin(cycle) * 2.6f);
        case 2: return 1.0f - 4.0f * std::abs(phase - 0.5f);
        // Rotated half a cycle, the same way the main table stores its saw:
        // same harmonics either way round, but the picture shows the edge.
        case 3: return phase < 0.5f ? phase * 2.0f : phase * 2.0f - 2.0f;
        case 4: return phase < 0.5f ? 1.0f : -1.0f;
        case 5: return phase < 0.25f ? 1.0f : -1.0f;
        default: break;
    }
    return std::sin(cycle);
}

// The six, rendered into a band-limited table once. A table of its own rather
// than frames added to the built-in ten: POSITION morphs between neighbours in
// that one, and a sub shape is chosen outright, so putting them in the same
// table would have put six shapes in the oscillators' morph that nobody asked
// to travel through.
inline const Wavetable& subWavetable()
{
    static const Wavetable table = []
    {
        std::vector<float> samples(static_cast<size_t>(subShapeCount) * wavetableFrameSize);
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(subShapeCount));
        for (int shape = 0; shape < subShapeCount; ++shape)
        {
            names.push_back(subShapeName(shape));
            for (int i = 0; i < wavetableFrameSize; ++i)
                samples[static_cast<size_t>(shape) * wavetableFrameSize + static_cast<size_t>(i)]
                    = subShape(shape, static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
        }
        return Wavetable(samples.data(), subShapeCount, "SUB SHAPES", std::move(names));
    }();
    return table;
}

// Which shape a sub setting names. Rounded rather than truncated, and clamped,
// so a value arriving from a host lands on a real frame.
inline int subShapeOf(float wave)
{
    return juce::jlimit(0, subShapeCount - 1, juce::roundToInt(wave));
}

// How far the sub sits below the note. OCT 0 is one octave down -- the pitch
// the sub has always run at, and what makes it a sub -- so every patch written
// before the control existed still sounds as it did, and the reading matches
// Serum's, whose own sub is an octave below at zero.
inline float subRatio(float octave)
{
    return 0.5f * std::pow(2.0f, juce::jlimit(-2.0f, 2.0f, octave));
}

// How many LFOs there are. They are identical: none is defined in terms of
}
