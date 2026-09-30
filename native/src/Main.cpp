#include "Session.h"
#include "StepGrid.h"
#include "ProjectFiles.h"
#include "Theme.h"
#include "Arrangement.h"
#include "SessionView.h"
#include "BrowserPanel.h"
#include "DeviceRack.h"
#include "AudioClipPanel.h"
#include "Playhead.h"
#include "ComputerKeyboard.h"
#include "StartupScreen.h"
#include "TransportDisplay.h"
#include "ControlBarFields.h"
#include "SystemUsage.h"
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shlobj.h>
#endif

namespace rhino
{
juce::File rhinoLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino").getChildFile("rhino.log");
}

void avoidLegacyDirectSound(te::Engine& engine)
{
   #if JUCE_WINDOWS
    auto& manager = engine.getDeviceManager().deviceManager;
    const auto currentType = manager.getCurrentAudioDeviceType();
    if (currentType.isNotEmpty() && currentType != "DirectSound")
        return;

    for (auto* type : manager.getAvailableDeviceTypes())
        if (type != nullptr && type->getTypeName() == "Windows Audio")
        {
            juce::Logger::writeToLog("Rhino: using Windows Audio instead of legacy DirectSound");
            manager.setCurrentAudioDeviceType("Windows Audio", true);
            return;
        }
   #else
    juce::ignoreUnused(engine);
   #endif
}

void prepareCommandLineAudio()
{
   #if JUCE_WINDOWS
    te::Engine testEngine {"Rhino Native Tests"};
    avoidLegacyDirectSound(testEngine);
    testEngine.getDeviceManager().deviceManager.closeAudioDevice();
   #endif
}

// Renders a component to a PNG without a desktop peer, so a panel can be
// looked at without bringing the app to the front and taking the screen away
// from whoever is using the machine.
//
// The file is deleted first, and that is not a tidiness measure: JUCE's
// File::createOutputStream opens an existing file for *append*, so writing a
// second PNG to the same path leaves the first one intact at the front and
// every reader sees the original image. That cost an hour of chasing a text
// run that was being drawn correctly the whole time.
void writeSnapshot(juce::Component& component, const juce::File& destination)
{
    destination.deleteFile();
    if (auto stream = destination.createOutputStream())
        juce::PNGImageFormat().writeImageToStream(
            component.createComponentSnapshot(component.getLocalBounds()), *stream);
}

void registerRhinoProjectFileAssociation()
{
   #if JUCE_WINDOWS
    constexpr auto registryRoot = "HKEY_CURRENT_USER\\Software\\Classes\\";
    constexpr auto projectType = "Rhino.Project";
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().quoted();
    const auto typeKey = juce::String(registryRoot) + projectType;
    const auto extensionKey = juce::String(registryRoot) + ".rhinoedit\\";
    const auto associated = juce::WindowsRegistry::setValue(extensionKey, projectType)
                         && juce::WindowsRegistry::setValue(typeKey + "\\", "Rhino project")
                         && juce::WindowsRegistry::setValue(typeKey + "\\DefaultIcon\\", executable + ",0")
                         && juce::WindowsRegistry::setValue(typeKey + "\\shell\\open\\command\\", executable + " \"%1\"");
    if (associated)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    else
        juce::Logger::writeToLog("Rhino: could not register .rhinoedit file association");
   #endif
}

class BrowserToggleButton final : public juce::TextButton
{
public:
    BrowserToggleButton() : juce::TextButton("Browser") {}

    void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
    {
        if (highlighted || pressed)
            g.fillAll(juce::Colour(0x182f3942));

        const auto colour = getToggleState() ? playheadColour : juce::Colour(0xffc7cdd2);
        const auto icon = getLocalBounds().withSizeKeepingCentre(18, 14);
        g.setColour(colour);
        g.fillRect(icon.getX(), icon.getY(), 4, icon.getHeight());
        g.fillRect(icon.getX() + 7, icon.getY(), 10, icon.getHeight());
    }
};

// Stop parks the playhead on the line the arrangement is working from, which
// is what makes play-stop-play repeat a passage. Double-clicking it means the
// top of the song instead. The second click is told apart here, where the
// system's own click counting is already available, rather than by timing
// clicks in the handler.
class StopButton final : public juce::TextButton
{
public:
    StopButton() : juce::TextButton("Stop") {}

    std::function<void()> onStop, onReturnToStart;

    void mouseDown(const juce::MouseEvent& event) override
    {
        secondClick = event.getNumberOfClicks() > 1;
        juce::TextButton::mouseDown(event);
    }

    void clicked() override
    {
        // Cleared as it is read: a click arriving from the keyboard or from
        // triggerClick has no mouse event behind it and must not inherit the
        // last one's count.
        if (secondClick)
        {
            secondClick = false;
            if (onReturnToStart) onReturnToStart();
            return;
        }
        if (onStop) onStop();
    }

private:
    bool secondClick = false;
};

// The record button paints its own dot rather than borrowing a glyph. It has
// three things to say - nothing is armed, something is armed, and the transport
// is rolling or counting into it - and a colour on a circle says all three at a
// glance where a character would need a legend.
class RecordButton final : public juce::TextButton
{
public:
    enum class State { idle, armed, countingIn, recording };

    RecordButton() : juce::TextButton("Record") {}

    void setState(State next)
    {
        if (state == next) return;
        state = next;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff343a40));
        g.fillRoundedRectangle(bounds, 3.0f);
        if (highlighted || pressed)
        {
            g.setColour(juce::Colour(0x1affffff));
            g.fillRoundedRectangle(bounds, 3.0f);
        }
        const auto dot = bounds.withSizeKeepingCentre(12.0f, 12.0f);
        const auto colour = state == State::recording  ? juce::Colour(0xffe4443a)
                          : state == State::countingIn ? juce::Colour(0xffe0a03c)
                          : state == State::armed      ? juce::Colour(0xffbb5349)
                                                       : juce::Colour(0xff70797f);
        g.setColour(colour);
        g.fillEllipse(dot);
        // A ring while it is actually capturing, so a rolling recording cannot
        // be mistaken for a track merely sitting armed.
        if (state == State::recording || state == State::countingIn)
        {
            g.setColour(colour.withAlpha(0.45f));
            g.drawEllipse(dot.expanded(3.0f), 1.6f);
        }
    }

private:
    State state = State::idle;
};

// Wall-clock readings for the control bar. Minutes are padded to two digits
// rather than widened past an hour: an arrangement that long would push the
// load meters out of the box, and the bar number above is the reading that
// matters at that length anyway.
static juce::String formatClock(double seconds, bool withMilliseconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto totalMs = static_cast<juce::int64>(seconds * 1000.0 + 0.5);
    const auto milliseconds = static_cast<int>(totalMs % 1000);
    const auto totalSeconds = totalMs / 1000;
    auto text = juce::String(static_cast<int>(totalSeconds / 60)).paddedLeft('0', 2)
              + ":" + juce::String(static_cast<int>(totalSeconds % 60)).paddedLeft('0', 2);
    if (withMilliseconds) text += ":" + juce::String(milliseconds).paddedLeft('0', 3);
    return text;
}

static juce::String formatMemory(juce::uint64 bytes)
{
    const auto megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes >= 1024.0) return juce::String(megabytes / 1024.0, 2) + " GB";
    return juce::String(juce::roundToInt(megabytes)) + " MB";
}

// Session view development is paused; see SESSION-VIEW.md for what exists, what
// is missing, and how to pick it up. The view and its model are still built and
// tested, but nothing in the shell reaches them. Setting this to true restores
// the control-bar switch and the Tab shortcut.
static constexpr bool sessionViewEnabled = false;

// The clip and device panes float over the foot of the arrangement, so the
// split between them has to be a component of its own: the arrangement is
// underneath it and would take every press meant for the handle. Positions are
// read in screen coordinates because the bar moves with the drag, and a
// position relative to a component that is being moved chases itself.
class SplitterBar final : public juce::Component
{
public:
    SplitterBar() { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff3a434b));
        g.fillRect(getLocalBounds().withSizeKeepingCentre(getWidth(), 4));
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (dragStarted) dragStarted(event.getScreenPosition().y);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragged) dragged(event.getScreenPosition().y);
    }
    std::function<void(int)> dragStarted, dragged;
};

