#pragma once
#include "dsp/ScaleQuantizer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The original Ableton Push's MIDI protocol, and the arithmetic Rhino does
// on top of it, with no JUCE and no Session.
//
// Ableton never published the Push 1's protocol. Everything here comes from
// Live 9's own Push script, and from DrivenByMoss (Bitwig) and push-wrapper,
// which drive one without Live; see wiki/pages/ableton-push.md for the points
// still to be confirmed on the hardware. It is a pure header, as
// ClipGeometry.h is, so all of it is checked in the geometry runner without a
// device, a Session or a port.
namespace rhino::push
{
using Bytes = std::vector<std::uint8_t>;

// ---- ports -----------------------------------------------------------------
// A Push has two ports each way. Live drives it through the first, its Live
// port; every host without Live's script uses the second, its User port, for
// everything, and Rhino does the same. The Live port is recognised only so it
// can be kept away from the engine, which would otherwise merge its raw pads
// into All Ins.
enum class PortRole { none, live, user };

// Windows tells a second identical device apart by prefixing its name with
// "2- ", and inside the second port's parentheses too.
inline std::string_view withoutInstancePrefix(std::string_view name)
{
    std::size_t digits = 0;
    while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9')
        ++digits;
    if (digits > 0 && name.substr(digits, 2) == "- ")
        return name.substr(digits + 2);
    return name;
}

// Windows: "Ableton Push" and "MIDIIN2 (Ableton Push)" / "MIDIOUT2 (...)".
// macOS: "Ableton Push Live Port" and "Ableton Push User Port". A Push 2 is
// "Ableton Push 2", which speaks another protocol and is left alone.
inline PortRole portRole(std::string_view name)
{
    constexpr std::string_view push {"Ableton Push"};
    name = withoutInstancePrefix(name);
    if (name == push || name == "Ableton Push Live Port")
        return PortRole::live;
    if (name == "Ableton Push User Port")
        return PortRole::user;
    for (const std::string_view second : {std::string_view {"MIDIIN2 ("}, std::string_view {"MIDIOUT2 ("}})
        if (name.size() > second.size() + 1 && name.substr(0, second.size()) == second && name.back() == ')')
            if (withoutInstancePrefix(name.substr(second.size(), name.size() - second.size() - 1)) == push)
                return PortRole::user;
    return PortRole::none;
}

// ---- what it sends ---------------------------------------------------------
// Everything arrives on MIDI channel 1.
inline constexpr int padNoteFirst = 36, padCount = 64, padColumns = 8;
inline constexpr bool isPadNote(int note) { return note >= padNoteFirst && note < padNoteFirst + padCount; }
// Touching an encoder sends a note below the pads: the eight track encoders
// are 0-7, then the master, swing and tempo encoders, and the touch strip 12.
inline constexpr int touchMasterEncoder = 8, touchSwingEncoder = 9, touchTempoEncoder = 10, touchStrip = 12;
inline constexpr bool isTouchNote(int note) { return note >= 0 && note <= touchStrip; }

// Buttons send their CC at 127 when pressed and 0 when let go; the same CC
// sent back sets the button's light.
namespace cc
{
inline constexpr int tapTempo = 3, metronome = 9, tempoEncoder = 14, swingEncoder = 15;
inline constexpr int upperRowFirst = 20, master = 28, stop = 29, sceneFirst = 36;
inline constexpr int left = 44, right = 45, up = 46, down = 47, select = 48, shift = 49;
inline constexpr int note = 50, session = 51, addEffect = 52, addTrack = 53;
inline constexpr int octaveDown = 54, octaveUp = 55, repeat = 56, accent = 57, scales = 58, user = 59;
inline constexpr int mute = 60, solo = 61, pageLeft = 62, pageRight = 63;
inline constexpr int trackEncoderFirst = 71, masterEncoder = 79;
inline constexpr int play = 85, record = 86, newClip = 87, duplicate = 88, automation = 89, fixedLength = 90;
inline constexpr int lowerRowFirst = 102, device = 110, browse = 111, track = 112, clip = 113;
inline constexpr int volume = 114, panAndSend = 115, quantize = 116, doubleLoop = 117, deleteClip = 118, undo = 119;
}
inline constexpr int rowButtons = 8;
inline constexpr bool isUpperRow(int controller) { return controller >= cc::upperRowFirst && controller < cc::upperRowFirst + rowButtons; }
inline constexpr bool isLowerRow(int controller) { return controller >= cc::lowerRowFirst && controller < cc::lowerRowFirst + rowButtons; }
inline constexpr bool isTrackEncoder(int controller) { return controller >= cc::trackEncoderFirst && controller < cc::trackEncoderFirst + rowButtons; }

// The encoders are endless and say how far they turned, as a 7-bit two's
// complement number: 1 to 63 clockwise, 127 down to 64 anticlockwise. A fast
// turn sends bigger steps.
inline constexpr int encoderDelta(int value)
{
    value &= 0x7f;
    return value < 64 ? value : value - 128;
}

// ---- what it is sent -------------------------------------------------------
// Every Push sysex is Akai's, device 7F, product 15.
inline Bytes sysex(std::initializer_list<std::uint8_t> body)
{
    Bytes bytes {0xF0, 0x47, 0x7F, 0x15};
    bytes.insert(bytes.end(), body.begin(), body.end());
    bytes.push_back(0xF7);
    return bytes;
}
// User mode routes the pads, buttons and encoders to the User port. A Push
// starts in it, but one Live has crashed out of can be left in Live mode.
inline Bytes userModeSysex() { return sysex({0x62, 0x00, 0x01, 0x01}); }
// Pressure per pad rather than per channel, so it can follow the note a pad
// plays.
inline Bytes polyAftertouchSysex() { return sysex({0x5C, 0x00, 0x01, 0x00}); }
// The strip as a pitch bend that springs back, Live's own default.
inline Bytes touchStripPitchBendSysex() { return sysex({0x63, 0x00, 0x01, 0x05}); }

// The display is four lines of 68 characters, in four blocks of 17 that
// each hold two cells of eight with a space between - so a cell begins at
// one of these.
inline constexpr int displayLineCount = 4, displayWidth = 68, cellWidth = 8, cellCount = 8;
inline constexpr std::array<int, cellCount> cellOffsets {0, 9, 17, 26, 34, 43, 51, 60};
// The display's own glyphs below the printable range, and its selection
// arrow at the top of it.
namespace glyph
{
inline constexpr char barHalf = 3, barFull = 5, barEmpty = 6, selected = 127;
}

// Writing a line is what clears "Please start Live to play": there is no
// handshake to make.
inline Bytes displayLineSysex(int line, std::string_view text)
{
    Bytes bytes {0xF0, 0x47, 0x7F, 0x15, static_cast<std::uint8_t>(0x18 + std::clamp(line, 0, displayLineCount - 1)),
                 0x00, static_cast<std::uint8_t>(displayWidth + 1), 0x00};
    for (int i = 0; i < displayWidth; ++i)
        bytes.push_back(i < static_cast<int>(text.size()) ? static_cast<std::uint8_t>(text[static_cast<std::size_t>(i)] & 0x7f)
                                                          : std::uint8_t {' '});
    bytes.push_back(0xF7);
    return bytes;
}
inline Bytes clearLineSysex(int line)
{
    return sysex({static_cast<std::uint8_t>(0x1C + std::clamp(line, 0, displayLineCount - 1)), 0x00, 0x00});
}

// The display is ASCII. A UTF-8 name keeps its ASCII and shows a '?' for
// each character outside it, rather than one for each byte.
inline std::string toDisplayText(std::string_view text)
{
    std::string shown;
    for (const auto c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte < 0x7f)
            shown.push_back(static_cast<char>(byte));
        else if (byte >= 0xC0 || byte < 0x20)
            shown.push_back(byte == '\t' ? ' ' : '?');
        // A continuation byte belongs to the '?' already written.
    }
    return shown;
}

