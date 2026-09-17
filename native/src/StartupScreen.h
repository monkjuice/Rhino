#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
class StartupScreen final : public juce::Component
{
public:
    StartupScreen() { setOpaque(true); }
    void setStage(int next) { stage = next; repaint(); }
    void showError(const juce::String& message) { error = message; repaint(); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff171a1e));
        const auto centre = static_cast<float>(getWidth()) * 0.5f;
        g.setColour(juce::Colour(0xffc6d58c));
        g.drawEllipse(centre - 22.0f, 35.0f, 44.0f, 44.0f, 3.0f);
        g.fillRect(centre - 14.0f, 55.5f, 28.0f, 3.0f);
        g.setFont(juce::FontOptions(32.0f));
        g.setColour(juce::Colour(0xffe7ebdf));
        g.drawText("RHINO", 0, 94, getWidth(), 42, juce::Justification::centred);
        g.setFont(juce::FontOptions(14.0f));
        g.setColour(juce::Colour(0xffa4afb8));
        const juce::String messages[] {"Starting audio engine...", "Connecting audio devices...", "Preparing your workspace..."};
        g.drawText(error.isEmpty() ? messages[juce::jlimit(0, 2, stage)] : "Startup could not finish",
                   24, 152, getWidth() - 48, 28, juce::Justification::centred);
        if (error.isNotEmpty())
        {
            g.setFont(juce::FontOptions(12.0f));
            g.drawFittedText(error, getLocalBounds().withTrimmedTop(194).reduced(24, 12), juce::Justification::centred, 4);
            return;
        }
        // Each marker represents a real completed initialization stage, not
        // estimated elapsed time or a simulated percentage.
        const juce::String labels[] {"Engine", "Devices", "Workspace"};
        for (int i = 0; i < 3; ++i)
        {
            const auto x = centre + static_cast<float>(i - 1) * 126.0f;
            g.setColour(juce::Colour(i <= stage ? 0xffc6d58c : 0xff3b444c));
            if (i < stage) g.fillEllipse(x - 4, 215, 8, 8);
            else g.drawEllipse(x - 4, 215, 8, 8, 1.5f);
            g.setFont(juce::FontOptions(12.0f));
            g.setColour(juce::Colour(i <= stage ? 0xffbfcab1 : 0xff6c7883));
            g.drawText(labels[i], static_cast<int>(x) - 55, 238, 110, 24, juce::Justification::centred);
        }
    }
private:
    int stage = 0;
    juce::String error;
};
}
