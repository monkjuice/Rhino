#pragma once

// Development tools in the test binary, none a test case.
namespace rhino::duck::tests
{
// --snapshot <out.png> [SYNC|MIDI|AUDIO] [BAND]: renders the panel to a PNG
// after a couple of seconds of a bassline under a kick, so the scope has
// something to show, without opening a window.
int runSnapshot(int argc, char** argv);
}
