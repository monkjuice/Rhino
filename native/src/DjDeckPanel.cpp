#include "DjDeckPanel.h"
#include "BrowserIds.h"
#include "ContentLibrary.h"
#include <algorithm>
#include <cmath>

// A deck's console: see DjDeckPanel.h.

namespace rhino
{
namespace
{
constexpr double beatLoopSizes[] {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
constexpr int jumpSizes[] {1, 2, 4, 8, 16, 32};
constexpr int tempoRanges[] {6, 10, 16, 100};

juce::String beatsName(double beats)
{
    if (beats >= 1.0) return juce::String(static_cast<int>(beats));
    return "1/" + juce::String(static_cast<int>(std::round(1.0 / beats)));
}

void dressMenuButton(juce::TextButton& button, const juce::String& tooltip)
{
    button.setColour(juce::TextButton::buttonColourId, palette::control);
    button.setColour(juce::TextButton::buttonOnColourId, palette::hover);
    button.setColour(juce::TextButton::textColourOffId, palette::text);
    button.setColour(juce::TextButton::textColourOnId, palette::text);
    button.setTooltip(tooltip);
    button.setWantsKeyboardFocus(false);
    button.setMouseClickGrabsKeyboardFocus(false);
}
}

DjDeckPanel::DjDeckPanel(Session& s, int deckIndex) : session(s), deck(deckIndex), display(s, deckIndex)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    display.status = [this](const juce::String& message) { if (status) status(message); };
    dressMenuButton(source, "What this deck plays: a track or a group of the song, bounced to audio, or a file. "
                            "Drop an audio file on the deck to load it.");
    source.onClick = [this] { showSourceMenu(); };
    live.setTooltip("Live - play the track's instrument from the keys over the bounce. The track's input is monitored "
                    "while this is lit; a Drum Rack on it plays its pads.");
    live.onClick = [this]
    {
        const auto result = session.setDjDeckLive(deck, !info.live);
        if (status) status(result.failed() ? result.getErrorMessage()
                                           : info.live ? "Live off" : "Live: the track's input plays over the deck. Press M for the typing keyboard.");
    };
    edit.setTooltip("Edit - open the track's clip in the note editor. The deck bounces again a moment after each change, "
                    "keeping its place.");
    edit.onClick = [this]
    {
        const auto clip = session.djDeckEditClip(deck);
        if (clip == te::EditItemID())
        {
            if (status) status("Only a track of the song is edited. Load one on this deck first.");
            return;
        }
        if (editRequested) editRequested(info.track, clip);
    };
    reload.setTooltip("Bounce the track again now. Lit while the bounce is behind the song.");
    reload.onClick = [this]
    {
        const auto result = session.rebounceDjDeck(deck);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    reload.onRightClick = [this]
    {
        session.setDjDeckAutoRebounce(deck, !info.autoRebounce);
        if (status) status(info.autoRebounce ? "The deck bounces again only when Reload is pressed"
                                             : "The deck bounces again by itself after a change");
    };
    eject.setTooltip("Eject - take the material off the deck. Right-click removes the deck.");
    eject.onClick = [this] { session.ejectDjDeck(deck); };
    eject.onRightClick = [this] { if (removeRequested) removeRequested(); };
    for (int i = 0; i < DjDeck::hotCueCount; ++i)
    {
        const auto letter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + i));
        auto pad = std::make_unique<DjPad>(letter, palette::djHotCue[static_cast<size_t>(i)]);
        pad->setTooltip("Hot cue " + letter + " - press on an empty pad to set it where the deck is, press again to jump "
                        "there and play. Right-click clears it.");
        pad->setGhosted(true);
        pad->onClick = [this, i] { session.djHotCue(deck, i); };
        pad->onRightClick = [this, i] { session.djClearHotCue(deck, i); };
        addAndMakeVisible(*pad);
        hotCues[static_cast<size_t>(i)] = std::move(pad);
    }
    cue.setTooltip("Cue - playing: stop and go back to the cue point. Stopped: set the cue point here, or hold to hear "
                   "from it.");
    cue.setTriggeredOnMouseDown(true);
    cue.onStateChange = [this]
    {
        if (cue.isDown()) session.djCueDown(deck);
        else session.djCueUp(deck);
    };
    play.setTooltip("Play/pause. With quantise on and another deck playing, the start waits for that deck's next beat "
                    "or bar and the key blinks until then.");
    play.onClick = [this] { session.djTogglePlay(deck); };
    loopIn.setTooltip("Loop in - the loop starts here.");
    loopIn.onClick = [this] { session.djLoopIn(deck); };
    loopOut.setTooltip("Loop out - the loop ends here and begins at once.");
    loopOut.onClick = [this] { session.djLoopOut(deck); };
    reloop.setTooltip("Reloop/exit - leave the loop, or go back to it.");
    reloop.onClick = [this] { session.djReloopExit(deck); };
    loopHalve.setTooltip("Halve the loop.");
    loopHalve.onClick = [this] { session.djLoopHalve(deck); };
    loopDouble.setTooltip("Double the loop.");
    loopDouble.onClick = [this] { session.djLoopDouble(deck); };
    dressMenuButton(beatLoop, "Beat loop - a loop of this many beats from the beat the deck is on. Click to loop, "
                              "right-click to choose the length.");
    beatLoop.onClick = [this] { session.djBeatLoop(deck, beatLoopBeats); };
    beatLoop.addMouseListener(this, false);
    jumpBack.setTooltip("Beat jump back.");
    jumpBack.onClick = [this] { session.djBeatJump(deck, -jumpBeats); };
    jumpForward.setTooltip("Beat jump forward.");
    jumpForward.onClick = [this] { session.djBeatJump(deck, jumpBeats); };
    dressMenuButton(jumpSize, "How many beats a beat jump moves.");
    jumpSize.onClick = [this] { showJumpMenu(); };
    syncKey.setTooltip("Sync - play at the master deck's tempo, beats locked to its beats.");
    syncKey.onClick = [this] { session.djSetSynced(deck, !state.synced); };
    master.setTooltip("Master - the deck the others sync to and the beat the effect follows. The first deck to play "
                      "takes it until another is chosen.");
    master.onClick = [this] { session.djSetMaster(deck); };
    reverse.setTooltip("Reverse - play backwards.");
    reverse.onClick = [this] { session.djSetReversed(deck, !state.reversed); };
    tempo.setTooltip("Tempo - drag to play faster or slower, within the range beside it. Shift drags fine; double-click "
                     "returns to zero.");
    tempo.setDefault(0.5f);
    tempo.setDetent(0.5f);
    tempo.setValue(0.5f, false);
    tempo.onChange = [this](float v)
    {
        const auto range = static_cast<float>(state.tempoRange);
        session.djSetTempoPercent(deck, (v - 0.5f) * 2.0f * range);
    };
    dressMenuButton(tempoRange, "The tempo fader's range: 6, 10, 16 or 100 per cent either way.");
    tempoRange.onClick = [this] { showTempoRangeMenu(); };
    tempoReset.setTooltip("Tempo reset - back to the track's own tempo.");
    tempoReset.onClick = [this] { session.djSetTempoPercent(deck, 0.0f); };
    for (auto* component : std::initializer_list<juce::Component*>{&display, &source, &live, &edit, &reload, &eject, &cue, &play,
                                                                   &loopIn, &loopOut, &reloop, &loopHalve, &loopDouble, &beatLoop,
                                                                   &jumpBack, &jumpForward, &jumpSize, &syncKey, &master, &reverse,
                                                                   &tempo, &tempoRange, &tempoReset})
        addAndMakeVisible(component);
    beatLoop.setButtonText(beatsName(beatLoopBeats) + " BEAT");
    jumpSize.setButtonText(juce::String(jumpBeats));
    sync();
}

