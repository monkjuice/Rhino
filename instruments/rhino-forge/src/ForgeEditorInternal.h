#pragma once

#include "ForgeEditor.h"

// Private to the Editor's own translation units, in the spelling Rhino already
// uses for this: one class defined across several .cpp files, sharing what used
// to sit in an anonymous namespace. Nothing else should include it.
namespace rhino::forge
{
// The destination index a parameter corresponds to, or 0 if the matrix cannot
// point at it.
inline int destinationFor(const juce::String& parameterId)
{
    for (int i = 1; i < destinationCount; ++i)
        if (parameterId == destinations()[static_cast<size_t>(i)].id) return i;
    return 0;
}

inline juce::String slotParameter(int slot, const char* suffix)
{
    return "mod" + juce::String(slot + 1) + suffix;
}

// How many semitones of the piano the computer keys can reach at once. JUCE's
// default qwerty mapping is "awsedftgyhujkolp;" laid on the note offsets 0..16
// from the C of the mapping octave, so the reach is a C to the E an octave and
// a third above it.
inline constexpr int computerKeySpan = 17;

inline bool isFxModule(const ui::Module& module)
{
    return module.id != nullptr && module.id[0] == 'f' && module.id[1] == 'x' && module.id[2] == '\0';
}
}
