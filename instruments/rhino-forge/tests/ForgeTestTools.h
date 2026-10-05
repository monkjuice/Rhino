#pragma once

#include <juce_core/juce_core.h>

// The two things the test binary does that are not checks: render the panel to
// a file, and time a frame of it. Both open a real editor, which is why they
// live beside the tests rather than in the plugin - and why neither is a CTest
// case. See README.md for the arguments.
namespace rhino::forge
{
class Processor;
}

namespace rhino::forge::tests
{
int runSnapshot(int argc, char** argv);
int runProfile(int argc, char** argv);

// Render a deliberately busy patch and write the samples raw, so two builds of
// the engine can be compared byte for byte. See ForgeTestRender.cpp.
int runRender(int argc, char** argv);

// Time blocks of that patch and count what they allocate, at a small and an
// ordinary block size. The audio counterpart of runProfile.
int runProfileAudio(int argc, char** argv);

// The patch both of those play: as much of the voice at once as one render can
// touch. Also what the engine area's real-time check plays.
void everythingPatch(Processor& processor);
}