void DjDeckPanel::setDeck(int deckIndex)
{
    deck = deckIndex;
    display.setDeck(deckIndex);
    sync();
}

void DjDeckPanel::sync()
{
    info = session.djDeckInfo(deck);
    state = session.djDeckState(deck);
    const auto trackDeck = info.kind == Session::DjSourceKind::track;
    const auto bounced = trackDeck || info.kind == Session::DjSourceKind::group;
    source.setButtonText(info.kind == Session::DjSourceKind::none ? "Choose a track..." : info.name);
    live.setEnabled(trackDeck && info.loaded);
    live.setLit(info.live);
    edit.setEnabled(trackDeck && info.loaded);
    reload.setEnabled(bounced);
    reload.setLit(bounced && info.stale);
    reload.setLabel(info.autoRebounce ? "RELOAD" : "RELOAD*");
    eject.setEnabled(info.kind != Session::DjSourceKind::none);
    const auto loaded = info.loaded;
    for (auto* pad : {&cue, &play, &loopIn, &loopOut, &reloop, &loopHalve, &loopDouble, &jumpBack, &jumpForward, &syncKey, &master, &reverse,
                      &tempoReset})
        pad->setEnabled(loaded);
    for (auto& pad : hotCues)
        pad->setEnabled(loaded);
    beatLoop.setEnabled(loaded);
    tempo.setEnabled(loaded);
    tempoRange.setButtonText(state.tempoRange >= 100 ? "WIDE" : juce::String::charToString(0xb1) + juce::String(state.tempoRange));
    display.sync();
    tick(true);
    repaint(headerArea());
}

