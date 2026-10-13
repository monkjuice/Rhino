#include "DjDeckPanelInternal.h"
#include "ui/browser/BrowserIds.h"
#include "ui/shared/ClipDrag.h"
#include "content/ContentLibrary.h"
#include <iterator>

// A deck's console, what it plays and its keys' menus: the source menu, the
// file chooser, the beat loop's length, the jump's size, the tempo range,
// and what lands when something is dropped on the console. See
// DjDeckPanel.h.

namespace rhino
{
using namespace deckpanel;

void DjDeckPanel::showSourceMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("TRACKS OF THE SONG");
    const auto count = session.trackCount();
    for (int track = 0; track < count; ++track)
    {
        if (session.isGroupBusTrack(track)) continue;
        const auto kind = session.trackType(track) == Session::TrackType::midi ? "MIDI" : "Audio";
        menu.addItem(100 + track, juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track) + "  (" + kind + ")",
                     true, info.kind == Session::DjSourceKind::track && info.track == track);
    }
    menu.addSectionHeader("FILES");
    menu.addItem(1, "Choose a file...");
    juce::PopupMenu library;
    const auto& samples = ContentLibrary::samples();
    juce::String pack;
    juce::PopupMenu packMenu;
    int sampleId = 10000;
    for (const auto& sample : samples)
    {
        if (sample.pack != pack)
        {
            if (pack.isNotEmpty()) library.addSubMenu(pack, packMenu);
            packMenu = {};
            pack = sample.pack;
        }
        packMenu.addItem(sampleId++, sample.name);
    }
    if (pack.isNotEmpty()) library.addSubMenu(pack, packMenu);
    menu.addSubMenu("Library samples", library, !samples.empty());
    juce::PopupMenu drives;
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots(roots);
    int driveId = 2000;
    for (const auto& root : roots)
    {
        if (driveId >= 2100) break;
        drives.addItem(driveId++, root.getFullPathName());
    }
    menu.addSubMenu("Drives", drives, !roots.isEmpty());
    if (info.kind != Session::DjSourceKind::none)
    {
        menu.addSeparator();
        menu.addItem(2, "Eject");
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(source),
        [safe = juce::Component::SafePointer<DjDeckPanel>(this), roots, samples](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            juce::Result result = juce::Result::ok();
            if (choice == 1) safe->chooseFile(juce::File::getSpecialLocation(juce::File::userMusicDirectory));
            else if (choice == 2) safe->session.ejectDjDeck(safe->deck);
            else if (choice >= 100 && choice < 1000) result = safe->session.loadDjDeckTrack(safe->deck, choice - 100);
            else if (choice >= 2000 && choice < 2100) safe->chooseFile(roots[choice - 2000]);
            else if (choice >= 10000 && choice - 10000 < static_cast<int>(samples.size()))
                safe->load(samples[static_cast<size_t>(choice - 10000)].file);
            if (result.failed() && safe->status) safe->status(result.getErrorMessage());
        });
}

void DjDeckPanel::chooseFile(const juce::File& startIn)
{
    chooser = std::make_unique<juce::FileChooser>("Load onto deck " + juce::String(deck + 1), startIn,
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.wma;*.m4a");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<DjDeckPanel>(this)](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult();
            if (file != juce::File())
                safe->load(file);
        });
}

void DjDeckPanel::load(const juce::File& file)
{
    const auto result = session.loadDjDeckFile(deck, file);
    if (status) status(result.failed() ? result.getErrorMessage() : "Reading " + file.getFileName() + " onto deck " + juce::String(deck + 1));
}

void DjDeckPanel::showBeatLoopMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("BEAT LOOP");
    for (int i = 0; i < static_cast<int>(std::size(beatLoopSizes)); ++i)
        menu.addItem(i + 1, beatsName(beatLoopSizes[i]) + (beatLoopSizes[i] == 1.0 ? " beat" : " beats"), true,
                     beatLoopSizes[i] == beatLoopBeats);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&beatLoop),
        [safe = juce::Component::SafePointer<DjDeckPanel>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            safe->beatLoopBeats = beatLoopSizes[choice - 1];
            safe->beatLoop.setLabel(beatsName(safe->beatLoopBeats) + " BEAT");
        });
}

void DjDeckPanel::showJumpMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("BEAT JUMP");
    for (int i = 0; i < static_cast<int>(std::size(jumpSizes)); ++i)
        menu.addItem(i + 1, juce::String(jumpSizes[i]) + (jumpSizes[i] == 1 ? " beat" : " beats"), true, jumpSizes[i] == jumpBeats);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&jumpSize),
        [safe = juce::Component::SafePointer<DjDeckPanel>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            safe->jumpBeats = jumpSizes[choice - 1];
            safe->jumpSize.setLabel(juce::String(safe->jumpBeats));
        });
}

void DjDeckPanel::showTempoRangeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("TEMPO RANGE");
    for (int i = 0; i < static_cast<int>(std::size(tempoRanges)); ++i)
        menu.addItem(i + 1, rangeName(tempoRanges[i]), true, tempoRanges[i] == state.tempoRange);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tempoRange),
        [safe = juce::Component::SafePointer<DjDeckPanel>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            safe->session.djSetTempoRange(safe->deck, tempoRanges[choice - 1]);
        });
}

bool DjDeckPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isDroppedSoundFile(juce::File(path)))
            return true;
    return false;
}

// A drop on the console beside the grid: on a track deck it goes into the
// first free cell and plays, held to the track's kind by the model, as the
// grid's own drops are; a deck playing a file or nothing takes a sound file
// as its material, as before.
void DjDeckPanel::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
        if (isDroppedSoundFile(juce::File(path)))
        {
            if (grid.hasTrack())
            {
                const auto result = grid.applyDrop(DjClipGrid::firstFree, "rhino-browser:file:" + path, true);
                if (status) status(result.failed() ? result.getErrorMessage() : "The deck plays " + juce::File(path).getFileNameWithoutExtension());
            }
            else
                load(juce::File(path));
            return;
        }
}

bool DjDeckPanel::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    return grid.hasTrack() ? isCrossViewDrag(description) : browserDropFile(description) != juce::File();
}

void DjDeckPanel::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    if (grid.hasTrack())
    {
        const auto result = grid.applyDrop(DjClipGrid::firstFree, description, true);
        if (status) status(result.failed() ? result.getErrorMessage() : "Dropped on the deck");
        return;
    }
    const auto file = browserDropFile(description);
    if (file != juce::File())
        load(file);
}
}
