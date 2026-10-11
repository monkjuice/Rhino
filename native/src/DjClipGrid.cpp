#include "DjClipGrid.h"
#include "BrowserDrops.h"
#include "ClipDrag.h"
#include <algorithm>

// The clips grid on a console: see DjClipGrid.h.

namespace rhino
{
namespace
{
// A cell's clip wears its track's colour until someone colours it, darkened
// as the arrangement darkens a clip on its lane (ArrangementPainter.cpp).
juce::Colour cellColour(const Session::SlotClip& clip, juce::Colour track)
{
    if (!clip.colour.isTransparent()) return clip.colour;
    return track.isTransparent() ? palette::control.brighter(0.06f) : track.darker(0.5f);
}
}

DjClipGrid::DjClipGrid(Session& s, int deckIndex) : session(s), deck(deckIndex)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTooltip("The track's clips. SONG is its arrangement; each cell below is a clip slot. Press a cell to play it on "
               "the deck, double-click an empty one for a new one-bar clip, drag a cell to another console or to "
               "the timeline through the Arrange switch, and drop clips, samples and sounds on a cell. Right-click "
               "deletes a clip.");
}

void DjClipGrid::sync()
{
    sync(session.djDeckInfo(deck));
}

// Reads every cell of the track, compares it with what is shown, and
// repaints the cells that changed - all of them only when the track, the
// number of scenes, its colour or the scroll changed.
void DjClipGrid::sync(const Session::DjDeckInfo& info)
{
    const auto nextTrack = info.kind == Session::DjSourceKind::track ? info.track : -1;
    const auto nextScenes = nextTrack >= 0 ? session.sceneCount() : 0;
    const auto nextLit = nextTrack >= 0 && info.slot >= 0 ? info.slot : songCell;
    const auto nextColour = nextTrack >= 0 ? session.trackColour(nextTrack) : juce::Colour();
    std::vector<Cell> next(static_cast<size_t>(nextScenes));
    for (int scene = 0; scene < nextScenes; ++scene)
    {
        const auto clip = session.slotClip(nextTrack, scene);
        if (clip.hasClip)
            next[static_cast<size_t>(scene)] = {true, clip.name, cellColour(clip, nextColour), clip.clipID};
    }
    const auto whole = nextTrack != track || nextScenes != scenes || nextColour != trackColour;
    if (!whole)
    {
        for (int scene = 0; scene < nextScenes; ++scene)
            if (!(next[static_cast<size_t>(scene)] == cells[static_cast<size_t>(scene)]))
                repaintCell(scene);
        if (nextLit != lit)
        {
            repaintCell(lit);
            repaintCell(nextLit);
        }
    }
    track = nextTrack;
    scenes = nextScenes;
    lit = nextLit;
    trackColour = nextColour;
    cells = std::move(next);
    names.resize(cells.size());
    const auto scroll = juce::jlimit(0, lastFirstScene(), firstScene);
    if (whole || scroll != firstScene)
    {
        firstScene = scroll;
        repaint();
    }
}

int DjClipGrid::rows() const
{
    return std::max(0, (getHeight() + cellGap) / (cellHeight + cellGap));
}

int DjClipGrid::lastFirstScene() const
{
    return std::max(0, scenes + 1 - std::max(1, rows() - 1));
}

juce::Rectangle<int> DjClipGrid::cellBounds(int scene) const
{
    const auto row = scene == songCell ? 0 : scene - firstScene + 1;
    if (row < 0 || row >= rows()) return {};
    return {0, row * (cellHeight + cellGap), getWidth(), cellHeight};
}

void DjClipGrid::repaintCell(int scene)
{
    if (scene == noCell) return;
    if (const auto bounds = cellBounds(scene); !bounds.isEmpty())
        repaint(bounds);
}

const DjClipGrid::Cell* DjClipGrid::cellFor(int scene) const
{
    return juce::isPositiveAndBelow(scene, static_cast<int>(cells.size())) ? &cells[static_cast<size_t>(scene)] : nullptr;
}

