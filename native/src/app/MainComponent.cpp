#include "MainComponent.h"
#include "AppPlatform.h"

namespace rhino
{

// The browser toggle: a narrow panel beside a wide one. Grey - the state is
// carried by brightness alone, not by a colour, because a coloured toggle in a
// neutral bar reads as a warning rather than as a switch.
static void paintSidebarIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour);
    icons::drawSidebar(g, area);
}

MainComponent::MainComponent(Session& s)
    : session(s), browser(s), grid(s), arrangement(s), djView(s), rack(s), audioClip(s), files(s), push(s)
{
    setOpaque(true);
    files.status = [this](const juce::String& message) { logStatus(message); };
    files.loadingChanged = [this](bool loading) { setEnabled(!loading); };
    arrangement.status = files.status;
    // The typing keyboard lives here, but the card is where a MIDI track is
    // armed and where its input is chosen, so the arrangement is given the
    // two questions it needs to ask about it.
    arrangement.typingKeyboardEnabled = [this] { return computerKeyboard.isEnabled(); };
    arrangement.enableTypingKeyboard = [this] { computerKeyboard.setEnabled(true); };
    arrangement.trackSelected = [this](int track)
    {
        if (!djViewOpen) rack.selectTrack(track);
        refreshEditorPanes();
        push.setSelectedTrack(track);
    };
    // The Push works on the arrangement's selected track and the device the
    // rack shows on it, and moves both. Its Play and Record mean what Space
    // and F9 mean in the view that is showing.
    rack.deviceSelected = [this](int track, int pluginIndex) { push.setSelectedDevice(track, pluginIndex); };
    push.selectTrack = [this](int track) { arrangement.selectTrack(track); };
    push.selectDevice = [this](int track, int pluginIndex)
    {
        if (djViewOpen) return;
        rack.selectTrack(track);
        rack.selectPluginIndex(pluginIndex);
    };
    push.play = [this]
    {
        if (djViewOpen) djView.togglePlayFocused();
        else session.togglePlayback();
    };
    push.returnToStart = [this] { if (!djViewOpen) session.returnToStart(); };
    push.record = [this]
    {
        if (djViewOpen) logStatus("Recording is the arrangement's: press Tab to return to it");
        else toggleRecording();
    };
    push.status = [this](const juce::String& message) { logStatus(message); };
    push.commandsAllowed = [this] { return isEnabled() && !loadingScreen.isRunning(); };
    // The arrangement starts a bounce and the shell renders it, a slice per
    // frame behind the loading screen, then hands it back to the arrangement.
    arrangement.bounceStarted = [this]
    {
        loadingScreen.run({"Bouncing to new track",
                           [this](double milliseconds) { return session.continueBounce(milliseconds); },
                           [this]
                           {
                               const auto progress = session.bounceProgress();
                               auto detail = "Rendering " + progress.source;
                               if (progress.steps > 1)
                                   detail << "  (" << progress.step << " of " << progress.steps << ")";
                               return std::make_pair(progress.fraction, detail);
                           },
                           [this] { session.cancelBounce(); },
                           [this] { arrangement.bounceFinished(); }});
    };
    // Export WAV the same way, handed back to the file commands to report.
    files.exportStarted = [this](const juce::File& file)
    {
        loadingScreen.run({"Exporting WAV",
                           [this](double milliseconds) { return session.continueExport(milliseconds); },
                           [this, detail = "Rendering " + file.getFileName()]
                           {
                               return std::make_pair(session.exportProgress(), detail);
                           },
                           [this] { session.cancelExport(); },
                           [this] { files.exportFinished(); }});
    };
    // The two lower panes answer to two different things, and to nothing
    // else: the clip editors to the selected clip, the Device View to a
    // clicked track card. Clicking a clip moves the working track too, so
    // this deliberately hangs off the card click rather than off the move.
    arrangement.trackFocused = [this](int) { showDeviceView(); };
    // Which editor a clip opens in is decided here rather than in the
    // timeline: an audio clip brings up the audio editor, a MIDI clip the
    // note editor, and a selection that is neither closes the pane.
    arrangement.clipSelected = [this](te::EditItemID) { refreshEditorPanes(); };
    arrangement.clipOpened = [this](te::EditItemID id) { openClip(id); };
    audioClip.status = files.status;
    // The panel's Split button advertises Ctrl+E, so it has to cut where
    // Ctrl+E cuts. The timeline owns the line; the panel only asks for it.
    audioClip.splitPosition = [this] { return arrangement.insertPointTime(); };
    djView.status = files.status;
    // A press on a console focuses its deck, and a deck that plays a
    // track of the song hands that track to the lower pane: its chain to
    // the Device View, its clip to whichever editor is open.
    djView.deckSelected = [this](int deck) { if (djViewOpen) followDeck(deck, true); };
    // The deck focused now plays another track, or another cell of its
    // track: the pane follows it to the clip it plays, and is not opened
    // for it.
    djView.focusedSourceChanged = [this](int deck) { if (djViewOpen) followDeck(deck, false); };
    // A deck's Edit key opens the cell it plays - a MIDI cell in the note
    // editor, an audio cell in the audio editor - which is the same lower
    // pane under either view: the deck makes the cell again at the next
    // poll after each change, so the edit is heard on the deck.
    djView.editRequested = [this](int track, te::EditItemID clip)
    {
        if (track < 0 || clip == te::EditItemID()) return;
        arrangement.selectTrack(track);
        rack.selectTrack(track);
        if (session.findAudioClip(clip) == nullptr)
        {
            const auto result = session.selectPatternClip(clip);
            if (result.failed())
            {
                logStatus(result.getErrorMessage());
                return;
            }
        }
        openClip(clip);
        logStatus("Editing " + session.trackName(track).quoted() + ": the deck follows each change");
    };
    djToggle.setTooltip("Show the DJ view: decks either side of a mixer, each playing a track's own clips live, or a file. Tab switches.");
    arrangementToggle.setTooltip("Show the Arrangement timeline. Tab switches.");
    djToggle.onClick = [this] { setDjViewOpen(true); };
    arrangementToggle.onClick = [this] { setDjViewOpen(false); };
    // A drag hovering a toggle shows that view, so a clip can be carried
    // from a lane to a console's grid and back.
    djToggle.dragHovered = [this] { setDjViewOpen(true); };
    arrangementToggle.dragHovered = [this] { setDjViewOpen(false); };
    browser.status = files.status;
    // A browser double-click has no drop target of its own, so it follows
    // whichever track the visible arrangement has selected.
    rack.status = files.status;
    rack.presetsChanged = [this] { browser.refreshPresets(); };
    // The Drum Rack's window is another place the hand works, so Space,
    // undo and the rest answer there, and the typing keyboard plays its pads.
    rack.shortcut = [this](const juce::KeyPress& key) { return keyPressed(key); };
    rack.listenForKeys = [this](juce::Component& window) { computerKeyboard.listenTo(window); };
    rack.keepAboveShell = [this](juce::Component& window, bool floating)
    {
        keepWindowAbove(window, floating ? this : nullptr);
    };
    editorToggle.onClick = [this] { toggleClipEditor(); };
    rackToggle.onClick = [this] { toggleDeviceView(); };
    editorToggle.setButtonText("Clip");
    rackToggle.setButtonText("Devices");
    editorToggle.setTooltip("Clip - show or hide the editor for the selected clip: the notes of a MIDI clip, "
                            "the waveform of an audio one.");
    rackToggle.setTooltip("Devices - show or hide the device chain of the selected track.");
    editorResolution.addItem("1/16", 16);
    editorResolution.addItem("1/32", 32);
    editorResolution.addItem("1/64", 64);
    editorResolution.setJustificationType(juce::Justification::centred);
    editorResolution.onChange = [this]
    {
        if (!updatingEditorResolution && editorResolution.getSelectedId() > 0)
            session.setEditorStepCount(editorResolution.getSelectedId());
    };
    editorZoomOut.setButtonText("-");
    editorZoomIn.setButtonText("+");
    editorZoomOut.setTooltip("Zoom out of the note editor - more bars across the same width.");
    editorZoomIn.setTooltip("Zoom in to the note editor - fewer bars across the same width.");
    editorZoomOut.onClick = [this] { grid.zoomOut(); };
    editorZoomIn.onClick = [this] { grid.zoomIn(); };
    scaleHighlight.addItem("Scale: off", 1);
    static constexpr const char* roots[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (int root = 0; root < 12; ++root)
        scaleHighlight.addItem(juce::String(roots[root]) + " Major", root + 2);
    for (int root = 0; root < 12; ++root)
        scaleHighlight.addItem(juce::String(roots[root]) + " Minor", root + 14);
    scaleHighlight.setSelectedId(1, juce::dontSendNotification);
    scaleHighlight.setTooltip("Highlight the rows of a scale in the note editor, so the notes that belong "
                              "to it stand out from the ones that do not.");
    scaleHighlight.onChange = [this] { grid.setScaleHighlight(scaleHighlight.getSelectedId()); };
    // Clip and Devices are two faces of one strip, so the one that is
    // showing is marked in grey rather than coloured in: a tab that lights
    // up teal in a neutral bar reads as a warning rather than as "you are
    // here". The restraint is the point.
    for (auto* toggle : std::initializer_list<juce::TextButton*>{&editorToggle, &rackToggle})
    {
        toggle->setColour(juce::TextButton::buttonColourId, palette::control);
        toggle->setColour(juce::TextButton::buttonOnColourId, palette::hover);
        toggle->setColour(juce::TextButton::textColourOffId, palette::textDim);
        toggle->setColour(juce::TextButton::textColourOnId, palette::text);
    }
    // The typing keyboard plays the MIDI input, so it is caught wherever
    // the focus happens to be. A text editor with focus consumes its own
    // keys and never reaches a listener, which is what keeps typing a
    // track name from playing a chord.
    computerKeyboard.note = [this](int midiNote, int velocity, bool isNoteOn)
    {
        session.sendMidiInputNote(midiNote, velocity, isNoteOn);
    };
    computerKeyboard.status = [this](const juce::String& message) { logStatus(message); };
    // Every panel that can hold focus, because a key is offered to the
    // listeners of the focused component and then of each of its parents
    // in turn - so a listener has to be on the branch the focus is on. The
    // editors are named individually to get first refusal ahead of their
    // own letter shortcuts; the shell catches whatever reaches it, and the
    // window itself is added later as the backstop for the moment nothing
    // holds focus at all.
    for (auto* component : std::initializer_list<juce::Component*>{this, &browser, &arrangement, &grid,
                                                                   &audioClip, &rack})
        computerKeyboard.listenTo(*component);
    // Nothing here takes focus on its own, and a window with no focused
    // component hands its keys to the window rather than to its content -
    // which is what made every shortcut, Space and F9 included, do nothing
    // at all until something had been clicked.
    setWantsKeyboardFocus(true);
    logStatus("PATTERN 1  /  4OSC     Double-click a cell for a note, or press B to draw, then press Play");
    infoView.setMultiLine(true, true);
    infoView.setReadOnly(true);
    infoView.setScrollbarsShown(false);
    infoView.setCaretVisible(false);
    infoView.setWantsKeyboardFocus(false);
    infoView.setColour(juce::TextEditor::backgroundColourId, palette::sideSurface);
    infoView.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    infoView.setColour(juce::TextEditor::textColourId, palette::textDim);
    infoView.setFont(uiFont(10.5f));
    // Tempo is a field you drag, which is how every DAW sets one: press
    // and move the mouse up or down, Ctrl for hundredths, double-click to
    // type an exact number. The range and the steps are the model's, so the
    // field cannot offer a tempo setTempo would then refuse.
    tempoBox.setRange(Session::minimumTempo, Session::maximumTempo, 1.0, 0.01);
    tempoBox.setDecimalPlaces(2);
    tempoBox.setSuffix("BPM");
    tempoBox.setFontSize(16.0f, 8.0f);
    tempoBox.setJustification(juce::Justification::centredLeft);
    tempoBox.setValue(session.tempo());
    tempoBox.setTooltip("Tempo - drag up or down, hold Ctrl for hundredths, double-click to type");
    // One undo step per drag rather than one per pixel: every tempo change
    // rescales the automation lanes and the loop with it.
    tempoBox.onDragStart = [this] { session.beginTempoGesture(); };
    tempoBox.onDragEnd = [this] { session.endTempoGesture(); };
    tempoBox.onValueChange = [this](double bpm) { session.setTempo(bpm); };
    // Two fields over one rule, dragged the same way. The limits come from
    // the model, which takes Live's: 1 to 99 over 1, 2, 4, 8 or 16.
    std::vector<double> denominators;
    for (const auto denominator : Session::timeSignatureDenominators)
        denominators.push_back(static_cast<double>(denominator));
    signatureField.setLimits(Session::minimumTimeSignatureNumerator,
                             Session::maximumTimeSignatureNumerator, denominators);
    signatureField.setFontSize(15.0f);
    signatureField.setSignature(session.timeSignature().numerator, session.timeSignature().denominator);
    signatureField.onDragStart = [this] { session.beginTempoGesture(); };
    signatureField.onDragEnd = [this] { session.endTempoGesture(); };
    signatureField.onChange = [this](int numerator, int denominator)
    {
        const auto result = session.setTimeSignature(numerator, denominator);
        if (result.failed()) logStatus(result.getErrorMessage());
    };
    position.configurationRequested = [this] { showDisplayMenu(); };
    undo.onClick = [this] { session.undo(); };
    redo.onClick = [this] { session.redo(); };
    metronome.setClickingTogglesState(true);
    metronome.onClick = [this] { session.setClickTrackEnabled(metronome.getToggleState()); };
    metronomeMenu.onClick = [this] { showMetronomeMenu(); };
    play.onClick = [this] { session.togglePlayback(); };
    stop.onStop = [this] { session.stop(); };
    stop.onReturnToStart = [this] { session.returnToStart(); };
    rewind.onClick = [this] { session.returnToStart(); };
    record.onClick = [this] { toggleRecording(); };
    undo.setTooltip("Undo - take back the last change to the project. The arrow dims when there is "
                    "nothing left to take back.");
    redo.setTooltip("Redo - put back the change that was undone last.");
    metronome.setTooltip("Metronome - a click on every beat while the transport is rolling.");
    metronomeMenu.setTooltip("Metronome settings - whether the first beat of a bar is emphasised, how loud "
                             "the click is, and how many bars it counts in before a recording.");
    rewind.setTooltip("Return to the start of the song");
    stop.setTooltip("Stop and return to the selected line  (double-click for the start of the song)");
    record.setTooltip("Record into the armed tracks  (F9)");

    // What each control on the bar draws. Kept together rather than spread
    // through the constructor, because they are one set: the bar reads as a
    // bar precisely because these are struck at the same weight, in the same
    // greys, from the same paths.
    browserToggle.setGlyphInset(0.15f);
    browserToggle.setActiveColour(palette::text);
    // The one wiring the bar was missing: without it the toggle lit and
    // dimmed with browserOpen but never changed it, so the only way to the
    // browser was View > Browser.
    browserToggle.onClick = [this] { toggleBrowser(); };
    browserToggle.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        paintSidebarIcon(g, area, colour);
    });
    metronome.setGlyphInset(0.12f);
    metronome.setActiveColour(palette::text);
    metronome.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::strokeFitted(g, icons::metronome(), area, 1.4f);
    });
    metronomeMenu.setGlyphInset(0.08f);
    metronomeMenu.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::strokeFitted(g, icons::chevronDown(), area.withSizeKeepingCentre(9.0f, 5.0f), 1.3f);
    });
    rewind.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::fillFitted(g, icons::returnToStart(), area.withSizeKeepingCentre(area.getWidth() * 0.88f, area.getHeight() * 0.7f));
    });
    stop.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::fillFitted(g, icons::stop(), area.withSizeKeepingCentre(area.getHeight() * 0.74f, area.getHeight() * 0.74f));
    });
    // Play is struck a shade larger than its neighbours, which is the only
    // hierarchy the transport has: it is the button people reach for.
    play.setGlyphInset(0.24f);
    play.setActiveColour(palette::text);
    play.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::fillFitted(g, icons::play(), area.withSizeKeepingCentre(area.getWidth() * 0.86f, area.getHeight() * 0.86f));
    });
    for (auto* arrow : {&undo, &redo})
    {
        // No wash and no surface under these two: the glyph alone, brighter
        // under the pointer and dimmer when there is nothing on the stack.
        arrow->setWashesOnHover(false);
        arrow->setGlyphInset(0.16f);
    }
    undo.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::drawCurvedArrow(g, area, true, 1.5f);
    });
    redo.setPainter([](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour(colour);
        icons::drawCurvedArrow(g, area, false, 1.5f);
    });
    for (auto* component : std::initializer_list<juce::Component*>{
             &infoView, &position, &play, &stop, &record, &rewind,
             &browser, &browserToggle, &editorToggle, &rackToggle, &grid, &audioClip, &arrangement, &djView,
             &djToggle, &arrangementToggle, &rack, &tempoBox, &signatureField, &undo, &redo, &metronome, &metronomeMenu,
             &patternLabel, &editorResolution, &editorZoomOut, &editorZoomIn, &scaleHighlight,
             &lowerSplitter})
        addAndMakeVisible(component);
    // Last, so it is above everything it covers.
    addChildComponent(loadingScreen);
    lowerSplitter.dragStarted = [this] (int screenY)
    {
        resizeStartY = screenY;
        resizeStartLowerPaneHeight = lowerPaneHeight;
        // A height someone chose is theirs: the note editor's opening raise
        // only ever replaces the shell's own default.
        paneHeightDragged = true;
    };
    lowerSplitter.dragged = [this] (int screenY)
    {
        // Dragging the handle up grows the pane, which is the direction the
        // pane's own top edge moves. resized() does the clamping, so the
        // value carried here is free to run past the limits and come back.
        lowerPaneHeight = resizeStartLowerPaneHeight - (screenY - resizeStartY);
        resized();
        repaint();
    };
    // The transport's state and the undo history reach the shell through the
    // model's relays, whichever edit is open, so a new document needs no
    // re-subscription here.
    session.transportState.addChangeListener(this);
    session.undoHistory.addChangeListener(this);
    session.addChangeListener(this);
    session.listeners.add(this);
    patternLabel.setText("PATTERN 1  /  NOTE EDITOR", juce::dontSendNotification);
    patternLabel.setColour(juce::Label::textColourId, palette::textDim);
    setSize(1280, 900);
    changeListenerCallback(nullptr);
    // Records the selection the pane state belongs to, so the first real
    // selection change is recognised as one.
    refreshEditorPanes();
    applyPaneLayout();
    // This updates a text readout only. Pointer events and control painting
    // are not throttled to this timer; there is no full-window repaint loop.
    startTimerHz(30);
    // Once by hand, so the readout is right in the first frame rather than
    // blank until the timer comes round.
    timerCallback();
}