// A name squeezed into a cell: outer spaces go, then inner ones, then the
// end. Centred, as Live centres its cells.
inline std::string fitCell(std::string_view text, int width = cellWidth)
{
    auto shown = toDisplayText(text);
    const auto first = shown.find_first_not_of(' ');
    shown = first == std::string::npos ? std::string {} : shown.substr(first, shown.find_last_not_of(' ') - first + 1);
    if (static_cast<int>(shown.size()) > width)
        shown.erase(std::remove(shown.begin(), shown.end(), ' '), shown.end());
    if (static_cast<int>(shown.size()) > width)
        shown.resize(static_cast<std::size_t>(width));
    const auto pad = (width - static_cast<int>(shown.size())) / 2;
    return std::string(static_cast<std::size_t>(pad), ' ') + shown
         + std::string(static_cast<std::size_t>(width - pad - static_cast<int>(shown.size())), ' ');
}

// Eight cells laid out on one line. A cell may carry the selection glyph,
// which fitCell would sanitise away, so it is placed rather than fitted.
inline std::string displayLine(const std::array<std::string, cellCount>& cells)
{
    std::string line(static_cast<std::size_t>(displayWidth), ' ');
    for (int cell = 0; cell < cellCount; ++cell)
    {
        const auto& text = cells[static_cast<std::size_t>(cell)];
        const auto offset = static_cast<std::size_t>(cellOffsets[static_cast<std::size_t>(cell)]);
        for (std::size_t i = 0; i < text.size() && i < static_cast<std::size_t>(cellWidth); ++i)
            line[offset + i] = text[i];
    }
    return line;
}