int DjClipGrid::cellAt(juce::Point<int> point) const
{
    if (track < 0 || !getLocalBounds().contains(point)) return noCell;
    const auto row = point.y / (cellHeight + cellGap);
    if (row >= rows()) return noCell;
    if (row == 0) return songCell;
    const auto scene = firstScene + row - 1;
    return scene <= scenes ? scene : noCell;
}

void DjClipGrid::resized()
{
    firstScene = juce::jlimit(0, lastFirstScene(), firstScene);
}

void DjClipGrid::report(const juce::Result& result, const juce::String& success)
{
    if (status) status(result.failed() ? result.getErrorMessage() : success);
}

juce::Result DjClipGrid::chooseCell(int scene)
{
    if (track < 0) return juce::Result::fail("Load a track of the song on this deck first.");
    const auto result = scene == songCell ? session.loadDjDeckTrack(deck, track) : session.loadDjDeckSlot(deck, track, scene);
    if (result.wasOk()) sync();
    return result;
}

// A one-bar clip in the cell, played on the deck and opened to edit, so the
// notes go in while the deck turns.
juce::Result DjClipGrid::createClipAt(int scene)
{
    if (track < 0) return juce::Result::fail("Load a track of the song on this deck first.");
    if (scene < 0) return juce::Result::fail("Double-click an empty cell for a new clip.");
    if (const auto result = session.createSlotClip(track, scene); result.failed())
        return result;
    sync();
    if (const auto result = chooseCell(scene); result.failed())
        return result;
    if (editRequested)
        editRequested(track, session.slotClip(track, scene).clipID);
    return juce::Result::ok();
}

juce::Result DjClipGrid::applyDrop(int scene, const juce::String& description, bool loadOnDeck)
{
    if (track < 0) return juce::Result::fail("Load a track of the song on this deck first, or drop the file on the deck to play it.");
    // An instrument, an effect, a kit, a drum sound or a preset joins the
    // track's chain and makes no clip, as on a lane.
    if (const auto chain = applyChainDrop(session, description, track))
    {
        if (chain->wasOk()) sync();
        return *chain;
    }
    auto target = scene;
    if (target == firstFree || target == songCell || target == noCell)
    {
        target = session.firstFreeSlot(track);
        if (target < 0) target = session.sceneCount();
    }
    // A target past the last scene is the + row: the model makes that scene
    // with the clip it lands, so a drop that makes none adds no row.
    const auto kind = browserDropKind(description);
    const auto id = browserDropId(description);
    auto result = juce::Result::ok();
    int fromTrack = -1, fromScene = -1;
    if (const auto clip = draggedArrangementClip(description); clip != te::EditItemID())
        result = session.copyClipToTrackSlot(clip, track, target);
    else if (draggedSlot(description, fromTrack, fromScene))
    {
        if (fromTrack == track && fromScene == target)
            return juce::Result::ok();
        const auto source = session.slotClip(fromTrack, fromScene);
        if (!source.hasClip) return juce::Result::fail("That cell is empty.");
        result = session.copyClipToTrackSlot(source.clipID, track, target);
    }
    else if (kind == "file")
    {
        const auto file = browserDropFile(description);
        if (file == juce::File() || !file.existsAsFile())
            return juce::Result::fail("That audio file is missing.");
        result = session.insertAudioFileInSlot(file, track, target);
    }
    else if (kind == "sample")
    {
        const auto sample = builtInSampleFromId(id);
        if (!sample) return juce::Result::fail("That browser item cannot be dropped here.");
        result = session.insertBuiltInSampleInSlot(*sample, track, target);
    }
    else if (kind == "preset")
    {
        const auto preset = patternPresetFromId(id);
        if (!preset) return juce::Result::fail("That browser item cannot be dropped here.");
        result = session.insertPatternPresetInSlot(*preset, track, target);
    }
    else
        return juce::Result::fail("That cannot be dropped on a console.");
    if (result.failed())
        return result;
    sync();
    if (loadOnDeck)
        return chooseCell(target);
    return juce::Result::ok();
}

