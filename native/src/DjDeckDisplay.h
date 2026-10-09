#pragma once
#include "Session.h"
#include "DjTrack.h"
#include <vector>

namespace rhino
{
// A deck's screen: the waveform scrolling under a fixed playhead, as a
// CDJ-3000 shows it, with the beat grid, the cue points and the loop drawn
// on it; the whole track in a strip below, with its drops and cues, that a
// click seeks in; and a line of readouts - time, tempo, key, bar.
//
// The scrolling strip is blitted from tiles of the waveform painted once at
// the current zoom, because painting six decks' columns on every display
// refresh is what would cost the frame; only the grid, the markers and the
// playhead are drawn on each frame.
class DjDeckDisplay final : public juce::Component
{
public:
    DjDeckDisplay(Session&, int deck);
    void setDeck(int);
    // The material may have changed: re-reads the deck and drops what no
    // longer applies.
    void sync();
    // Once per display refresh: reads the position and repaints what moved.
    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void(juce::String)> status;

    static constexpr int readoutHeight = 18, overviewHeight = 22;
    juce::Rectangle<int> waveArea() const;
    juce::Rectangle<int> overviewArea() const;
    juce::Rectangle<int> readoutArea() const;

private:
    friend int runArrangementTest();
    struct Tile
    {
        int index = -1;
        juce::Image image;
    };
    double pixelsPerSecond() const;
    const juce::Image* tileFor(int index);
    void paintTile(juce::Image&, int index) const;
    void buildOverview();
    void paintWave(juce::Graphics&, juce::Rectangle<int> area);
    void paintOverview(juce::Graphics&, juce::Rectangle<int> area);
    void paintReadouts(juce::Graphics&, juce::Rectangle<int> area);
    void seekAt(juce::Point<int> point, juce::Rectangle<int> area);
    static juce::String clock(double seconds);
    // The readout fields as one string, compared by tick() and drawn by
    // paintReadouts(), so the row repaints only when a field changes.
    juce::String readoutText() const;

    Session& session;
    int deck;
    const DjTrack* track = nullptr;
    int generation = -1;
    Session::DjDeckState state;
    double secondsAcross = 12.0;
    std::vector<Tile> tiles;
    juce::Image overview;
    int overviewWidth = 0;
    // What the last frame drew, so a frame that changed nothing repaints
    // nothing.
    double drawnPosition = -1.0;
    juce::uint32 drawnJumps = 0;
    juce::String drawnReadout;
    bool nudging = false;
    juce::Point<float> nudgeStart;
    static constexpr int tileWidth = 256;
};
}
