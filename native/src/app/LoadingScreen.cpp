#include "LoadingScreen.h"
#include "ui/shared/Theme.h"

namespace rhino
{
namespace
{
// Thirty frames a second is plenty for a bar that fills, and leaves the work
// about three quarters of every frame; the rest is the window drawing.
constexpr int frameMilliseconds = 33;
constexpr double workMilliseconds = 24.0;
constexpr int cardWidth = 380, cardHeight = 132, cardPadX = 20, cardPadY = 16, buttonHeight = 26;
}

LoadingScreen::LoadingScreen()
{
    setVisible(false);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(cancelButton);
    cancelButton.setColour(juce::TextButton::buttonColourId, palette::control);
    cancelButton.setColour(juce::TextButton::textColourOffId, palette::text);
    cancelButton.onClick = [this] { cancelTask(); };
}

// Gone with the window: whatever was running is stopped, not left half done.
LoadingScreen::~LoadingScreen()
{
    stopTimer();
    if (running && task.cancel)
        task.cancel();
}

void LoadingScreen::run(Task next)
{
    jassert(!running);
    task = std::move(next);
    running = true;
    fraction = 0.0f;
    detail = {};
    if (task.progress)
        std::tie(fraction, detail) = task.progress();
    toFront(false);
    // Shows the screen and takes the keyboard. Nothing else in the app takes
    // input until it goes.
    enterModalState(true, nullptr, false);
    repaint();
    // The first slice waits a frame, so the screen is drawn before any of the
    // work holds the thread.
    startTimer(frameMilliseconds);
}

void LoadingScreen::timerCallback()
{
    const auto more = task.step && task.step(workMilliseconds);
    if (task.progress)
        std::tie(fraction, detail) = task.progress();
    if (!more)
    {
        end(false);
        return;
    }
    repaint(card());
}

void LoadingScreen::end(bool cancelled)
{
    if (!running)
        return;
    if (cancelled && task.cancel)
        task.cancel();
    running = false;
    stopTimer();
    exitModalState(0);
    setVisible(false);
    // Taken out first: what it does may run the screen again.
    const auto finished = std::move(task.finished);
    task = {};
    if (finished)
        finished();
}

bool LoadingScreen::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
        cancelTask();
    return true;
}

juce::Rectangle<int> LoadingScreen::card() const
{
    return getLocalBounds().withSizeKeepingCentre(std::min(cardWidth, getWidth() - 32), cardHeight);
}

void LoadingScreen::resized()
{
    cancelButton.setBounds(card().reduced(cardPadX, cardPadY).removeFromBottom(buttonHeight).removeFromRight(84));
}

void LoadingScreen::paint(juce::Graphics& g)
{
    // The window stays in sight, dimmed, so it is plain what is being worked
    // on and that it is waiting rather than hung.
    g.fillAll(palette::appBackground.withAlpha(0.7f));
    const auto box = card();
    g.setColour(palette::sideSurface);
    g.fillRect(box);
    g.setColour(palette::border);
    g.drawRect(box, 1);

    auto inner = box.reduced(cardPadX, cardPadY);
    g.setColour(palette::text);
    g.setFont(uiFontBold(13.0f));
    drawSnappedText(g, task.title, inner.removeFromTop(20), juce::Justification::centredLeft, true);
    inner.removeFromTop(4);
    auto line = inner.removeFromTop(18);
    g.setFont(uiFont(11.0f));
    g.setColour(palette::textDim);
    drawSnappedText(g, juce::String(juce::roundToInt(fraction * 100.0f)) + "%", line.removeFromRight(40),
                    juce::Justification::right);
    drawSnappedText(g, detail, line, juce::Justification::centredLeft, true);
    inner.removeFromTop(12);
    const auto bar = inner.removeFromTop(6);
    g.setColour(palette::displayInset);
    g.fillRect(bar);
    g.setColour(palette::deviceAccent);
    g.fillRect(bar.withWidth(juce::roundToInt(static_cast<float>(bar.getWidth()) * juce::jlimit(0.0f, 1.0f, fraction))));
}
}