// Draws only the cells a repaint reaches, from what the last sync kept.
void DjClipGrid::paint(juce::Graphics& g)
{
    if (track < 0) return;
    const auto count = rows();
    for (int row = 0; row < count; ++row)
    {
        const auto scene = row == 0 ? songCell : firstScene + row - 1;
        if (scene > scenes) break;
        const auto cell = cellBounds(scene);
        if (cell.isEmpty() || (cullRepaints && !g.clipRegionIntersects(cell))) continue;
        const auto isLit = scene == lit, isHovered = scene == hovered, isTarget = scene == dropTarget;
        auto inner = cell.toFloat();
        if (scene == songCell)
        {
            g.setColour(isHovered ? palette::control.brighter(0.12f) : palette::control);
            g.fillRect(inner);
            g.setColour(trackColour);
            g.fillRect(inner.removeFromLeft(4.0f));
            g.setColour(palette::text);
            songLine.set("SONG", keyFont, cell.withTrimmedLeft(8), juce::Justification::centredLeft, true);
            songLine.draw(g);
        }
        else if (scene == scenes)
        {
            // The row past the last scene: a double-click makes a scene and
            // a clip in it.
            g.setColour(palette::control.withMultipliedAlpha(isHovered ? 0.9f : 0.5f));
            g.fillRect(inner);
            g.setColour(palette::textDim);
            addLine.set("+", keyFont, cell, juce::Justification::centred, true);
            addLine.draw(g);
        }
        else if (const auto& kept = cells[static_cast<size_t>(scene)]; kept.hasClip)
        {
            auto fill = kept.colour.withMultipliedSaturation(0.85f);
            if (!isLit) fill = fill.withMultipliedBrightness(0.62f);
            if (isHovered) fill = fill.brighter(0.12f);
            g.setColour(fill);
            g.fillRect(inner);
            g.setColour(fill.contrasting(0.85f));
            auto& name = names[static_cast<size_t>(scene)];
            name.set(kept.name, nameFont, cell.withTrimmedLeft(5).withTrimmedRight(3), juce::Justification::centredLeft, true);
            name.draw(g);
        }
        else
        {
            g.setColour(isHovered ? palette::control.brighter(0.12f) : palette::control);
            g.fillRect(inner);
            g.setColour(palette::border.brighter(0.2f));
            g.fillRect(inner.withWidth(14.0f).withSizeKeepingCentre(6.0f, 6.0f));
        }
        if (isLit)
        {
            g.setColour(palette::selection);
            g.drawRect(cell, 2);
        }
        if (isTarget)
        {
            g.setColour(palette::djPlay);
            g.drawRect(cell, 2);
        }
    }
}

void DjClipGrid::setHovered(int scene)
{
    if (scene == hovered) return;
    repaintCell(hovered);
    hovered = scene;
    repaintCell(hovered);
}

void DjClipGrid::setDropTarget(int scene)
{
    if (scene == dropTarget) return;
    repaintCell(dropTarget);
    dropTarget = scene;
    repaintCell(dropTarget);
}

void DjClipGrid::mouseMove(const juce::MouseEvent& event)
{
    setHovered(cellAt(event.getPosition()));
}

void DjClipGrid::mouseExit(const juce::MouseEvent&)
{
    setHovered(noCell);
}

void DjClipGrid::mouseDown(const juce::MouseEvent& event)
{
    pressedCell = cellAt(event.getPosition());
    dragStarted = false;
    if (event.mods.isPopupMenu() && pressedCell >= 0 && pressedCell < scenes)
        showCellMenu(pressedCell);
}

// A filled cell dragged far enough leaves as a copy of its clip.
void DjClipGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (dragStarted || pressedCell < 0 || pressedCell >= scenes || event.mods.isPopupMenu()) return;
    if (event.getDistanceFromDragStart() < 5) return;
    const auto* cell = cellFor(pressedCell);
    if (cell == nullptr || !cell->hasClip) return;
    dragStarted = true;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging(slotDragDescription(track, pressedCell), this, clipDragImage(cell->name, cell->colour), true);
}

