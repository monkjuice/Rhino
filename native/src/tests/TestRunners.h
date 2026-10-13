#pragma once
#include <juce_core/juce_core.h>
#include <optional>

namespace rhino
{

// Runs the test runner or profiler a command-line flag names and gives its
// exit code, or nothing when the flag is not one of theirs. Each runner
// decides whether it opens an audio device.
std::optional<int> runTestFlag(const juce::String& args);

}
