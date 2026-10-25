#pragma once

#include "ForgeNoteGuide.h"
#include "ForgeDisplays.h"
#include <array>
#include <functional>

// The note guide's two faces: the door on the keyboard shelf that says what the
// guide is set to, and the panel it opens over the modulators, where a root, a
// chord and a scale are picked straight off the panel and what they make is
// spelt out. Neither changes MIDI or the sound; both edit the editor's
// NoteGuideState, which the keys are drawn from.
namespace rhino::forge::ui
{
// --- Where the panel puts things ----------------------------------------------
//
// Three columns over a readout: ROOT is an octave of keys to pick it on, CHORD
// and SCALE are grids of buttons, and the readout under them lists the notes
// each makes and what each note is to the root. Every column has the same four
// rows, so the grids line up across the panel, and each title carries the OFF
// for its column at its right-hand end.
inline constexpr int guideTitleHeight = 18;
inline constexpr int guideTitleGap = 6;
inline constexpr int guideCellGap = 5;
inline constexpr int guideColumnGap = 20;
inline constexpr int guideMinCellHeight = 18;
inline constexpr int guideMaxCellHeight = 36;
inline constexpr int guideReadoutGap = 10;
inline constexpr int guideMinReadout = 64;
inline constexpr int guideMaxReadout = 80;
inline constexpr int guideOffWidth = 44;
// The size a cell's caption is set at. The layout test measures every caption
// against its cell at this size.
inline constexpr float guideCellTextSize = 11.5f;

// The captions on the buttons, by GuideChord and GuideScale. Short forms of the
// menu names the guide's fields used to list, in capitals as every other
// button on the panel is.
inline constexpr std::array<const char*, 12> guideChordCellNames {
    "OFF", "TRIAD", "6TH", "7TH", "MAJ7", "AUG", "DIM", "AUG7", "DIM7", "HALF-DIM", "SUS2", "SUS4"
};
inline constexpr std::array<const char*, 13> guideScaleCellNames {
    "OFF", "MAJOR", "NAT MINOR", "MAJ PENTA", "MIN PENTA", "BLUES", "HARM MINOR", "MEL MINOR",
    "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN"
};

struct NoteGuideLayout
{
    juce::Rectangle<int> rootTitle, chordTitle, scaleTitle;
    juce::Rectangle<int> rootOff, chordOff, scaleOff;
    // The twelve keys a root is picked on.
    juce::Rectangle<int> octave;
    // By GuideChord and GuideScale. Entry 0 is the column's OFF, so a press
    // and a paint can walk one list.
    std::array<juce::Rectangle<int>, 12> chords {};
    std::array<juce::Rectangle<int>, 13> scales {};
    juce::Rectangle<int> major, minor;
    // The engraved rules between the columns: their x, and how far down they
    // run.
    std::array<int, 2> rules {};
    int rulesTop = 0, rulesBottom = 0;
    juce::Rectangle<int> readout, chordReadout, scaleReadout;
};

// One cell of a row cut into `columns`, `span` wide. Worked from the row's two
// edges rather than from a rounded width, so the last cell meets the row's
// right-hand edge exactly.
inline juce::Rectangle<int> guideCell(juce::Rectangle<int> row, int columns, int column, int span = 1)
{
    const auto unit = row.getWidth() + guideCellGap;
    const auto left = row.getX() + unit * column / columns;
    const auto right = row.getX() + unit * (column + span) / columns - guideCellGap;
    return {left, row.getY(), right - left, row.getHeight()};
}

inline NoteGuideLayout noteGuideLayout(juce::Rectangle<int> area)
{
    NoteGuideLayout layout;
    // The rows take what they need up to a size past which a bigger button is
    // no easier to hit, the readout takes the rest up to its own cap, and
    // whatever is left over at a tall window goes above and below the whole,
    // so it stays one composition rather than a grid with a gap under it.
    const auto titleBlock = guideTitleHeight + guideTitleGap;
    const auto rowHeight = juce::jlimit(guideMinCellHeight, guideMaxCellHeight,
        (area.getHeight() - titleBlock - guideReadoutGap - guideMinReadout - 3 * guideCellGap) / 4);
    const auto gridHeight = 4 * rowHeight + 3 * guideCellGap;
    const auto readoutHeight = juce::jlimit(guideMinReadout, guideMaxReadout,
                                            area.getHeight() - titleBlock - gridHeight - guideReadoutGap);
    const auto total = titleBlock + gridHeight + guideReadoutGap + readoutHeight;
    auto body = area.withTrimmedTop(juce::jmax(0, (area.getHeight() - total) / 2)).withHeight(total);

    layout.readout = body.removeFromBottom(readoutHeight);
    body.removeFromBottom(guideReadoutGap);
    const auto inside = layout.readout.reduced(12, 0);
    layout.chordReadout = inside.withWidth(inside.getWidth() * 42 / 100);
    layout.scaleReadout = inside.withLeft(layout.chordReadout.getRight() + 16);

    const auto spread = body.getWidth() - 2 * guideColumnGap;
    auto rootColumn = body.removeFromLeft(spread * 27 / 100);
    body.removeFromLeft(guideColumnGap);
    auto chordColumn = body.removeFromLeft(spread * 38 / 100);
    body.removeFromLeft(guideColumnGap);
    auto scaleColumn = body;
    layout.rules = {rootColumn.getRight() + guideColumnGap / 2, chordColumn.getRight() + guideColumnGap / 2};
    layout.rulesTop = rootColumn.getY();
    layout.rulesBottom = rootColumn.getBottom();

    const auto titled = [] (juce::Rectangle<int>& column, juce::Rectangle<int>& title, juce::Rectangle<int>& off)
    {
        title = column.removeFromTop(guideTitleHeight);
        off = title.removeFromRight(guideOffWidth);
        column.removeFromTop(guideTitleGap);
    };
    titled(rootColumn, layout.rootTitle, layout.rootOff);
    titled(chordColumn, layout.chordTitle, layout.chordOff);
    titled(scaleColumn, layout.scaleTitle, layout.scaleOff);
    layout.chords[0] = layout.chordOff;
    layout.scales[0] = layout.scaleOff;

    const auto row = [rowHeight] (juce::Rectangle<int> column, int index)
    {
        return column.withY(column.getY() + index * (rowHeight + guideCellGap)).withHeight(rowHeight);
    };
    layout.octave = rootColumn.withHeight(gridHeight);

    // The four chords the quality switch changes, the switch under them, and
    // the seven that are what they are.
    for (int chord = 1; chord <= 4; ++chord)
        layout.chords[static_cast<size_t>(chord)] = guideCell(row(chordColumn, 0), 4, chord - 1);
    layout.major = guideCell(row(chordColumn, 1), 4, 0, 2);
    layout.minor = guideCell(row(chordColumn, 1), 4, 2, 2);
    for (int chord = 5; chord <= 11; ++chord)
        layout.chords[static_cast<size_t>(chord)] = guideCell(row(chordColumn, 2 + (chord - 5) / 4), 4, (chord - 5) % 4);

    for (int scale = 1; scale <= 12; ++scale)
        layout.scales[static_cast<size_t>(scale)] = guideCell(row(scaleColumn, (scale - 1) / 3), 3, (scale - 1) % 3);
    return layout;
}

// --- The octave a root is picked on ---------------------------------------------

inline constexpr std::array<int, 7> guideWhitePitches {0, 2, 4, 5, 7, 9, 11};

inline bool guidePitchIsBlack(int pitch)
{
    return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
}

// One key of that octave. A black key straddles the edge between the two white
// keys either side of it, as on the piano under it.
inline juce::Rectangle<int> guideOctaveKey(juce::Rectangle<int> octave, int pitch)
{
    const auto edge = [&octave] (int white) { return octave.getX() + octave.getWidth() * white / 7; };
    if (!guidePitchIsBlack(pitch))
    {
        int white = 0;
        while (guideWhitePitches[static_cast<size_t>(white)] != pitch) ++white;
        return {edge(white), octave.getY(), edge(white + 1) - edge(white), octave.getHeight()};
    }
    // The white edge each black key sits on: C# on the first, D# the second,
    // F#, G# and A# the fourth, fifth and sixth.
    const auto boundary = pitch < 5 ? (pitch + 1) / 2 : (pitch + 2) / 2;
    const auto width = juce::roundToInt(static_cast<float>(octave.getWidth()) * 0.62f / 7.0f);
    return juce::Rectangle<int>(width, octave.getHeight() * 60 / 100)
        .withPosition(edge(boundary) - width / 2, octave.getY());
}

// The key under a point, black keys first since they stand on the white, or
// -1.
inline int guideOctaveKeyAt(juce::Rectangle<int> octave, juce::Point<int> at)
{
    if (!octave.contains(at)) return -1;
    for (const auto pitch : {1, 3, 6, 8, 10})
        if (guideOctaveKey(octave, pitch).contains(at)) return pitch;
    for (const auto pitch : guideWhitePitches)
        if (guideOctaveKey(octave, pitch).contains(at)) return pitch;
    return -1;
}

// --- What a press does ------------------------------------------------------------

enum class GuideTarget { none, root, rootOff, chord, major, minor, scale };

struct GuideHit
{
    GuideTarget target = GuideTarget::none;
    int index = -1;
    bool operator==(const GuideHit& other) const { return target == other.target && index == other.index; }
    bool operator!=(const GuideHit& other) const { return !(*this == other); }
};

inline GuideHit noteGuideHit(const NoteGuideLayout& layout, juce::Point<int> at)
{
    if (const auto pitch = guideOctaveKeyAt(layout.octave, at); pitch >= 0) return {GuideTarget::root, pitch};
    if (layout.rootOff.contains(at)) return {GuideTarget::rootOff, -1};
    for (int chord = 0; chord < static_cast<int>(layout.chords.size()); ++chord)
        if (layout.chords[static_cast<size_t>(chord)].contains(at)) return {GuideTarget::chord, chord};
    if (layout.major.contains(at)) return {GuideTarget::major, 0};
    if (layout.minor.contains(at)) return {GuideTarget::minor, 1};
    for (int scale = 0; scale < static_cast<int>(layout.scales.size()); ++scale)
        if (layout.scales[static_cast<size_t>(scale)].contains(at)) return {GuideTarget::scale, scale};
    return {};
}

// Whether a press on this target can do anything now. A chord or a scale means
// nothing until there is a root to build it on, and the quality switch nothing
// unless the chord is one of the four it changes; both are greyed meanwhile, as
// the fields they replaced were.
inline bool guideHitLive(const NoteGuideState& state, GuideHit hit)
{
    switch (hit.target)
    {
        case GuideTarget::root:
        case GuideTarget::rootOff: return true;
        case GuideTarget::chord:
        case GuideTarget::scale:   return state.root >= 0;
        case GuideTarget::major:
        case GuideTarget::minor:   return state.root >= 0 && guideChordHasQuality(state.chord);
        case GuideTarget::none:    break;
    }
    return false;
}

// What a press on this target makes of the state. True when it changed it.
inline bool applyNoteGuideHit(NoteGuideState& state, GuideHit hit)
{
    if (!guideHitLive(state, hit)) return false;
    const auto before = state;
    switch (hit.target)
    {
        case GuideTarget::root:    state.root = juce::jlimit(0, 11, hit.index); break;
        case GuideTarget::rootOff: state.root = -1; break;
        case GuideTarget::chord:   state.chord = static_cast<GuideChord>(juce::jlimit(0, 11, hit.index)); break;
        case GuideTarget::major:   state.minor = false; break;
        case GuideTarget::minor:   state.minor = true; break;
        case GuideTarget::scale:   state.scale = static_cast<GuideScale>(juce::jlimit(0, 12, hit.index)); break;
        case GuideTarget::none:    break;
    }
    return state.root != before.root || state.chord != before.chord
        || state.minor != before.minor || state.scale != before.scale;
}

// --- The door --------------------------------------------------------------------

// What the door says, on its two lines. The chord's symbol when there is one,
// the root alone when there is not, and OFF with no root; under it the scale,
// or with no root what is behind the door.
inline juce::String guideDoorChordLine(const NoteGuideState& state)
{
    if (state.root < 0) return "OFF";
    const auto symbol = guideChordSymbol(state);
    return symbol.isNotEmpty() ? symbol : juce::String(guideSharpNames[static_cast<size_t>(state.root)]);
}

inline juce::String guideDoorScaleLine(const NoteGuideState& state)
{
    if (state.root < 0) return "CHORDS & SCALES";
    return state.scale == GuideScale::off ? juce::String("NO SCALE")
                                          : juce::String(guideScaleCellNames[static_cast<size_t>(state.scale)]);
}

// The plate at the right-hand end of the keyboard shelf. A button now, where it
// was four fields: what the guide is set to is read off it at a glance, and a
// click opens the whole guide over the modulators the way the ARP plate's name
// opens the arp, with the same faint outline to say it is open. It sits over
// the well the housing draws, so it paints only what changes.
class NoteGuideDoor final : public juce::Button
{
public:
    explicit NoteGuideDoor(const NoteGuideState& guide) : juce::Button("NOTE GUIDE"), state(guide)
    {
        setOpaque(false);
        sayWhatItDoes();
    }

