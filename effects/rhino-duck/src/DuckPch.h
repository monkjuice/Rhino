#pragma once

// What every Rhino Duck translation unit pays for before it compiles a line
// of its own: JUCE's module headers and the pieces of the standard library
// they are used with, precompiled once per target. Deliberately no Duck
// header, which change far more often than the pinned JUCE does.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
