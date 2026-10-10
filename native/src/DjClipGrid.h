#pragma once
#include "Session.h"
#include "Theme.h"

namespace rhino
{
// The clips grid on a console: the clip slots of the deck's track, one cell
// a scene, under a SONG cell that is the track's arrangement itself. The
// deck plays the cell that is lit. A press on a filled cell loads it on the
// deck; a double-click on an empty cell makes a one-bar clip there, loads it
// and opens it in the editor; a double-click on a filled one opens it. A
// cell dragged goes to another console's grid or, through the Arrange
// switch, to the timeline; and a cell takes a clip dragged from the
// arrangement, another console's cell, a library item that makes a clip, or
// a sound file, each held to the track's kind by the model. The grid shows
// only on a deck that plays a track of the song.
class DjClipGrid final : public juce::Component,
                         public juce::DragAndDropTarget,
                         public juce::FileDragAndDropTarget,
                         public juce::SettableTooltipClient
{
public:
    DjClipGrid(Session&, int deck);
    void setDeck(int);
    // Re-reads the deck's track, its scenes and the cell the deck plays.
    void sync();
    bool hasTrack() const { return track >= 0; }
    int trackIndex() const { return track; }

    static constexpr int songCell = -1;   // the arrangement
    static constexpr int firstFree = -2;  // a drop: the first empty slot, a new scene when none is
    static constexpr int noCell = -3;
    static constexpr int cellHeight = 20, cellGap = 2;

    // The gestures by cell, which the console's own drops and the tests call.
    juce::Result chooseCell(int scene);
    juce::Result createClipAt(int scene);
    // Lands a drag's description on a cell; loadOnDeck plays what it made.
    juce::Result applyDrop(int scene, const juce::String& description, bool loadOnDeck);
    // The cell under a point: songCell, a scene, the empty row past the last
    // scene (which a double-click makes a scene of), or noCell.
    int cellAt(juce::Point<int>) const;
    int litCell() const { return lit; }

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;

    std::function<void(juce::String)> status;
    // A clip made or opened from the grid: the track and the clip.
    std::function<void(int track, te::EditItemID clip)> editRequested;

private:
    friend int runArrangementTest();
    juce::Rectangle<int> cellBounds(int scene) const;
    int rows() const;
    void showCellMenu(int scene);
    void report(const juce::Result&, const juce::String& success);

    Session& session;
    int deck;
    int track = -1;
    int scenes = 0;
    int lit = songCell;
    int hovered = noCell, dropTarget = noCell;
    int firstScene = 0;
    int pressedCell = noCell;
    bool dragStarted = false;
};
}
