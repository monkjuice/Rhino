#include "MainComponent.h"
#include <algorithm>
#include <cmath>

namespace rhino
{

void MainComponent::paint(juce::Graphics& g)
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

void MainComponent::resized()
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
        // The note editor's first appearance in a run makes room for sixteen
        // named rows and its velocity lane, whichever face opened the pane
        // first; a fifth of a 1080p window folds the lane away. Only the
        // shell's own default is raised: a height the pane was dragged to,
        // before or after, is kept.
        if (lowerPane == LowerPane::notes && !noteEditorHasOpened)
        {
            noteEditorHasOpened = true;
            if (!paneHeightDragged)
                lowerPaneHeight = std::max(lowerPaneHeight, StepGrid::openingHeight);
        }
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
    arrangement.setVisible(!djViewOpen);
    djView.setVisible(djViewOpen);
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
    // Over all of it, those raised just now included.
    loadingScreen.setBounds(getLocalBounds());
    loadingScreen.toFront(false);
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(isOverSplitter(event.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                  : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    resizingBrowser = browserOpen && std::abs(event.x - browserWidth) <= 5
                      && event.y >= browserTop && event.y < browserColumnBottom();
    resizeStartX = event.x;
    resizeStartY = event.y;
    resizeStartBrowserWidth = browserWidth;
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (resizingBrowser)
    {
        browserWidth = juce::jlimit(180, 360, resizeStartBrowserWidth + event.x - resizeStartX);
        resized();
        repaint();
    }
}

void MainComponent::mouseUp(const juce::MouseEvent&)
{
    resizingBrowser = false;
}

void MainComponent::toggleBrowser()
{
    browserOpen = !browserOpen;
    resized();
    repaint();
}

// What the window paints itself - the band the toggles stand on, the
// browser's divider, the Info View, the control bar's rules - moves only
// with the panes, and a pane that moves or resizes repaints itself and
// the ground it leaves. So the window repaints only when its own picture
// moved: laying out panes that had not moved repainted every panel in it.
void MainComponent::applyPaneLayout()
{
    paneLayoutPending = false;
    const auto before = paintedLayout();
    resized();
    if (paintedLayout() != before)
        repaint();
}

std::tuple<int, int, int, juce::Rectangle<int>, std::vector<int>> MainComponent::paintedLayout() const
{
    return {lowerBandTop, lowerBandLeft, workspaceBottom, infoViewArea(), barDividers};
}

// The Info View sits at the foot of the browser column, so it has nowhere
// to go while the browser is hidden.
void MainComponent::toggleInfoView()
{
    if (!browserOpen) return;
    infoVisible = !infoVisible;
    resized();
    repaint();
}

// The bottom-left corner of the window. It runs to the client edge rather
// than stopping where the browser does, because the clip and device panel
// begins to the right of it: the Info View sits beside that panel now,
// rather than being buried above it.
juce::Rectangle<int> MainComponent::infoViewArea() const
{
    if (!infoVisible || !browserOpen) return {};
    const auto top = std::max(browserTop + 96, getHeight() - infoViewHeight);
    return {0, top, browserWidth, std::max(1, getHeight() - top)};
}

// The foot of the browser's whole column, Info View included. The divider
// and the drag that resizes the browser both run the length of it.
int MainComponent::browserColumnBottom() const
{
    return infoVisible && browserOpen ? getHeight() : workspaceBottom;
}

bool MainComponent::isOverSplitter(juce::Point<float> point) const
{
    return browserOpen && std::abs(point.x - static_cast<float>(browserWidth)) <= 5.0f
        && point.y >= static_cast<float>(browserTop)
        && point.y < static_cast<float>(browserColumnBottom());
}

}
