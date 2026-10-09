#include "Session.h"
#include "StepGrid.h"
#include "ProjectFiles.h"
#include "Theme.h"
#include "Arrangement.h"
#include "DjView.h"
#include "BrowserPanel.h"
#include "DeviceRack.h"
#include "AudioClipPanel.h"
#include "Playhead.h"
#include "ComputerKeyboard.h"
#include "StartupScreen.h"
#include "TransportDisplay.h"
#include "WallClock.h"
#include "ControlBarFields.h"
#include "ControlBarIcons.h"
#include "InfoHints.h"
#include "SystemUsage.h"
#include "tests/Pattern/DeviceRackTest.h"
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

// The browser toggle: a narrow panel beside a wide one. Grey - the state is
// carried by brightness alone, not by a colour, because a coloured toggle in a
// neutral bar reads as a warning rather than as a switch.
inline void paintSidebarIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour);
    icons::drawSidebar(g, area);
}

// Stop parks the playhead on the line the arrangement is working from, which
// is what makes play-stop-play repeat a passage. Double-clicking it means the
// top of the song instead. The second click is told apart here, where the
// system's own click counting is already available, rather than by timing
// clicks in the handler.
class StopButton final : public IconButton
{
public:
    StopButton() : IconButton("Stop") {}

    std::function<void()> onStop, onReturnToStart;

    void mouseDown(const juce::MouseEvent& event) override
    {
        secondClick = event.getNumberOfClicks() > 1;
        IconButton::mouseDown(event);
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
        // No box. The dot sits on the bar like every other transport glyph,
        // and the wash under the pointer is what shows the hit area is larger
        // than the twelve pixels the dot occupies.
        if (highlighted || pressed)
        {
            g.setColour(juce::Colour(pressed ? 0x24ffffff : 0x14ffffff));
            g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);
        }
        const auto dot = bounds.withSizeKeepingCentre(12.0f, 12.0f);
        // Red is the one colour the neutral chrome keeps, and it keeps it here:
        // record is the only control in the bar whose state is worth a colour.
        const auto colour = state == State::recording  ? palette::recordAccent
                          : state == State::countingIn ? juce::Colour(0xffe0a03c)
                          : state == State::armed      ? palette::recordAccent.withMultipliedSaturation(0.72f).darker(0.25f)
                                                       : palette::disabled;
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

static juce::String formatMemory(juce::uint64 bytes)
{
    const auto megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes >= 1024.0) return juce::String(megabytes / 1024.0, 2) + " GB";
    return juce::String(juce::roundToInt(megabytes)) + " MB";
}

