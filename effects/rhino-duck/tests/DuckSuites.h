#pragma once

// Every area Rhino Duck is tested by, one entry point per file. This list and
// the table in DuckTestMain.cpp are the whole registry: a new area is a new
// .cpp, a line here, a line there and two lines in CMakeLists.txt.
namespace rhino::duck::tests
{
// DuckTestsCurves.cpp: the sixteen curves, their families, Slope and Shift.
void curveTests();

// DuckTestsEngine.cpp: the clock in every mode, the mix, the band split, the
// trigger latency and the anti-click fade, all measured on rendered audio.
void engineTests();

// DuckTestsProcessor.cpp: parameters, state, buses, latency, the host clock
// and the MIDI filters.
void processorTests();

// DuckTestsLayout.cpp: the panel's geometry and the editor painted.
void layoutTests();
}
