#pragma once
#include "DeviceEditorPanel.h"
#include "instruments/DrumRackDevice.h"
#include <array>

// Shared by the Drum Rack face's two translation units of DeviceEditorPanel,
// DeviceEditorPanelDrums.cpp (the map, the pads and their menus) and
// DeviceEditorPanelDrumSample.cpp (the selected pad's side), and nothing else.
namespace rhino::drumface
{
constexpr int padColumns = DrumRackDevice::rowSize;
constexpr int padsShown = DrumRackDevice::bankSize;
constexpr int padRows = padsShown / padColumns;
constexpr int padWidth = 66, padHeight = 34, padGap = 2;
constexpr int gridWidth = padColumns * padWidth + (padColumns - 1) * padGap;
constexpr int gridHeight = padRows * padHeight + (padRows - 1) * padGap;
// The map: every note, four to a row as the pads are, 32 rows from the
// lowest at the bottom. A cell and the gap after it.
constexpr int mapRows = DrumRackDevice::padCount / padColumns;
constexpr int mapCellWidth = 6, mapCellHeight = 4;
constexpr int mapWidth = padColumns * mapCellWidth, mapHeight = mapRows * mapCellHeight;
constexpr int gapAfterMap = 6, gapAfterGrid = 10;
constexpr int padButtonWidth = 18, padButtonHeight = 11;
// The selected pad's side: a heading row, the modes beside the picture, and a
// row of cells under both, six for the pad's controls and up to five for its
// sample's.
constexpr int headerRow = 16;
constexpr int pictureHeight = 66;
constexpr int modeWidth = 50;
constexpr int controlCells = DrumRackDevice::controlCount;
constexpr int sampleCells = 5;
constexpr int cellWidth = 48, dividerWidth = 6;
constexpr int editorWidth = (controlCells + sampleCells) * cellWidth + dividerWidth;
constexpr int faceWidth = 4 + mapWidth + gapAfterMap + gridWidth + gapAfterGrid + editorWidth + 4;

// The sample editor's settings, a knob each.
enum class SampleKnob { start, end, fadeIn, fadeOut, attack, sustain, release, sensitivity, divisions, count };
constexpr int sampleKnobCount = static_cast<int>(SampleKnob::count);
// What a cell of the sample's row holds: one of its knobs, the chooser for
// how Slice cuts, or the button that spreads the slices across pads.
struct SampleCell
{
    enum Kind { knob, sliceBy, spread } kind = knob;
    SampleKnob setting = SampleKnob::start;
};
std::vector<SampleCell> sampleCellsFor(DrumRackEngine::PlayMode);

// The sample's settings (DeviceEditorPanelDrumSampleControls.cpp): what each
// knob is, how its reading is written, the cells a pad's playback shows -- a
// count of parts in place of a sensitivity when Slice cuts into divisions --
// and whether a pad is a sample the editor can work on, one whose file was read.
struct SampleKnobSpec
{
    const char* caption;
    const char* action;
    float minimum, maximum, defaultValue;
    // Where the knob's middle is, for a skewed range; zero for a straight one.
    float midpoint;
    double interval;
};
SampleKnobSpec specOf(SampleKnob);
juce::String textOf(SampleKnob, const DrumRackEngine::Playback&, double fileSeconds);
std::vector<SampleCell> cellsOf(const DrumRackEngine::Playback&);
bool playsSample(const DrumRackDevice::Pad&);
// What a pad is called before it holds anything.
juce::String padTitle(int note);

struct Layout
{
    juce::Rectangle<int> map;
    // The sixteen shown pads, the lowest note's first.
    std::array<juce::Rectangle<int>, padsShown> pad, mute, play, solo;
    juce::Rectangle<int> editor, nameArea, soundChooser, chokeChooser, picture, loop;
    // Classic, 1-Shot and Slice, top to bottom, as Live stacks them.
    std::array<juce::Rectangle<int>, 3> mode;
    juce::Rectangle<int> divider;
    std::array<juce::Rectangle<int>, controlCells + sampleCells> cell;
};
Layout layoutFor(juce::Rectangle<int> panel);
juce::Rectangle<int> knobIn(juce::Rectangle<int> cell);
juce::Rectangle<int> captionIn(juce::Rectangle<int> cell);
juce::Rectangle<int> valueIn(juce::Rectangle<int> cell);
juce::Rectangle<int> automationIn(juce::Rectangle<int> cell);
// A note's cell on the map.
juce::Rectangle<float> mapCell(const Layout&, int note);
// The note the pad at a place on the map belongs to, or -1.
int mapNoteAt(const Layout&, juce::Point<int>);
// The mode a stacked button stands for.
DrumRackEngine::PlayMode modeOfButton(int button);

DrumRackDevice* drumsIn(Session&, int track, int slot);

// The inks of the face. A pad's ground, empty and filled, and the inks that
// say what fills it: warm for a sample, as the browser draws a sample's row,
// cool for a synth.
inline const juce::Colour emptyPadInk { 0xff222222 };
inline const juce::Colour filledPadInk { 0xff2f3236 };
inline const juce::Colour padButtonInk { 0xff1b1d20 };
inline const juce::Colour sampleInk { 0xffe09a70 };
inline const juce::Colour synthInk { 0xff8cc5d2 };
inline const juce::Colour muteInk { 0xffe0c14a };
inline const juce::Colour soloInk { 0xff5f9ee0 };
inline const juce::Colour wellInk { 0xff121416 };
inline const juce::Colour missingInk { 0xffd4564e };
inline const juce::Colour lanedInk { 0xff2f7d55 };
inline const juce::Colour overriddenInk { 0xff8a6a2e };
// A key arriving on the map, Live's orange.
inline const juce::Colour keyInk { 0xffff9a2e };

// drawSnappedText, with each line kept once it is shaped (see the .cpp).
void drawLine(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
              juce::Justification = juce::Justification::centredLeft, bool elide = false);
void drawChooser(juce::Graphics&, juce::Rectangle<int> area, const juce::String& text, bool usable);
// A small drawn button, lit or not: a pad's M and S, a mode, Loop.
void drawToggle(juce::Graphics&, juce::Rectangle<int> area, const juce::String& text, juce::Colour onInk, bool lit,
                bool usable, float fontSize = 7.5f);
juce::String secondsText(double seconds);
}
