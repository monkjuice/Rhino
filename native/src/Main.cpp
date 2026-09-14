#include "Session.h"
#include "StepGrid.h"
#include "ProjectFiles.h"
#include "Theme.h"
#include "Arrangement.h"
#include "SessionView.h"
#include "BrowserPanel.h"
#include "DeviceRack.h"
#include "Playhead.h"
#include "StartupScreen.h"
#include "TransportDisplay.h"
#include <cmath>
#include <functional>
#include <stdexcept>

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

namespace theta
{
juce::File thetaLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Theta").getChildFile("theta.log");
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
            juce::Logger::writeToLog("Theta: using Windows Audio instead of legacy DirectSound");
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
    te::Engine testEngine {"Theta Native Tests"};
    avoidLegacyDirectSound(testEngine);
    testEngine.getDeviceManager().deviceManager.closeAudioDevice();
   #endif
}

void registerThetaProjectFileAssociation()
{
   #if JUCE_WINDOWS
    constexpr auto registryRoot = "HKEY_CURRENT_USER\\Software\\Classes\\";
    constexpr auto projectType = "Theta.Project";
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().quoted();
    const auto typeKey = juce::String(registryRoot) + projectType;
    const auto extensionKey = juce::String(registryRoot) + ".thetaedit\\";
    const auto associated = juce::WindowsRegistry::setValue(extensionKey, projectType)
                         && juce::WindowsRegistry::setValue(typeKey + "\\", "Theta project")
                         && juce::WindowsRegistry::setValue(typeKey + "\\DefaultIcon\\", executable + ",0")
                         && juce::WindowsRegistry::setValue(typeKey + "\\shell\\open\\command\\", executable + " \"%1\"");
    if (associated)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    else
        juce::Logger::writeToLog("Theta: could not register .thetaedit file association");
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

class ControlWindow final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private Session::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit ControlWindow(Session& s) : session(s), browser(s), grid(s), arrangement(s), sessionView(s), rack(s), files(s)
    {
        setOpaque(true);
        files.status = [this](const juce::String& message) { logStatus(message); };
        files.loadingChanged = [this](bool loading) { setEnabled(!loading); };
        arrangement.status = files.status;
        arrangement.trackSelected = [this](int track) { rack.selectTrack(track); };
        sessionView.status = files.status;
        sessionView.trackSelected = [this](int track) { rack.selectTrack(track); };
        sessionToggle.setButtonText("Session");
        arrangementToggle.setButtonText("Arrange");
        sessionToggle.setTooltip("Show the Session view clip launcher");
        arrangementToggle.setTooltip("Show the Arrangement timeline");
        sessionToggle.onClick = [this] { setSessionViewOpen(true); };
        arrangementToggle.onClick = [this] { setSessionViewOpen(false); };
        browser.status = files.status;
        rack.status = files.status;
        browserToggle.onClick = [this] { browserOpen = !browserOpen; resized(); repaint(); };
        editorToggle.onClick = [this]
        {
            clipEditorOpen = !clipEditorOpen;
            if (!clipEditorOpen && !rackOpen) rackOpen = true;
            resized();
            repaint();
        };
        rackToggle.onClick = [this]
        {
            rackOpen = !rackOpen;
            if (!rackOpen && !clipEditorOpen) clipEditorOpen = true;
            resized();
            repaint();
        };
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
                                                                     &sessionToggle, &arrangementToggle})
        {
            toggle->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b31));
            toggle->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff38505b));
            toggle->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaeb8c1));
            toggle->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffdce5ea));
        }
        logStatus("PATTERN 1  /  4OSC     Draw notes, then press Play");
        gainLabel.setText("PATTERN TRACK", juce::dontSendNotification);
        audioGainLabel.setText("AUDIO 1 TRACK", juce::dontSendNotification);
        infoView.setMultiLine(true, true);
        infoView.setReadOnly(true);
        infoView.setScrollbarsShown(false);
        infoView.setCaretVisible(false);
        infoView.setWantsKeyboardFocus(false);
        infoView.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1d2228));
        infoView.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        infoView.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc9d1c3));
        infoView.setFont(juce::FontOptions(13.0f));
        hint.setText({}, juce::dontSendNotification);
        hint.setVisible(false);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff8d98a3));
        tempo.setSliderStyle(juce::Slider::IncDecButtons);
        tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 30);
        tempo.setRange(40.0, 240.0, 1.0);
        tempo.setValue(session.tempo(), juce::dontSendNotification);
        tempo.setTextValueSuffix(" BPM");
        tempo.onValueChange = [this] { session.setTempo(tempo.getValue()); };
        timeSignature.addItem("3 / 4", 304);
        timeSignature.addItem("4 / 4", 404);
        timeSignature.addItem("5 / 4", 504);
        timeSignature.addItem("6 / 8", 608);
        timeSignature.addItem("7 / 8", 708);
        timeSignature.addItem("9 / 8", 908);
        timeSignature.addItem("12 / 8", 1208);
        timeSignature.setTooltip("Project time signature");
        timeSignature.onChange = [this]
        {
            const auto value = timeSignature.getSelectedId();
            const auto result = session.setTimeSignature(value / 100, value % 100);
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
        gain.setSliderStyle(juce::Slider::LinearHorizontal);
        gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 26);
        gain.setRange(-60.0, 6.0, 0.1);
        gain.setValue(session.utility->gain().getCurrentValue(), juce::dontSendNotification);
        gain.setTextValueSuffix(" dB");
        gain.setDoubleClickReturnValue(true, 0.0);
        gain.onDragStart = [this]
        {
            session.edit->getUndoManager().beginNewTransaction("Synth gain");
            session.utility->gain().parameterChangeGestureBegin();
        };
        gain.onDragEnd = [this]
        {
            session.utility->gain().parameterChangeGestureEnd();
            session.edit->getUndoManager().beginNewTransaction();
        };
        gain.onValueChange = [this]
        {
            session.utility->gain().setParameter(static_cast<float>(gain.getValue()), juce::sendNotification);
            session.markModified();
        };
        audioGain.setSliderStyle(juce::Slider::LinearHorizontal);
        audioGain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 26);
        audioGain.setRange(-60.0, 6.0, 0.1);
        audioGain.setValue(session.audioUtility->gain().getCurrentValue(), juce::dontSendNotification);
        audioGain.setTextValueSuffix(" dB");
        audioGain.setDoubleClickReturnValue(true, 0.0);
        audioGain.onDragStart = [this]
        {
            session.edit->getUndoManager().beginNewTransaction("Audio gain");
            session.audioUtility->gain().parameterChangeGestureBegin();
        };
        audioGain.onDragEnd = [this]
        {
            session.audioUtility->gain().parameterChangeGestureEnd();
            session.edit->getUndoManager().beginNewTransaction();
        };
        audioGain.onValueChange = [this]
        {
            session.audioUtility->gain().setParameter(static_cast<float>(audioGain.getValue()), juce::sendNotification);
            session.markModified();
        };
        play.onClick = [this] { session.togglePlayback(); };
        stop.onClick = [this] { session.stop(); };
        panic.onClick = [this]
        {
            session.panicReset();
            logStatus("Panic reset: stopped transport, reset plugins, restarted audio device");
        };
        play.setButtonText(L"\u25b6");
        stop.setButtonText(L"\u25a0");
        panic.setButtonText("!");
        play.setTooltip("Play or pause");
        stop.setTooltip("Stop and return to start");
        panic.setTooltip("Panic reset audio");
        for (auto* component : std::initializer_list<juce::Component*>{
                 &infoView, &position, &gainLabel, &gain, &audioGainLabel, &audioGain, &play, &stop, &panic,
                 &browser, &browserToggle, &editorToggle, &rackToggle, &grid, &arrangement, &sessionView,
                 &sessionToggle, &arrangementToggle, &rack, &tempo, &timeSignature, &undo, &redo, &clear, &metronome, &metronomeMenu, &hint,
                 &patternLabel, &editorResolution, &editorZoomOut, &editorZoomIn, &scaleHighlight})
            addAndMakeVisible(component);
        session.edit->getTransport().addChangeListener(this);
        session.addChangeListener(this);
        session.listeners.add(this);
        session.edit->getUndoManager().addChangeListener(this);
        patternLabel.setText("PATTERN 1  /  NOTE EDITOR", juce::dontSendNotification);
        patternLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4aa));
        setSize(1280, 900);
        changeListenerCallback(nullptr);
        // This updates a text readout only. Pointer events and control painting
        // are not throttled to this timer; there is no full-window repaint loop.
        startTimerHz(30);
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
        const auto bottomX = (browserOpen ? browserWidth : collapsedRailWidth) + 18;
        g.setColour(juce::Colour(0xff24282d));
        g.fillRect(bottomX, getHeight() - 64, getWidth() - bottomX - 24, 44);
        if (!browserOpen)
        {
            g.setColour(juce::Colour(0xff11161b));
            g.fillRect(0, browserTop, collapsedRailWidth, getHeight() - browserTop);
        }
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
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("INFO VIEW   ?  HIDE", area.withTrimmedLeft(10).withHeight(24), juce::Justification::centredLeft);
        }
        if (clipEditorOpen && rackOpen)
        {
            g.setColour(juce::Colour(0xff3a434b));
            g.fillRect(deviceSplitterBounds());
        }
        g.setColour(juce::Colour(0xff3a434b));
        g.fillRect(arrangementSplitterBounds());
    }

    void resized() override
    {
        constexpr int gap = 18;
        const auto leftWidth = browserOpen ? browserWidth : collapsedRailWidth;
        const auto editorX = leftWidth + gap;
        const auto editorW = getWidth() - editorX - 24;
        // The transport is a full-width bar. Both the browser and arrangement
        // begin below it, so their top edges remain aligned.
        const auto arrangementTop = browserTop;
        arrangementHeight = juce::jlimit(150, std::max(150, getHeight() - 350), arrangementHeight);
        const auto arrangementBottom = arrangementTop + arrangementHeight;
        const auto lowerTop = arrangementBottom + 34;
        const auto bottomPanelTop = getHeight() - 64;
        const auto lowerH = std::max(112, bottomPanelTop - lowerTop - 14);
        const auto displayWidth = juce::jlimit(240, 320, getWidth() / 4);
        const auto displayX = getWidth() / 2 - displayWidth / 2;
        // Keep the control bar as one visual cluster. The browser may resize,
        // but transport should remain beside the display rather than drifting
        // to the arrangement's left edge.
        const auto transportX = std::max(152, displayX - 294);
        sessionToggle.setBounds(48, 34, 50, 30);
        arrangementToggle.setBounds(98, 34, 50, 30);
        sessionToggle.setToggleState(sessionViewOpen, juce::dontSendNotification);
        arrangementToggle.setToggleState(!sessionViewOpen, juce::dontSendNotification);
        play.setBounds(transportX, 34, 38, 30);
        stop.setBounds(transportX + 58, 34, 38, 30);
        panic.setBounds(transportX + 116, 34, 38, 30);
        position.setBounds(displayX, 21, displayWidth, 56);
        constexpr int rightControlsWidth = 356;
        auto rightX = std::min(displayX + displayWidth + 52, getWidth() - 24 - rightControlsWidth);
        rightX = std::max(displayX + displayWidth + 8, rightX);
        tempo.setBounds(rightX, 34, 100, 30);
        rightX += 108;
        timeSignature.setBounds(rightX, 34, 70, 30);
        rightX += 78;
        undo.setBounds(rightX, 34, 34, 30);
        rightX += 40;
        redo.setBounds(rightX, 34, 34, 30);
        rightX += 40;
        clear.setBounds(rightX, 34, 34, 30);
        rightX += 42;
        metronome.setBounds(rightX, 34, 30, 30);
        metronomeMenu.setBounds(rightX + 30, 34, 18, 30);
        browser.setVisible(browserOpen);
        const auto infoArea = infoViewArea();
        infoView.setVisible(infoVisible);
        infoView.setBounds(infoArea.withTrimmedTop(25).reduced(8, 5));
        browser.setBounds(0, browserTop, browserWidth, (infoVisible ? infoArea.getY() : getHeight()) - browserTop);
        browserToggle.setToggleState(browserOpen, juce::dontSendNotification);
        browserToggle.setBounds(0, 21, 44, 56);
        arrangement.setVisible(!sessionViewOpen);
        sessionView.setVisible(sessionViewOpen);
        arrangement.setBounds(editorX, arrangementTop, editorW, arrangementHeight);
        sessionView.setBounds(editorX, arrangementTop, editorW, arrangementHeight);
        editorToggle.setToggleState(clipEditorOpen, juce::dontSendNotification);
        rackToggle.setToggleState(rackOpen, juce::dontSendNotification);
        editorToggle.setBounds(editorX, arrangementBottom + 10, 48, 22);
        rackToggle.setBounds(editorX + 54, arrangementBottom + 10, 72, 22);
        patternLabel.setVisible(clipEditorOpen);
        patternLabel.setBounds(editorX + 136, arrangementBottom + 10, std::max(80, editorW - 500), 24);
        scaleHighlight.setVisible(clipEditorOpen && !session.isPatternDrums());
        if (clipEditorOpen && !session.isPatternDrums())
            scaleHighlight.setBounds(editorX + std::max(260, editorW - 328), arrangementBottom + 12, 146, 20);
        editorZoomOut.setVisible(clipEditorOpen);
        editorZoomIn.setVisible(clipEditorOpen);
        editorResolution.setVisible(clipEditorOpen);
        editorZoomOut.setBounds(editorX + std::max(414, editorW - 174), arrangementBottom + 12, 25, 20);
        editorZoomIn.setBounds(editorX + std::max(443, editorW - 145), arrangementBottom + 12, 25, 20);
        editorResolution.setBounds(editorX + std::max(510, editorW - 78), arrangementBottom + 12, 70, 20);

        grid.setVisible(clipEditorOpen);
        rack.setVisible(rackOpen);
        if (clipEditorOpen && rackOpen)
        {
            deviceViewHeight = juce::jlimit(112, std::max(112, lowerH - 112), deviceViewHeight);
            const auto clipHeight = std::max(0, lowerH - deviceViewHeight - 8);
            grid.setBounds(editorX, lowerTop, editorW, clipHeight);
            rack.setBounds(editorX, lowerTop + clipHeight + 8, editorW, deviceViewHeight);
        }
        else if (clipEditorOpen)
        {
            grid.setBounds(editorX, lowerTop, editorW, lowerH);
            rack.setBounds(editorX, lowerTop + lowerH, editorW, 0);
        }
        else
        {
            grid.setBounds(editorX, lowerTop, editorW, 0);
            rack.setBounds(editorX, lowerTop, editorW, lowerH);
        }
        browserToggle.toFront(false);
        editorToggle.toFront(false);
        rackToggle.toFront(false);
        hint.setBounds(0, 0, 0, 0);
        const auto half = (editorW - 28) / 2;
        gainLabel.setBounds(editorX + 16, getHeight() - 54, 100, 26);
        gain.setBounds(editorX + 112, getHeight() - 54, half - 112, 28);
        audioGainLabel.setBounds(editorX + half + 28, getHeight() - 54, 100, 26);
        audioGain.setBounds(editorX + half + 128, getHeight() - 54, editorW - half - 150, 28);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        setMouseCursor((isOverArrangementSplitter(event.position) || isOverDeviceSplitter(event.position))
            ? juce::MouseCursor::UpDownResizeCursor
            : isOverSplitter(event.position) ? juce::MouseCursor::LeftRightResizeCursor
            : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        resizingBrowser = browserOpen && std::abs(event.x - browserWidth) <= 5 && event.y >= browserTop;
        resizingDeviceView = isOverDeviceSplitter(event.position);
        resizingArrangement = isOverArrangementSplitter(event.position);
        resizeStartX = event.x;
        resizeStartY = event.y;
        resizeStartBrowserWidth = browserWidth;
        resizeStartDeviceViewHeight = deviceViewHeight;
        resizeStartArrangementHeight = arrangementHeight;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (resizingBrowser)
        {
            browserWidth = juce::jlimit(180, 360, resizeStartBrowserWidth + event.x - resizeStartX);
            resized();
            repaint();
        }
        else if (resizingDeviceView)
        {
            const auto available = std::max(112, grid.getHeight() + rack.getHeight() + 8);
            deviceViewHeight = juce::jlimit(112, std::max(112, available - 112),
                                            resizeStartDeviceViewHeight - (event.y - resizeStartY));
            resized();
            repaint();
        }
        else if (resizingArrangement)
        {
            arrangementHeight = juce::jlimit(150, std::max(150, getHeight() - 350), resizeStartArrangementHeight + event.y - resizeStartY);
            resized();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        resizingBrowser = false;
        resizingDeviceView = false;
        resizingArrangement = false;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getTextCharacter() == '?')
        {
            infoVisible = !infoVisible;
            resized();
            repaint();
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
        if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'E')
        {
            files.exportWav();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'F')
        {
            browser.focusSearch();
            return true;
        }
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

    void setSessionViewOpen(bool open)
    {
        if (sessionViewOpen == open) return;
        sessionViewOpen = open;
        logStatus(open ? "Session view: click a clip to launch it"
                       : "Arrangement view");
        resized();
        repaint();
    }

    void requestClose() { files.confirmUnsaved([] { juce::JUCEApplication::getInstance()->quit(); }); }
    void openProjectFile(const juce::File& file) { files.openFile(file); }
    void showFileMenuFrom(juce::Component& target) { showFileMenu(&target); }
    void showEditMenuFrom(juce::Component& target) { showEditMenu(&target); }
    void showHelpMenuFrom(juce::Component& target) { showHelpMenu(&target); }
    std::function<void(const juce::String&)> projectTitleChanged;

private:
    juce::TooltipWindow tooltipWindow {this, 700};
    void showFileMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Open project...", true, false);
        menu.addItem(2, "Save", true, false);
        menu.addItem(3, "Save as...");
        menu.addSeparator();
        menu.addItem(4, "Export WAV...", true, false);
        menu.addSeparator();
        menu.addItem(5, "Quit");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : fileMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)](int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->files.open();
                else if (result == 2) safe->files.save();
                else if (result == 3) safe->files.save(true);
                else if (result == 4) safe->files.exportWav();
                else if (result == 5) safe->requestClose();
            });
    }

    void showEditMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Undo", session.edit->getUndoManager().canUndo(), false);
        menu.addItem(2, "Redo", session.edit->getUndoManager().canRedo(), false);
        menu.addSeparator();
        menu.addItem(3, "Clear pattern");
        menu.addSeparator();
        menu.addItem(4, "Audio settings...");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : editMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)](int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->session.undo();
                else if (result == 2) safe->session.redo();
                else if (result == 3) safe->session.clearPattern();
                else if (result == 4) safe->showAudioSettings();
            });
    }

    void showHelpMenu(juce::Component* target = nullptr)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Keyboard shortcuts");
        menu.addItem(2, "About Theta");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target != nullptr ? *target : helpMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)](int result)
            {
                if (safe == nullptr) return;
                if (result == 1)
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Keyboard shortcuts",
                        "Space  Play/Pause\nCtrl+O  Open project\nCtrl+S  Save project\nCtrl+Shift+S  Save as\n"
                        "Ctrl+Shift+E  Export WAV\nCtrl+Z  Undo\nCtrl+Y / Ctrl+Shift+Z  Redo\nCtrl+F  Search browser\nTab  Session / Arrangement view\n?  Show/hide Info View");
                else if (result == 2)
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "About Theta",
                        "Theta\nA native desktop DAW for patterns, arrangement, and offline WAV export.");
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
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(metronomeMenu),
            [safe = juce::Component::SafePointer<ControlWindow>(this)] (int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->session.setClickTrackEmphasiseBars(!safe->session.clickTrackEmphasiseBars());
                else if (result >= 10 && result <= 12) safe->session.setClickTrackGain(static_cast<float>((result - 12) * 6));
            });
    }

    void showDisplayMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("DISPLAY");
        menu.addItem(1, "Musical position", true, displayPosition);
        menu.addItem(2, "Tempo", true, displayTempo);
        menu.addItem(3, "Time signature", true, displayTimeSignature);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(position),
            [safe = juce::Component::SafePointer<ControlWindow>(this)] (int result)
            {
                if (safe == nullptr) return;
                if (result == 1) safe->displayPosition = !safe->displayPosition;
                else if (result == 2) safe->displayTempo = !safe->displayTempo;
                else if (result == 3) safe->displayTimeSignature = !safe->displayTimeSignature;
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
        juce::Logger::writeToLog("Theta: " + message);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        const auto playing = session.edit->getTransport().isPlaying();
        play.setButtonText(playing ? juce::String(L"\u275a\u275a") : juce::String(L"\u25b6"));
        play.setTooltip(playing ? "Pause" : "Play");
        tempo.setValue(session.tempo(), juce::dontSendNotification);
        const auto signature = session.timeSignature();
        timeSignature.setSelectedId(signature.numerator * 100 + signature.denominator, juce::dontSendNotification);
        metronome.setToggleState(session.clickTrackEnabled(), juce::dontSendNotification);
        if (!gain.isMouseButtonDown())
            gain.setValue(session.utility->gain().getCurrentValue(), juce::dontSendNotification);
        if (!audioGain.isMouseButtonDown())
            audioGain.setValue(session.audioUtility->gain().getCurrentValue(), juce::dontSendNotification);
        undo.setEnabled(session.edit->getUndoManager().canUndo());
        redo.setEnabled(session.edit->getUndoManager().canRedo());
        patternLabel.setText(session.isPatternDrums() ? "PATTERN 1  /  DRUM EDITOR" : "PATTERN 1  /  NOTE EDITOR",
                             juce::dontSendNotification);
        scaleHighlight.setVisible(!session.isPatternDrums());
        {
            const juce::ScopedValueSetter<bool> scope(updatingEditorResolution, true);
            editorResolution.setSelectedId(session.editorStepResolution(), juce::dontSendNotification);
        }
        const auto name = session.projectFile == juce::File{} ? juce::String("Untitled") : session.projectFile.getFileNameWithoutExtension();
        const auto displayName = name + (session.hasUnsavedChanges() ? " *" : "");
        if (projectTitleChanged) projectTitleChanged(displayName);
    }

    void timerCallback() override
    {
        if (session.edit->getTransport().isPlaying())
            session.applyClipAutomationAt(playheadTime(session.edit->getTransport()));
        const auto seconds = session.edit->getTransport().getPosition().inSeconds();
        const auto beat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
        const auto signature = session.timeSignature();
        const auto barLength = session.beatsPerBar();
        const auto bar = static_cast<int>(std::floor(beat / barLength)) + 1;
        const auto beatInBar = static_cast<int>(std::floor(beat - (bar - 1) * barLength)) + 1;
        juce::StringArray parts;
        if (displayPosition) parts.add(juce::String(bar).paddedLeft('0', 3) + "  " + juce::String(beatInBar));
        if (displayTempo) parts.add(juce::String(session.tempo(), 0));
        if (displayTimeSignature) parts.add(juce::String(signature.numerator) + "/" + juce::String(signature.denominator));
        const auto text = parts.joinIntoString("     ");
        position.setDisplayText(text);
    }

    juce::Rectangle<int> deviceSplitterBounds() const
    {
        if (!clipEditorOpen || !rackOpen) return {};
        return {grid.getX(), grid.getBottom() + 2, grid.getWidth(), 4};
    }

    juce::Rectangle<int> arrangementSplitterBounds() const
    {
        return {arrangement.getX(), arrangement.getBottom() + 3, arrangement.getWidth(), 4};
    }

    juce::Rectangle<int> infoViewArea() const
    {
        if (!infoVisible) return {};
        const auto width = browserOpen ? browserWidth : collapsedRailWidth;
        return {0, std::max(browserTop + 96, getHeight() - infoViewHeight), width, infoViewHeight};
    }

    bool isOverSplitter(juce::Point<float> point) const
    {
        return browserOpen && std::abs(point.x - static_cast<float>(browserWidth)) <= 5.0f
            && point.y >= static_cast<float>(browserTop);
    }

    bool isOverArrangementSplitter(juce::Point<float> point) const
    {
        return arrangementSplitterBounds().expanded(0, 4).toFloat().contains(point);
    }

    bool isOverDeviceSplitter(juce::Point<float> point) const
    {
        if (!clipEditorOpen || !rackOpen) return false;
        return deviceSplitterBounds().expanded(0, 4).toFloat().contains(point);
    }

    Session& session;
    juce::Label gainLabel, audioGainLabel, hint, patternLabel;
    juce::TextEditor infoView;
    TransportDisplay position;
    juce::Slider gain, audioGain;
    BrowserPanel browser;
    StepGrid grid;
    Arrangement arrangement;
    SessionView sessionView;
    DeviceRack rack;
    juce::Slider tempo;
    juce::ComboBox timeSignature;
    juce::TextButton metronome, metronomeMenu;
    juce::TextButton undo {"Undo"}, redo {"Redo"}, clear {"Clear"};
    juce::TextButton play {"Play"}, stop {"Stop"}, panic {"Panic"};
    BrowserToggleButton browserToggle;
    juce::TextButton editorToggle {"Clip"}, rackToggle {"Devices"};
    juce::TextButton sessionToggle {"Session"}, arrangementToggle {"Arrange"};
    juce::ComboBox editorResolution;
    juce::TextButton editorZoomOut, editorZoomIn;
    juce::ComboBox scaleHighlight;
    juce::Component::SafePointer<juce::DialogWindow> audioSettings;
    juce::TextButton fileMenu {"File"}, editMenu {"Edit"}, helpMenu {"Help"};
    ProjectFiles files;
    int browserWidth = 244, arrangementHeight = 246, deviceViewHeight = 220;
    int resizeStartX = 0, resizeStartY = 0, resizeStartBrowserWidth = 244;
    int resizeStartArrangementHeight = 246, resizeStartDeviceViewHeight = 220;
    static constexpr int browserTop = 94, collapsedRailWidth = 44;
    static constexpr int infoViewHeight = 132;
    bool browserOpen = true, clipEditorOpen = true, rackOpen = false, infoVisible = true;
    bool sessionViewOpen = false;
    bool resizingBrowser = false, resizingDeviceView = false, resizingArrangement = false;
    bool updatingEditorResolution = false;
    bool displayPosition = true, displayTempo = true, displayTimeSignature = true;
};

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Theta"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override
    {
        const auto logFile = thetaLogFile();
        logFile.getParentDirectory().createDirectory();
        logger = std::make_unique<juce::FileLogger>(logFile, "Theta debug log", 512 * 1024);
        juce::Logger::setCurrentLogger(logger.get());
        juce::Logger::writeToLog("Theta: log started at " + logFile.getFullPathName());
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
            registerThetaProjectFileAssociation();
        const auto requestedProject = juce::File(args.trim().unquoted());
        if (requestedProject.existsAsFile() && requestedProject.hasFileExtension("thetaedit"))
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
        const auto screenshot = juce::SystemStats::getEnvironmentVariable("THETA_STARTUP_SNAPSHOT", {});
        if (startupTest && screenshot.isNotEmpty())
        {
            if (auto stream = juce::File(screenshot).createOutputStream())
                juce::PNGImageFormat().writeImageToStream(loading->createComponentSnapshot(loading->getLocalBounds()), *stream);
        }
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
                session->engine.getDeviceManager().initialise(0, 2);
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
                    window->helpRequested = [safeControls](juce::Component& target)
                    {
                        if (safeControls != nullptr) safeControls->showHelpMenuFrom(target);
                    };
                    controls->projectTitleChanged = [safeWindow](const juce::String& title)
                    {
                        if (safeWindow != nullptr) safeWindow->setProjectTitle(title);
                    };
                    window->setProjectTitle("Untitled");
                }
                if (projectToOpen != juce::File{})
                    if (auto* controls = dynamic_cast<ControlWindow*>(window->getContentComponent()))
                        controls->openProjectFile(projectToOpen);
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
            std::fprintf(stderr, "Theta startup failed: %s\n", error.what());
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
                g.setFont(juce::FontOptions(13.0f));
                g.drawFittedText(getButtonText(), getLocalBounds(), juce::Justification::centred, 1);
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
           #if JUCE_WINDOWS
            // The separate desktop shadow surface trails the right edge during
            // live D2D expansion and exposes a bright one-pixel strip.
            setDropShadowEnabled(false);
           #else
            setDropShadowEnabled(true);
           #endif
            setResizable(true, false);
            projectTitle.setJustificationType(juce::Justification::centred);
            projectTitle.setColour(juce::Label::textColourId, juce::Colours::white);
            projectTitle.setInterceptsMouseClicks(false, false);
            projectTitle.setText("Untitled", juce::dontSendNotification);
            for (auto* menu : {&fileMenu, &editMenu, &helpMenu})
                menu->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7dde2));
            for (auto* component : std::initializer_list<juce::Component*>{&fileMenu, &editMenu, &helpMenu, &projectTitle})
                addAndMakeVisible(component);
            fileMenu.onClick = [this] { if (fileRequested) fileRequested(fileMenu); };
            editMenu.onClick = [this] { if (editRequested) editRequested(editMenu); };
            helpMenu.onClick = [this] { if (helpRequested) helpRequested(helpMenu); };
        }

        void resized() override
        {
            DocumentWindow::resized();
            const auto h = getTitleBarHeight();
            fileMenu.setBounds(10, 0, 42, h);
            editMenu.setBounds(56, 0, 42, h);
            helpMenu.setBounds(102, 0, 44, h);
            projectTitle.setBounds(160, 0, std::max(80, getWidth() - 320), h);
        }

        void setProjectTitle(const juce::String& text) { projectTitle.setText(text, juce::dontSendNotification); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        std::function<void(juce::Component&)> fileRequested, editRequested, helpRequested;

    private:
        TitleMenuButton fileMenu {"File"}, editMenu {"Edit"}, helpMenu {"Help"};
        juce::Label projectTitle;
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
START_JUCE_APPLICATION(theta::Application)
