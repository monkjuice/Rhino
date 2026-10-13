#pragma once
#include <array>
#include <cmath>

namespace rhino
{
// The set of pitches a corrected note is allowed to land on.
//
// This is deliberately a pure header with no JUCE and no engine, the way
// ClipGeometry.h is: every answer here is a function of twelve booleans and a
// number, so it can be checked exhaustively without a Session, a render or a
// sample rate.  The auto-tune device owns one of these; so does its editor,
// which draws the same mask as a keyboard.
using PitchClassMask = std::array<bool, 12>;

// The scales offered by the device's Scale chooser.  Chromatic is first
// because it is the "correct to any semitone" case and the natural default.
enum class MusicalScale
{
    Chromatic,
    Major,
    Minor,
    HarmonicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    WholeTone
};
inline constexpr int musicalScaleCount = 13;

inline const char* musicalScaleName(MusicalScale scale)
{
    switch (scale)
    {
        case MusicalScale::Chromatic:       return "Chromatic";
        case MusicalScale::Major:           return "Major";
        case MusicalScale::Minor:           return "Minor";
        case MusicalScale::HarmonicMinor:   return "Harm Minor";
        case MusicalScale::Dorian:          return "Dorian";
        case MusicalScale::Phrygian:        return "Phrygian";
        case MusicalScale::Lydian:          return "Lydian";
        case MusicalScale::Mixolydian:      return "Mixolydian";
        case MusicalScale::Locrian:         return "Locrian";
        case MusicalScale::MajorPentatonic: return "Major Pent";
        case MusicalScale::MinorPentatonic: return "Minor Pent";
        case MusicalScale::Blues:           return "Blues";
        case MusicalScale::WholeTone:       return "Whole Tone";
    }
    return "Chromatic";
}

// Semitone offsets from the root, as a bit per pitch class.  Written as
// twelve-bit literals so a scale reads as its own keyboard: bit 0 is the root.
inline unsigned int musicalScaleBits(MusicalScale scale)
{
    switch (scale)
    {
        case MusicalScale::Chromatic:       return 0b111111111111u;
        case MusicalScale::Major:           return 0b101010110101u;
        case MusicalScale::Minor:           return 0b010110101101u;
        case MusicalScale::HarmonicMinor:   return 0b100110101101u;
        case MusicalScale::Dorian:          return 0b011010101101u;
        case MusicalScale::Phrygian:        return 0b010110101011u;
        case MusicalScale::Lydian:          return 0b101011010101u;
        case MusicalScale::Mixolydian:      return 0b011010110101u;
        case MusicalScale::Locrian:         return 0b010101101011u;
        case MusicalScale::MajorPentatonic: return 0b001010010101u;
        case MusicalScale::MinorPentatonic: return 0b010010101001u;
        case MusicalScale::Blues:           return 0b010011101001u;
        case MusicalScale::WholeTone:       return 0b010101010101u;
    }
    return 0b111111111111u;
}

// The mask in absolute pitch classes, so index 0 is always C however the
// scale is rooted.  That is the order the editor's keyboard draws in.
inline PitchClassMask maskForScale(MusicalScale scale, int root)
{
    const auto bits = musicalScaleBits(scale);
    const auto offset = ((root % 12) + 12) % 12;
    PitchClassMask mask {};
    for (int degree = 0; degree < 12; ++degree)
        mask[static_cast<size_t>((degree + offset) % 12)] = (bits >> degree) & 1u;
    return mask;
}

inline bool maskIsEmpty(const PitchClassMask& mask)
{
    for (const auto allowed : mask)
        if (allowed) return false;
    return true;
}

// The nearest allowed note to a fractional MIDI note.  An empty mask means
// "correct to nothing", which is how the editor lets every key be switched
// off without the device falling silent or snapping to C.
//
// Ties go to the lower note, which matters only at exactly a quarter tone and
// keeps the answer stable rather than flickering between two neighbours.
inline float nearestAllowedNote(float midiNote, const PitchClassMask& mask)
{
    if (!std::isfinite(midiNote) || maskIsEmpty(mask))
        return midiNote;
    const auto centre = static_cast<int>(std::lround(midiNote));
    auto best = midiNote;
    auto bestDistance = 1.0e9f;
    // Six either side is enough: twelve consecutive semitones always contain
    // at least one allowed pitch class when the mask is not empty.
    for (int candidate = centre - 6; candidate <= centre + 6; ++candidate)
    {
        const auto pitchClass = static_cast<size_t>(((candidate % 12) + 12) % 12);
        if (!mask[pitchClass]) continue;
        const auto distance = std::abs(static_cast<float>(candidate) - midiNote);
        if (distance < bestDistance - 1.0e-6f)
        {
            bestDistance = distance;
            best = static_cast<float>(candidate);
        }
    }
    return best;
}

// Transposes an allowed note by whole scale degrees, so +1 in a pentatonic
// moves further than +1 in a major scale.  This is Auto Shift's Shift slider:
// it runs after correction, on a note that is already in the scale.
inline float shiftByScaleDegrees(float allowedNote, const PitchClassMask& mask, int degrees)
{
    if (degrees == 0 || !std::isfinite(allowedNote) || maskIsEmpty(mask))
        return allowedNote;
    const auto step = degrees > 0 ? 1 : -1;
    auto note = static_cast<int>(std::lround(allowedNote));
    for (int taken = 0; taken != degrees; taken += step)
    {
        // A degree is the next allowed semitone in that direction.  The bound
        // stops a mask with one note in it from looping forever.
        for (int walked = 0; walked < 12; ++walked)
        {
            note += step;
            if (mask[static_cast<size_t>(((note % 12) + 12) % 12)])
                break;
        }
    }
    return static_cast<float>(note);
}

// Note names in the spelling the device shows.  Sharps only: the device has no
// key signature to decide a flat from a sharp, and mixing the two reads worse
// than picking one.
inline const char* pitchClassName(int pitchClass)
{
    static const char* const names[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[static_cast<size_t>(((pitchClass % 12) + 12) % 12)];
}

inline float midiNoteToFrequency(float midiNote)
{
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

inline float frequencyToMidiNote(float frequency)
{
    return frequency > 0.0f ? 69.0f + 12.0f * std::log2(frequency / 440.0f) : 0.0f;
}
}
