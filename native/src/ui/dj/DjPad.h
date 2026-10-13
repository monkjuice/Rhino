#pragma once
#include "ui/shared/ShapedText.h"
#include <functional>

namespace rhino
{
// A key that lights in a colour: the hot cues, PLAY and CUE, and the
// booth's switches. A right-click is a second action, which is how a hot
// cue is cleared without a modifier. Round, it is the CDJ's big key: a
// dark face in a ring of its colour that fills when lit.
//
// Its label is shaped when it changes or the key is resized, not when the
// key is painted: a key repaints on every hover and every blink.
class DjPad final : public juce::Button
{
public:
    DjPad(const juce::String& name, juce::Colour litColour, const juce::String& label = {});
    void setLit(bool shouldBeLit);
    // Blinking says "about to": a start held for the beat.
    void setBlinking(bool shouldBlink);
    void setBlinkPhase(bool on);
    void setLabel(const juce::String& label);
    // A pad that is dark rather than off: a hot cue not yet set still shows
    // its colour faintly, so a hand finds it.
    void setGhosted(bool shouldGhost);
    void setRound(bool shouldBeRound);
    std::function<void()> onRightClick;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void paintButton(juce::Graphics&, bool highlighted, bool pressed) override;
    void resized() override;

private:
    void layOutLabel();

    juce::Colour colour;
    juce::String text;
    ShapedLine label;
    bool lit = false, blinking = false, blinkOn = true, ghosted = false, round = false;
};
}
