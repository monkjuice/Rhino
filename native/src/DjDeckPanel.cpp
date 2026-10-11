#include "DjDeckPanel.h"
#include "BrowserIds.h"
#include "ClipDrag.h"
#include "ContentLibrary.h"
#include <algorithm>
#include <cmath>
#include <iterator>

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

juce::String rangeName(int range)
{
    return range >= 100 ? juce::String("WIDE") : juce::String::charToString(static_cast<juce::juce_wchar>(0xb1)) + juce::String(range) + "%";
}
}

DjDeckPanel::DjDeckPanel(Session& s, int deckIndex) : session(s), deck(deckIndex), display(s, deckIndex), grid(s, deckIndex)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    // A press anywhere on the console selects the deck for the Device View.
    addMouseListener(this, true);
    display.status = [this](const juce::String& message) { if (status) status(message); };
    grid.status = display.status;
    grid.editRequested = [this](int track, te::EditItemID clip) { if (editRequested) editRequested(track, clip); };
    source.setColour(juce::TextButton::buttonColourId, palette::control);
    source.setColour(juce::TextButton::buttonOnColourId, palette::hover);
    source.setColour(juce::TextButton::textColourOffId, palette::text);
    source.setColour(juce::TextButton::textColourOnId, palette::text);
    source.setTooltip("What this deck plays: a track or a group of the song, bounced to audio, or a file. "
                      "Drop an audio file on the deck to load it.");
    source.setWantsKeyboardFocus(false);
    source.setMouseClickGrabsKeyboardFocus(false);
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
    reload.setTooltip("Bounce the track again now. Lit while the bounce is behind the song. Right-click to stop it "
                      "bouncing by itself after a change.");
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
    cue.setRound(true);
    cue.setTooltip("Cue - playing: stop and go back to the cue point. Stopped: set the cue point here, or hold to hear "
                   "from it.");
    cue.setTriggeredOnMouseDown(true);
    cue.onStateChange = [this]
    {
        if (cue.isDown()) session.djCueDown(deck);
        else session.djCueUp(deck);
    };
    play.setRound(true);
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
    beatLoop.setTooltip("Beat loop - a loop of this many beats from the beat the deck is on. Press to loop, "
                        "right-click to choose the length.");
    beatLoop.onClick = [this] { session.djBeatLoop(deck, beatLoopBeats); };
    beatLoop.onRightClick = [this] { showBeatLoopMenu(); };
    jumpBack.setTooltip("Beat jump back.");
    jumpBack.onClick = [this] { session.djBeatJump(deck, -jumpBeats); };
    jumpForward.setTooltip("Beat jump forward.");
    jumpForward.onClick = [this] { session.djBeatJump(deck, jumpBeats); };
    jumpSize.setTooltip("How many beats a beat jump moves. Press to choose.");
    jumpSize.onClick = [this] { showJumpMenu(); };
    searchBack.setTooltip("Back to the previous cue point - a hot cue or the cue - or to the top of the track.");
    searchBack.onClick = [this] { session.djJumpToCue(deck, false); };
    searchForward.setTooltip("On to the next cue point.");
    searchForward.onClick = [this] { session.djJumpToCue(deck, true); };
    reverse.setTooltip("Reverse - play backwards.");
    reverse.onClick = [this] { session.djSetReversed(deck, !state.reversed); };
    quantise.setTooltip("Quantize - cue points and loop points snap to the nearest beat while this is lit.");
    quantise.onClick = [this] { session.djSetQuantiseSnap(deck, !state.quantiseSnap); };
    jog.setTooltip("The jog wheel. In vinyl mode, dragging the platter scratches: the deck follows the hand, backwards "
                   "too, and stands still while the hand rests. The ring, or the platter in CDJ mode, nudges the "
                   "tempo. A standing deck is scrubbed.");
    jog.deckPlaying = [this] { return state.isPlaying(); };
    jog.onScratch = [this](double rate, bool active) { session.djScratch(deck, rate, active); };
    jog.onNudge = [this](float percent) { session.djNudge(deck, percent); };
    jog.onScrub = [this](double seconds) { session.djSeek(deck, state.positionSeconds + seconds); };
    vinyl.setTooltip("Vinyl mode: the platter scratches. Off, it nudges as the ring does.");
    vinyl.onClick = [this]
    {
        jog.setVinylMode(!jog.isVinylMode());
        vinyl.setLit(jog.isVinylMode());
    };
    vinyl.setLit(true);
    dressDjKnob(brake, palette::activeNeutral, 0.0, 2.0, 0.0, "Brake - how long a stop winds down and a start spins up, as "
                                                            "a turntable's motor would. At zero both are at once.");
    brake.onValueChange = [this] { session.djSetBrake(deck, static_cast<float>(brake.getValue())); };
    syncKey.setTooltip("Sync - play at the master deck's tempo, beats locked to its beats.");
    syncKey.onClick = [this] { session.djSetSynced(deck, !state.synced); };
    master.setTooltip("Master - the deck the others sync to and the beat the effect follows. The first deck to play "
                      "takes it until another is chosen.");
    master.onClick = [this] { session.djSetMaster(deck); };
    tempoRange.setTooltip("The tempo fader's range: 6, 10, 16 per cent either way, or wide.");
    tempoRange.onClick = [this] { showTempoRangeMenu(); };
    tempo.setTooltip("Tempo - drag to play faster or slower, within the range above it. Shift drags fine; double-click "
                     "returns to zero. The fader runs the CDJ's way: down is faster.");
    tempo.setDefault(0.5f);
    tempo.setDetent(0.5f);
    tempo.setTicks(9);
    tempo.setValue(0.5f, false);
    // A CDJ's tempo fader is faster at the bottom: the value is inverted.
    tempo.onChange = [this](float v)
    {
        const auto range = static_cast<float>(state.tempoRange);
        session.djSetTempoPercent(deck, (0.5f - v) * 2.0f * range);
    };
    tempoReset.setTooltip("Tempo reset - back to the track's own tempo.");
    tempoReset.onClick = [this] { session.djSetTempoPercent(deck, 0.0f); };
    for (auto* component : std::initializer_list<juce::Component*>{&display, &grid, &source, &live, &edit, &reload, &eject, &cue, &play,
                                                                   &loopIn, &loopOut, &reloop, &loopHalve, &loopDouble, &beatLoop,
                                                                   &jumpBack, &jumpForward, &jumpSize, &searchBack, &searchForward,
                                                                   &reverse, &quantise, &jog, &vinyl, &brake, &syncKey, &master,
                                                                   &tempoRange, &tempo, &tempoReset})
        addAndMakeVisible(component);
    beatLoop.setLabel(beatsName(beatLoopBeats) + " BEAT");
    jumpSize.setLabel(juce::String(jumpBeats));
    sync();
}