MainComponent::~MainComponent()
{
    stopTimer();
    delete preferences.getComponent();
    session.transportState.removeChangeListener(this);
    session.undoHistory.removeChangeListener(this);
    session.removeChangeListener(this);
    session.listeners.remove(this);
}

void MainComponent::editWillChange() {}

void MainComponent::editDidChange()
{
    changeListenerCallback(nullptr);
}

void MainComponent::logStatus(const juce::String& message)
{
    if (message.isEmpty()) return;
    infoView.setText(message, false);
    juce::Logger::writeToLog("Rhino: " + message);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const auto playing = session.isPlaying();
    // The glyph holds still and the brightness moves: a triangle that turns
    // into two bars asks the eye to re-read the button every time the
    // transport starts, and at fourteen pixels the two shapes are too alike
    // to tell apart at a glance in any case.
    play.setToggleState(playing, juce::dontSendNotification);
    play.setTooltip(playing ? "Pause the transport." : "Play - start the transport from the playhead.");
    updateRecordButton();
    tempoBox.setValue(session.tempo());
    const auto signature = session.timeSignature();
    signatureField.setSignature(signature.numerator, signature.denominator);
    metronome.setToggleState(session.clickTrackEnabled(), juce::dontSendNotification);
    undo.setEnabled(session.canUndo());
    redo.setEnabled(session.canRedo());
    updateEditorLabel();
    scaleHighlight.setVisible(lowerPane == LowerPane::notes && !session.isPatternDrums());
    // Dropping an instrument turns a bare track into a MIDI one without
    // moving the selection, and deleting or undoing a clip can take the one
    // the audio editor is showing. Both change which pane belongs here.
    refreshEditorPanes();
    {
        const juce::ScopedValueSetter<bool> scope(updatingEditorResolution, true);
        editorResolution.setSelectedId(session.editorStepResolution(), juce::dontSendNotification);
    }
    const auto name = session.projectFile == juce::File{} ? juce::String("Untitled") : session.projectFile.getFileNameWithoutExtension();
    const auto displayName = name + (session.hasUnsavedChanges() ? " *" : "");
    if (projectTitleChanged) projectTitleChanged(displayName);
}

}
