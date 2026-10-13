#pragma once
#include "ui/shared/Theme.h"
#include <functional>

namespace rhino
{

// The document window. It draws its own title bar, with the menus in it, and
// keeps that bar in full screen, which is kiosk mode rather than maximise.
class MainWindow final : public juce::DocumentWindow
{
public:
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

    MainWindow();
    juce::Rectangle<int> titleBarBounds() const;
    juce::BorderSize<int> getContentComponentBorder() const override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Full screen is kiosk mode, and deliberately not what the maximise
    // button does: JUCE's setFullScreen is the window's maximise state.
    bool isAppFullScreen() const { return juce::Desktop::getInstance().getKioskModeComponent() == this; }

    void maximiseButtonPressed() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void setAppFullScreen(bool shouldBeFullScreen);

    void setProjectTitle(const juce::String& text) { projectTitle.setText(text, juce::dontSendNotification); }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    std::function<void(juce::Component&)> fileRequested, editRequested, viewRequested, helpRequested;

private:
    TitleMenuButton fileMenu {"File"}, editMenu {"Edit"}, viewMenu {"View"}, helpMenu {"Help"};
    juce::Label projectTitle;
    bool maximisedBeforeFullScreen = false;
};

}