void DjDeckPanel::setDeck(int deckIndex)
{
    deck = deckIndex;
    display.setDeck(deckIndex);
    grid.setDeck(deckIndex);
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
    // A deck keeps playing what it had while a load is in flight, so its
    // keys stay lit through a cell switch rather than blinking off and on.
    const auto loaded = info.loaded || (info.loading && session.djDeckTrack(deck) != nullptr);
    for (auto* pad : {&cue, &play, &loopIn, &loopOut, &reloop, &loopHalve, &loopDouble, &beatLoop, &jumpBack, &jumpForward,
                      &searchBack, &searchForward, &syncKey, &master, &reverse, &quantise, &tempoReset})
        pad->setEnabled(loaded);
    for (auto& pad : hotCues)
        pad->setEnabled(loaded);
    tempo.setEnabled(loaded);
    jog.setEnabled(loaded);
    jog.setTrackName(loaded ? info.name : juce::String());
    tempoRange.setLabel(rangeName(state.tempoRange));
    brake.setValue(state.brakeSeconds, juce::dontSendNotification);
    display.sync();
    grid.sync();
    if (grid.hasTrack() != grid.isVisible())
    {
        grid.setVisible(grid.hasTrack());
        resized();
    }
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
    quantise.setLit(state.quantiseSnap);
    if (!tempo.isDragging())
        tempo.setValue(0.5f - state.tempoPercent / (2.0f * static_cast<float>(std::max(1, state.tempoRange))), false);
}

void DjDeckPanel::tickDisplay()
{
    display.tick();
    const auto now = session.djDeckState(deck);
    jog.setPosition(now.positionSeconds, now.isPlaying());
}

juce::Rectangle<int> DjDeckPanel::headerArea() const
{
    return getLocalBounds().removeFromTop(headerHeight);
}