void DjDeckPanel::tick(bool blinkPhase)
{
    state = session.djDeckState(deck);
    const auto waiting = state.transport == Session::DjDeckState::Transport::waiting;
    play.setLit(state.isPlaying() || waiting);
    play.setBlinking(waiting);
    play.setBlinkPhase(blinkPhase);
    // The cue key lights at the cue point, as a CDJ's does, and blinks where
    // a press would set one.
    const auto atCue = std::abs(state.positionSeconds - state.cueSeconds) < 0.001;
    const auto stopped = state.transport == Session::DjDeckState::Transport::stopped;
    cue.setLit(state.transport == Session::DjDeckState::Transport::cueing || (stopped && atCue) || (stopped && !atCue && blinkPhase));
    for (int i = 0; i < DjDeck::hotCueCount; ++i)
        hotCues[static_cast<size_t>(i)]->setLit(state.hotCueSeconds[static_cast<size_t>(i)] >= 0.0);
    loopIn.setLit(state.loop.startSeconds >= 0.0);
    loopOut.setLit(state.loop.valid());
    reloop.setLit(state.loop.active);
    syncKey.setLit(state.synced);
    master.setLit(state.master);
    reverse.setLit(state.reversed);
    if (!tempo.isDragging())
        tempo.setValue(0.5f + state.tempoPercent / (2.0f * static_cast<float>(std::max(1, state.tempoRange))), false);
}

juce::Rectangle<int> DjDeckPanel::headerArea() const
{
    return getLocalBounds().removeFromTop(headerHeight);
}

void DjDeckPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    const auto header = headerArea();
    g.setColour(palette::trackCard);
    g.fillRect(header);
    g.setColour(palette::border);
    g.fillRect(0, header.getBottom() - 1, getWidth(), 1);
    g.drawRect(getLocalBounds());
    // The deck's number, as a player's.
    g.setColour(palette::displayInset);
    g.fillRect(header.withWidth(26).reduced(3));
    g.setColour(palette::displayText);
    g.setFont(uiFontBold(12.0f));
    drawSnappedText(g, juce::String(deck + 1), header.withWidth(26), juce::Justification::centred);
    if (dropHighlight)
    {
        g.setColour(palette::selection.withAlpha(0.25f));
        g.fillRect(getLocalBounds());
    }
}

void DjDeckPanel::layoutRow(juce::Rectangle<int> row, const std::vector<std::pair<juce::Component*, int>>& cells)
{
    // Widths are weights; a zero-weight cell is a gap.
    int total = 0;
    for (const auto& cell : cells) total += cell.second;
    if (total <= 0) return;
    const auto gap = 3;
    const auto usable = row.getWidth() - gap * (static_cast<int>(cells.size()) - 1);
    auto x = row.getX();
    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto width = usable * cells[i].second / total;
        if (cells[i].first != nullptr)
            cells[i].first->setBounds(x, row.getY(), width, row.getHeight());
        x += width + gap;
    }
}

