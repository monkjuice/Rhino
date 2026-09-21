#pragma once

// Every area Forge can be tested by, one entry point per file. This list and
// the table in ForgeTestMain.cpp are the whole registry: a new area is a new
// .cpp, a line here, a line there and a line in CMakeLists.txt. Nothing is
// discovered and nothing self-registers, for the reason a static library's
// unreferenced objects are dropped by the linker without an error.
//
// Each entry runs the suites of one translation unit, so the argument that
// selects it is also the file that changes when you are working on it.
namespace rhino::forge::tests
{
// ForgeTestsLayout.cpp — what is declared, where it lands, and that the layout
// and the parameter list agree in both directions.
void layoutTests();

// ForgeTestsDisplays.cpp — the filter response, the envelope, the cached metal.
void displayTests();

// ForgeTestsPresets.cpp — saving, loading, and opening an older preset.
void presetTests();

// ForgeTestsOscillator.cpp — tuning, the unison stack, the shapes, the sub, and
// the band-limited copies.
void oscillatorTests();

// ForgeTestsWarp.cpp — the two warp stages under each oscillator.
void warpTests();

// ForgeTestsFilter.cpp — filter routing.
void filterTests();

// ForgeTestsMixer.cpp — the eight channels, the sends and the two busses.
void mixerTests();

// ForgeTestsFx.cpp — the three effects racks, rendered and drawn.
void fxTests();

// ForgeTestsModulation.cpp — the matrix and the macros.
void modulationTests();

// ForgeTestsEnvelope.cpp — the four envelopes.
void envelopeTests();

// ForgeTestsLfo.cpp — the six LFOs, free, triggered and synced.
void lfoTests();

// ForgeTestsTable.cpp — the wavetable editor and the files it loads.
void tableTests();

// ForgeTestsVoicing.cpp — polyphony, mono, stealing and the voice tail.
void voicingTests();

// ForgeTestsArp.cpp — the shapes, the clock, and the notes the arp emits.
void arpTests();

// ForgeTestsEngine.cpp — every source audible on its own, and an extreme patch
// that stays finite and inside full scale.
void engineTests();
}