// Free text across a line, left to right, cut at its end.
inline std::string displayText(std::string_view text)
{
    auto line = toDisplayText(text);
    line.resize(static_cast<std::size_t>(displayWidth), ' ');
    return line;
}

// A value from 0 to 1 as a bar across a cell: two steps a character, so
// sixteen in all, from the display's own split-bar glyphs.
inline std::string valueBar(double proportion, int width = cellWidth)
{
    const auto steps = static_cast<int>(std::lround(std::clamp(proportion, 0.0, 1.0) * width * 2));
    std::string bar;
    for (int i = 0; i < width; ++i)
        bar.push_back(steps >= 2 * (i + 1) ? glyph::barFull : steps == 2 * i + 1 ? glyph::barHalf : glyph::barEmpty);
    return bar;
}

// ---- lights ----------------------------------------------------------------
// A pad's light is a note-on whose velocity picks a colour from the Push's
// palette: black, three greys, then fourteen hues of four shades each.
namespace colour
{
inline constexpr int black = 0, darkGrey = 1, grey = 2, white = 3;
inline constexpr int hueCount = 14;
// light, full, dim, very dim
inline constexpr int full = 1, dim = 2, veryDim = 3;
inline constexpr int red = 0, amber = 1, yellow = 2, lime = 3, green = 4, spring = 5, turquoise = 6;
inline constexpr int cyan = 7, sky = 8, ocean = 9, blue = 10, orchid = 11, magenta = 12, pink = 13;
inline constexpr int hue(int which, int shade = full) { return 4 + 4 * which + shade; }
}

// The nearest of the palette's hues to a colour, by hue alone; a colour too
// grey to have one is a grey. The hues are not evenly spaced around the
// wheel, so each is matched to where it actually sits.
inline int paletteIndexFor(int red, int green, int blue, int shade = colour::full)
{
    const auto r = std::clamp(red, 0, 255) / 255.0, g = std::clamp(green, 0, 255) / 255.0, b = std::clamp(blue, 0, 255) / 255.0;
    const auto high = std::max({r, g, b}), low = std::min({r, g, b});
    const auto chroma = high - low;
    if (high <= 0.0 || chroma / high < 0.2)
        return high > 0.66 ? colour::white : high > 0.33 ? colour::grey : colour::darkGrey;
    double hue = high == r ? std::fmod((g - b) / chroma, 6.0) : high == g ? (b - r) / chroma + 2.0 : (r - g) / chroma + 4.0;
    hue *= 60.0;
    if (hue < 0.0)
        hue += 360.0;
    static constexpr std::array<double, colour::hueCount> centres {0, 30, 55, 80, 120, 145, 165, 185, 200, 215, 235, 270, 300, 330};
    auto best = 0;
    auto bestDistance = 360.0;
    for (int which = 0; which < colour::hueCount; ++which)
    {
        const auto apart = std::abs(hue - centres[static_cast<std::size_t>(which)]);
        const auto distance = std::min(apart, 360.0 - apart);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = which;
        }
    }
    return colour::hue(best, shade);
}

// A button's light. The ones that wear one colour take off, dim and lit;
// the upper row under the display is red, orange, yellow or green.
namespace led
{
inline constexpr int off = 0, dim = 1, dimBlink = 2, lit = 4, litBlink = 5;
inline constexpr int redDim = 1, redLit = 4, orangeDim = 7, orangeLit = 10;
inline constexpr int yellowDim = 13, yellowLit = 16, greenDim = 19, greenLit = 22;
}

