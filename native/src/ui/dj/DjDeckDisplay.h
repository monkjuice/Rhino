#pragma once
#include "model/Session.h"
#include "dj/DjTrack.h"
#include "DjWaveGeometry.h"
#include "ui/shared/ShapedText.h"
#include <array>
#include <optional>
#include <tuple>
#include <vector>

namespace rhino
{
// A deck's screen: the waveform scrolling under a fixed playhead, as a
// CDJ-3000 shows it, with the beat grid, the cue points and the loop drawn
// on it - a MIDI track's notes in place of its waveform; the whole track in
// a strip below, with its drops and cues, that a click seeks in; and a line
// of readouts - time, tempo, key, bar.
//
// What does not move is kept as pictures: tiles of the scrolling strip, each
// a run of the waveform or the notes with the beat grid under it, painted at
// the zoom, the height and the scale they are shown at, and the overview.
// Each is drawn on whole device pixels of the window, so it is copied as it
// was painted rather than resampled (DevicePixels.h).
// Pictures are painted on the display's refresh, never inside a paint, and
// two a frame at most where the old ones stand in: a bounce, a zoom or a
// resize brings its pictures in over a frame or two, the old ones scaled
// until then; painting them all at once was a frame the screen stopped for
// (the freeze reported on 2026-10-11). A tile of the strip that nothing
// stands in for - a jump to where the strip has not been, the view shown
// again after the deck played on - is painted that frame whatever the
// budget, at some twenty microseconds a tile: left to the budget, the strip
// showed bare ground around the two it painted. A picture whose content did not change
// is kept: a bounce that changed only the sound keeps the note tiles, one
// that changed only the notes keeps the overview.
//
// The strip moves by whole device pixels, so a frame that moved the deck
// less than one paints nothing, and the waveform, the grid and the marks on
// it move together. Each frame draws the loop, the cue flags and the
// playhead over the tiles.
class DjDeckDisplay final : public juce::Component
{
public:
    DjDeckDisplay(Session&, int deck);
    // The deck's material may have changed: re-reads it, keeping every
    // picture whose content did not change, and repaints the readouts when
    // what they show did.
    void sync();
    void sync(const Session::DjDeckInfo&);
    // Once per display refresh, with the deck as it stands: repaints what
    // moved, and paints the pictures the screen lacks - two at most where the
    // last ones stand in.
    void tick();
    void tick(const Session::DjDeckState&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int readoutHeight = 18, overviewHeight = 22;
    juce::Rectangle<int> waveArea() const;
    juce::Rectangle<int> overviewArea() const;
    juce::Rectangle<int> readoutArea() const;

private:
    friend int runArrangementTest();
    friend int runUiProfile();
    // What a picture was painted from: what it shows - a material's waveform
    // or its notes, with the grid - and the zoom and the size, in device
    // pixels, it was painted at.
    struct PictureKey
    {
        juce::uint64 content = 0;
        double pixelsPerSecond = 0.0;
        int width = 0, height = 0;
        float scale = 0.0f;
        bool operator==(const PictureKey&) const = default;
    };
    struct Picture
    {
        long long index = 0;
        juce::Image image;
        // Painted in software, it is handed to the type of the context that
        // draws it the first time it is drawn.
        bool handedOver = false;
    };
    struct PictureSet
    {
        PictureKey key;
        std::vector<Picture> pictures;
        // Every tile the strip showed was painted, when it was last looked at.
        bool complete = false;
        Picture* find(long long index);
    };
    // The cue, the hot cues and the loop: what a stopped deck's screen shows
    // changing when they are set or cleared.
    struct Marks
    {
        double cue = 0.0, loopStart = -1.0, loopEnd = -1.0;
        bool loopActive = false;
        std::array<double, 8> hotCues {};
        bool operator==(const Marks&) const = default;
    };
    // What the readout line shows, as numbers: compared every frame, and
    // made into text only when it is painted.
    struct Readout
    {
        bool elapsedNegative = false, remainingNegative = false;
        int elapsedTenths = 0, remainingTenths = 0;
        Session::DjDeckState::Transport transport = Session::DjDeckState::Transport::empty;
        long long bpmHundredths = -1;   // -1: no tempo
        long long percentHundredths = 0;
        bool synced = false;
        int bar = 0, beatInBar = 0;
        bool operator==(const Readout&) const = default;
    };
    // Where a deck's material comes from: its kind, file, track and cell.
    using Source = std::tuple<Session::DjSourceKind, juce::File, te::EditItemID, int>;

    double pixelsPerSecond() const;
    PictureKey tileKey() const;
    PictureKey overviewKey() const;
    long long scrollFor(double seconds) const;
    // The part of the strip's timeline the strip shows at `scroll`, from and
    // to, a device pixel either side included, since the strip is drawn from
    // the device pixel nearest its middle rather than from the middle itself;
    // and the tiles that touches.
    std::pair<double, double> visibleSpan(double scroll) const;
    djwave::TileRange visibleTiles(double scroll) const;
    void readMaterial();
    void dropPictures();
    // Paints up to `budget` of the pictures the screen lacks, the tiles
    // nearest the playhead first, and every tile the strip shows that nothing
    // stands in for, budget or not; DjDeckDisplayWave.cpp.
    void paintPictures(int budget);
    // Whether the tiles of a key the screen has left stand in for the next
    // key's: when they covered the strip, or when nothing else would.
    bool tilesStandIn() const;
    juce::Image paintTile(const PictureKey&, long long index) const;
    juce::Image paintOverviewPicture(const PictureKey&) const;
    void paintWave(juce::Graphics&, juce::Rectangle<int> area);
    void paintOverview(juce::Graphics&, juce::Rectangle<int> area);
    void paintReadouts(juce::Graphics&, juce::Rectangle<int> area);
    // Gives a picture painted in software to the type of image `g` draws
    // best, once.
    void handOver(juce::Graphics& g, Picture&) const;
    void seekAt(juce::Point<int> point, juce::Rectangle<int> area);
    static Marks marksOf(const Session::DjDeckState&);
    static Readout readoutOf(const Session::DjDeckState&);
    // Where the overview's marker stands, in whole device pixels from the
    // overview's left edge: it moves, and the played part's dimming with it,
    // a device pixel at a time.
    long long overviewMarker(double seconds) const;

    Session& session;
    int deck;
    const DjTrack* track = nullptr;
    int generation = -1;
    // Where the material on screen came from: new material from the same
    // source may show the last material's pictures until its own are
    // painted, another source's never. Unknown for material first seen
    // while another load was in flight, since the deck's info then names
    // the source on its way; such material's pictures stand in for nothing.
    std::optional<Source> materialSource;
    Session::DjDeckState state;
    double secondsAcross = 12.0;
    // The material's content, read once when it arrives: its waveform and
    // grid, the same with its drops for the overview, and its notes and grid
    // with the notes as the strip draws them.
    juce::uint64 wavesContent = 0, overviewContent = 0, notesContent = 0;
    bool drawsNotes = false;
    std::vector<djwave::NoteSpan> notes;
    int lowestNote = 0, highestNote = -1;
    juce::String keyText;
    // The strip's tiles for the current key, and the last key's, which stand
    // in where a tile of the current key is not painted yet.
    PictureSet tiles, standIns;
    // The overview, and the last one, standing in the same way.
    PictureSet overview, overviewStandIn;
    // The scale the screen was last painted at, which the pictures are
    // painted for.
    float paintedScale = 1.0f;
    // What the last frame asked to be drawn, so a frame that changed nothing
    // repaints nothing.
    long long drawnScroll = 0;
    bool drawnPlaying = false;
    Marks drawnMarks;
    long long drawnOverviewMarker = -1;
    Readout shownReadout;
    // What an empty screen says: loading, an error, or how to load.
    juce::String message;
    // The text, shaped once and laid out again only when it changes; the
    // letters of the cue flags are shaped once and drawn wherever a flag
    // stands.
    const juce::Font messageFont {uiFont(11.0f)}, readoutFont {uiFont(10.0f)};
    ShapedLine messageLine, clockLine, transportLine, tempoLine, pitchLine, keyLine, barLine;
    std::array<ShapedLine, 9> flagLetters;
    bool nudging = false;
    juce::Point<float> nudgeStart;
    static constexpr int tileWidth = 256;   // device pixels
    // The pictures a frame paints at most, tiles and overview together, while
    // the last ones stand in for them.
    static constexpr int picturesPerFrame = 2;
};
}