void DjDeckPanel::setFocused(bool shouldBeFocused)
{
    if (focused == shouldBeFocused) return;
    focused = shouldBeFocused;
    repaint(headerArea());
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
    // The deck's number, as a player's. The focused deck's lights up and the
    // header wears the strip a selected track card wears, because Space and
    // the lower pane answer to this deck.
    g.setColour(focused ? palette::selection : palette::displayInset);
    g.fillRect(header.withWidth(26).reduced(3));
    if (focused)
        g.fillRect(0, 0, 3, header.getHeight());
    g.setColour(focused ? palette::sideSurface : palette::displayText);
    g.setFont(uiFontBold(12.0f));
    drawSnappedText(g, juce::String(deck + 1), header.withWidth(26), juce::Justification::centred);
    // The small labels: TEMPO over the fader, BRAKE under its knob.
    g.setColour(palette::textDim);
    g.setFont(uiFontBold(8.0f));
    if (tempo.isVisible())
        drawSnappedText(g, "TEMPO", tempo.getBounds().withHeight(10).translated(0, -11), juce::Justification::centred);
    if (brake.isVisible())
        drawSnappedText(g, "BRAKE", brake.getBounds().withHeight(10).translated(0, brake.getHeight() + 1), juce::Justification::centred);
    if (dropHighlight)
    {
        g.setColour(palette::selection.withAlpha(0.25f));
        g.fillRect(getLocalBounds());
    }
}

void DjDeckPanel::layoutRow(juce::Rectangle<int> row, const std::vector<std::pair<juce::Component*, int>>& cells)
{
    // Widths are weights; a null cell is a gap.
    int total = 0;
    for (const auto& cell : cells) total += cell.second;
    if (total <= 0) return;
    const auto gap = 3;
    const auto usable = row.getWidth() - gap * (static_cast<int>(cells.size()) - 1);
    auto x = row.getX();
    for (const auto& cell : cells)
    {
        const auto width = usable * cell.second / total;
        if (cell.first != nullptr)
            cell.first->setBounds(x, row.getY(), width, row.getHeight());
        x += width + gap;
    }
}