// ---- what the pads play ----------------------------------------------------
// A pad is numbered 0 to 63 from the bottom-left, across and then up.
inline constexpr int padRow(int pad) { return pad / padColumns; }
inline constexpr int padColumn(int pad) { return pad % padColumns; }

enum class PadRole { none, root, inScale, outOfScale };

struct NoteLayout
{
    int root = 0;
    MusicalScale scale = MusicalScale::Major;
    // In key: every pad is a note of the scale. Chromatic: every semitone,
    // with the scale's notes lit and the rest dark.
    bool inKey = true;
    // The octave of the bottom-left pad, which plays the root: 3 puts it on
    // MIDI 48 for C.
    int octave = 3;
};
inline constexpr int lowestOctave = -1, highestOctave = 8;

inline int lowestNote(const NoteLayout& layout)
{
    return 12 * (std::clamp(layout.octave, lowestOctave, highestOctave) + 1) + ((layout.root % 12) + 12) % 12;
}

// The scale's notes as semitones above the root.
inline std::vector<int> scaleDegrees(MusicalScale scale)
{
    std::vector<int> degrees;
    const auto bits = musicalScaleBits(scale);
    for (int semitone = 0; semitone < 12; ++semitone)
        if ((bits >> semitone) & 1u)
            degrees.push_back(semitone);
    return degrees;
}

// Each row starts a fourth above the one below it, as on Live's default
// layout: in key that is the smallest number of the scale's own steps that
// reaches five semitones, so three in a major scale; chromatic, five.
inline int rowStep(const std::vector<int>& degrees)
{
    const auto count = static_cast<int>(degrees.size());
    for (int steps = 1; steps < count; ++steps)
        if (degrees[static_cast<std::size_t>(steps)] >= 5)
            return steps;
    return std::max(1, count / 2);
}

struct PadNote
{
    int note = -1;
    PadRole role = PadRole::none;
};

inline PadNote padNote(const NoteLayout& layout, int pad)
{
    if (pad < 0 || pad >= padCount)
        return {};
    const auto bottom = lowestNote(layout);
    const auto row = padRow(pad), column = padColumn(pad);
    const auto degrees = scaleDegrees(layout.scale);
    const auto chromatic = !layout.inKey || degrees.size() == 12;
    PadNote played;
    if (chromatic)
    {
        played.note = bottom + column + 5 * row;
        const auto fromRoot = (((played.note - bottom) % 12) + 12) % 12;
        played.role = fromRoot == 0 ? PadRole::root
                    : std::find(degrees.begin(), degrees.end(), fromRoot) != degrees.end() ? PadRole::inScale
                                                                                         : PadRole::outOfScale;
    }
    else
    {
        const auto count = static_cast<int>(degrees.size());
        const auto degree = column + rowStep(degrees) * row;
        played.note = bottom + 12 * (degree / count) + degrees[static_cast<std::size_t>(degree % count)];
        played.role = degree % count == 0 ? PadRole::root : PadRole::inScale;
    }
    if (played.note < 0 || played.note > 127)
        return {};
    return played;
}

// The Drum Rack's bank of sixteen on the bottom-left four by four, the
// lowest note at the bottom left as on the rack's own face. The rest of the
// grid plays nothing.
inline int drumPadNote(int firstNote, int pad)
{
    if (pad < 0 || pad >= padCount)
        return -1;
    const auto row = padRow(pad), column = padColumn(pad);
    if (row >= 4 || column >= 4)
        return -1;
    const auto note = firstNote + 4 * row + column;
    return note >= 0 && note <= 127 ? note : -1;
}

// ---- tap tempo -------------------------------------------------------------
// The mean of the last four intervals; a pause of two seconds starts again.
class TapTempo
{
public:
    std::optional<double> tap(double seconds)
    {
        if (count > 0 && (seconds - taps[static_cast<std::size_t>((count - 1) % kept)] > 2.0
                          || seconds <= taps[static_cast<std::size_t>((count - 1) % kept)]))
            count = 0;
        taps[static_cast<std::size_t>(count % kept)] = seconds;
        ++count;
        if (count < 2)
            return std::nullopt;
        const auto used = std::min(count, kept);
        const auto first = taps[static_cast<std::size_t>((count - used) % kept)];
        return 60.0 * (used - 1) / (seconds - first);
    }
    void reset() { count = 0; }

private:
    static constexpr int kept = 5;
    std::array<double, kept> taps {};
    int count = 0;
};
}
