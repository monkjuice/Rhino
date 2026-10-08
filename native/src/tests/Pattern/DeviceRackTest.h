#pragma once

namespace rhino
{
void runPatternDeviceRackTest();
// The Drum Rack's face: its pads, the knobs that follow the selected one, and
// sounds dropped onto pads from the browser.
void runDrumRackFaceTest();
// The Drum Rack in a window of its own (DrumRackWindowTest.cpp): how it
// opens, its stacked face, drops on it, and how it follows its rack.
void runDrumRackWindowTest();
int runArpSnapshotTest();
}
