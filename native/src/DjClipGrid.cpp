#include "DjClipGrid.h"
#include "BrowserIds.h"
#include "ClipDrag.h"
#include <algorithm>

// The clips grid on a console: see DjClipGrid.h.

namespace rhino
{
DjClipGrid::DjClipGrid(Session& s, int deckIndex) : session(s), deck(deckIndex)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTooltip("The track's clips. SONG is its arrangement; each cell below is a clip slot. Press a cell to play it on "
               "the deck, double-click an empty one for a new one-bar clip, drag a cell to another console or to "
               "the timeline through the Arrange switch, and drop clips, samples and sounds on a cell. Right-click "
               "deletes a clip.");
}

void DjClipGrid::setDeck(int deckIndex)
{
    deck = deckIndex;
    sync();
}

void DjClipGrid::sync()
{
    const auto info = session.djDeckInfo(deck);
    const auto before = std::make_tuple(track, scenes, lit);
    track = info.kind == Session::DjSourceKind::track ? info.track : -1;
    scenes = track >= 0 ? session.sceneCount() : 0;
    lit = track >= 0 && info.slot >= 0 ? info.slot : songCell;
    firstScene = juce::jlimit(0, std::max(0, scenes + 1 - std::max(1, rows() - 1)), firstScene);
    if (before != std::make_tuple(track, scenes, lit) || track >= 0)
        repaint();
}

int DjClipGrid::rows() const
{
    return std::max(0, (getHeight() + cellGap) / (cellHeight + cellGap));
}

juce::Rectangle<int> DjClipGrid::cellBounds(int scene) const
{
    const auto row = scene == songCell ? 0 : scene - firstScene + 1;
    if (row < 0 || row >= rows()) return {};
    return {0, row * (cellHeight + cellGap), getWidth(), cellHeight};
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
    auto target = scene;
    if (target == firstFree || target == songCell || target == noCell)
    {
        target = session.firstFreeSlot(track);
        if (target < 0) target = session.sceneCount();
    }
    if (target >= session.sceneCount())
        if (const auto grown = session.addScene(); grown.failed())
            return grown;
    const auto kind = browserDropKind(description);
    const auto id = browserDropId(description);
    auto result = juce::Result::ok();
    bool madeClip = false;
    int fromTrack = -1, fromScene = -1;
    if (const auto clip = draggedArrangementClip(description); clip != te::EditItemID())
    {
        result = session.copyClipToTrackSlot(clip, track, target);
        madeClip = true;
    }
    else if (draggedSlot(description, fromTrack, fromScene))
    {
        if (fromTrack == track && fromScene == target)
            return juce::Result::ok();
        const auto source = session.slotClip(fromTrack, fromScene);
        if (!source.hasClip) return juce::Result::fail("That cell is empty.");
        result = session.copyClipToTrackSlot(source.clipID, track, target);
        madeClip = true;
    }
    else if (kind == "file")
    {
        const auto file = browserDropFile(description);
        if (file == juce::File() || !file.existsAsFile())
            return juce::Result::fail("That audio file is missing.");
        // A drum track takes a sample onto its next empty pad instead, as a
        // lane does.
        if (session.trackHasDrumRack(track))
            result = session.addDrumSound(file, track);
        else
        {
            result = session.insertAudioFileInSlot(file, track, target);
            madeClip = true;
        }
    }
    else if (kind == "sample")
    {
        const auto sample = builtInSampleFromId(id);
        if (!sample) return juce::Result::fail("That browser item cannot be dropped here.");
        result = session.insertBuiltInSampleInSlot(*sample, track, target);
        madeClip = true;
    }
    else if (kind == "preset")
    {
        const auto preset = patternPresetFromId(id);
        if (!preset) return juce::Result::fail("That browser item cannot be dropped here.");
        result = session.insertPatternPresetInSlot(*preset, track, target);
        madeClip = true;
    }
    else if (kind == "instrument" || kind == "effect" || kind == "midi-effect")
    {
        // A device joins the track's chain and makes no clip, as on a lane:
        // an instrument changes what the track runs, never its clips.
        const auto* device = DeviceCatalog::byId(id);
        if (device == nullptr) return juce::Result::fail("That device is not in this build.");
        result = session.addDevice(device->id, track);
    }
    else if (kind == "drumkit")
        result = session.addDrumKit(browserDropKitFile(description), track);
    else if (kind == "drum-preset")
        result = session.addDrumSound(browserDropDrumPresetFile(description), track);
    else if (kind == "device-preset")
        result = session.addDeviceFromPreset(browserDropPresetFile(description), track);
    else
        return juce::Result::fail("That cannot be dropped on a console.");
    if (result.failed())
        return result;
    sync();
    if (madeClip && loadOnDeck)
        return chooseCell(target);
    return juce::Result::ok();
}