void DjClipGrid::mouseUp(const juce::MouseEvent& event)
{
    const auto pressed = pressedCell;
    pressedCell = noCell;
    if (dragStarted || event.mods.isPopupMenu() || !event.mouseWasClicked() || pressed == noCell) return;
    const auto* cell = cellFor(pressed);
    if (pressed == songCell || (cell != nullptr && cell->hasClip))
    {
        const auto name = cell != nullptr ? cell->name : juce::String();
        report(chooseCell(pressed), pressed == songCell ? "The deck plays the song's track" : "The deck plays " + name.quoted());
    }
    else if (status)
        status("An empty cell: double-click it for a new one-bar clip, or drop a clip or a sample on it");
}

void DjClipGrid::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto scene = cellAt(event.getPosition());
    if (scene == noCell || scene == songCell) return;
    if (const auto* cell = cellFor(scene); cell != nullptr && cell->hasClip)
    {
        const auto name = cell->name;
        const auto clip = cell->clipID;
        report(chooseCell(scene), "Editing " + name.quoted() + ": the deck follows each change");
        if (editRequested) editRequested(track, clip);
        return;
    }
    report(createClipAt(scene), "A one-bar clip: the deck plays it and the editor is open on it");
}

void DjClipGrid::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto step = wheel.deltaY < 0.0f ? 1 : wheel.deltaY > 0.0f ? -1 : 0;
    const auto next = juce::jlimit(0, lastFirstScene(), firstScene + step);
    if (next == firstScene) return;
    firstScene = next;
    repaint();
}

void DjClipGrid::showCellMenu(int scene)
{
    const auto* cell = cellFor(scene);
    if (cell == nullptr || !cell->hasClip) return;
    juce::PopupMenu menu;
    menu.addSectionHeader(cell->name.toUpperCase());
    menu.addItem(1, "Delete clip");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(cellBounds(scene))),
        [safe = juce::Component::SafePointer<DjClipGrid>(this), scene](int choice)
        {
            if (safe == nullptr || choice != 1) return;
            const auto wasLit = safe->lit == scene;
            safe->report(safe->session.deleteSlotClip(safe->track, scene), "Clip deleted");
            // The deck that played it goes back to the song's track.
            if (wasLit) safe->chooseCell(songCell);
            safe->sync();
        });
}

bool DjClipGrid::isInterestedInDragSource(const SourceDetails& details)
{
    return track >= 0 && isCrossViewDrag(details.description.toString());
}

void DjClipGrid::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void DjClipGrid::itemDragMove(const SourceDetails& details)
{
    const auto cell = cellAt(details.localPosition);
    setDropTarget(cell == songCell ? noCell : cell);
}

void DjClipGrid::itemDragExit(const SourceDetails&)
{
    setDropTarget(noCell);
}

void DjClipGrid::itemDropped(const SourceDetails& details)
{
    const auto cell = cellAt(details.localPosition);
    setDropTarget(noCell);
    report(applyDrop(cell == songCell || cell == noCell ? firstFree : cell, details.description.toString(), false),
           "Dropped on the console: press the cell to play it");
}

bool DjClipGrid::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (track < 0) return false;
    for (const auto& path : files)
        if (isDroppedSoundFile(juce::File(path)))
            return true;
    return false;
}

void DjClipGrid::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
}

void DjClipGrid::fileDragMove(const juce::StringArray&, int x, int y)
{
    const auto cell = cellAt({x, y});
    setDropTarget(cell == songCell ? noCell : cell);
}

void DjClipGrid::fileDragExit(const juce::StringArray&)
{
    setDropTarget(noCell);
}

void DjClipGrid::filesDropped(const juce::StringArray& files, int x, int y)
{
    const auto cell = cellAt({x, y});
    setDropTarget(noCell);
    for (const auto& path : files)
        if (isDroppedSoundFile(juce::File(path)))
        {
            report(applyDrop(cell == songCell || cell == noCell ? firstFree : cell, "rhino-browser:file:" + path, false),
                   "Dropped on the console: press the cell to play it");
            break;
        }
}
}
