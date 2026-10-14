#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <utility>

namespace rhino
{
// A veil over the whole window while a long command works on the message
// thread a slice at a time: what it is doing, how far it has got, and a way to
// stop it. The window goes on drawing between slices instead of freezing.
//
// Modal while it shows, so no click, key or menu - the title bar's and every
// other window's included - can edit the document under the command. That is
// what lets the command hold on to tracks and clips from one slice to the next.
class LoadingScreen final : public juce::Component, private juce::Timer
{
public:
    struct Task
    {
        juce::String title;
        // Does about `milliseconds` of the work and says whether any is left.
        std::function<bool(double milliseconds)> step;
        // How far it has got, 0 to 1, and what it is working on.
        std::function<std::pair<float, juce::String>()> progress;
        std::function<void()> cancel;
        // Once it is over, finished or cancelled, and the screen has gone.
        std::function<void()> finished;
    };
    LoadingScreen();
    ~LoadingScreen() override;
    void run(Task);
    bool isRunning() const { return running; }
    // Cancel and Esc both come here.
    void cancelTask() { end(true); }
    void paint(juce::Graphics&) override;
    void resized() override;
    // Esc cancels; every other key stops here, since a shortcut is an edit.
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override { return true; }
    // A click on the title bar or another window is simply not taken, without
    // the alert sound and the raise JUCE gives it by default.
    void inputAttemptWhenModal() override {}

private:
    // The runner has no message loop, so it drives the timer itself.
    friend int runArrangementTest();
    void timerCallback() override;
    juce::Rectangle<int> card() const;
    void end(bool cancelled);
    Task task;
    bool running = false;
    float fraction = 0.0f;
    juce::String detail;
    juce::TextButton cancelButton {"Cancel"};
};
}
