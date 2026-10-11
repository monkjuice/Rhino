#include "DjDeckPanelInternal.h"
#include <algorithm>
#include <cmath>

// A deck's console: see DjDeckPanel.h. Its keys and how it follows its deck
// are here; its paint and layout in DjDeckPanelLayout.cpp; what it plays and
// its keys' menus in DjDeckPanelMenus.cpp.

namespace rhino
{
using namespace deckpanel;

DjDeckPanel::DjDeckPanel(Session& s, int deckIndex) : session(s), deck(deckIndex), display(s, deckIndex), grid(s, deckIndex)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    // A press anywhere on the console selects the deck for the Device View.
    addMouseListener(this, true);
    grid.status = [this](const juce::String& message) { if (status) status(message); };
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
    // The session announces the change at once and the console syncs under
    // the call, so what the deck was is read before it is changed.
    live.onClick = [this]
    {
        const auto wasLive = info.live;
        const auto result = session.setDjDeckLive(deck, !wasLive);
        if (status) status(result.failed() ? result.getErrorMessage()
                                           : wasLive ? "Live off" : "Live: the track's input plays over the deck. Press M for the typing keyboard.");
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
        const auto wasAutomatic = info.autoRebounce;
        session.setDjDeckAutoRebounce(deck, !wasAutomatic);
        if (status) status(wasAutomatic ? "The deck bounces again only when Reload is pressed"
                                        : "The deck bounces again by itself after a change");
    };
    eject.setTooltip("Eject - take the material off the deck. Right-click removes the deck.");
    eject.onClick = [this] { session.ejectDjDeck(deck); };
    eject.onRightClick = [this] { if (removeRequested) removeRequested(); };
    for (int i = 0; i < hotCueCount; ++i)
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
    // Every step of a scrub lands from where the deck stood when the scrub
    // began, read then: from the position the 30 Hz tick last read, every
    // step but the last between two ticks was lost.
    jog.onScrub = [this](double seconds, bool starting)
    {
        if (starting) scrubFrom = session.djDeckState(deck).positionSeconds;
        session.djSeek(deck, std::max(0.0, scrubFrom + seconds));
    };
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
    // The keys a set is played on act as the hand comes down, as a CDJ's do:
    // a start, a hot cue or a loop out lands where the press was, not a
    // click's hold later. The keys that open a menu act on release.
    for (auto* key : {&play, &loopIn, &loopOut, &reloop, &loopHalve, &loopDouble, &beatLoop, &jumpBack, &jumpForward,
                      &searchBack, &searchForward, &reverse, &syncKey, &master, &tempoReset})
        key->setTriggeredOnMouseDown(true);
    for (auto& pad : hotCues)
        pad->setTriggeredOnMouseDown(true);
    for (auto* component : std::initializer_list<juce::Component*>{&display, &grid, &source, &live, &edit, &reload, &eject, &cue, &play,
                                                                   &loopIn, &loopOut, &reloop, &loopHalve, &loopDouble, &beatLoop,
                                                                   &jumpBack, &jumpForward, &jumpSize, &searchBack, &searchForward,
                                                                   &reverse, &quantise, &jog, &vinyl, &brake, &syncKey, &master,
                                                                   &tempoRange, &tempo, &tempoReset})
        addAndMakeVisible(component);
    beatLoop.setLabel(beatsName(beatLoopBeats) + " BEAT");
    jumpSize.setLabel(juce::String(jumpBeats));
    sync(session.djDeckInfo(deck));
}

// What the deck plays. Every setter here compares before it repaints, so a
// sync that changed nothing on this deck repaints nothing on its console.
void DjDeckPanel::sync(const Session::DjDeckInfo& next)
{
    info = next;
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
    display.sync(info);
    grid.sync(info);
    if (grid.hasTrack() != grid.isVisible())
    {
        grid.setVisible(grid.hasTrack());
        resized();
    }
    tick(session.djDeckState(deck), blinkPhase);
}

// What the deck is doing, read from the audio thread's side at the view's
// 30 Hz: every key a press on the booth lights, so none of them needs the
// session to announce it.
void DjDeckPanel::tick(const Session::DjDeckState& now, bool phase)
{
    state = now;
    blinkPhase = phase;
    const auto waiting = state.transport == Session::DjDeckState::Transport::waiting;
    play.setLit(state.isPlaying() || waiting);
    play.setBlinking(waiting);
    play.setBlinkPhase(blinkPhase);
    // The cue key lights at the cue point, as a CDJ's does, and blinks where
    // a press would set one.
    const auto atCue = std::abs(state.positionSeconds - state.cueSeconds) < 0.001;
    const auto stopped = state.transport == Session::DjDeckState::Transport::stopped;
    cue.setLit(state.transport == Session::DjDeckState::Transport::cueing || (stopped && atCue) || (stopped && !atCue && blinkPhase));
    for (int i = 0; i < hotCueCount; ++i)
        hotCues[static_cast<size_t>(i)]->setLit(state.hotCueSeconds[static_cast<size_t>(i)] >= 0.0);
    loopIn.setLit(state.loop.startSeconds >= 0.0);
    loopOut.setLit(state.loop.valid());
    reloop.setLit(state.loop.active);
    syncKey.setLit(state.synced);
    master.setLit(state.master);
    reverse.setLit(state.reversed);
    quantise.setLit(state.quantiseSnap);
    if (state.tempoRange != shownRange)
    {
        shownRange = state.tempoRange;
        tempoRange.setLabel(rangeName(shownRange));
    }
    brake.setValue(state.brakeSeconds, juce::dontSendNotification);
    if (!tempo.isDragging())
        tempo.setValue(0.5f - state.tempoPercent / (2.0f * static_cast<float>(std::max(1, state.tempoRange))), false);
}

void DjDeckPanel::tickDisplay(const Session::DjDeckState& now)
{
    display.tick(now);
    jog.setPosition(now.positionSeconds, now.isPlaying());
}

void DjDeckPanel::tickDisplay()
{
    tickDisplay(session.djDeckState(deck));
}

void DjDeckPanel::setFocused(bool shouldBeFocused)
{
    if (focused == shouldBeFocused) return;
    focused = shouldBeFocused;
    repaint(headerArea());
}

void DjDeckPanel::mouseDown(const juce::MouseEvent& event)
{
    // Heard once directly and once as the console's own listener.
    if (event.eventTime == lastSelectTime) return;
    lastSelectTime = event.eventTime;
    if (nameBarPressed && headerArea().contains(event.getEventRelativeTo(this).getPosition()))
        nameBarPressed();
    else if (selected)
        selected();
}
}
