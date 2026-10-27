#include "MainComponent.h"
#include "ui/shared/ClipDrag.h"

namespace rhino
{

bool MainComponent::keyPressed(const juce::KeyPress& key)
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
    // repeats that choice instead of asking again, and starts on MIDI. The
    // track lands under the arrangement's selected card either way.
    // Ctrl+Alt+T adds a return instead, as it does in Live.
    if (key.getModifiers().isCommandDown() && key.getModifiers().isAltDown() && key.getKeyCode() == 'T')
    {
        arrangement.insertReturnTrack();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'T')
    {
        arrangement.insertTrack(Session::lastAddedTrackType());
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == ',')
    {
        showPreferences(lastPreferencesTab);
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
    // Bounce reaches the shell for the same reason. The DJ view hides the
    // selection it would act on, so there it says so instead.
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'B')
    {
        if (djViewOpen) logStatus("Bouncing is the arrangement's: press Tab to return to it");
        else arrangement.bounceSelected();
        return true;
    }
    // Looping the selection reaches the shell for the same reason. The note
    // editor's own Ctrl+L loops its clip and never reaches here.
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'L')
    {
        if (djViewOpen) logStatus("Looping the selection is the arrangement's: press Tab to return to it");
        else arrangement.loopSelection();
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
    if (key.getKeyCode() == juce::KeyPress::tabKey && !key.getModifiers().isAnyModifierKeyDown())
    {
        setDjViewOpen(!djViewOpen);
        return true;
    }
    // In the DJ view the song's transport is out of reach: Space is the
    // focused deck's play key, and F9 waits for the arrangement too.
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (djViewOpen) djView.togglePlayFocused();
        else session.togglePlayback();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F9Key)
    {
        if (djViewOpen) logStatus("Recording is the arrangement's: press Tab to return to it");
        else toggleRecording();
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

void MainComponent::showFileMenu(juce::Component& target)
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
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
void MainComponent::sendClipboardShortcut(int keyCode, bool toNoteEditor)
{
    auto& editor = toNoteEditor ? static_cast<juce::Component&>(grid)
                                : static_cast<juce::Component&>(arrangement);
    editor.grabKeyboardFocus();
    editor.keyPressed(juce::KeyPress(keyCode, juce::ModifierKeys::commandModifier, 0));
}

void MainComponent::showEditMenu(juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Undo", session.canUndo(), false);
    menu.addItem(2, "Redo", session.canRedo(), false);
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
    juce::PopupMenu::Item preferencesItem {"Preferences..."};
    preferencesItem.itemID = 13;
    preferencesItem.shortcutKeyDescription = "Ctrl+,";
    menu.addItem(std::move(preferencesItem));
    // The way to the Audio tab it always was, kept where people look for it.
    menu.addItem(4, "Audio settings...");
    // Panic was an exclamation mark on the control bar, which told nobody
    // what it did. It is named here instead, beside the audio settings it
    // restarts. Clear pattern, the other symbol that used to sit up there,
    // was already in this menu.
    menu.addItem(12, "Panic reset audio");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
        [safe = juce::Component::SafePointer<MainComponent>(this), noteEditorHasFocus](int result)
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
            else if (result == 4) safe->showPreferences(PreferencesPanel::Tab::audio);
            else if (result == 13) safe->showPreferences(safe->lastPreferencesTab);
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

void MainComponent::showViewMenu(juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Browser", true, browserOpen);
    menu.addItem(2, lowerPane == LowerPane::audio ? "Audio Editor" : "Clip Editor",
                 true, clipPaneOpen());
    menu.addItem(3, "Device View", true, lowerPane == LowerPane::devices);
    menu.addItem(4, "Info View", browserOpen, infoVisible && browserOpen);
    juce::PopupMenu::Item dj {"DJ View"};
    dj.itemID = 6;
    dj.shortcutKeyDescription = "Tab";
    dj.isTicked = djViewOpen;
    menu.addItem(std::move(dj));
    menu.addSeparator();
    juce::PopupMenu::Item fullScreen {"Full Screen"};
    fullScreen.itemID = 5;
    fullScreen.shortcutKeyDescription = "F12";
    fullScreen.isEnabled = fullScreenToggleRequested != nullptr;
    fullScreen.isTicked = fullScreenActive != nullptr && fullScreenActive();
    menu.addItem(std::move(fullScreen));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
        {
            if (safe == nullptr) return;
            if (result == 1) safe->toggleBrowser();
            else if (result == 2) safe->toggleClipEditor();
            else if (result == 3) safe->toggleDeviceView();
            else if (result == 4) safe->toggleInfoView();
            else if (result == 5 && safe->fullScreenToggleRequested) safe->fullScreenToggleRequested();
            else if (result == 6) safe->setDjViewOpen(!safe->djViewOpen);
        });
}

void MainComponent::showHelpMenu(juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Keyboard shortcuts");
    menu.addItem(2, "About Rhino");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
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
                    "Right-click in the MIDI editor  Options for the notes or the grid there\n"
                    "Alt while dragging a note or its edge  Place it off the grid\n"
                    "Click a key of the MIDI editor  Select its notes\n"
                    "Ctrl+X / Ctrl+C / Ctrl+V  Cut, copy and paste the selection, or whole tracks from their cards\n"
                    "Ctrl+D  Duplicate it directly after itself\nCtrl+E  Split the selected clip at the playhead\n"
                    "Ctrl+E in the MIDI editor  Chop the selected notes on the grid; keep Ctrl down and drag up for more parts, down for fewer\n"
                    "Ctrl+J  Merge the selected audio clips into one audio file\n"
                    "Ctrl+B  Bounce the selection, the selected clips, tracks or group to new audio tracks\n"
                    "Ctrl+L  Loop the selection; again to loop the whole song\n"
                    "R  Play the selected audio clips backwards\nDelete  Empty the selection, or delete the selected tracks\n"
                    "?  Show/hide Info View\nF12  Full screen\nCtrl+,  Preferences: audio device, MIDI keyboards");
            else if (result == 2)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "About Rhino",
                    "Rhino\nA native desktop DAW for patterns, arrangement, and offline WAV export.");
        });
}

void MainComponent::showPreferences(PreferencesPanel::Tab tab)
{
    lastPreferencesTab = tab;
    if (preferences != nullptr)
    {
        if (auto* panel = dynamic_cast<PreferencesPanel*>(preferences->getContentComponent()))
            panel->showTab(tab);
        preferences->toFront(true);
        return;
    }
    auto panel = std::make_unique<PreferencesPanel>(session, tab);
    panel->onTabShown = [safe = juce::Component::SafePointer<MainComponent>(this)](PreferencesPanel::Tab shown)
    {
        if (safe != nullptr)
            safe->lastPreferencesTab = shown;
    };
    panel->setSize(PreferencesPanel::defaultWidth, PreferencesPanel::defaultHeight);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel.release());
    options.dialogTitle = "Preferences";
    options.dialogBackgroundColour = palette::sideSurface;
    options.useNativeTitleBar = true;
    preferences = options.launchAsync();
}

}
