#include "PushProtocolTest.h"
#include "app/PushProtocol.h"
#include <stdexcept>
#include <string>

namespace rhino
{
namespace
{
void require(bool condition, const char* what)
{
    if (!condition)
        throw std::runtime_error(std::string("Push protocol: ") + what);
}
}

void checkPushProtocol()
{
    using namespace push;

    // The messages that set the Push up, byte for byte as Live sends them.
    require(userModeSysex() == Bytes {0xF0, 0x47, 0x7F, 0x15, 0x62, 0x00, 0x01, 0x01, 0xF7}, "User mode");
    require(polyAftertouchSysex() == Bytes {0xF0, 0x47, 0x7F, 0x15, 0x5C, 0x00, 0x01, 0x00, 0xF7}, "poly aftertouch");
    require(touchStripPitchBendSysex() == Bytes {0xF0, 0x47, 0x7F, 0x15, 0x63, 0x00, 0x01, 0x05, 0xF7},
            "touch strip as pitch bend");
    require(clearLineSysex(3) == Bytes {0xF0, 0x47, 0x7F, 0x15, 0x1F, 0x00, 0x00, 0xF7}, "clearing the bottom line");
    {
        const auto line = displayLineSysex(2, "AB");
        require(line.size() == 77, "a display line is 68 characters in 77 bytes");
        require(line[4] == 0x1A && line[5] == 0x00 && line[6] == 0x45 && line[7] == 0x00, "a line's header");
        require(line[8] == 'A' && line[9] == 'B' && line[10] == ' ' && line[75] == ' ', "a short line is padded");
        require(line.back() == 0xF7, "a line ends its sysex");
        require(displayLineSysex(0, std::string(1, glyph::selected))[8] == 127, "the selection arrow survives");
    }

    // Encoders count in two's complement.
    require(encoderDelta(1) == 1 && encoderDelta(63) == 63, "clockwise steps");
    require(encoderDelta(127) == -1 && encoderDelta(64) == -64, "anticlockwise steps");

    // Cells: squeezed, ASCII, centred, and at the display's own offsets.
    require(fitCell("Cutoff") == " Cutoff ", "a short name is centred");
    require(fitCell("Filter Cutoff") == "FilterCu", "a long name loses its spaces, then its end");
    require(fitCell("  Gain  ") == "  Gain  ", "outer spaces do not count");
    require(fitCell("Tune \xC3\xBC") == " Tune ? ", "one '?' for a character the display lacks");
    {
        std::array<std::string, cellCount> cells;
        cells[1] = "X";
        cells[7] = "12345678";
        const auto line = displayLine(cells);
        require(line.size() == static_cast<size_t>(displayWidth), "a line is 68 wide");
        require(line[9] == 'X' && line[60] == '1' && line[67] == '8', "cells start at 0, 9, 17, 26, 34, 43, 51, 60");
    }
    require(valueBar(0.0) == std::string(8, glyph::barEmpty), "an empty bar");
    require(valueBar(1.0) == std::string(8, glyph::barFull), "a full bar");
    require(valueBar(0.5) == std::string(4, glyph::barFull) + std::string(4, glyph::barEmpty), "half a bar");
    require(valueBar(1.0 / 16.0)[0] == glyph::barHalf, "a bar moves in half characters");

    // Ports: the User port is Rhino's, the Live port is only kept from the
    // engine, and a Push 2 is not a Push.
    require(portRole("MIDIIN2 (Ableton Push)") == PortRole::user, "Windows' User port in");
    require(portRole("MIDIOUT2 (Ableton Push)") == PortRole::user, "Windows' User port out");
    require(portRole("Ableton Push") == PortRole::live, "Windows' Live port");
    require(portRole("2- Ableton Push") == PortRole::live, "a second Push's Live port");
    require(portRole("MIDIIN2 (2- Ableton Push)") == PortRole::user, "a second Push's User port");
    require(portRole("Ableton Push User Port") == PortRole::user, "macOS's User port");
    require(portRole("Ableton Push Live Port") == PortRole::live, "macOS's Live port");
    require(portRole("Ableton Push 2") == PortRole::none && portRole("MIDIIN2 (Ableton Push 2)") == PortRole::none,
            "a Push 2 is left alone");
    require(portRole("Launchkey Mini") == PortRole::none && portRole("MIDIIN2 (Launchkey Mini)") == PortRole::none,
            "another controller is left alone");

    // In key: across a scale step, up a fourth, the root at the bottom left.
    {
        NoteLayout cMajor;
        require(padNote(cMajor, 0).note == 48 && padNote(cMajor, 0).role == PadRole::root, "C major starts on C3");
        require(padNote(cMajor, 1).note == 50 && padNote(cMajor, 2).note == 52, "across is up the scale");
        require(padNote(cMajor, 7).note == 60 && padNote(cMajor, 7).role == PadRole::root, "the eighth pad is the octave");
        require(padNote(cMajor, 8).note == 53, "a row up is a fourth up");
        require(padNote(cMajor, 63).note == 96, "the top right of C major");
        NoteLayout aMinor;
        aMinor.root = 9;
        aMinor.scale = MusicalScale::Minor;
        require(padNote(aMinor, 0).note == 57 && padNote(aMinor, 1).note == 59 && padNote(aMinor, 2).note == 60,
                "A minor starts on A");
        require(rowStep(scaleDegrees(MusicalScale::MinorPentatonic)) == 2, "a fourth is two steps of a minor pentatonic");
    }
    // Chromatic: every semitone, the scale's notes marked.
    {
        NoteLayout chromatic;
        chromatic.inKey = false;
        require(padNote(chromatic, 0).role == PadRole::root && padNote(chromatic, 1).note == 49, "chromatic runs in semitones");
        require(padNote(chromatic, 1).role == PadRole::outOfScale && padNote(chromatic, 2).role == PadRole::inScale,
                "C sharp is outside C major, D inside");
        require(padNote(chromatic, 8).note == 53, "a chromatic row is a fourth too");
    }
    // Nothing past the ends of MIDI.
    {
        NoteLayout high;
        high.root = 11;
        high.octave = highestOctave;
        require(padNote(high, 0).note == 119, "the highest octave starts where it says");
        require(padNote(high, 63).note == -1 && padNote(high, 63).role == PadRole::none, "a pad past 127 plays nothing");
        NoteLayout low;
        low.octave = lowestOctave - 5;
        require(padNote(low, 0).note == 0, "the octave is clamped at the bottom");
    }

    // Drums: the bottom-left four by four, lowest note at the bottom left.
    require(drumPadNote(36, 0) == 36 && drumPadNote(36, 3) == 39 && drumPadNote(36, 8) == 40, "the first drum rows");
    require(drumPadNote(36, 27) == 51, "the top right drum pad");
    require(drumPadNote(36, 4) == -1 && drumPadNote(36, 32) == -1, "the rest of the grid plays nothing");
    require(drumPadNote(124, 27) == -1, "no drum past 127");

    // Colours: to the nearest hue in the palette, greys to greys.
    require(paletteIndexFor(255, 0, 0) == colour::hue(colour::red), "red");
    require(paletteIndexFor(0, 255, 0) == colour::hue(colour::green), "green");
    require(paletteIndexFor(0, 0, 255) == colour::hue(colour::blue), "blue");
    require(paletteIndexFor(255, 128, 0) == colour::hue(colour::amber), "orange is amber");
    require(paletteIndexFor(255, 0, 0, colour::dim) == colour::hue(colour::red, colour::dim), "a dim shade");
    require(paletteIndexFor(255, 255, 255) == colour::white && paletteIndexFor(128, 128, 128) == colour::grey
                && paletteIndexFor(20, 20, 20) == colour::darkGrey,
            "greys");

    // Tap tempo: the mean of the intervals, starting again after a pause.
    {
        TapTempo taps;
        require(!taps.tap(10.0).has_value(), "one tap is no tempo");
        require(std::abs(*taps.tap(10.5) - 120.0) < 1.0e-9, "two taps half a second apart");
        require(std::abs(*taps.tap(11.0) - 120.0) < 1.0e-9, "a steady third");
        require(std::abs(*taps.tap(11.4) - 60.0 * 3.0 / 1.4) < 1.0e-9, "the mean of the intervals");
        require(!taps.tap(15.0).has_value(), "a pause starts again");
    }
}
}
