#include "DjPad.h"

// The booth's lit key: see DjPad.h.

namespace rhino
{
DjPad::DjPad(const juce::String& name, juce::Colour litColour, const juce::String& label)
    : juce::Button(name), colour(litColour), text(label.isEmpty() ? name : label)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void DjPad::setLit(bool shouldBeLit)
{
    if (lit == shouldBeLit) return;
    lit = shouldBeLit;
    repaint();
}

void DjPad::setBlinking(bool shouldBlink)
{
    if (blinking == shouldBlink) return;
    blinking = shouldBlink;
    repaint();
}

void DjPad::setBlinkPhase(bool on)
{
    if (!blinking || blinkOn == on) return;
    blinkOn = on;
    repaint();
}

void DjPad::setLabel(const juce::String& newLabel)
{
    if (text == newLabel) return;
    text = newLabel;
    layOutLabel();
    repaint();
}

void DjPad::setGhosted(bool shouldGhost)
{
    if (ghosted == shouldGhost) return;
    ghosted = shouldGhost;
    repaint();
}

void DjPad::setRound(bool shouldBeRound)
{
    if (round == shouldBeRound) return;
    round = shouldBeRound;
    layOutLabel();
    repaint();
}

void DjPad::resized()
{
    layOutLabel();
}

void DjPad::layOutLabel()
{
    if (round)
    {
        const auto size = static_cast<float>(std::min(getWidth(), getHeight()));
        const auto face = getLocalBounds().toFloat().withSizeKeepingCentre(size, size).reduced(1.5f);
        label.set(text, uiFontBold(size >= 44.0f ? 11.0f : 9.0f), face.toNearestInt(), juce::Justification::centred, true);
    }
    else
    {
        label.set(text, uiFontBold(getHeight() >= 22 ? 10.0f : 9.0f), getLocalBounds(), juce::Justification::centred, true);
    }
}

void DjPad::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (onRightClick) onRightClick();
        return;
    }
    juce::Button::mouseDown(event);
}

void DjPad::mouseUp(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu()) return;
    juce::Button::mouseUp(event);
}

void DjPad::paintButton(juce::Graphics& g, bool highlighted, bool pressed)
{
    const auto on = (lit && (!blinking || blinkOn)) || pressed;
    if (round)
    {
        const auto size = static_cast<float>(std::min(getWidth(), getHeight()));
        const auto face = getLocalBounds().toFloat().withSizeKeepingCentre(size, size).reduced(1.5f);
        auto ground = on ? palette::control.overlaidWith(colour.withAlpha(0.35f)) : palette::control;
        if (highlighted && !pressed) ground = ground.brighter(0.08f);
        g.setColour(ground);
        g.fillEllipse(face);
        g.setColour(on ? colour : colour.withMultipliedAlpha(isEnabled() ? 0.55f : 0.25f));
        g.drawEllipse(face.reduced(1.0f), size >= 40.0f ? 3.0f : 2.0f);
        g.setColour(isEnabled() ? palette::text : palette::disabled);
        label.draw(g);
        return;
    }
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto ground = on ? colour : (ghosted ? palette::control.overlaidWith(colour.withMultipliedAlpha(0.18f)) : palette::control);
    if (highlighted && !pressed) ground = ground.brighter(0.08f);
    if (!isEnabled()) ground = ground.withMultipliedAlpha(0.5f);
    g.setColour(ground);
    g.fillRoundedRectangle(bounds, 2.0f);
    g.setColour(on ? colour.darker(0.4f) : palette::border);
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    g.setColour(on ? colour.contrasting(0.9f) : (isEnabled() ? palette::text : palette::disabled));
    label.draw(g);
}
}