    void setOpen(bool shouldBeOpen)
    {
        if (open == shouldBeOpen) return;
        open = shouldBeOpen;
        sayWhatItDoes();
        repaint();
    }

    bool isOpen() const { return open; }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto face = getLocalBounds().toFloat();
        // The same light the ARP plate's face takes when its panes are up, in
        // the guide's own blue, and a fainter rim of it under the pointer so the
        // plate reads as a thing to press.
        if (open || over)
        {
            juce::Path rim;
            if (isMerli()) rim.addRoundedRectangle(face.reduced(0.5f), noteGuideFaceCut - 0.5f);
            else rim = chamferedPath(face.reduced(0.5f), noteGuideFaceCut - 0.5f);
            if (open)
            {
                g.setColour(electricBlue.withAlpha(0.05f));
                g.fillPath(rim);
            }
            g.setColour(electricBlue.withAlpha(open ? 0.32f : 0.18f));
            g.strokePath(rim, juce::PathStrokeType(1.0f));
        }

        auto box = face.reduced(10.0f, 7.0f);
        auto top = box.removeFromTop(16.0f);
        g.setFont(panelFont(Face::header, 12.0f));
        g.setColour(state.root >= 0 ? rhino::forge::ui::text : mutedText);
        drawTrackedText(g, "GUIDE", top, 1.6f, juce::Justification::centredLeft);
        stamp(g, open ? "CLOSE" : "SETTINGS", top, 8.0f, 0.7f, juce::Justification::centredRight);
        box.removeFromTop(3.0f);

