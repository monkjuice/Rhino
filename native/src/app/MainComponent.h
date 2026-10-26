#pragma once
#include "model/Session.h"
#include "ui/notes/StepGrid.h"
#include "ui/notes/NoteToolsPanel.h"
#include "ProjectFiles.h"
#include "ui/shared/Theme.h"
#include "ui/arrangement/Arrangement.h"
#include "ui/dj/DjView.h"
#include "ui/browser/BrowserPanel.h"
#include "ui/rack/DeviceRack.h"
#include "ui/audioclip/AudioClipPanel.h"
#include "ComputerKeyboard.h"
#include "PushController.h"
#include "TransportDisplay.h"
#include "ControlBarFields.h"
#include "ControlBarIcons.h"
#include "ControlBarButtons.h"
#include "LoadingScreen.h"
#include "ui/preferences/PreferencesPanel.h"
#include <functional>
#include <tuple>
#include <vector>

namespace rhino
{

// The content of the main window: the control bar, the docked browser, the
// arrangement or the DJ view, and the one lower pane. Its members are defined
// across the MainComponent*.cpp units by responsibility.
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private Session::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent(Session& s);
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void paintControlBar(juce::Graphics& g);
    void layoutControlBar();
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setDjViewOpen(bool open);
    void toggleBrowser();
    void toggleClipEditor();
    te::EditItemID clipForPane() const;
    void toggleDeviceView();
    void showDeviceView();
    void followDeck(int deck, bool openPane);
    te::EditItemID selectedAudioClipID() const;
    bool isMidiClipSelected() const;
    void rememberPaneSelection();
    void openClip(te::EditItemID id);
    void refreshEditorPanes();

    // Opening or closing the pane changes the arrangement's height, and the
    // selection that asks for it arrives from a mouse *down* - the same press
    // that may be starting a clip drag. Resizing the lanes underneath that
    // gesture would move the clip out from under the pointer, so the layout
    // waits until the button comes up; the 30 Hz timer applies it.
    void requestPaneLayout() { paneLayoutPending = true; }

    void applyPaneLayout();
    std::tuple<int, int, int, juce::Rectangle<int>, std::vector<int>> paintedLayout() const;
    void updateEditorLabel();
    void toggleInfoView();
    void toggleRecording();

    // Closing the window stops a bounce or an export rather than waiting it
    // out; the loading screen sees it has gone and reports it cancelled.
    void requestClose()
    {
        session.cancelBounce();
        session.cancelExport();
        files.confirmUnsaved([] { juce::JUCEApplication::getInstance()->quit(); });
    }
    void openProjectFile(const juce::File& file) { files.openFile(file); }
    void showFileMenuFrom(juce::Component& target) { showFileMenu(target); }
    void showEditMenuFrom(juce::Component& target) { showEditMenu(target); }
    void showViewMenuFrom(juce::Component& target) { showViewMenu(target); }
    void showHelpMenuFrom(juce::Component& target) { showHelpMenu(target); }
    // The typing keyboard has to be heard wherever focus is, including where
    // it is nowhere: keys then go to the window, which is above this component
    // and so is never reached by walking up from here.
    void listenForKeysOn(juce::Component& component) { computerKeyboard.listenTo(component); }
    // Takes a connected Ableton Push, and any plugged in later, and opens the
    // keyboards that have a velocity curve. The shell calls it once it is up;
    // the test runners never do, so a test run never takes a controller or a
    // keyboard from whoever is playing it.
    void startHardwareControllers()
    {
        push.start();
        session.startKeyboardPorts();
    }
    std::function<void(const juce::String&)> projectTitleChanged;
    // Full screen is the window's business, not its content's: the shell wires
    // these to the document window that owns this component.
    std::function<void()> fullScreenToggleRequested;
    std::function<bool()> fullScreenActive;

private:
    void showFileMenu(juce::Component& target);
    void sendClipboardShortcut(int keyCode, bool toNoteEditor);
    void showEditMenu(juce::Component& target);
    void showViewMenu(juce::Component& target);
    void showHelpMenu(juce::Component& target);
    void showMetronomeMenu();
    void showDisplayMenu();
    void editWillChange() override;
    void editDidChange() override;
    // One window for what belongs to the machine, opened on the tab asked for.
    // Audio settings in the Edit menu is the Audio tab; Ctrl+, opens the tab
    // last looked at.
    void showPreferences(PreferencesPanel::Tab tab);
    void logStatus(const juce::String& message);
    void updateHint();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void updateRecordButton();
    void timerCallback() override;
    void updateReadings(double seconds);
    juce::String loopText() const;
    juce::String placeText(double seconds) const;
    juce::String audioDeviceText() const;
    juce::Rectangle<int> infoViewArea() const;
    int browserColumnBottom() const;
    bool isOverSplitter(juce::Point<float> point) const;

    Session& session;
    juce::Label patternLabel;
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
    // Down the note editor's left in the same pane, after the grid it acts on.
    NoteToolsPanel noteTools {session, grid};
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
    ViewToggle djToggle {"DJ", BinaryData::dj_view_svg, BinaryData::dj_view_svgSize};
    ViewToggle arrangementToggle {"Arrange", BinaryData::arrangement_view_svg, BinaryData::arrangement_view_svgSize};
    juce::ComboBox editorResolution;
    juce::TextButton editorZoomOut, editorZoomIn;
    ComputerKeyboard computerKeyboard;
    juce::Component::SafePointer<juce::DialogWindow> preferences;
    PreferencesPanel::Tab lastPreferencesTab = PreferencesPanel::Tab::audio;
    ProjectFiles files;
    // Over the whole window while a bounce or an export renders, so the window
    // draws and nothing can be edited under the render.
    LoadingScreen loadingScreen;
    SplitterBar lowerSplitter;
    // Zero until the pane is opened for the first time, which is what asks for
    // a fifth of the window rather than a number chosen here: the right height
    // depends on the display and cannot be known at construction.
    int browserWidth = 244, lowerPaneHeight = 0;
    // Whether the note editor has been shown yet this run, and whether the
    // pane has been dragged to a height of someone's choosing; see resized.
    bool noteEditorHasOpened = false, paneHeightDragged = false;
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
    // One pane showing one thing. Both start hidden: double-clicking a clip
    // reveals the editor that clip belongs in, and clicking a track card the
    // devices. The clip editors and the Device View used to
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
    // The clip the last deck followed handed the lower pane.
    te::EditItemID followedClip;
    bool browserOpen = true, infoVisible = true;
    bool djViewOpen = false;
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
    // Last, so it goes first: it closes its port and ends what its encoders
    // hold while every panel it talks to is still here.
    PushController push;
};

}
