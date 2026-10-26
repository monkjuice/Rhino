#pragma once

// Every area Rhino Duck is tested by, one entry point per file. This list and
// the table in DuckTestMain.cpp are the whole registry: a new area is a new
// .cpp, a line here, a line there and two lines in CMakeLists.txt.
namespace rhino::duck::tests
{
// DuckTestsCurves.cpp: the sixteen curves, their families, and Hold, Release,
// Attack, Depth and Shift.
void curveTests();

// DuckTestsEngine.cpp: the clock in every mode, the mix, the band split, the
// trigger latency, the anti-click fade and the curve's adjustments, all
// measured on rendered audio, and what the scope and meter are given.
void engineTests();

// DuckTestsProcessor.cpp: parameters, state, buses, latency, the host clock,
// the MIDI filters and the curves' memory.
void processorTests();

// DuckTestsLayout.cpp: the panel's geometry, the editor painted, the strip
// editing the chosen curve and the duck meter.
void layoutTests();
}