        const auto chordArea = box.removeFromTop(box.getHeight() * 0.56f).toNearestInt();
        const auto chordTone = state.root < 0 ? GuideTone::normal
                             : state.chord == GuideChord::off ? GuideTone::root : GuideTone::chord;
        g.setFont(panelFont(Face::header, 16.0f));
        g.setColour(chordTone == GuideTone::normal ? mutedText : guideToneColour(chordTone));
        g.drawFittedText(guideDoorChordLine(state), chordArea, juce::Justification::centredLeft, 1, 0.8f);

        g.setFont(panelFont(Face::emphasis, 11.5f));
        g.setColour(state.root < 0 || state.scale == GuideScale::off ? legendText
                                                                      : guideToneColour(GuideTone::scale));
        g.drawFittedText(guideDoorScaleLine(state), box.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

private:
    const NoteGuideState& state;
    bool open = false;

    void sayWhatItDoes()
    {
        setTooltip(open ? "Put the note guide away"
                        : "Open the note guide over the envelopes and LFOs: pick a root, a chord and a scale "
                          "to light on the keys");
    }
};

// --- The panel ---------------------------------------------------------------------

class NoteGuidePanel final : public juce::Component, public juce::TooltipClient
{
public:
    explicit NoteGuidePanel(NoteGuideState& guide) : state(guide) {}