class ControlWindow final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private Session::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit ControlWindow(Session& s) : session(s), browser(s), grid(s), arrangement(s), sessionView(s), rack(s), audioClip(s), files(s)
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
            if (!sessionViewOpen) rack.selectTrack(track);
            refreshEditorPanes();
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
        sessionView.status = files.status;
        sessionView.trackSelected = [this](int track) { if (sessionViewOpen) rack.selectTrack(track); };
        sessionToggle.setButtonText("Session");
        arrangementToggle.setButtonText("Arrange");
        sessionToggle.setTooltip("Show the Session view clip launcher");
        arrangementToggle.setTooltip("Show the Arrangement timeline");
        sessionToggle.onClick = [this] { setSessionViewOpen(true); };
        arrangementToggle.onClick = [this] { setSessionViewOpen(false); };
        // Launched clips override a track's timeline clips. This hands those
        // tracks back to the arrangement, as Live's Back to Arrangement does.
        backToArrangement.setButtonText(juce::String(L"\u21ba") + " Arrangement");
        backToArrangement.setTooltip("Stop launched clips and play the arrangement again");
        backToArrangement.onClick = [this]
        {
            session.returnToArrangement();
            logStatus("Tracks returned to the arrangement");
        };
        browser.status = files.status;
        // A browser double-click has no drop target of its own, so it follows
        // whichever track the visible arrangement has selected.
        rack.status = files.status;
        browserToggle.onClick = [this] { toggleBrowser(); };
        editorToggle.onClick = [this] { toggleClipEditor(); };
        rackToggle.onClick = [this] { toggleDeviceView(); };
        browserToggle.setTooltip("Show or hide browser");
        editorToggle.setButtonText("Clip");
        rackToggle.setButtonText("Devices");
        editorToggle.setTooltip("Show or hide the Clip / MIDI Editor");
        rackToggle.setTooltip("Show or hide Device View");
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
        editorZoomOut.setTooltip("Zoom out of note editor");
        editorZoomIn.setTooltip("Zoom in to note editor");
        editorZoomOut.onClick = [this] { grid.zoomOut(); };
        editorZoomIn.onClick = [this] { grid.zoomIn(); };
        scaleHighlight.addItem("Scale: off", 1);
        static constexpr const char* roots[] {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int root = 0; root < 12; ++root)
            scaleHighlight.addItem(juce::String(roots[root]) + " Major", root + 2);
        for (int root = 0; root < 12; ++root)
            scaleHighlight.addItem(juce::String(roots[root]) + " Minor", root + 14);
        scaleHighlight.setSelectedId(1, juce::dontSendNotification);
        scaleHighlight.setTooltip("Highlight notes in a scale");
        scaleHighlight.onChange = [this] { grid.setScaleHighlight(scaleHighlight.getSelectedId()); };
        for (auto* toggle : std::initializer_list<juce::TextButton*>{&browserToggle, &editorToggle, &rackToggle,
                                                                     &sessionToggle, &arrangementToggle,
                                                                     &backToArrangement})
        {
            toggle->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b31));
            toggle->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff38505b));
            toggle->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaeb8c1));
            toggle->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffdce5ea));
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
        infoView.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1d2228));
        infoView.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        infoView.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc9d1c3));
        infoView.setFont(uiFont(10.5f));
        hint.setText({}, juce::dontSendNotification);
        hint.setVisible(false);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff8d98a3));
        // Tempo is a field you drag, which is how every DAW sets one: press
        // and move the mouse up or down, Ctrl for hundredths, double-click to
        // type an exact number. The range and the steps are the model's, so the
        // field cannot offer a tempo setTempo would then refuse.
        tempoBox.setRange(Session::minimumTempo, Session::maximumTempo, 1.0, 0.01);
        tempoBox.setDecimalPlaces(2);
        tempoBox.setSuffix("BPM");
        tempoBox.setFontSize(17.0f, 8.0f);
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
        clear.onClick = [this] { session.clearPattern(); };
        undo.setButtonText(L"\u21b6");
        redo.setButtonText(L"\u21b7");
        clear.setButtonText(L"\u00d7");
        undo.setTooltip("Undo");
        redo.setTooltip("Redo");
        clear.setTooltip("Clear pattern");
        metronome.setButtonText(L"\u266b");
        metronomeMenu.setButtonText("v");
        metronome.setTooltip("Toggle metronome");
        metronomeMenu.setTooltip("Metronome settings");
        metronome.setClickingTogglesState(true);
        metronome.onClick = [this] { session.setClickTrackEnabled(metronome.getToggleState()); };
        metronomeMenu.onClick = [this] { showMetronomeMenu(); };
        play.onClick = [this] { session.togglePlayback(); };
        stop.onStop = [this] { session.stop(); };
        stop.onReturnToStart = [this] { session.returnToStart(); };
        record.onClick = [this] { toggleRecording(); };
        record.setTooltip("Record into the armed tracks  (F9)");
        panic.onClick = [this]
        {
            session.panicReset();
            logStatus("Panic reset: stopped transport, reset plugins, restarted audio device");
        };
        play.setButtonText(L"\u25b6");
        stop.setButtonText(L"\u25a0");
        panic.setButtonText("!");
        play.setTooltip("Play or pause");
        stop.setTooltip("Stop and return to the selected line  (double-click for the start of the song)");
        panic.setTooltip("Panic reset audio");
        for (auto* component : std::initializer_list<juce::Component*>{
                 &infoView, &position, &play, &stop, &record, &panic,
                 &browser, &browserToggle, &editorToggle, &rackToggle, &grid, &audioClip, &arrangement, &sessionView,
                 &sessionToggle, &arrangementToggle, &backToArrangement, &rack, &tempoBox, &signatureField, &undo, &redo, &clear, &metronome, &metronomeMenu, &hint,
                 &patternLabel, &editorResolution, &editorZoomOut, &editorZoomIn, &scaleHighlight, &lowerSplitter})
            addAndMakeVisible(component);
        lowerSplitter.dragStarted = [this] (int screenY)
        {
            resizeStartY = screenY;
            resizeStartLowerPaneHeight = lowerPaneHeight;
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
        session.edit->getTransport().addChangeListener(this);
        session.addChangeListener(this);
        session.listeners.add(this);
        session.edit->getUndoManager().addChangeListener(this);
        patternLabel.setText("PATTERN 1  /  NOTE EDITOR", juce::dontSendNotification);
        patternLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4aa));
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

    ~ControlWindow() override
    {
        stopTimer();
        delete audioSettings.getComponent();
        session.edit->getTransport().removeChangeListener(this);
        session.removeChangeListener(this);
        session.listeners.remove(this);
        session.edit->getUndoManager().removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff171a1e));
        paintControlBar(g);
        if (browserOpen)
        {
            g.setColour(juce::Colour(0xff3a434b));
            g.fillRect(browserWidth, browserTop, 4, getHeight() - browserTop);
        }
        if (infoVisible)
        {
            const auto area = infoViewArea();
            g.setColour(juce::Colour(0xff1d2228));
            g.fillRect(area);
            g.setColour(juce::Colour(0xff3a434b));
            g.drawRect(area);
            g.setColour(juce::Colour(0xffb8c4aa));
            g.setFont(uiFont(9.0f));
            drawSnappedText(g, "INFO VIEW   ?  HIDE", area.withTrimmedLeft(10).withHeight(24),
                            juce::Justification::centredLeft);
        }
    }

    // The captions and the rules between the modules. Drawn from what the
    // layout recorded rather than from a second copy of the geometry, so a
    // control that moves takes its caption with it.
    void paintControlBar(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff1b2026));
        g.fillRect(0, 0, getWidth(), browserTop - 4);
        g.setColour(juce::Colour(0xff2b333a));
        for (const auto divider : barDividers)
            g.fillRect(divider, barCaptionTop, 1, barControlTop + barControlHeight - barCaptionTop);
        g.setFont(uiFontBold(8.0f));
        g.setColour(juce::Colour(0xff707d88));
        for (const auto& group : barGroups)
            drawSnappedText(g, group.caption, group.bounds, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff272f36));
        g.fillRect(0, browserTop - 4, getWidth(), 1);
    }

    // The control bar is a row of modules, each a caption over the controls it
    // names, ruled off from its neighbours. Everything is placed left to right
    // from one running x, the edit group is pinned to the right edge, and the
    // readout takes whatever lies between - so the bar reflows as the window
    // changes width instead of the clusters sliding over each other, which is
    // what a layout hung off a centred readout used to do.
    void layoutControlBar()
    {
        barGroups.clear();
        barDividers.clear();
        // A module: its caption goes on the caption row and its controls come
        // back as a rectangle on the control row.
        const auto module = [this](const char* caption, int x, int width)
        {
            barGroups.push_back({juce::String(caption), {x, barCaptionTop, width, barCaptionHeight}});
            return juce::Rectangle<int>(x, barControlTop, width, barControlHeight);
        };
        const auto rule = [this](int x) { barDividers.push_back(x); };
        const auto row = [](juce::Rectangle<int> area, std::initializer_list<juce::Component*> buttons, int gap)
        {
            const auto count = static_cast<int>(buttons.size());
            const auto width = std::max(1, (area.getWidth() - gap * (count - 1)) / count);
            for (auto* button : buttons)
            {
                button->setBounds(area.removeFromLeft(width));
                area.removeFromLeft(gap);
            }
        };

        browserToggle.setBounds(6, barControlTop, 38, barControlHeight);
        auto x = 58;
        rule(x - barGroupGap / 2);

        constexpr int tempoWidth = 106, signatureWidth = 86, clickWidth = 54;
        constexpr int transportWidth = 186, editWidth = 122, viewWidth = 112;
        tempoBox.setBounds(module("TEMPO", x, tempoWidth));
        x += tempoWidth + barGroupGap;
        signatureField.setBounds(module("SIGNATURE", x, signatureWidth));
        x += signatureWidth + barGroupGap;
        {
            auto area = module("CLICK", x, clickWidth);
            metronome.setBounds(area.removeFromLeft(34));
            metronomeMenu.setBounds(area.removeFromLeft(18));
        }
        x += clickWidth + barGroupGap;

        rule(x - barGroupGap / 2);
        row(module("TRANSPORT", x, transportWidth), {&play, &stop, &record, &panic}, 6);
        x += transportWidth + barGroupGap;
        rule(x - barGroupGap / 2);

        // Pinned to the right edge and placed before the readout, because the
        // readout is the elastic one: it is given whatever is left over.
        auto rightEdge = getWidth() - 14;
        row({rightEdge - editWidth, barControlTop, editWidth, barControlHeight}, {&undo, &redo, &clear}, 6);
        module("EDIT", rightEdge - editWidth, editWidth);
        rightEdge -= editWidth + barGroupGap;
        sessionToggle.setVisible(sessionViewEnabled);
        arrangementToggle.setVisible(sessionViewEnabled);
        if constexpr (sessionViewEnabled)
        {
            row({rightEdge - viewWidth, barControlTop, viewWidth, barControlHeight},
                {&sessionToggle, &arrangementToggle}, 4);
            module("VIEW", rightEdge - viewWidth, viewWidth);
            sessionToggle.setToggleState(sessionViewOpen, juce::dontSendNotification);
            arrangementToggle.setToggleState(!sessionViewOpen, juce::dontSendNotification);
            backToArrangement.setBounds(rightEdge - viewWidth, barControlTop + barControlHeight + 2, viewWidth, 0);
            rightEdge -= viewWidth + barGroupGap;
        }
        rule(rightEdge + barGroupGap / 2);

        // The readout is the one control here that gains from being wider, so
        // it gets the whole of the space left between the two clusters. Below
        // the width its two lines need it is dropped rather than squeezed: a
        // clock with half its digits missing is worse than no clock.
        const auto readoutWidth = rightEdge - x;
        position.setVisible(readoutWidth >= 220);
        if (position.isVisible())
            position.setBounds(module("POSITION", x, readoutWidth));
    }

    void resized() override
    {
        constexpr int gap = 18;
        const auto leftWidth = browserOpen ? browserWidth : 0;
        const auto editorX = leftWidth + gap;
        const auto editorW = getWidth() - editorX - 24;
        // The transport is a full-width bar. Both the browser and arrangement
        // begin below it, so their top edges remain aligned.
        const auto arrangementTop = browserTop;
        // The clip and device panes float over the foot of the arrangement
        // rather than pushing it up. The arrangement therefore always has the
        // whole window and keeps the lane heights it was laid out with, and
        // dragging the split moves only the pane: the clips it slides over do
        // not grow and shrink under the pointer, which is what pushing the
        // panel up used to do to every one of them.
        const auto arrangementH = std::max(150, getHeight() - arrangementTop);
        // Nothing selected means nothing to edit, so the pane takes no room at
        // all and only the toggle strip is reserved. That strip stays where it
        // is, which is what makes the pane reachable again.
        auto paneH = 0;
        if (lowerPaneVisible())
        {
            // A fifth of the window the first time the pane is opened: enough
            // for a clip editor without the timeline giving up its half of the
            // screen to it, which a pane sized from the foot of the window was
            // doing on every machine with a tall display.
            if (lowerPaneHeight <= 0)
                lowerPaneHeight = getHeight() / 5;
            lowerPaneHeight = juce::jlimit(minimumPaneHeight,
                                           std::max(minimumPaneHeight, getHeight() - arrangementTop - 150 - toggleStripHeight),
                                           lowerPaneHeight);
            paneH = lowerPaneHeight;
        }
        const auto lowerH = paneH;
        const auto lowerTop = getHeight() - 12 - lowerH;
        // Where the arrangement is covered from: the toggle strip sits in the
        // band between the lanes and the pane, exactly as it did when the two
        // were stacked rather than layered.
        const auto arrangementBottom = lowerTop - 34;
        layoutControlBar();
        browser.setVisible(browserOpen);
        const auto infoArea = infoViewArea();
        // The Info View lives at the foot of the browser column, so it goes
        // with it. Only the toggle stays behind.
        infoView.setVisible(infoVisible && browserOpen);
        infoView.setBounds(infoArea.withTrimmedTop(25).reduced(8, 5));
        browser.setBounds(0, browserTop, browserWidth,
                          (infoVisible ? infoArea.getY() : getHeight()) - browserTop);
        browserToggle.setToggleState(browserOpen, juce::dontSendNotification);
        arrangement.setVisible(!sessionViewOpen);
        sessionView.setVisible(sessionViewOpen);
        arrangement.setBounds(editorX, arrangementTop, editorW, arrangementH);
        // The arrangement spans the window, so it is told how much of its own
        // foot the pane covers: its main row and its scrollbars ride up to sit
        // above the pane while the lanes behind it stay where they are.
        arrangement.setBottomInset(static_cast<float>(std::max(0, arrangementTop + arrangementH - arrangementBottom)));
        // The session view has no such inset, so it simply stops at the strip.
        sessionView.setBounds(editorX, arrangementTop, editorW,
                              std::max(150, arrangementBottom - arrangementTop));
        const auto notes = lowerPane == LowerPane::notes;
        const auto audio = lowerPane == LowerPane::audio;
        const auto devices = lowerPane == LowerPane::devices;
        editorToggle.setToggleState(notes || audio, juce::dontSendNotification);
        editorToggle.setButtonText(audio ? "Audio" : "Clip");
        rackToggle.setToggleState(devices, juce::dontSendNotification);
        editorToggle.setBounds(editorX, arrangementBottom + 10, 48, 22);
        rackToggle.setBounds(editorX + 54, arrangementBottom + 10, 72, 22);
        patternLabel.setVisible(notes || audio);
        patternLabel.setBounds(editorX + 136, arrangementBottom + 10, std::max(80, editorW - 500), 24);
        // The scale, zoom and resolution controls belong to the note editor and
        // mean nothing over a waveform, so they follow it rather than the pane.
        scaleHighlight.setVisible(notes && !session.isPatternDrums());
        if (notes && !session.isPatternDrums())
            scaleHighlight.setBounds(editorX + std::max(260, editorW - 328), arrangementBottom + 12, 146, 20);
        editorZoomOut.setVisible(notes);
        editorZoomIn.setVisible(notes);
        editorResolution.setVisible(notes);
        editorZoomOut.setBounds(editorX + std::max(414, editorW - 174), arrangementBottom + 12, 25, 20);
        editorZoomIn.setBounds(editorX + std::max(443, editorW - 145), arrangementBottom + 12, 25, 20);
        editorResolution.setBounds(editorX + std::max(510, editorW - 78), arrangementBottom + 12, 70, 20);

        grid.setVisible(notes);
        audioClip.setVisible(audio);
        rack.setVisible(devices);
        // The note editor, the audio editor and the Device View are three faces
        // of one pane: they share its rectangle and exactly one of them is ever
        // visible in it, so the pane is as tall as the thing being worked on.
        const juce::Rectangle<int> paneBounds {editorX, lowerTop, editorW, lowerH};
        grid.setBounds(paneBounds);
        audioClip.setBounds(paneBounds);
        rack.setBounds(paneBounds);
        // Everything in the lower pane is layered over the arrangement, which
        // is a sibling that covers the same ground.
        lowerSplitter.setVisible(lowerPaneVisible());
        lowerSplitter.setBounds(editorX, arrangementBottom - 2, editorW, 8);
        for (auto* component : std::initializer_list<juce::Component*>{&grid, &audioClip, &rack, &lowerSplitter})
            component->toFront(false);
        browserToggle.toFront(false);
        editorToggle.toFront(false);
        rackToggle.toFront(false);
        hint.setBounds(0, 0, 0, 0);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        setMouseCursor(isOverSplitter(event.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                      : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        resizingBrowser = browserOpen && std::abs(event.x - browserWidth) <= 5 && event.y >= browserTop;
        resizeStartX = event.x;
        resizeStartY = event.y;
        resizeStartBrowserWidth = browserWidth;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (resizingBrowser)
        {
            browserWidth = juce::jlimit(180, 360, resizeStartBrowserWidth + event.x - resizeStartX);
            resized();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        resizingBrowser = false;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getTextCharacter() == '?')
        {
            toggleInfoView();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::F12Key)
        {
            if (fullScreenToggleRequested) fullScreenToggleRequested();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
        {
            files.save(key.getModifiers().isShiftDown());
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
        {
            files.open();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'N')
        {
            files.newProject();
            return true;
        }
        // Adding a track is a document command rather than an arrangement one,
        // so it answers wherever the focus is, the way New and Open do. The
        // arrangement's + button is where the kind is chosen; the shortcut
        // repeats that choice instead of asking again, and starts on MIDI.
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'T')
        {
            const auto result = session.addTrack(Session::lastAddedTrackType());
            logStatus(result.failed() ? result.getErrorMessage()
                                      : "Added " + session.trackName(session.trackCount() - 1));
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'E')
        {
            files.exportWav();
            return true;
        }
        // Splitting is a clip command, so the arrangement owns it - but the
        // keyboard is usually somewhere else by the time it is wanted: on a
        // knob in the audio editor, or in the browser. This runs after the
        // focused component has had its say, so the note editor's own Ctrl+E
        // still subdivides notes and never reaches here.
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'E')
        {
            arrangement.splitAtInsertPoint();
            return true;
        }
        // Merge reaches the shell for the same reason: the clips being folded
        // together are usually the ones the audio editor is open on, and it
        // still means the same thing wherever the keyboard happens to be.
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'J')
        {
            arrangement.mergeSelected();
            return true;
        }
        // Reverse reaches the shell for the same reason, and more often: the
        // clip you want to hear backwards is usually the one already open in
        // the audio editor, with the keyboard on one of its knobs. A plain
        // letter is safe here because this runs last - a text field, the
        // browser search or a rename editor has already swallowed it, and the
        // typing keyboard's note keys do not include R.
        if (!key.getModifiers().isAnyModifierKeyDown() && key.getKeyCode() == 'R')
        {
            arrangement.reverseSelected();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'F')
        {
            browser.focusSearch();
            return true;
        }
        if constexpr (sessionViewEnabled)
            if (key.getKeyCode() == juce::KeyPress::tabKey && !key.getModifiers().isAnyModifierKeyDown())
            {
                setSessionViewOpen(!sessionViewOpen);
                return true;
            }
        if (key.getKeyCode() == juce::KeyPress::spaceKey)
        {
            session.togglePlayback();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::F9Key)
        {
            toggleRecording();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
        {
            if (key.getModifiers().isShiftDown()) session.redo();
            else session.undo();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
        {
            session.redo();
            return true;
        }
        return false;
    }

    // The two views show one project, but each keeps its own selection and
    // focus, as Live does. Switching views therefore hands the Device View
    // over to whatever the view being shown already had selected.
    void setSessionViewOpen(bool open)
    {
        if constexpr (sessionViewEnabled)
        {
            if (sessionViewOpen == open) return;
            sessionViewOpen = open;
            rack.selectTrack(open ? sessionView.selectedTrackIndex() : arrangement.selectedTrackIndex());
            logStatus(open ? "Session view: click a clip to launch it"
                           : "Arrangement view");
            resized();
            repaint();
        }
        else
        {
            juce::ignoreUnused(open);
        }
    }

    void toggleBrowser()
    {
        browserOpen = !browserOpen;
        resized();
        repaint();
    }

    // The clip pane belongs to whatever clip is selected. The toggle closes it,
    // or re-opens the editor the current selection calls for - there is no
    // longer an "empty pane" to protect against, because an empty one is given
    // back to the arrangement.
    void toggleClipEditor()
    {
        if (clipPaneOpen())
            lowerPane = LowerPane::none;
        else if (const auto id = selectedAudioClipID(); id != te::EditItemID())
        {
            audioClip.setClip(id);
            lowerPane = LowerPane::audio;
        }
        else if (isMidiClipSelected())
            lowerPane = LowerPane::notes;
        else
        {
            logStatus("Select a clip first: a MIDI clip opens the note editor, an audio clip the audio editor");
            return;
        }
        rememberPaneSelection();
        updateEditorLabel();
        applyPaneLayout();
    }

    // The other half of the pair. Pressing Devices while a clip editor is open
    // swaps to the devices rather than stacking them, which is the whole point
    // of the two being one pane.
    void toggleDeviceView()
    {
        lowerPane = lowerPane == LowerPane::devices ? LowerPane::none : LowerPane::devices;
        updateEditorLabel();
        applyPaneLayout();
    }

    // Clicking a track card asks for its devices. Every track has a chain -
    // an audio track's effects and the main row's are as much a chain as an
    // instrument track's - so this refuses nothing and consults no clip. The
    // card click arrives from a mouse *down* that may be starting a card drag,
    // so the layout waits for the button, as the clip panes do.
    // Clicking a card still asks for that track's devices, but only when the
    // pane is free. A clip editor that is open is being worked in, and having
    // it swapped out by a click on the card beside it would be a surprise; the
    // Devices toggle is the way across while one is open.
    void showDeviceView()
    {
        if (lowerPane != LowerPane::none) return;
        lowerPane = LowerPane::devices;
        updateEditorLabel();
        requestPaneLayout();
    }

    // The clip a command would act on, when that clip is an audio clip.
    te::EditItemID selectedAudioClipID() const
    {
        const auto id = arrangement.selectedClipID();
        return session.findAudioClip(id) != nullptr ? id : te::EditItemID();
    }

    // What the note editor is for, and the whole of it: a selected MIDI or drum
    // clip. A track that runs an instrument no longer counts, however likely it
    // is to hold one - it may have no clip at all, and the notes of a clip
    // nobody selected are not what the track click was asking to see.
    bool isMidiClipSelected() const
    {
        const auto id = arrangement.selectedClipID();
        return session.findClip(id) != nullptr && session.findAudioClip(id) == nullptr;
    }

    void rememberPaneSelection()
    {
        paneClip = arrangement.selectedClipID();
        paneMidi = isMidiClipSelected();
    }

    // Double-clicking a clip opens it: audio in the audio editor, MIDI in the
    // note editor. This is the only thing that reveals either of them, which is
    // why a single click on a clip - on a waveform or on a bar of notes - still
    // only selects it. Whether the Device View is showing is not this
    // function's business either way.
    void openClip(te::EditItemID id)
    {
        if (session.findAudioClip(id) != nullptr)
        {
            audioClip.setClip(id);
            lowerPane = LowerPane::audio;
            logStatus("Audio clip " + audioClip.clipName().quoted()
                      + ": gain, pan, pitch and fades here apply to this clip only");
        }
        else if (session.findClip(id) != nullptr)
            lowerPane = LowerPane::notes;
        else
            return;
        rememberPaneSelection();
        updateEditorLabel();
        requestPaneLayout();
    }

    // The clip selection moves the clip pane, but only when it actually moved:
    // a toggle the user pressed while standing on one clip has to survive the
    // next notification about that same clip. The Device View is not touched
    // here at all - selecting a clip says nothing about whether its track's
    // devices are wanted, and clicking a card is what answers that.
    void refreshEditorPanes()
    {
        const auto clipID = arrangement.selectedClipID();
        const auto midi = isMidiClipSelected();
        if (clipID == paneClip && midi == paneMidi)
        {
            // The clip the audio editor was showing can still be deleted, or
            // taken away by an undo, without the selection moving at all.
            if (lowerPane == LowerPane::audio && !audioClip.hasClip())
            {
                lowerPane = LowerPane::none;
                requestPaneLayout();
            }
            return;
        }
        paneClip = clipID;
        paneMidi = midi;
        // Selecting a clip never reveals the pane: opening one is a
        // double-click, for a MIDI clip exactly as for an audio clip, so a
        // single click is only ever a selection. What an open pane does is
        // follow that selection the way Live's clip view does - onto the next
        // clip, whichever editor that clip calls for - and close when what was
        // selected is not a clip at all.
        if (clipPaneOpen())
        {
            if (const auto audio = selectedAudioClipID(); audio != te::EditItemID())
            {
                audioClip.setClip(audio);
                lowerPane = LowerPane::audio;
            }
            else
                lowerPane = midi ? LowerPane::notes : LowerPane::none;
        }
        updateEditorLabel();
        requestPaneLayout();
    }

    // Opening or closing the pane changes the arrangement's height, and the
    // selection that asks for it arrives from a mouse *down* - the same press
    // that may be starting a clip drag. Resizing the lanes underneath that
    // gesture would move the clip out from under the pointer, so the layout
    // waits until the button comes up; the 30 Hz timer applies it.
    void requestPaneLayout() { paneLayoutPending = true; }

    void applyPaneLayout()
    {
        paneLayoutPending = false;
        resized();
        repaint();
    }

    void updateEditorLabel()
    {
        patternLabel.setText(lowerPane == LowerPane::audio
                                 ? "AUDIO CLIP  /  " + audioClip.clipName().toUpperCase()
                             : lowerPane == LowerPane::devices
                                 ? juce::String("DEVICES")
                             : session.isPatternDrums() ? "PATTERN 1  /  DRUM EDITOR" : "PATTERN 1  /  NOTE EDITOR",
                             juce::dontSendNotification);
    }

    // The Info View sits at the foot of the browser column, so it has nowhere
    // to go while the browser is hidden.
    void toggleInfoView()
    {
        if (!browserOpen) return;
        infoVisible = !infoVisible;
        resized();
        repaint();
    }

    // Record is one button for three things - start the count-in, start
    // recording, stop what is running - because that is one idea to the person
    // pressing it. Session decides which of them it is.
    void toggleRecording()
    {
        const auto result = session.toggleRecording();
        if (result.failed())
        {
            logStatus(result.getErrorMessage());
            return;
        }
        if (session.isCountingIn())
            logStatus("Counting in " + juce::String(session.countInBars())
                      + (session.countInBars() == 1 ? " bar..." : " bars..."));
        else if (session.isRecording())
            logStatus("Recording into the armed tracks");
        else
            logStatus("Recording stopped");
    }

    void requestClose() { files.confirmUnsaved([] { juce::JUCEApplication::getInstance()->quit(); }); }
    void openProjectFile(const juce::File& file) { files.openFile(file); }
    void showFileMenuFrom(juce::Component& target) { showFileMenu(&target); }
    void showEditMenuFrom(juce::Component& target) { showEditMenu(&target); }
    void showViewMenuFrom(juce::Component& target) { showViewMenu(&target); }
    void showHelpMenuFrom(juce::Component& target) { showHelpMenu(&target); }
    // The typing keyboard has to be heard wherever focus is, including where
    // it is nowhere: keys then go to the window, which is above this component
    // and so is never reached by walking up from here.
    void listenForKeysOn(juce::Component& component) { computerKeyboard.listenTo(component); }
    std::function<void(const juce::String&)> projectTitleChanged;
    // Full screen is the window's business, not its content's: the shell wires
    // these to the document window that owns this component.
    std::function<void()> fullScreenToggleRequested;
    std::function<bool()> fullScreenActive;

private:
    juce::TooltipWindow tooltipWindow {this, 700};
    void showFileMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "New project");
        menu.addItem(2, "Open project...", true, false);
        menu.addItem(3, "Save", true, false);
        menu.addItem(4, "Save as...");
        menu.addSeparator();
        menu.addItem(5, "Export WAV...", true, false);
        menu.addSeparator();
        menu.addItem(6, "Quit");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : fileMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)](int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->files.newProject();
                else if (result == 2) safe->files.open();
                else if (result == 3) safe->files.save();
                else if (result == 4) safe->files.save(true);
                else if (result == 5) safe->files.exportWav();
                else if (result == 6) safe->requestClose();
            });
    }

    // Cut, copy, paste and duplicate belong to whichever editor has the
    // keyboard, and both of them already answer the shortcut, so the menu
    // sends the shortcut rather than reaching into either one. Which editor
    // that is has to be read before the menu opens, because opening it takes
    // the focus away.
    void sendClipboardShortcut(int keyCode, bool toNoteEditor)
    {
        auto& editor = toNoteEditor ? static_cast<juce::Component&>(grid)
                                    : static_cast<juce::Component&>(arrangement);
        editor.grabKeyboardFocus();
        editor.keyPressed(juce::KeyPress(keyCode, juce::ModifierKeys::commandModifier, 0));
    }

    void showEditMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Undo", session.edit->getUndoManager().canUndo(), false);
        menu.addItem(2, "Redo", session.edit->getUndoManager().canRedo(), false);
        menu.addSeparator();
        const auto noteEditorHasFocus = grid.isVisible() && grid.hasKeyboardFocus(true);
        menu.addItem(6, "Cut       Ctrl+X");
        menu.addItem(7, "Copy       Ctrl+C");
        menu.addItem(8, "Paste       Ctrl+V");
        menu.addItem(9, "Duplicate       Ctrl+D");
        menu.addSeparator();
        menu.addItem(3, "Clear pattern");
        menu.addSeparator();
        menu.addItem(11, "Computer keyboard plays MIDI       M", true, computerKeyboard.isEnabled());
        menu.addItem(5, "Preview library sounds", true, session.previewEnabled());
        juce::PopupMenu monitoring;
        for (const auto mode : {Session::InputMonitoring::off, Session::InputMonitoring::automatic,
                                Session::InputMonitoring::on})
            monitoring.addItem(20 + static_cast<int>(mode),
                               Session::inputMonitoringName(mode)
                                   + (mode == Session::InputMonitoring::automatic ? "   (while armed)"
                                    : mode == Session::InputMonitoring::on        ? "   (always)"
                                                                                  : ""),
                               true, session.inputMonitoring() == mode);
        menu.addSubMenu("Monitor the audio input: " + Session::inputMonitoringName(session.inputMonitoring()),
                        monitoring);
        menu.addSeparator();
        menu.addItem(4, "Audio settings...");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : editMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this), noteEditorHasFocus](int result)
            {
                if (safe == nullptr) return;
                if (result >= 6 && result <= 9)
                {
                    static constexpr int keys[] {'X', 'C', 'V', 'D'};
                    safe->sendClipboardShortcut(keys[result - 6], noteEditorHasFocus);
                    return;
                }
                if (result == 1) safe->session.undo();
                else if (result == 2) safe->session.redo();
                else if (result == 3) safe->session.clearPattern();
                else if (result == 4) safe->showAudioSettings();
                else if (result == 5)
                {
                    const auto on = !safe->session.previewEnabled();
                    safe->session.setPreviewEnabled(on);
                    safe->logStatus(on ? "Clicking a library sound plays it"
                                       : "Library sounds are no longer played when clicked");
                }
                else if (result == 11) safe->computerKeyboard.toggle();
                else if (result >= 20 && result <= 22)
                {
                    const auto mode = static_cast<Session::InputMonitoring>(result - 20);
                    safe->session.setInputMonitoring(mode);
                    // Said plainly, because hearing the input on a machine with
                    // its own microphone and speakers is a feedback loop.
                    safe->logStatus(mode == Session::InputMonitoring::off
                                        ? "The audio input is not played back"
                                        : "The audio input is played back"
                                          + juce::String(mode == Session::InputMonitoring::automatic
                                                             ? " while a track is armed" : " at all times")
                                          + ". Use headphones: a built-in microphone and speakers will feed back.");
                }
            });
    }

    void showViewMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Browser", true, browserOpen);
        menu.addItem(2, lowerPane == LowerPane::audio ? "Audio Editor" : "Clip Editor",
                     true, clipPaneOpen());
        menu.addItem(3, "Device View", true, lowerPane == LowerPane::devices);
        menu.addItem(4, "Info View", browserOpen, infoVisible && browserOpen);
        menu.addSeparator();
        juce::PopupMenu::Item fullScreen {"Full Screen"};
        fullScreen.itemID = 5;
        fullScreen.shortcutKeyDescription = "F12";
        fullScreen.isEnabled = fullScreenToggleRequested != nullptr;
        fullScreen.isTicked = fullScreenActive != nullptr && fullScreenActive();
        menu.addItem(std::move(fullScreen));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : viewMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)](int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->toggleBrowser();
                else if (result == 2) safe->toggleClipEditor();
                else if (result == 3) safe->toggleDeviceView();
                else if (result == 4) safe->toggleInfoView();
                else if (result == 5 && safe->fullScreenToggleRequested) safe->fullScreenToggleRequested();
            });
    }

    void showHelpMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Keyboard shortcuts");
        menu.addItem(2, "About Rhino");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : helpMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)](int result)
            {
                if (safe == nullptr) return;
                if (result == 1)
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Keyboard shortcuts",
                        "Space  Play/Pause\nCtrl+N  New project\nCtrl+O  Open project\nCtrl+S  Save project\nCtrl+Shift+S  Save as\n"
                        "Ctrl+Shift+E  Export WAV\nCtrl+Z  Undo\nCtrl+Y / Ctrl+Shift+Z  Redo\nCtrl+F  Search browser\nCtrl+A  Add a clip to the focused track\nDouble-click a lane  Add a clip there\n"
                        "F9  Record into the armed tracks / Click the dot on a track card to arm it\nM  Play MIDI from the typing keyboard: A-P are notes, Z/X octave, C/V velocity\nCtrl+T  Add a track of the kind last picked from the + menu\nF2  Rename the selected track or group\nCtrl+G  Group the selected tracks\nCtrl+Shift+G  Ungroup\n"
                        "Drag an empty lane or cell  Select what it sweeps\nClick  Select that one clip or note\n"
                        "Ctrl+click  Add one or take it back out / Shift+click  Add one\n"
                        "Double-click a cell  Add a note there\nB  Draw mode in the MIDI editor: drag paints notes\n"
                        "Right-drag in the MIDI editor  Erase notes\n"
                        "Ctrl+X / Ctrl+C / Ctrl+V  Cut, copy and paste the selection\n"
                        "Ctrl+D  Duplicate it directly after itself\nCtrl+E  Split the selected clip at the playhead\n"
                        "Ctrl+J  Merge the selected audio clips into one audio file\n"
                        "R  Play the selected audio clips backwards\nDelete  Empty the selection\n"
                        "?  Show/hide Info View\nF12  Full screen");
                else if (result == 2)
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "About Rhino",
                        "Rhino\nA native desktop DAW for patterns, arrangement, and offline WAV export.");
            });
    }

    void showMetronomeMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("METRONOME");
        menu.addItem(1, "Emphasize first beat", true, session.clickTrackEmphasiseBars());
        menu.addSeparator();
        menu.addSectionHeader("Level");
        menu.addItem(10, "-12 dB", true, std::abs(session.clickTrackGain() + 12.0f) < 0.1f);
        menu.addItem(11, "-6 dB", true, std::abs(session.clickTrackGain() + 6.0f) < 0.1f);
        menu.addItem(12, "0 dB", true, std::abs(session.clickTrackGain()) < 0.1f);
        menu.addSeparator();
        // The count-in is a metronome setting: it is the click, counted before
        // the transport moves, so it belongs in the metronome's own menu rather
        // than in a preferences dialog nobody would look in.
        menu.addSectionHeader("Count-in");
        menu.addItem(20, "Off", true, session.countInBars() == 0);
        for (int bars = 1; bars <= Session::maximumCountInBars; ++bars)
            menu.addItem(20 + bars, juce::String(bars) + (bars == 1 ? " bar" : " bars"),
                         true, session.countInBars() == bars);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(metronomeMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)] (int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->session.setClickTrackEmphasiseBars(!safe->session.clickTrackEmphasiseBars());
                else if (result >= 10 && result <= 12) safe->session.setClickTrackGain(static_cast<float>((result - 12) * 6));
                else if (result >= 20 && result <= 20 + Session::maximumCountInBars)
                {
                    const auto bars = result - 20;
                    safe->session.setCountInBars(bars);
                    safe->logStatus(bars == 0 ? "Recording starts immediately"
                                              : "Recording counts in " + juce::String(bars)
                                                    + (bars == 1 ? " bar" : " bars"));
                }
            });
    }

    void showDisplayMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("DISPLAY");
        menu.addItem(1, "Musical position", true, displayPosition);
        menu.addItem(2, "Tempo", true, displayTempo);
        menu.addItem(3, "Time signature", true, displayTimeSignature);
        menu.addItem(4, "Loop range", true, displayLoop);
        menu.addSeparator();
        menu.addItem(5, "Time and song length", true, displayTime);
        menu.addItem(6, "CPU", true, displayCpu);
        menu.addItem(7, "Memory", true, displayMemory);
        menu.addItem(8, "Audio device", true, displayAudio);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(position),
            [safe = juce::Component::SafePointer<ControlWindow>(this)] (int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->displayPosition = !safe->displayPosition;
                else if (result == 2) safe->displayTempo = !safe->displayTempo;
                else if (result == 3) safe->displayTimeSignature = !safe->displayTimeSignature;
                else if (result == 4) safe->displayLoop = !safe->displayLoop;
                else if (result == 5) safe->displayTime = !safe->displayTime;
                else if (result == 6) safe->displayCpu = !safe->displayCpu;
                else if (result == 7) safe->displayMemory = !safe->displayMemory;
                else if (result == 8) safe->displayAudio = !safe->displayAudio;
                safe->timerCallback();
            });
    }

    void editWillChange() override
    {
        session.edit->getTransport().removeChangeListener(this);
        session.edit->getUndoManager().removeChangeListener(this);
    }

    void editDidChange() override
    {
        session.edit->getTransport().addChangeListener(this);
        session.edit->getUndoManager().addChangeListener(this);
        changeListenerCallback(nullptr);
    }
    void showAudioSettings()
    {
        if (audioSettings != nullptr)
        {
            audioSettings->toFront(true);
            return;
        }
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            session.engine.getDeviceManager().deviceManager, 0, 2, 0, 2, true, false, true, false);
        selector->setSize(520, 420);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector.release());
        options.dialogTitle = "Audio settings";
        options.dialogBackgroundColour = juce::Colour(0xff202327);
        options.useNativeTitleBar = true;
        audioSettings = options.launchAsync();
    }

    void logStatus(const juce::String& message)
    {
        if (message.isEmpty()) return;
        infoView.setText(message, false);
        juce::Logger::writeToLog("Rhino: " + message);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        const auto playing = session.edit->getTransport().isPlaying();
        play.setButtonText(playing ? juce::String(L"\u275a\u275a") : juce::String(L"\u25b6"));
        play.setTooltip(playing ? "Pause" : "Play");
        updateRecordButton();
        tempoBox.setValue(session.tempo());
        const auto signature = session.timeSignature();
        signatureField.setSignature(signature.numerator, signature.denominator);
        metronome.setToggleState(session.clickTrackEnabled(), juce::dontSendNotification);
        undo.setEnabled(session.edit->getUndoManager().canUndo());
        redo.setEnabled(session.edit->getUndoManager().canRedo());
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

    void updateRecordButton()
    {
        const auto state = session.isRecording()   ? RecordButton::State::recording
                         : session.isCountingIn()  ? RecordButton::State::countingIn
                         : session.anyTrackArmed() ? RecordButton::State::armed
                                                   : RecordButton::State::idle;
        // A count-in starts the transport itself, so the button press that
        // began it is long over by the time recording actually starts. This is
        // what says so, rather than leaving the count-in message standing while
        // the take is being made.
        if (state != recordState)
        {
            if (state == RecordButton::State::recording && recordState == RecordButton::State::countingIn)
                logStatus("Recording into the armed tracks");
            recordState = state;
        }
        record.setState(state);
    }

    void timerCallback() override
    {
        if (paneLayoutPending && !juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown())
            applyPaneLayout();
        // The engine both starts and finishes a recording on the audio thread
        // and broadcasts neither, so the clips it wrote are collected here -
        // the same reason the slot override below is polled rather than
        // listened for.
        session.recordingStopped();
        updateRecordButton();
        // The engine raises a track's slot-override flag from the audio thread
        // without broadcasting, so this is polled rather than event-driven.
        if constexpr (sessionViewEnabled)
            if (const auto overriding = session.anyTrackPlayingSlots(); overriding != backToArrangement.isVisible())
            {
                backToArrangement.setVisible(overriding);
                resized();
            }
        if (session.edit->getTransport().isPlaying())
            session.applyTrackAutomationAt(playheadTime(session.edit->getTransport()));
        const auto seconds = session.edit->getTransport().getPosition().inSeconds();
        updateReadings(seconds);
        // The loop is where the transport will turn, so it belongs beside the
        // position rather than under it.
        position.setTrailingText(displayLoop ? loopText() : juce::String());
        // A count-in owns the first line while it runs, but the clock, the
        // load meters and the loop keep reading, which is why all of them are
        // updated above this.
        if (session.isCountingIn())
        {
            position.setDisplayText("COUNT-IN     " + juce::String(session.countInBarsRemaining()));
            return;
        }
        const auto place = barAndBeat(seconds);
        const auto signature = session.timeSignature();
        juce::StringArray parts;
        if (displayPosition) parts.add(juce::String(place.first).paddedLeft('0', 3) + "  " + juce::String(place.second));
        if (displayTempo) parts.add(juce::String(session.tempo(), 0));
        if (displayTimeSignature) parts.add(juce::String(signature.numerator) + "/" + juce::String(signature.denominator));
        const auto text = parts.joinIntoString("     ");
        position.setDisplayText(text);
    }

    // The second line of the display. The clock follows the playhead at the
    // timer's own rate, but both load meters are sampled a few times a second:
    // each one walks kernel structures, and a figure that changed thirty times
    // a second could not be read anyway.
    void updateReadings(double seconds)
    {
        if (++readingSample >= 15)
        {
            readingSample = 0;
            cpuLoad = SystemUsage::processCpuLoad();
            memoryBytes = SystemUsage::processMemoryBytes();
            audioDescription = audioDeviceText();
        }
        juce::StringArray readings;
        if (displayTime)
            readings.add(formatClock(seconds, true) + " / "
                         + formatClock(session.edit->getLength().inSeconds(), true));
        if (displayCpu) readings.add("CPU " + juce::String(juce::roundToInt(cpuLoad * 100.0)) + "%");
        if (displayMemory) readings.add("RAM " + formatMemory(memoryBytes));
        position.setSecondaryText(readings.joinIntoString("   "));
        position.setSecondaryTrailingText(displayAudio ? audioDescription : juce::String());
    }

    // Bars and beats at a point on the timeline, both counted from one, which
    // is how the position readout and the loop beside it are both written.
    std::pair<int, int> barAndBeat(double seconds) const
    {
        const auto beats = session.edit->tempoSequence
                               .toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
        const auto barLength = session.beatsPerBar();
        const auto bar = static_cast<int>(std::floor(beats / barLength)) + 1;
        return {bar, static_cast<int>(std::floor(beats - (bar - 1) * barLength)) + 1};
    }

    // The loop, in the bars the position is counted in. Looping is always on:
    // with no span dragged on the ruler it is the whole arrangement, so until
    // someone narrows it this reads as the length of the project. The beat is
    // left off a loop that begins and ends on a downbeat, which is nearly all
    // of them.
    juce::String loopText() const
    {
        const auto range = session.loopRange();
        auto place = [] (std::pair<int, int> point)
        {
            return juce::String(point.first).paddedLeft('0', 3)
                 + (point.second == 1 ? juce::String() : "." + juce::String(point.second));
        };
        return "LOOP  " + place(barAndBeat(range.getStart().inSeconds()))
             + " - " + place(barAndBeat(range.getEnd().inSeconds()));
    }

    // What the engine is actually running on. Sampled with the load meters
    // rather than read every frame, because it changes only when the audio
    // settings do. The milliseconds are the block's own latency: the figure
    // that moves when the buffer size is changed to chase a crackle.
    juce::String audioDeviceText() const
    {
        auto* device = session.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        if (device == nullptr) return "NO AUDIO DEVICE";
        const auto rate = device->getCurrentSampleRate();
        const auto block = device->getCurrentBufferSizeSamples();
        if (rate <= 0.0 || block <= 0) return device->getName().toUpperCase();
        return juce::String(rate / 1000.0, 1) + " kHz   " + juce::String(block)
             + "   " + juce::String(block * 1000.0 / rate, 1) + " ms";
    }

    juce::Rectangle<int> infoViewArea() const
    {
        if (!infoVisible || !browserOpen) return {};
        return {0, std::max(browserTop + 96, getHeight() - infoViewHeight), browserWidth, infoViewHeight};
    }

    bool isOverSplitter(juce::Point<float> point) const
    {
        return browserOpen && std::abs(point.x - static_cast<float>(browserWidth)) <= 5.0f
            && point.y >= static_cast<float>(browserTop);
    }

    Session& session;
    juce::Label hint, patternLabel;
    juce::TextEditor infoView;
    TransportDisplay position;
    BrowserPanel browser;
    StepGrid grid;
    Arrangement arrangement;
    SessionView sessionView;
    DeviceRack rack;
    AudioClipPanel audioClip;
    ValueDragBox tempoBox;
    TimeSignatureField signatureField;
    juce::TextButton metronome, metronomeMenu;
    juce::TextButton undo {"Undo"}, redo {"Redo"}, clear {"Clear"};
    juce::TextButton play {"Play"}, panic {"Panic"};
    StopButton stop;
    RecordButton record;
    BrowserToggleButton browserToggle;
    juce::TextButton editorToggle {"Clip"}, rackToggle {"Devices"};
    juce::TextButton sessionToggle {"Session"}, arrangementToggle {"Arrange"};
    juce::TextButton backToArrangement;
    juce::ComboBox editorResolution;
    juce::TextButton editorZoomOut, editorZoomIn;
    juce::ComboBox scaleHighlight;
    ComputerKeyboard computerKeyboard;
    juce::Component::SafePointer<juce::DialogWindow> audioSettings;
    juce::TextButton fileMenu {"File"}, editMenu {"Edit"}, viewMenu {"View"}, helpMenu {"Help"};
    ProjectFiles files;
    SplitterBar lowerSplitter;
    // Zero until the pane is opened for the first time, which is what asks for
    // a fifth of the window rather than a number chosen here: the right height
    // depends on the display and cannot be known at construction.
    int browserWidth = 244, lowerPaneHeight = 0;
    int resizeStartX = 0, resizeStartY = 0, resizeStartBrowserWidth = 244;
    int resizeStartLowerPaneHeight = 0;
    // The control bar's own grid: a caption row over a control row, with the
    // same top and the same height for every module in the bar. Laying every
    // group out against these two numbers is what makes the bar read as one
    // band instead of as controls that happen to be near each other.
    static constexpr int barCaptionTop = 10, barCaptionHeight = 11;
    static constexpr int barControlTop = 25, barControlHeight = 42;
    static constexpr int barGroupGap = 16;
    static constexpr int browserTop = barControlTop + barControlHeight + 14;
    // A pane shorter than this shows neither a usable editor nor a device, so
    // it is the floor for both the pane and the Device View inside it.
    static constexpr int minimumPaneHeight = 112;
    static constexpr int infoViewHeight = 132;
    // Room under the arrangement for the Clip and Devices toggles, which stay
    // put whether or not the pane they open is showing.
    static constexpr int toggleStripHeight = 46;
    // The clip pane and the Device View are two independent panels that happen
    // to stack in the same strip, and each answers to one thing: this says
    // which clip editor the clip pane is showing, and rackOpen whether the
    // Device View is up. Both start hidden - double-clicking a clip reveals the
    // editor that clip belongs in, and clicking a track card the devices.
    // One pane showing one thing. The clip editors and the Device View used to
    // stack inside it, which asked for a window tall enough for both and left
    // neither of them tall enough to work in. They are a pair of toggles now:
    // opening either closes the other, the way Live's Clip and Device views
    // are two faces of one strip rather than two strips.
    enum class LowerPane { none, notes, audio, devices };
    LowerPane lowerPane = LowerPane::none;
    bool lowerPaneVisible() const { return lowerPane != LowerPane::none; }
    bool clipPaneOpen() const { return lowerPane == LowerPane::notes || lowerPane == LowerPane::audio; }
    // The clip selection the pane state was chosen for, so a notification about
    // the same selection does not overwrite a toggle the user just pressed.
    te::EditItemID paneClip;
    bool paneMidi = false;
    bool paneLayoutPending = false;
    bool browserOpen = true, infoVisible = true;
    bool sessionViewOpen = false;
    bool resizingBrowser = false;
    // What the bar paints over the controls it has placed: one caption per
    // module and a hairline between groups. Recorded by the layout, because
    // only the layout knows where the modules ended up.
    struct BarGroup { juce::String caption; juce::Rectangle<int> bounds; };
    std::vector<BarGroup> barGroups;
    std::vector<int> barDividers;
    bool updatingEditorResolution = false;
    // The tempo and the signature have fields of their own in the bar now, so
    // the readout no longer repeats them: it is the position, the clock and
    // the loop. Both are still in the display menu for anyone who wants them
    // back on the one line their eye is already on while playing.
    bool displayPosition = true, displayTempo = false, displayTimeSignature = false, displayLoop = true;
    bool displayTime = true, displayCpu = true, displayMemory = true, displayAudio = true;
    double cpuLoad = 0.0;
    juce::uint64 memoryBytes = 0;
    juce::String audioDescription;
    int readingSample = 15;
    RecordButton::State recordState = RecordButton::State::idle;
};

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Rhino"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override
    {
        const auto logFile = rhinoLogFile();
        logFile.getParentDirectory().createDirectory();
        logger = std::make_unique<juce::FileLogger>(logFile, "Rhino debug log", 512 * 1024);
        juce::Logger::setCurrentLogger(logger.get());
        juce::Logger::writeToLog("Rhino: log started at " + logFile.getFullPathName());
        if (args == "--self-test" || args == "--pattern-test" || args == "--arrangement-test"
            || args == "--arrangement-geometry-test")
        {
            Session::setCommandLineTestMode(true);
            if (args != "--arrangement-geometry-test") prepareCommandLineAudio();
            setApplicationReturnValue(args == "--self-test" ? runSelfTest()
                : args == "--pattern-test" ? runPatternTest()
                : args == "--arrangement-test" ? runArrangementTest() : runArrangementGeometryTest());
            quit();
            return;
        }
        startupTest = args == "--startup-test";
        if (!startupTest)
            registerRhinoProjectFileAssociation();
        const auto requestedProject = juce::File(args.trim().unquoted());
        if (requestedProject.existsAsFile() && requestedProject.hasFileExtension("rhinoedit"))
            projectToOpen = requestedProject;
        theme.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343a40));
        theme.setColour(juce::Slider::trackColourId, juce::Colour(0xffc6d58c));
        juce::LookAndFeel::setDefaultLookAndFeel(&theme);
        window = std::make_unique<Window>();
        loading = new StartupScreen();
        loading->setSize(560, 320);
        window->setContentOwned(loading.getComponent(), true);
        window->centreWithSize(560, 320);
        window->setVisible(!startupTest);

        // RHINO_RENDERER=software selects JUCE's rasteriser. It keeps one
        // persistent surface, so a region nobody repainted still holds the last
        // correct frame; the Direct2D backend presents from rotating buffers
        // where that same region holds an older frame instead.
        if (auto* peer = window->getPeer())
        {
            const auto engines = peer->getAvailableRenderingEngines();
            const auto requested = juce::SystemStats::getEnvironmentVariable("RHINO_RENDERER", {}).trim();
            if (requested.isNotEmpty())
            {
                auto match = -1;
                for (int i = 0; i < engines.size(); ++i)
                    if (engines[i].containsIgnoreCase(requested)) { match = i; break; }
                if (match >= 0) peer->setCurrentRenderingEngine(match);
            }
            juce::Logger::writeToLog("Rhino: rendering engine " + engines[peer->getCurrentRenderingEngine()]);
        }
        const auto screenshot = juce::SystemStats::getEnvironmentVariable("RHINO_STARTUP_SNAPSHOT", {});
        if (startupTest && screenshot.isNotEmpty())
            writeSnapshot(*loading, juce::File(screenshot));
        // Let the window become visible before constructing the engine. Engine
        // initialization requires the message thread; yield between its phases.
        startTimer(40);
    }
    void shutdown() override
    {
        stopTimer();
        window.reset();
        session.reset();
        juce::Logger::setCurrentLogger(nullptr);
        logger.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    void systemRequestedQuit() override
    {
        if (window)
            if (auto* controls = dynamic_cast<ControlWindow*>(window->getContentComponent()))
            {
                controls->requestClose();
                return;
            }
        stopTimer();
        quit();
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (!window || !loading) return;
        try
        {
            if (window->getContentComponent() != loading.getComponent())
                throw std::runtime_error("Startup screen was replaced before initialization finished.");
            if (auto* peer = window->getPeer()) peer->performAnyPendingRepaintsNow();
            if (startupStage == 0)
                session = std::make_unique<Session>();
            else if (startupStage == 1)
            {
                avoidLegacyDirectSound(session->engine);
                // Two inputs as well as two outputs: a device opened with no
                // input channels leaves the engine with no wave input device,
                // and recording has nothing to arm.
                session->engine.getDeviceManager().initialise(2, 2);
                avoidLegacyDirectSound(session->engine);
            }
            else
            {
                window->setContentOwned(new ControlWindow(*session), true);
                window->setResizeLimits(960, 680, 2400, 1600);
                window->centreWithSize(1120, 760);
                if (auto* controls = dynamic_cast<ControlWindow*>(window->getContentComponent()))
                {
                    const juce::Component::SafePointer<ControlWindow> safeControls(controls);
                    const juce::Component::SafePointer<Window> safeWindow(window.get());
                    window->fileRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showFileMenuFrom(target);
                    };
                    window->editRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showEditMenuFrom(target);
                    };
                    window->viewRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showViewMenuFrom(target);
                    };
                    window->helpRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showHelpMenuFrom(target);
                    };
                    controls->fullScreenToggleRequested = [safeWindow]
                    {
                        if (safeWindow != nullptr) safeWindow->setAppFullScreen(!safeWindow->isAppFullScreen());
                    };
                    controls->fullScreenActive = [safeWindow]
                    {
                        return safeWindow != nullptr && safeWindow->isAppFullScreen();
                    };
                    controls->projectTitleChanged = [safeWindow](const juce::String& title)
                    {
                        if (safeWindow != nullptr) safeWindow->setProjectTitle(title);
                    };
                    window->setProjectTitle("Untitled");
                    controls->listenForKeysOn(*window);
                    // Give the content the focus it never takes for itself, so
                    // the shortcuts answer from the first frame.
                    controls->grabKeyboardFocus();
                }
                if (projectToOpen != juce::File{})
                    if (auto* controls = dynamic_cast<ControlWindow*>(window->getContentComponent()))
                        controls->openProjectFile(projectToOpen);
                // The shell is the one panel that cannot be rendered from a
                // test: ControlWindow is defined inside this file and the test
                // runners cannot reach it. This is the way to look at the
                // control bar without taking the display away from whoever is
                // using the machine - RHINO_SHELL_SNAPSHOT under
                // --startup-test writes the window to a PNG and quits.
                if (const auto shellShot = juce::SystemStats::getEnvironmentVariable("RHINO_SHELL_SNAPSHOT", {});
                    startupTest && shellShot.isNotEmpty())
                    if (auto* content = window->getContentComponent())
                        writeSnapshot(*content, juce::File(shellShot));
                if (startupTest) quit();
                return;
            }
            loading->setStage(++startupStage);
            startTimer(1);
        }
        catch (const std::exception& error)
        {
            setApplicationReturnValue(1);
            if (loading) loading->showError(error.what());
            std::fprintf(stderr, "Rhino startup failed: %s\n", error.what());
            if (startupTest) quit();
        }
    }

    struct Window final : juce::DocumentWindow
    {
        struct TitleMenuButton final : juce::TextButton
        {
            using juce::TextButton::TextButton;

            void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
            {
                auto colour = findColour(juce::TextButton::textColourOffId);
                if (highlighted || pressed) colour = colour.brighter(0.25f);
                g.setColour(colour);
                g.setFont(uiFont(10.5f));
                drawSnappedText(g, getButtonText(), getLocalBounds(), juce::Justification::centred);
            }
        };

        Window() : DocumentWindow({}, juce::Colour(0xff171a1e), allButtons)
        {
            // Keep the frame and content in JUCE's single client-area layout.
            // Native Windows non-client bounds can put the title bar above the
            // display work area when a constrained window is resized/snapped.
            setUsingNativeTitleBar(false);
            setTitleBarHeight(28);
            setOpaque(true);
            // On Windows this flag is what decides whether a borderless window
            // gets the native frame styles. Without it JUCE creates a bare
            // WS_POPUP: no Aero Snap when the caption is dragged to an edge, no
            // snap-layouts flyout, and a maximise that covers the taskbar
            // instead of stopping at the work area. With it JUCE uses the
            // system shadow rather than the separate shadow surface that used
            // to trail the right edge during live D2D expansion.
            setDropShadowEnabled(true);
            setResizable(true, false);
            projectTitle.setJustificationType(juce::Justification::centred);
            projectTitle.setColour(juce::Label::textColourId, juce::Colours::white);
            projectTitle.setInterceptsMouseClicks(false, false);
            projectTitle.setText("Untitled", juce::dontSendNotification);
            for (auto* menu : {&fileMenu, &editMenu, &viewMenu, &helpMenu})
                menu->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7dde2));
            for (auto* component : std::initializer_list<juce::Component*>{&fileMenu, &editMenu, &viewMenu, &helpMenu, &projectTitle})
                addAndMakeVisible(component);
            fileMenu.onClick = [this] { if (fileRequested) fileRequested(fileMenu); };
            editMenu.onClick = [this] { if (editRequested) editRequested(editMenu); };
            viewMenu.onClick = [this] { if (viewRequested) viewRequested(viewMenu); };
            helpMenu.onClick = [this] { if (helpRequested) helpRequested(helpMenu); };
        }

        // Rhino keeps its title bar in full screen, where JUCE's kiosk mode
        // would hand the whole window to the content. These three overrides put
        // the bar back: its strip is painted, its buttons are placed, and the
        // content starts below it, exactly as in a windowed session.
        juce::Rectangle<int> titleBarBounds() const
        {
            const auto border = getBorderThickness();
            return {border.getLeft(), border.getTop(), getWidth() - border.getLeftAndRight(), getTitleBarHeight()};
        }

        juce::BorderSize<int> getContentComponentBorder() const override
        {
            auto border = DocumentWindow::getContentComponentBorder();
            if (isKioskMode()) border.setTop(border.getTop() + getTitleBarHeight());
            return border;
        }

        void paint(juce::Graphics& g) override
        {
            DocumentWindow::paint(g);
            if (!isKioskMode()) return;
            const auto bar = titleBarBounds();
            g.reduceClipRegion(bar);
            g.setOrigin(bar.getPosition());
            getLookAndFeel().drawDocumentWindowTitleBar(*this, g, bar.getWidth(), bar.getHeight(),
                                                        6, std::max(1, bar.getWidth() - 12), nullptr, false);
        }

        void resized() override
        {
            DocumentWindow::resized();
            if (isKioskMode())
            {
                const auto bar = titleBarBounds();
                getLookAndFeel().positionDocumentWindowButtons(*this, bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(),
                                                               getMinimiseButton(), getMaximiseButton(), getCloseButton(), false);
            }
            const auto h = getTitleBarHeight();
            fileMenu.setBounds(10, 0, 42, h);
            editMenu.setBounds(56, 0, 42, h);
            viewMenu.setBounds(102, 0, 44, h);
            helpMenu.setBounds(150, 0, 44, h);
            projectTitle.setBounds(200, 0, std::max(80, getWidth() - 400), h);
        }

        // Full screen is kiosk mode, and deliberately not what the maximise
        // button does: JUCE's setFullScreen is the window's maximise state.
        bool isAppFullScreen() const { return juce::Desktop::getInstance().getKioskModeComponent() == this; }

        // In full screen the title bar is still there, so its maximise button
        // has to lead back out rather than maximise a window that already
        // covers the display.
        void maximiseButtonPressed() override
        {
            if (isAppFullScreen()) setAppFullScreen(false);
            else DocumentWindow::maximiseButtonPressed();
        }

        // The content handles F12 whenever something inside it holds keyboard
        // focus. This catches the case where nothing does, as on a fresh start.
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::F12Key)
            {
                setAppFullScreen(!isAppFullScreen());
                return true;
            }
            return DocumentWindow::keyPressed(key);
        }

        void setAppFullScreen(bool shouldBeFullScreen)
        {
            if (shouldBeFullScreen == isAppFullScreen()) return;
            auto& desktop = juce::Desktop::getInstance();
            if (shouldBeFullScreen)
            {
                // Kiosk mode restores the bounds it was given, which are the
                // restored bounds of a maximised window. Maximise again on the
                // way out so the window comes back as the user left it.
                maximisedBeforeFullScreen = isFullScreen();
                if (maximisedBeforeFullScreen) setFullScreen(false);
                desktop.setKioskModeComponent(this, true);
            }
            else
            {
                desktop.setKioskModeComponent(nullptr, true);
                if (maximisedBeforeFullScreen) setFullScreen(true);
            }
            resized();
            repaint();
        }

        void setProjectTitle(const juce::String& text) { projectTitle.setText(text, juce::dontSendNotification); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        std::function<void(juce::Component&)> fileRequested, editRequested, viewRequested, helpRequested;

    private:
        TitleMenuButton fileMenu {"File"}, editMenu {"Edit"}, viewMenu {"View"}, helpMenu {"Help"};
        juce::Label projectTitle;
        bool maximisedBeforeFullScreen = false;
    };
    Theme theme;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<Session> session;
    std::unique_ptr<Window> window;
    juce::Component::SafePointer<StartupScreen> loading;
    int startupStage = 0;
    bool startupTest = false;
    juce::File projectToOpen;
};
}
START_JUCE_APPLICATION(rhino::Application)
