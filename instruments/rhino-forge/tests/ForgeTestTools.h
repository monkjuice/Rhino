#pragma once

#include <juce_core/juce_core.h>

// The two things the test binary does that are not checks: render the panel to
// a file, and time a frame of it. Both open a real editor, which is why they
// live beside the tests rather than in the plugin - and why neither is a CTest
// case. See README.md for the arguments.
namespace rhino::forge::tests
{
int runSnapshot(int argc, char** argv);
int runProfile(int argc, char** argv);

// Render a deliberately busy patch and write the samples raw, so two builds of
// the engine can be compared byte for byte. See ForgeTestRender.cpp.
int runRender(int argc, char** argv);
}