// The second view. The Arrange/DJ switch in the control bar and Tab open the
// DJ view (wiki/pages/dj-view.md) in the arrangement's place. The clip
// launcher it replaced is still built and tested (wiki/pages/session-view.md)
// but nothing in the shell reaches it; setting this to false hides the switch
// and the shortcut again.
static constexpr bool sessionViewEnabled = true;

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
        g.setColour(palette::border);
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
    explicit ControlWindow(Session& s) : session(s), browser(s), grid(s), arrangement(s), djView(s), rack(s), audioClip(s), files(s)
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
        djView.status = files.status;
        // A deck that plays a track of the song hands that track to the
        // Device View, so its chain is a click away while it plays.
        djView.deckSelected = [this](int track) { if (sessionViewOpen && track >= 0) rack.selectTrack(track); };
        // A deck's Edit key opens its track's clip in the note editor, which
        // is the same lower pane under either view: the deck bounces again
        // a moment after each change, so the edit is heard on the deck.
        djView.editRequested = [this](int track, te::EditItemID clip)
        {
            if (track < 0 || clip == te::EditItemID()) return;
            arrangement.selectTrack(track);
            rack.selectTrack(track);
            const auto result = session.selectPatternClip(clip);
            if (result.failed())
            {
                logStatus(result.getErrorMessage());
                return;
            }
            openClip(clip);
            logStatus("Editing " + session.trackName(track).quoted() + ": the deck follows each change");
        };
        sessionToggle.setButtonText("DJ");
        arrangementToggle.setButtonText("Arrange");
        sessionToggle.setTooltip("Show the DJ view: decks either side of a mixer, each playing a track, a group or a file. Tab switches.");
        arrangementToggle.setTooltip("Show the Arrangement timeline. Tab switches.");
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
        rack.presetsChanged = [this] { browser.refreshPresets(); };
        // The Drum Rack's window is another place the hand works, so Space,
        // undo and the rest answer there, and the typing keyboard plays its pads.
        rack.shortcut = [this](const juce::KeyPress& key) { return keyPressed(key); };
        rack.listenForKeys = [this](juce::Component& window) { computerKeyboard.listenTo(window); };
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
        for (auto* toggle : std::initializer_list<juce::TextButton*>{&editorToggle, &rackToggle,
                                                                     &sessionToggle, &arrangementToggle,
                                                                     &backToArrangement})
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
        hint.setText({}, juce::dontSendNotification);
        hint.setVisible(false);
        hint.setColour(juce::Label::textColourId, palette::textDim);
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
                 &sessionToggle, &arrangementToggle, &backToArrangement, &rack, &tempoBox, &signatureField, &undo, &redo, &metronome, &metronomeMenu, &hint,
                 &patternLabel, &editorResolution, &editorZoomOut, &editorZoomIn, &scaleHighlight,
                 &lowerSplitter})
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
        g.fillAll(palette::appBackground);
        paintControlBar(g);
        // The only thing between the browser and the arrangement: a gutter
        // three pixels wide that is also the handle the pointer grabs. There
        // are no margins around either panel and no frames on them, so the
        // regions are told apart by their own surface tones and by this.
        if (browserOpen)
        {
            g.setColour(palette::border);
            g.fillRect(browserWidth, browserTop, browserDividerWidth,
                       std::max(0, browserColumnBottom() - browserTop));
        }
        // The ground the Clip and Devices strip stands on. In the shell's own
        // background rather than the panel grey, because the arrangement spans
        // the window and paints this same strip that colour for the part of it
        // the panel covers - two answers here would leave a seam down the
        // browser's edge whenever the Info View is hidden.
        if (lowerBandTop > 0 && lowerBandTop < getHeight())
        {
            g.setColour(palette::appBackground);
            g.fillRect(lowerBandLeft, lowerBandTop, getWidth() - lowerBandLeft, getHeight() - lowerBandTop);
        }
        // The Info View lives at the foot of the browser column, so it goes
        // when the browser does. Asking infoVisible alone drew its heading into
        // an empty rectangle at the origin, which put the words on the control
        // bar with the browser hidden.
        if (infoVisible && browserOpen)
        {
            const auto area = infoViewArea();
            g.setColour(palette::sideSurface);
            g.fillRect(area);
            // A rule along the top only. A box drawn round the Info View made
            // it a card floating in the browser, which is the thing this
            // layout is getting rid of everywhere else.
            g.setColour(palette::border);
            g.fillRect(area.withHeight(1));
            g.setColour(palette::textDim);
            g.setFont(uiFontBold(9.0f));
            drawSnappedText(g, "INFO VIEW", area.withTrimmedLeft(12).withTrimmedTop(6).withHeight(18),
                            juce::Justification::centredLeft);
            g.setColour(palette::disabled);
            g.setFont(uiFont(9.0f));
            drawSnappedText(g, "?   HIDE", area.withTrimmedLeft(12).withTrimmedTop(6).withHeight(18).withTrimmedRight(12),
                            juce::Justification::right);
        }
    }

    // One continuous band across the top of the window, and the hairlines that
    // separate the few groups that need separating. The micro-headings that
    // used to sit over each group - TEMPO, SIGNATURE, CLICK, TRANSPORT,
    // POSITION, EDIT - are gone: six words of eight pixel type to label six
    // controls that each say what they are, at the cost of a second row of
    // height in the one band that is on screen the whole time.
    void paintControlBar(juce::Graphics& g)
    {
        g.setColour(palette::globalBar);
        g.fillRect(0, 0, getWidth(), controlBarHeight);
        g.setColour(palette::border);
        for (const auto divider : barDividers)
            g.fillRect(divider, barDividerTop, 1, controlBarHeight - barDividerTop * 2);
        // The foot of the bar, which is also the top edge of everything docked
        // beneath it. Drawn here rather than by the panels, so it runs the
        // whole width whether or not the browser is showing.
        g.fillRect(0, controlBarHeight - 1, getWidth(), 1);
    }

    // Four sections, ruled off from each other: the browser toggle, the song's
    // own fields, the transport, and - from the right client edge inwards - the
    // metronome and the undo pair. Every control takes the width it needs, and
    // the readout is centred on the window in whatever is left between the two
    // sides. It is the thing the bar is built around, but by being the one
    // panel on it and the tallest thing in the row rather than by being wide:
    // stretched across the whole leftover span it read as an empty bezel.
    void layoutControlBar()
    {
        barDividers.clear();
        const auto rule = [this](int x) { barDividers.push_back(x); };
        // A run of controls at one height, laid out from a running x.
        auto x = barEdgeMargin;
        const auto place = [&x](juce::Component& component, int width, int height, int top, int gap)
        {
            component.setBounds(x, top, width, height);
            x += width + gap;
        };

        // A square glyph and a field are different heights, so the glyphs are
        // centred on the fields rather than sharing their top edge: the bar
        // reads as one row because everything on it shares a middle.
        const auto transportTop = barControlTop + (fieldHeight - transportSize) / 2;
        // The sections spread apart on a wide bar and close up on a narrow one.
        // The readout is what the space is being spent on, and at the smallest
        // window a clock is worth more than air between the groups: held at the
        // full gap, the bar ran out of room for the readout at 976 pixels and
        // dropped it, which is inside the smallest window this supports.
        const auto sectionGap = juce::jlimit(barGroupGap, barSectionGap,
                                             barGroupGap + (getWidth() - 960) / 12);

        place(browserToggle, browserToggleWidth, transportSize, transportTop, sectionGap);
        rule(x - sectionGap / 2);

        // Tempo and signature are one group - they are both the song's own
        // settings - and take no rule between them, only the field gap.
        place(tempoBox, tempoWidth, fieldHeight, barControlTop, barFieldGap);
        place(signatureField, signatureWidth, fieldHeight, barControlTop, sectionGap);
        rule(x - sectionGap / 2);

        for (auto* button : std::initializer_list<juce::Component*>{&rewind, &stop, &play, &record})
            place(*button, transportSize, transportSize, transportTop, barTransportGap);
        x += sectionGap - barTransportGap;

        // The right of the bar, laid out from the right client edge inwards:
        // undo and redo pinned to it, and the metronome in its own section
        // beside them. The click belongs with the tools rather than with the
        // song's settings - it is something switched on while working, not a
        // property of the document - so it sits over here now.
        auto rightEdge = getWidth() - barEdgeMargin;
        const auto placeRight = [&rightEdge](juce::Component& component, int width, int height, int top, int gap)
        {
            rightEdge -= width;
            component.setBounds(rightEdge, top, width, height);
            rightEdge -= gap;
        };
        placeRight(redo, undoSize, transportSize, transportTop, barTransportGap);
        placeRight(undo, undoSize, transportSize, transportTop, sectionGap);
        rule(rightEdge + sectionGap / 2);
        placeRight(metronomeMenu, metronomeMenuWidth, transportSize, transportTop, 0);
        placeRight(metronome, transportSize, transportSize, transportTop, sectionGap);

        sessionToggle.setVisible(sessionViewEnabled);
        arrangementToggle.setVisible(sessionViewEnabled);
        if constexpr (sessionViewEnabled)
        {
            constexpr int viewWidth = 112;
            const auto area = juce::Rectangle<int>(rightEdge - viewWidth, barControlTop, viewWidth, fieldHeight);
            sessionToggle.setBounds(area.withWidth(viewWidth / 2 - 2));
            arrangementToggle.setBounds(area.withTrimmedLeft(viewWidth / 2 + 2));
            sessionToggle.setToggleState(sessionViewOpen, juce::dontSendNotification);
            arrangementToggle.setToggleState(!sessionViewOpen, juce::dontSendNotification);
            backToArrangement.setBounds(rightEdge - viewWidth, barControlTop + fieldHeight + 2, viewWidth, 0);
            rightEdge -= viewWidth + barGroupGap;
        }

        // Centred in what the two sides have left rather than on the window:
        // the left of the bar carries four sections and the right two, so a
        // readout centred on the window sat hard against the transport with a
        // gulf on the other side of it. Below the width its three columns need
        // it is dropped rather than squeezed: a clock with half its digits
        // missing is worse than no clock, and the controls around it stay
        // reachable.
        const auto room = rightEdge - x;
        position.setVisible(room >= 260);
        if (position.isVisible())
        {
            const auto width = std::min(displayWidth, room);
            position.setBounds(x + (room - width) / 2, displayTop, width, displayHeight);
        }
    }

    void resized() override
    {
        // Docked, not floated: the browser sits against the left client edge,
        // the arrangement fills everything right of the divider out to the
        // right client edge, and neither has a margin around it. Hiding the
        // browser takes its width *and* its divider out of the sum, so the
        // arrangement expands into the whole of the freed space.
        const auto leftWidth = browserOpen ? browserWidth : 0;
        const auto editorX = browserOpen ? leftWidth + browserDividerWidth : 0;
        const auto editorW = std::max(120, getWidth() - editorX);
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
            // A device face has a fixed height, and a Device View shorter than
            // one cuts every device off at its knobs, so that face of the pane
            // has a taller floor than the clip editors.
            const auto paneFloor = lowerPane == LowerPane::devices
                ? std::max(minimumPaneHeight, DeviceRack::minimumHeight) : minimumPaneHeight;
            lowerPaneHeight = juce::jlimit(paneFloor,
                                           std::max(paneFloor, getHeight() - arrangementTop - 150 - toggleStripHeight),
                                           lowerPaneHeight);
            paneH = lowerPaneHeight;
        }
        const auto lowerH = paneH;
        // Flush with the bottom client edge. The pane used to stop twelve
        // pixels short of it, which read as a card sitting on the window
        // rather than as the foot of the workspace.
        const auto lowerTop = getHeight() - lowerH;
        // Where the arrangement is covered from: the Clip and Devices strip
        // sits in the band between the lanes and the pane and moves with the
        // pane as it is dragged. The times that read the timeline are printed
        // inside the panel, in the main row's own lane, so nothing is reserved
        // for them here.
        const auto arrangementBottom = lowerTop - toggleStripHeight;
        // The Info View takes the bottom-left corner of the window whenever it
        // is showing, and the band beside it starts at its right edge rather
        // than running underneath it. Hidden, the band takes the whole width.
        const auto infoArea = infoViewArea();
        const auto infoColumn = !infoArea.isEmpty();
        lowerBandTop = arrangementBottom;
        lowerBandLeft = infoColumn ? browserWidth + browserDividerWidth : 0;
        // Where the browser column stops: above the Info View when there is
        // one, and at the band otherwise.
        workspaceBottom = infoColumn ? infoArea.getY() : arrangementBottom;
        layoutControlBar();
        browser.setVisible(browserOpen);
        // The Info View lives in the browser column, so it goes with it. Only
        // the toggle stays behind.
        infoView.setVisible(infoColumn);
        infoView.setBounds(infoArea.withTrimmedTop(25).reduced(8, 5));
        browser.setBounds(0, browserTop, browserWidth, std::max(0, workspaceBottom - browserTop));
        browserToggle.setToggleState(browserOpen, juce::dontSendNotification);
        // The tooltip says what the click will do, not what the button is.
        browserToggle.setTooltip(browserOpen ? "Hide the browser - the instruments, patterns, samples and "
                                               "effects column down the left of the window."
                                             : "Show the browser - the instruments, patterns, samples and "
                                               "effects column down the left of the window.");
        arrangement.setVisible(!sessionViewOpen);
        djView.setVisible(sessionViewOpen);
        arrangement.setBounds(editorX, arrangementTop, editorW, arrangementH);
        // The arrangement spans the window, so it is told how much of its own
        // foot the pane covers: its main row and its scrollbars ride up to sit
        // above the pane while the lanes behind it stay where they are.
        arrangement.setBottomInset(static_cast<float>(std::max(0, arrangementTop + arrangementH - arrangementBottom)));
        // The DJ view has no such inset, so it simply stops at the band.
        djView.setBounds(editorX, arrangementTop, editorW,
                         std::max(150, arrangementBottom - arrangementTop));
        const auto notes = lowerPane == LowerPane::notes;
        const auto audio = lowerPane == LowerPane::audio;
        const auto devices = lowerPane == LowerPane::devices;
        editorToggle.setToggleState(notes || audio, juce::dontSendNotification);
        editorToggle.setButtonText(audio ? "Audio" : "Clip");
        rackToggle.setToggleState(devices, juce::dontSendNotification);
        // The strip and the pane under it run to both client edges, stopping
        // only at the Info View: a clip editor or a device rack is the thing
        // being worked on, and cutting it off at the browser's edge cost it two
        // hundred pixels of the waveform for a column that is not part of it.
        const auto paneX = lowerBandLeft;
        const auto paneW = std::max(120, getWidth() - paneX);
        editorToggle.setBounds(paneX + 8, arrangementBottom + 5, 52, 24);
        rackToggle.setBounds(paneX + 66, arrangementBottom + 5, 72, 24);
        patternLabel.setVisible(notes || audio);
        patternLabel.setBounds(paneX + 148, arrangementBottom + 5, std::max(80, paneW - 500), 24);
        // The scale, zoom and resolution controls belong to the note editor and
        // mean nothing over a waveform, so they follow it rather than the pane.
        scaleHighlight.setVisible(notes && !session.isPatternDrums());
        if (notes && !session.isPatternDrums())
            scaleHighlight.setBounds(paneX + std::max(260, paneW - 328), arrangementBottom + 7, 146, 20);
        editorZoomOut.setVisible(notes);
        editorZoomIn.setVisible(notes);
        editorResolution.setVisible(notes);
        editorZoomOut.setBounds(paneX + std::max(414, paneW - 174), arrangementBottom + 7, 25, 20);
        editorZoomIn.setBounds(paneX + std::max(443, paneW - 145), arrangementBottom + 7, 25, 20);
        editorResolution.setBounds(paneX + std::max(510, paneW - 78), arrangementBottom + 7, 70, 20);

        grid.setVisible(notes);
        audioClip.setVisible(audio);
        rack.setVisible(devices);
        // The note editor, the audio editor and the Device View are three faces
        // of one pane: they share its rectangle and exactly one of them is ever
        // visible in it, so the pane is as tall as the thing being worked on.
        const juce::Rectangle<int> paneBounds {paneX, lowerTop, paneW, lowerH};
        grid.setBounds(paneBounds);
        audioClip.setBounds(paneBounds);
        rack.setBounds(paneBounds);
        // Everything in the lower pane is layered over the arrangement, which
        // is a sibling that covers the same ground.
        lowerSplitter.setVisible(lowerPaneVisible());
        lowerSplitter.setBounds(paneX, lowerBandTop - 2, paneW, 8);
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
        resizingBrowser = browserOpen && std::abs(event.x - browserWidth) <= 5
                          && event.y >= browserTop && event.y < browserColumnBottom();
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
            const auto track = open ? djView.selectedTrack() : arrangement.selectedTrackIndex();
            if (track >= 0) rack.selectTrack(track);
            logStatus(open ? "DJ view: add a deck and choose what it plays from its name bar; Tab returns to the arrangement"
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
        // Monitoring used to be here, as one setting for the whole document.
        // It is a property of the track now - each one names its own input -
        // so it lives where that input is chosen: the card, and the track menu
        // for a row too short to carry the control.
        menu.addSeparator();
        menu.addItem(4, "Audio settings...");
        // Panic was an exclamation mark on the control bar, which told nobody
        // what it did. It is named here instead, beside the audio settings it
        // restarts. Clear pattern, the other symbol that used to sit up there,
        // was already in this menu.
        menu.addItem(12, "Panic reset audio");
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
                else if (result == 12)
                {
                    safe->session.panicReset();
                    safe->logStatus("Panic reset: stopped transport, reset plugins, restarted audio device");
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
        if constexpr (sessionViewEnabled)
        {
            juce::PopupMenu::Item dj {"DJ View"};
            dj.itemID = 6;
            dj.shortcutKeyDescription = "Tab";
            dj.isTicked = sessionViewOpen;
            menu.addItem(std::move(dj));
        }
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
                else if (result == 6) safe->setSessionViewOpen(!safe->sessionViewOpen);
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
                        "Space  Play/Pause\nTab  Switch between the Arrangement and the DJ view\nCtrl+N  New project\nCtrl+O  Open project\nCtrl+S  Save project\nCtrl+Shift+S  Save as\n"
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
        options.dialogBackgroundColour = palette::sideSurface;
        options.useNativeTitleBar = true;
        audioSettings = options.launchAsync();
    }

    void logStatus(const juce::String& message)
    {
        if (message.isEmpty()) return;
        infoView.setText(message, false);
        juce::Logger::writeToLog("Rhino: " + message);
    }

    // What the Info View says while the pointer is resting on a control.
    //
    // The app floated a tooltip for this. A tooltip covers the thing it is
    // describing, is gone the moment the pointer moves, and holds about two
    // words because that is all a popup over a timeline can carry - so the
    // interesting half of what a control does was never written down anywhere
    // the user could read it. The Info View is already on screen, has room for
    // a sentence, and is where Live puts the same thing.
    //
    // A control still says what it is through setTooltip, which keeps the
    // words beside the control they describe and next to the code that knows
    // when they change. Only where they are shown has moved.
    void updateHint()
    {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto* under = source.isTouch() ? nullptr : source.getComponentUnderMouse();
        // The control directly under the pointer and no ancestor of it: a
        // control that says nothing says nothing deliberately, and must not
        // inherit the explanation of the panel it happens to sit on.
        auto* client = dynamic_cast<juce::TooltipClient*>(under);
        if (client == nullptr) under = nullptr;
        const auto text = client != nullptr ? client->getTooltip() : juce::String();
        // Which of this and the status line wins the panel is the whole of the
        // behaviour, so the rule is stated once in InfoHints.h rather than
        // here: this only remembers what the panel was last told about.
        const auto replaces = infoHintReplaces(hintSource.getComponent(), hintText, under, text);
        hintSource = under;
        hintText = text;
        if (replaces) infoView.setText(text, false);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        const auto playing = session.edit->getTransport().isPlaying();
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
        updateHint();
        // The engine both starts and finishes a recording on the audio thread
        // and broadcasts neither, so the clips it wrote are collected here -
        // the same reason the slot override below is polled rather than
        // listened for.
        session.recordingStopped();
        // The booth's loads finish on a worker and its stale bounces wait
        // for a quiet moment; neither is announced, so both are polled.
        session.djPoll();
        updateRecordButton();
        // The engine raises a track's slot-override flag from the audio thread
        // without broadcasting, so this is polled rather than event-driven.
        if constexpr (sessionViewEnabled)
            if (const auto overriding = session.anyTrackPlayingSlots(); overriding != backToArrangement.isVisible())
            {
                backToArrangement.setVisible(overriding);
                resized();
            }
        const auto seconds = session.edit->getTransport().getPosition().inSeconds();
        updateReadings(seconds);
        // The loop is where the transport will turn, so it heads the column of
        // readings that describe the session rather than follow it.
        position.setLoop(displayLoop ? loopText() : juce::String());
        const auto signature = session.timeSignature();
        position.setTempoAndSignature(
            displayTempo ? juce::String(session.tempo(), 0) : juce::String(),
            displayTimeSignature ? juce::String(signature.numerator) + "/" + juce::String(signature.denominator)
                                 : juce::String());
        // A count-in owns the position while it runs, but the clock, the load
        // meters and the loop keep reading, which is why all of them are
        // updated above this.
        if (session.isCountingIn())
        {
            position.setPosition("COUNT-IN  " + juce::String(session.countInBarsRemaining()));
            return;
        }
        position.setPosition(displayPosition ? placeText(seconds) : juce::String());
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
        position.setClock(displayTime ? formatClock(seconds, true) + "  /  "
                                            + formatClock(session.edit->getLength().inSeconds(), true)
                                      : juce::String());
        juce::StringArray load;
        if (displayCpu) load.add("CPU " + juce::String(juce::roundToInt(cpuLoad * 100.0)) + "%");
        if (displayMemory) load.add("RAM " + formatMemory(memoryBytes));
        position.setStatistics(load.joinIntoString("   "));
        // The device reading is the lowest priority thing in the box: the
        // display drops it first when the window is too narrow to hold
        // everything, before it gives up any of the readings above.
        position.setDeviceInfo(displayAudio ? audioDescription : juce::String());
    }

    // The loop, written exactly as the position above it is - bar, beat and
    // sixteenth - because a loop counted differently from the playhead is a
    // loop nobody can read against it. Looping is always on: with no span
    // dragged on the ruler it is the whole arrangement, so until someone
    // narrows it this reads as the length of the project. The word LOOP is the
    // display's, not this function's; it drops it when the box is tight.
    juce::String loopText() const
    {
        const auto range = session.loopRange();
        return placeText(range.getStart().inSeconds())
             + " - " + placeText(range.getEnd().inSeconds());
    }

    // Bar, count and sixteenth, which is how the position and the loop beside
    // it are both written. One formatter for both, because a loop that counts
    // differently from the playhead is a loop nobody can read against it. The
    // sixteenth is the field that moves while the transport rolls: bars and
    // counts change too slowly to tell a stalled readout from a stopped one.
    juce::String placeText(double seconds) const
    {
        const auto beats = session.edit->tempoSequence
                               .toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
        const auto signature = session.timeSignature();
        const auto place = musicalPosition(beats, signature.numerator, signature.denominator);
        return juce::String(place.bar).paddedLeft('0', 3)
             + "." + juce::String(place.count)
             + "." + juce::String(place.sixteenth);
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

    // The bottom-left corner of the window. It runs to the client edge rather
    // than stopping where the browser does, because the clip and device panel
    // begins to the right of it: the Info View sits beside that panel now,
    // rather than being buried above it.
    juce::Rectangle<int> infoViewArea() const
    {
        if (!infoVisible || !browserOpen) return {};
        const auto top = std::max(browserTop + 96, getHeight() - infoViewHeight);
        return {0, top, browserWidth, std::max(1, getHeight() - top)};
    }

    // The foot of the browser's whole column, Info View included. The divider
    // and the drag that resizes the browser both run the length of it.
    int browserColumnBottom() const
    {
        return infoVisible && browserOpen ? getHeight() : workspaceBottom;
    }

    bool isOverSplitter(juce::Point<float> point) const
    {
        return browserOpen && std::abs(point.x - static_cast<float>(browserWidth)) <= 5.0f
            && point.y >= static_cast<float>(browserTop)
            && point.y < static_cast<float>(browserColumnBottom());
    }

    Session& session;
    juce::Label hint, patternLabel;
    juce::TextEditor infoView;
    // The control the Info View is currently explaining, and what it said about
    // it. Both are needed: the control alone misses a button that relabels
    // itself under the pointer, and the words alone would have the hint fight
    // every status message for the panel.
    juce::Component::SafePointer<juce::Component> hintSource;
    juce::String hintText;
    TransportDisplay position;
    BrowserPanel browser;
    StepGrid grid;
    Arrangement arrangement;
    DjView djView;
    DeviceRack rack;
    AudioClipPanel audioClip;
    ValueDragBox tempoBox;
    TimeSignatureField signatureField;
    // Every control on the bar is a glyph on the bar's own surface. Clear and
    // panic used to sit here as an x and an exclamation mark, which said
    // nothing about what either one did; both are in the Edit menu now, where
    // they are spelt out. Nothing was dropped - see showEditMenu.
    IconButton metronome {"Metronome"}, metronomeMenu {"Metronome settings"};
    IconButton undo {"Undo"}, redo {"Redo"};
    IconButton play {"Play"}, rewind {"Return to start"};
    StopButton stop;
    RecordButton record;
    IconButton browserToggle {"Browser"};
    juce::TextButton editorToggle {"Clip"}, rackToggle {"Devices"};
    juce::TextButton sessionToggle {"DJ"}, arrangementToggle {"Arrange"};
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
    // Three results of the layout, written by resized() and read by paint and
    // by the hit tests so that the panels, the ground under them and the
    // pointer all agree about where one region ends and the next begins.
    // Where the browser column stops; the top of the band under the
    // arrangement, which is the time ruler and the Clip and Devices strip; and
    // that band's left edge, which is the Info View's right edge when the Info
    // View is showing and the client edge when it is not.
    int workspaceBottom = 0, lowerBandTop = 0, lowerBandLeft = 0;
    int resizeStartLowerPaneHeight = 0;
    // The control bar's own grid. One row of controls on one band: every
    // control shares a vertical centre, and the three gaps below are the whole
    // of the grouping - a wide gap between groups, a narrow one inside a group,
    // and a narrower one still between the transport's four glyphs, which read
    // as one control with four parts.
    static constexpr int controlBarHeight = 72;
    static constexpr int fieldHeight = 34;
    static constexpr int transportSize = 34;
    static constexpr int undoSize = 28;
    static constexpr int barControlTop = (controlBarHeight - fieldHeight) / 2;
    // A fourth gap, wider than the rest, wherever a hairline rules one section
    // of the bar off from the next. The sections used to be a group gap apart
    // and the rule between them did all the separating on its own, which put
    // the tempo hard against the browser toggle and the transport hard against
    // the tempo. This is the widest it opens to; it closes back towards
    // barGroupGap as the window narrows - see layoutControlBar.
    static constexpr int barSectionGap = 64;
    static constexpr int barGroupGap = 18;
    static constexpr int barFieldGap = 18;
    static constexpr int barTransportGap = 3;
    static constexpr int barEdgeMargin = 12;
    static constexpr int barDividerTop = 18;
    // The widths the bar's own controls take. The readout is a panel of a fixed
    // size now rather than whatever the window had left over: stretched across
    // a wide display it was a bezel with three small readings adrift in it, and
    // the digits it is built around never grew with it.
    static constexpr int browserToggleWidth = 34;
    static constexpr int tempoWidth = 92;
    static constexpr int signatureWidth = 72;
    static constexpr int metronomeMenuWidth = 18;
    // Wide enough for the position, the clock beside it and one column of
    // readings against the right edge - which is what the load meters need to
    // survive the box narrowing. Below this the readout starts dropping them.
    static constexpr int displayWidth = 480;
    // Taller than the controls beside it, which is what makes it the focal
    // point now that it is no longer the widest thing on the bar.
    static constexpr int displayHeight = 52;
    static constexpr int displayTop = (controlBarHeight - displayHeight) / 2;
    // The gutter between the browser and the arrangement, which is also the
    // handle that resizes the browser.
    static constexpr int browserDividerWidth = 3;
    static constexpr int browserTop = controlBarHeight;
    // A pane shorter than this shows no usable clip editor. The Device View's
    // floor is taller, DeviceRack::minimumHeight, so it shows a whole device.
    static constexpr int minimumPaneHeight = 112;
    static constexpr int infoViewHeight = 132;
    // Room under the arrangement for the Clip and Devices toggles, which stay
    // put whether or not the pane they open is showing. The arrangement stops
    // at the top of the strip and the pane, when it is open, slides out from
    // under the bottom of it.
    static constexpr int toggleStripHeight = 34;
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
    // Where the bar rules off one group from the next. Recorded by the layout,
    // because only the layout knows where the groups ended up.
    std::vector<int> barDividers;
    bool updatingEditorResolution = false;
    // The tempo and the signature have fields of their own in the bar now, so
    // the readout no longer repeats them: it is the position, the clock and
    // the loop. Both are still in the display menu for anyone who wants them
    // back on the one line their eye is already on while playing.
    // Tempo and signature were off by default because the old readout had one
    // line and putting them on it shoved the position sideways. The middle
    // column is where they go now, so they are on: they are what the display
    // is asked for most often after the position itself.
    bool displayPosition = true, displayTempo = true, displayTimeSignature = true, displayLoop = true;
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
        // A component snapshot has no audio path, so it stays available while
        // another Rhino instance owns the Windows audio backend. The normal
        // workflow test below still prepares audio for its render scenarios.
        if (args == "--arp-snapshot")
        {
            Session::setCommandLineTestMode(true);
            setApplicationReturnValue(runArpSnapshotTest());
            quit();
            return;
        }
        // Renders every device offline, so it opens no audio device either.
        if (args == "--device-test")
        {
            Session::setCommandLineTestMode(true);
            setApplicationReturnValue(runDeviceConformance());
            quit();
            return;
        }
        // Timings, not checks, and no audio device: it measures the message
        // thread, and leaves the audio backend to any Rhino already running.
        if (args == "--profile-ui")
        {
            Session::setCommandLineTestMode(true);
            setApplicationReturnValue(runUiProfile());
            quit();
            return;
        }
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
                // RHINO_SHELL_SNAPSHOT_SIZE=960x680 renders it at a size of
                // your choosing first, which is how the smallest supported
                // window gets looked at without resizing the real one.
                if (const auto shellShot = juce::SystemStats::getEnvironmentVariable("RHINO_SHELL_SNAPSHOT", {});
                    startupTest && shellShot.isNotEmpty())
                    if (auto* content = window->getContentComponent())
                    {
                        const auto size = juce::SystemStats::getEnvironmentVariable("RHINO_SHELL_SNAPSHOT_SIZE", {});
                        if (const auto cross = size.indexOfChar('x'); cross > 0)
                            content->setSize(std::max(200, size.substring(0, cross).getIntValue()),
                                             std::max(200, size.substring(cross + 1).getIntValue()));
                        writeSnapshot(*content, juce::File(shellShot));
                    }
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
                // A lit key rather than brighter lettering alone: the menus sit
                // on the same ground as the caption buttons beside them and are
                // answered the same way, so the whole bar responds alike.
                if (highlighted || pressed)
                {
                    g.setColour(palette::hover.brighter(pressed ? 0.3f : 0.1f));
                    g.fillRect(getLocalBounds());
                }
                auto colour = findColour(juce::TextButton::textColourOffId);
                if (highlighted || pressed) colour = colour.brighter(0.25f);
                g.setColour(colour);
                g.setFont(uiFont(10.5f));
                drawSnappedText(g, getButtonText(), getLocalBounds(), juce::Justification::centred);
            }
        };

        // The title bar takes the control bar's own tone, so the two read as
        // one band of chrome across the top of the window rather than as a
        // frame with a toolbar inside it.
        Window() : DocumentWindow({}, palette::globalBar, allButtons)
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
                menu->setColour(juce::TextButton::textColourOffId, palette::text);
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