// The screen takes the top, the hot cues sit under it, and the body below
// is the CDJ's three columns: keys, the jog wheel, the tempo. The keys at
// full size want six rows and two big round keys; a short panel squeezes
// the rows before it touches the screen, and the jog wheel is the first
// thing to go.
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
    bounds.reduce(4, 2);
    constexpr int fullRow = 20, fullGap = 3, bigFull = 52, bigCompact = 30;
    constexpr int fullKeys = fullRow * 6 + fullGap * 7 + bigFull * 2 + 6;
    constexpr int screenMinimum = DjDeckDisplay::readoutHeight + DjDeckDisplay::overviewHeight + 36;
    const auto padsHeight = bounds.getHeight() < 320 ? 16 : padRowHeight;
    const auto available = bounds.getHeight() - padsHeight - 3;
    // The screen takes about a third of the console and no more than 220 px;
    // the body - the keys, the jog wheel and the clips grid - takes the rest,
    // and never less than the keys need while the screen can give it.
    const auto screen = juce::jlimit(std::min(screenMinimum, std::max(0, available)), std::max(screenMinimum, 220),
                                     available * 32 / 100);
    const auto body = juce::jlimit(std::min(fullKeys, std::max(0, available - screenMinimum)),
                                   std::max(fullKeys, available - screenMinimum), available - screen);
    const auto compact = body < fullKeys;
    const auto bigKey = compact ? bigCompact : bigFull;
    const auto gap = compact ? 2 : fullGap;
    const auto row = juce::jlimit(12, fullRow, (body - bigKey * 2 - 6 - gap * 7) / 6);
    auto bodyArea = bounds.removeFromBottom(body);
    auto pads = bounds.removeFromBottom(padsHeight);
    std::vector<std::pair<juce::Component*, int>> cells;
    for (auto& pad : hotCues) cells.push_back({pad.get(), 1});
    layoutRow(pads, cells);
    bounds.removeFromBottom(3);
    display.setBounds(bounds);

    bodyArea.removeFromTop(gap);
    const auto showJog = bodyArea.getHeight() >= 150 && bodyArea.getWidth() >= 230;
    const auto width = bodyArea.getWidth();
    const auto leftWidth = showJog ? std::max(96, width * 30 / 100) : width * 58 / 100;
    const auto rightWidth = showJog ? std::max(64, width * 20 / 100) : width - leftWidth - 4;
    auto left = bodyArea.removeFromLeft(leftWidth);
    auto right = bodyArea.removeFromRight(rightWidth);
    auto centre = bodyArea.reduced(4, 0);
    // The left column: loops, jump, cue search, direction, then the big keys.
    const auto nextRow = [&left, row, gap]
    {
        auto r = left.removeFromTop(row);
        left.removeFromTop(gap);
        return r;
    };
    layoutRow(nextRow(), {{&loopIn, 1}, {&loopOut, 1}});
    layoutRow(nextRow(), {{&reloop, 1}});
    layoutRow(nextRow(), {{&beatLoop, 3}, {&loopHalve, 1}, {&loopDouble, 1}});
    layoutRow(nextRow(), {{&jumpBack, 1}, {&jumpSize, 1}, {&jumpForward, 1}});
    layoutRow(nextRow(), {{&searchBack, 1}, {&searchForward, 1}});
    layoutRow(nextRow(), {{&reverse, 1}, {&quantise, 1}});
    // The clips grid stands beside the big keys when the column is wide
    // enough for both, else above them, and only on a track deck.
    const auto keySize = std::min(bigKey, left.getWidth());
    const auto showGrid = grid.isVisible();
    auto gridArea = juce::Rectangle<int>();
    if (showGrid && left.getWidth() >= keySize + 70)
    {
        auto keysColumn = left.removeFromRight(keySize + 4);
        auto keys = keysColumn.removeFromBottom(keySize * 2 + 6);
        cue.setBounds(keys.removeFromTop(keySize).withSizeKeepingCentre(keySize, keySize));
        keys.removeFromTop(6);
        play.setBounds(keys.removeFromTop(keySize).withSizeKeepingCentre(keySize, keySize));
        gridArea = left.withTrimmedRight(4);
    }
    else
    {
        auto keys = left.removeFromBottom(keySize * 2 + 6);
        cue.setBounds(keys.removeFromTop(keySize).withSizeKeepingCentre(keySize, keySize));
        keys.removeFromTop(6);
        play.setBounds(keys.removeFromTop(keySize).withSizeKeepingCentre(keySize, keySize));
        if (showGrid)
            gridArea = left.withTrimmedBottom(4);
    }
    // A grid with no room for a cell takes empty bounds inside the console.
    grid.setBounds(gridArea.getHeight() >= DjClipGrid::cellHeight ? gridArea : juce::Rectangle<int>());
    // The right column: sync, master, the range, the fader, its reset.
    const auto nextRight = [&right, row, gap]
    {
        auto r = right.removeFromTop(row);
        right.removeFromTop(gap);
        return r;
    };
    syncKey.setBounds(nextRight());
    master.setBounds(nextRight());
    tempoRange.setBounds(nextRight());
    tempoReset.setBounds(right.removeFromBottom(row));
    right.removeFromBottom(gap);
    right.removeFromTop(12);   // the TEMPO label
    tempo.setBounds(right.withSizeKeepingCentre(std::min(28, right.getWidth()), right.getHeight()));
    // The centre: the platter, with vinyl and brake beneath it.
    jog.setVisible(showJog);
    vinyl.setVisible(showJog);
    brake.setVisible(showJog);
    if (!showJog)
    {
        jog.setBounds({});
        vinyl.setBounds({});
        brake.setBounds({});
        return;
    }
    auto foot = centre.removeFromBottom(row + 20);
    const auto half = foot.getWidth() / 2;
    vinyl.setBounds(foot.withWidth(half - 4).withSizeKeepingCentre(std::min(60, half - 4), row).withY(foot.getY()));
    brake.setBounds(juce::Rectangle<int>(28, 28).withCentre({foot.getX() + half + half / 2, foot.getY() + 14}));
    centre.removeFromBottom(4);
    const auto size = std::min(centre.getWidth(), centre.getHeight());
    jog.setBounds(centre.withSizeKeepingCentre(size, size));
}

void DjDeckPanel::mouseDown(const juce::MouseEvent& event)
{
    // Heard once directly and once as the console's own listener.
    if (event.eventTime == lastSelectTime) return;
    lastSelectTime = event.eventTime;
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