void DjDeckPanel::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(headerHeight).withTrimmedLeft(28).reduced(2);
    eject.setBounds(header.removeFromRight(46));
    header.removeFromRight(3);
    reload.setBounds(header.removeFromRight(52));
    header.removeFromRight(3);
    edit.setBounds(header.removeFromRight(44));
    header.removeFromRight(3);
    live.setBounds(header.removeFromRight(44));
    header.removeFromRight(3);
    source.setBounds(header);
    bounds.reduce(4, 0);
    // Three rows of keys and the tempo row at the foot; the screen takes
    // the rest.
    auto foot = bounds.removeFromBottom(rowHeight * 4 + rowGap * 4);
    foot.removeFromTop(rowGap);
    auto pads = foot.removeFromTop(rowHeight);
    std::vector<std::pair<juce::Component*, int>> cells;
    for (auto& pad : hotCues) cells.push_back({pad.get(), 1});
    layoutRow(pads, cells);
    foot.removeFromTop(rowGap);
    layoutRow(foot.removeFromTop(rowHeight), {{&cue, 4}, {&play, 4}, {nullptr, 1}, {&loopIn, 3}, {&loopOut, 3}, {&reloop, 4},
                                              {&loopHalve, 2}, {&loopDouble, 2}, {&beatLoop, 4}});
    foot.removeFromTop(rowGap);
    layoutRow(foot.removeFromTop(rowHeight), {{&jumpBack, 2}, {&jumpSize, 2}, {&jumpForward, 2}, {nullptr, 1}, {&syncKey, 4},
                                              {&master, 4}, {&reverse, 3}, {nullptr, 1}, {&tempoRange, 3}, {&tempoReset, 3}});
    foot.removeFromTop(rowGap);
    tempo.setBounds(foot.removeFromTop(rowHeight));
    display.setBounds(bounds.reduced(0, 3));
}

void DjDeckPanel::mouseDown(const juce::MouseEvent& event)
{
    if (event.eventComponent == &beatLoop && event.mods.isPopupMenu())
    {
        showBeatLoopMenu();
        return;
    }
    if (selected) selected();
}

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
    const auto groups = session.trackGroups();
    if (!groups.empty())
    {
        menu.addSectionHeader("GROUPS");
        for (const auto& group : groups)
            menu.addItem(1000 + group.id, group.name, true, info.kind == Session::DjSourceKind::group && info.groupId == group.id);
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
            else if (choice >= 1000 && choice < 2000) result = safe->session.loadDjDeckGroup(safe->deck, choice - 1000);
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(beatLoop),
        [safe = juce::Component::SafePointer<DjDeckPanel>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            safe->beatLoopBeats = beatLoopSizes[choice - 1];
            safe->beatLoop.setButtonText(beatsName(safe->beatLoopBeats) + " BEAT");
        });
}

void DjDeckPanel::showJumpMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("BEAT JUMP");
    for (int i = 0; i < static_cast<int>(std::size(jumpSizes)); ++i)
        menu.addItem(i + 1, juce::String(jumpSizes[i]) + (jumpSizes[i] == 1 ? " beat" : " beats"), true, jumpSizes[i] == jumpBeats);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(jumpSize),
        [safe = juce::Component::SafePointer<DjDeckPanel>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            safe->jumpBeats = jumpSizes[choice - 1];
            safe->jumpSize.setButtonText(juce::String(safe->jumpBeats));
        });
}

void DjDeckPanel::showTempoRangeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("TEMPO RANGE");
    for (int i = 0; i < static_cast<int>(std::size(tempoRanges)); ++i)
        menu.addItem(i + 1, tempoRanges[i] >= 100 ? juce::String("Wide") : juce::String::charToString(0xb1) + juce::String(tempoRanges[i]) + "%",
                     true, tempoRanges[i] == state.tempoRange);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(tempoRange),
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

void DjDeckPanel::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
        if (isDroppedSoundFile(juce::File(path)))
        {
            load(juce::File(path));
            return;
        }
}

bool DjDeckPanel::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return browserDropFile(details.description.toString()) != juce::File();
}

void DjDeckPanel::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto file = browserDropFile(details.description.toString());
    if (file != juce::File())
        load(file);
}
}