    // Called after a press has changed the state, so the editor can save it
    // and redraw the keys.
    std::function<void()> onChange;

    NoteGuideLayout layout() const { return noteGuideLayout(getLocalBounds()); }

    // A press at a point in the panel's own coordinates. Public so a test can
    // press where a hand would; true when it changed anything.
    bool press(juce::Point<int> at)
    {
        if (!applyNoteGuideHit(state, noteGuideHit(layout(), at))) return false;
        if (onChange) onChange();
        repaint();
        return true;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu()) press(event.getPosition());
    }

    void mouseMove(const juce::MouseEvent& event) override { hoverOver(noteGuideHit(layout(), event.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { hoverOver({}); }

    juce::String getTooltip() override
    {
        const auto hit = noteGuideHit(layout(), getMouseXYRelative());
        if (hit.target == GuideTarget::none) return {};
        if (!guideHitLive(state, hit))
            return state.root < 0 ? "Pick a root first" : "Only a triad, 6th, 7th or Maj7 has a major and a minor";
        switch (hit.target)
        {
            case GuideTarget::root:
                return "Make " + juce::String(guideRootNames[static_cast<size_t>(hit.index)])
                       + " the root of the chord and the scale";
            case GuideTarget::rootOff:
                return "Switch the guide off. The Cs keep their octave numbers";
            case GuideTarget::chord:
                return hit.index == 0 ? juce::String("No chord")
                                      : juce::String(guideChordMenuNames[static_cast<size_t>(hit.index)])
                                            + ": its notes light up in every octave";
            case GuideTarget::major:
            case GuideTarget::minor:
                return "MAJ -> 7; MIN -> m7. Maj7: MAJ -> maj7; MIN -> m(maj7)";
            case GuideTarget::scale:
                return hit.index == 0 ? juce::String("No scale")
                                      : juce::String(guideScaleMenuNames[static_cast<size_t>(hit.index)])
                                            + ": its notes light up in every octave";
            case GuideTarget::none:
                break;
        }
        return {};
    }

    void paint(juce::Graphics& g) override
    {
        const auto at = layout();
        g.setColour(plateEdgeLit.withAlpha(0.16f));
        for (const auto x : at.rules)
            g.drawLine(static_cast<float>(x), static_cast<float>(at.rulesTop) + 2.0f,
                       static_cast<float>(x), static_cast<float>(at.rulesBottom) - 2.0f, 0.8f);

        drawTitle(g, at.rootTitle, "ROOT", GuideTone::root);
        drawTitle(g, at.chordTitle, "CHORD", GuideTone::chord);
        drawTitle(g, at.scaleTitle, "SCALE", GuideTone::scale);

        drawOctave(g, at.octave);
        drawCell(g, at.rootOff, "OFF", electricBlue, state.root < 0, {GuideTarget::rootOff, -1});

        const auto chordAccent = guideToneColour(GuideTone::chord);
        for (int chord = 0; chord < static_cast<int>(at.chords.size()); ++chord)
            drawCell(g, at.chords[static_cast<size_t>(chord)], guideChordCellNames[static_cast<size_t>(chord)],
                     chord == 0 ? electricBlue : chordAccent, static_cast<int>(state.chord) == chord,
                     {GuideTarget::chord, chord});
        const auto quality = guideChordHasQuality(state.chord);
        drawCell(g, at.major, "MAJ", chordAccent, quality && !state.minor, {GuideTarget::major, 0});
        drawCell(g, at.minor, "MIN", chordAccent, quality && state.minor, {GuideTarget::minor, 1});

        const auto scaleAccent = guideToneColour(GuideTone::scale);
        for (int scale = 0; scale < static_cast<int>(at.scales.size()); ++scale)
            drawCell(g, at.scales[static_cast<size_t>(scale)], guideScaleCellNames[static_cast<size_t>(scale)],
                     scale == 0 ? electricBlue : scaleAccent, static_cast<int>(state.scale) == scale,
                     {GuideTarget::scale, scale});

        drawReadout(g, at);
    }

private:
    NoteGuideState& state;
    GuideHit hover;

    void hoverOver(GuideHit hit)
    {
        if (hit == hover) return;
        hover = hit;
        repaint();
    }

    // A column's name with a dot of its colour before it: the legend for the
    // piano, the octave and the readout at once.
    static void drawTitle(juce::Graphics& g, juce::Rectangle<int> area, const char* name, GuideTone tone)
    {
        const auto box = area.toFloat();
        g.setColour(guideToneColour(tone));
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({box.getX() + 4.0f, box.getCentreY()}));
        g.setColour(mutedText);
        g.setFont(panelFont(Face::header, 12.0f));
        drawTrackedText(g, name, box.withTrimmedLeft(14.0f), 1.4f, juce::Justification::centredLeft);
    }

    void drawCell(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& caption, juce::Colour accent,
                  bool on, GuideHit hit) const
    {
        const auto enabled = guideHitLive(state, hit);
        const auto hovered = enabled && hover == hit;
        const auto box = area.toFloat().reduced(0.5f);
        g.setColour(on ? accent.withAlpha(enabled ? 0.22f : 0.08f) : fieldFloor);
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour((on || hovered ? accent : line).withAlpha(enabled ? (on ? 0.95f : hovered ? 0.7f : 0.85f) : 0.3f));
        g.drawRoundedRectangle(box, 3.0f, 1.0f);
        g.setColour((on ? accent : rhino::forge::ui::text).withAlpha(enabled ? 1.0f : 0.35f));
        g.setFont(panelFont(Face::emphasis, guideCellTextSize));
        g.drawFittedText(caption, area.reduced(3, 0), juce::Justification::centred, 1, 0.8f);
    }

    // An octave drawn as the piano under it is, coloured as the piano is, so
    // what is picked here and what lights up there are visibly the same.
    void drawOctave(juce::Graphics& g, juce::Rectangle<int> octave) const
    {
        g.setColour(wellFloor);
        g.fillRoundedRectangle(octave.toFloat().expanded(2.0f), 3.0f);
        for (const auto black : {false, true})
            for (int pitch = 0; pitch < 12; ++pitch)
            {
                if (guidePitchIsBlack(pitch) != black) continue;
                const auto key = guideOctaveKey(octave, pitch).toFloat();
                const auto face = black ? key : key.reduced(0.5f, 0.0f);
                g.setColour(black ? keyBlack : keyWhite);
                g.fillRect(face);
                if (black)
                {
                    g.setColour(keyBlack.brighter(0.5f));
                    g.fillRect(face.reduced(face.getWidth() / 8.0f, 0.0f).withHeight(1.0f));
                }

                const auto tone = guideTone(state, pitch);
                const auto colour = guideToneColour(tone);
                if (tone != GuideTone::normal)
                {
                    g.setColour(colour.withAlpha(black ? 0.42f : 0.30f));
                    g.fillRoundedRectangle(face.reduced(1.0f), 1.5f);
                    g.setColour(colour.withAlpha(0.9f));
                    g.fillRect(face.getX() + 1.0f, face.getBottom() - 3.0f, face.getWidth() - 2.0f, 2.0f);
                }
                if (hover == GuideHit {GuideTarget::root, pitch})
                {
                    g.setColour(electricBlue.withAlpha(0.25f));
                    g.fillRect(face);
                }
                if (tone == GuideTone::root)
                {
                    const auto marker = juce::Rectangle<float>(11.0f, 11.0f)
                                            .withCentre({face.getCentreX(), face.getY() + 10.0f});
                    g.setColour(colour);
                    g.fillEllipse(marker);
                    g.setColour(juce::Colours::black.withAlpha(0.8f));
                    g.setFont(panelFont(Face::emphasis, 8.0f));
                    g.drawText("R", marker, juce::Justification::centred);
                }

                auto caption = face.reduced(1.0f);
                caption = caption.withTop(caption.getBottom() - (black ? 18.0f : 20.0f));
                g.setColour(tone == GuideTone::normal ? (black ? keyLabelOnBlack : keyLabelOnWhite).withAlpha(0.8f)
                                                      : colour);
                g.setFont(panelFont(Face::emphasis, black ? 9.5f : 11.0f));
                g.drawText(guideSharpNames[static_cast<size_t>(pitch)], caption, juce::Justification::centred);
            }
    }

    // The readout: for the chord and for the scale, its name, and under it each
    // note it is made of with what that note is to the root.
    void drawReadout(juce::Graphics& g, const NoteGuideLayout& at) const
    {
        drawDisplayWell(g, at.readout, false);
        if (state.root < 0)
        {
            g.setColour(mutedText);
            g.setFont(panelFont(Face::label, 11.5f));
            g.drawFittedText("PICK A ROOT ON THE OCTAVE TO LIGHT ITS CHORD AND SCALE ACROSS THE KEYS",
                             at.readout.reduced(12, 0), juce::Justification::centred, 1, 0.8f);
            return;
        }
        const juce::String root = guideSharpNames[static_cast<size_t>(state.root)];
        drawNotes(g, at.chordReadout, "CHORD", GuideTone::chord, guideChordSymbol(state),
                  guideChordDescription(state), guideNotes(state, true));
        drawNotes(g, at.scaleReadout, "SCALE", GuideTone::scale,
                  state.scale == GuideScale::off ? juce::String()
                      : root + " " + juce::String(guideScaleMenuNames[static_cast<size_t>(state.scale)]).toUpperCase(),
                  {}, state.scale == GuideScale::off ? std::vector<GuideNote> {} : guideNotes(state, false));
    }

    static void drawNotes(juce::Graphics& g, juce::Rectangle<int> area, const char* tag, GuideTone tone,
                          const juce::String& name, const juce::String& description,
                          const std::vector<GuideNote>& notes)
    {
        auto box = area.reduced(0, 6).toFloat();
        auto heading = box.removeFromTop(20.0f);
        g.setColour(guideToneColour(tone));
        g.setFont(panelFont(Face::label, 11.0f));
        const auto tagWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), tag) + 1.4f * 5.0f + 10.0f;
        drawTrackedText(g, tag, heading.removeFromLeft(tagWidth), 1.4f, juce::Justification::centredLeft);
        if (name.isEmpty())
        {
            g.setColour(legendText);
            g.setFont(panelFont(Face::emphasis, 13.5f));
            g.drawText("NONE", heading, juce::Justification::centredLeft);
            return;
        }
        g.setColour(rhino::forge::ui::text);
        g.setFont(panelFont(Face::header, 15.0f));
        const auto nameWidth = juce::jmin(heading.getWidth(),
                                          juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name) + 10.0f);
        g.drawFittedText(name, heading.removeFromLeft(nameWidth).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        if (description.isNotEmpty())
        {
            g.setColour(mutedText);
            g.setFont(panelFont(Face::label, 11.0f));
            g.drawFittedText(description, heading.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }

        if (notes.empty()) return;
        box.removeFromTop(2.0f);
        const auto step = juce::jmin(48.0f, box.getWidth() / static_cast<float>(notes.size()));
        const auto noteRow = box.removeFromTop(box.getHeight() * 0.56f);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto x = box.getX() + step * static_cast<float>(i);
            const auto& note = notes[i];
            g.setColour(guideToneColour(note.tone));
            g.setFont(panelFont(Face::emphasis, 14.0f));
            g.drawText(guideSharpNames[static_cast<size_t>(note.pitch)],
                       juce::Rectangle<float>(x, noteRow.getY(), step, noteRow.getHeight()),
                       juce::Justification::centredLeft);
            g.setColour(mutedText);
            g.setFont(panelFont(Face::label, 11.0f));
            g.drawText(note.degree, juce::Rectangle<float>(x, box.getY(), step, box.getHeight()),
                       juce::Justification::centredLeft);
        }
    }
};
}