void DjClipGrid::paint(juce::Graphics& g)
{
    if (track < 0) return;
    const auto trackColour = session.trackColour(track);
    const auto count = rows();
    for (int row = 0; row < count; ++row)
    {
        const auto scene = row == 0 ? songCell : firstScene + row - 1;
        if (scene > scenes) break;
        const auto cell = cellBounds(scene);
        if (cell.isEmpty()) continue;
        const auto isLit = scene == lit, isHovered = scene == hovered, isTarget = scene == dropTarget;
        auto inner = cell.toFloat();
        if (scene == songCell)
        {
            g.setColour(isHovered ? palette::control.brighter(0.12f) : palette::control);
            g.fillRect(inner);
            g.setColour(trackColour);
            g.fillRect(inner.removeFromLeft(4.0f));
            g.setColour(palette::text);
            g.setFont(uiFontBold(9.0f));
            drawSnappedText(g, "SONG", cell.withTrimmedLeft(8), juce::Justification::centredLeft, true);
        }
        else if (scene == scenes)
        {
            // The row past the last scene: a double-click makes a scene and
            // a clip in it.
            g.setColour(palette::control.withMultipliedAlpha(isHovered ? 0.9f : 0.5f));
            g.fillRect(inner);
            g.setColour(palette::textDim);
            g.setFont(uiFontBold(9.0f));
            drawSnappedText(g, "+", cell, juce::Justification::centred, true);
        }
        else if (const auto info = session.slotClip(track, scene); info.hasClip)
        {
            auto fill = info.colour.withMultipliedSaturation(0.85f);
            if (!isLit) fill = fill.withMultipliedBrightness(0.62f);
            if (isHovered) fill = fill.brighter(0.12f);
            g.setColour(fill);
            g.fillRect(inner);
            g.setColour(fill.contrasting(0.85f));
            g.setFont(uiFont(9.0f));
            drawSnappedText(g, info.name, cell.withTrimmedLeft(5).withTrimmedRight(3), juce::Justification::centredLeft, true);
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

void DjClipGrid::mouseMove(const juce::MouseEvent& event)
{
    const auto cell = cellAt(event.getPosition());
    if (cell == hovered) return;
    hovered = cell;
    repaint();
}

void DjClipGrid::mouseExit(const juce::MouseEvent&)
{
    if (hovered == noCell) return;
    hovered = noCell;
    repaint();
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
    const auto info = session.slotClip(track, pressedCell);
    if (!info.hasClip) return;
    dragStarted = true;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging(slotDragDescription(track, pressedCell), this, clipDragImage(info.name, info.colour), true);
}

void DjClipGrid::mouseUp(const juce::MouseEvent& event)
{
    const auto cell = pressedCell;
    pressedCell = noCell;
    if (dragStarted || event.mods.isPopupMenu() || !event.mouseWasClicked() || cell == noCell) return;
    if (cell == songCell || (cell < scenes && session.slotClip(track, cell).hasClip))
        report(chooseCell(cell), cell == songCell ? "The deck plays the song's track" : "The deck plays " + session.slotClip(track, cell).name.quoted());
    else if (status)
        status("An empty cell: double-click it for a new one-bar clip, or drop a clip or a sample on it");
}

void DjClipGrid::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto cell = cellAt(event.getPosition());
    if (cell == noCell || cell == songCell) return;
    if (cell < scenes && session.slotClip(track, cell).hasClip)
    {
        report(chooseCell(cell), "Editing " + session.slotClip(track, cell).name.quoted() + ": the deck follows each change");
        if (editRequested) editRequested(track, session.slotClip(track, cell).clipID);
        return;
    }
    report(createClipAt(cell), "A one-bar clip: the deck plays it and the editor is open on it");
}

void DjClipGrid::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto step = wheel.deltaY < 0.0f ? 1 : wheel.deltaY > 0.0f ? -1 : 0;
    const auto next = juce::jlimit(0, std::max(0, scenes + 1 - std::max(1, rows() - 1)), firstScene + step);
    if (next == firstScene) return;
    firstScene = next;
    repaint();
}

void DjClipGrid::showCellMenu(int scene)
{
    const auto info = session.slotClip(track, scene);
    if (!info.hasClip) return;
    juce::PopupMenu menu;
    menu.addSectionHeader(info.name.toUpperCase());
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
    const auto target = cell == songCell ? noCell : cell;
    if (target == dropTarget) return;
    dropTarget = target;
    repaint();
}

void DjClipGrid::itemDragExit(const SourceDetails&)
{
    if (dropTarget == noCell) return;
    dropTarget = noCell;
    repaint();
}

void DjClipGrid::itemDropped(const SourceDetails& details)
{
    const auto cell = cellAt(details.localPosition);
    dropTarget = noCell;
    report(applyDrop(cell == songCell || cell == noCell ? firstFree : cell, details.description.toString(), false),
           "Dropped on the console: press the cell to play it");
    repaint();
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
    const auto target = cell == songCell ? noCell : cell;
    if (target == dropTarget) return;
    dropTarget = target;
    repaint();
}

void DjClipGrid::fileDragExit(const juce::StringArray&)
{
    if (dropTarget == noCell) return;
    dropTarget = noCell;
    repaint();
}

void DjClipGrid::filesDropped(const juce::StringArray& files, int x, int y)
{
    const auto cell = cellAt({x, y});
    dropTarget = noCell;
    for (const auto& path : files)
        if (isDroppedSoundFile(juce::File(path)))
        {
            report(applyDrop(cell == songCell || cell == noCell ? firstFree : cell, "rhino-browser:file:" + path, false),
                   "Dropped on the console: press the cell to play it");
            break;
        }
    repaint();
}
}
