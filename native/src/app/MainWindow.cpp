#include "MainWindow.h"
#include <algorithm>

namespace rhino
{

// The title bar takes the control bar's own tone, so the two read as
// one band of chrome across the top of the window rather than as a
// frame with a toolbar inside it.
MainWindow::MainWindow() : DocumentWindow({}, palette::globalBar, allButtons)
{
    // Keep the frame and content in JUCE's single client-area layout.
    // Native Windows non-client bounds can put the title bar above the
    // display work area when a constrained window is resized/snapped.
    setUsingNativeTitleBar(false);
    setTitleBarHeight(captionHeight);
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

// None of this recreates the native window: the title bar is Rhino's own, so
// its height, buttons and resizability are only layout.
void MainWindow::setTitleBarShown(bool shouldShow)
{
    setTitleBarHeight(shouldShow ? captionHeight : 0);
    setTitleBarButtonsRequired(shouldShow ? allButtons : 0, false);
    setResizable(shouldShow, false);
    for (auto* component : std::initializer_list<juce::Component*>{&fileMenu, &editMenu, &viewMenu, &helpMenu, &projectTitle})
        component->setVisible(shouldShow);
}

// Rhino keeps its title bar in full screen, where JUCE's kiosk mode
// would hand the whole window to the content. These three overrides put
// the bar back: its strip is painted, its buttons are placed, and the
// content starts below it, exactly as in a windowed session.
juce::Rectangle<int> MainWindow::titleBarBounds() const
{
    const auto border = getBorderThickness();
    return {border.getLeft(), border.getTop(), getWidth() - border.getLeftAndRight(), getTitleBarHeight()};
}

juce::BorderSize<int> MainWindow::getContentComponentBorder() const
{
    auto border = DocumentWindow::getContentComponentBorder();
    if (isKioskMode()) border.setTop(border.getTop() + getTitleBarHeight());
    return border;
}

void MainWindow::paint(juce::Graphics& g)
{
    DocumentWindow::paint(g);
    if (!isKioskMode()) return;
    const auto bar = titleBarBounds();
    g.reduceClipRegion(bar);
    g.setOrigin(bar.getPosition());
    getLookAndFeel().drawDocumentWindowTitleBar(*this, g, bar.getWidth(), bar.getHeight(),
                                                6, std::max(1, bar.getWidth() - 12), nullptr, false);
}

void MainWindow::resized()
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

// In full screen the title bar is still there, so its maximise button
// has to lead back out rather than maximise a window that already
// covers the display.
void MainWindow::maximiseButtonPressed()
{
    if (isAppFullScreen()) setAppFullScreen(false);
    else DocumentWindow::maximiseButtonPressed();
}

// The content handles F12 whenever something inside it holds keyboard
// focus. This catches the case where nothing does, as on a fresh start.
bool MainWindow::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F12Key)
    {
        setAppFullScreen(!isAppFullScreen());
        return true;
    }
    return DocumentWindow::keyPressed(key);
}

void MainWindow::setAppFullScreen(bool shouldBeFullScreen)
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

}
