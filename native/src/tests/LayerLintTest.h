#pragma once

namespace rhino
{
// Reads the UI and the shell's sources and throws if one reaches past
// Session into the engine (LayerLintTest.cpp). Run by
// --arrangement-geometry-test; it opens no audio device.
void runLayerLintTest();
}
