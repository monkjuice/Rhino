#pragma once

// What every Forge translation unit pays for before it compiles a line of
// Forge: JUCE's module headers and the pieces of the standard library they are
// used with. Precompiled once per target, so splitting a file in two costs
// roughly what the new code costs rather than another full parse of JUCE.
//
// Deliberately no Forge header. These change many times an hour; JUCE's change
// when the pinned checkout moves. A Forge header in here would rebuild the
// precompiled header, and then every translation unit, on every edit — the
// opposite of what it is for.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
