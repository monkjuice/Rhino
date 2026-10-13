#include "DjFader.h"

// The booth's fader: see DjFader.h.

namespace rhino
{
DjFader::DjFader(bool isVertical, juce::Colour accentColour, juce::Colour groundColour)
    : vertical(isVertical), accent(accentColour), ground(groundColour)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    setMouseCursor(vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
}

void DjFader::setValue(float v, bool notify)
{
    v = juce::jlimit(0.0f, 1.0f, v);
    if (current == v) return;
    current = v;
    repaint();
    if (notify && onChange) onChange(current);
}

void DjFader::setDefault(float v) { resting = juce::jlimit(0.0f, 1.0f, v); }

void DjFader::setDetent(float v) { detent = v; }

void DjFader::setTicks(int count) { ticks = count; }

void DjFader::paint(juce::Graphics& g)
{
    g.fillAll(ground);
    const auto bounds = getLocalBounds().toFloat();
    const auto groove = vertical ? bounds.withSizeKeepingCentre(4.0f, bounds.getHeight() - capLength)
                                 : bounds.withSizeKeepingCentre(bounds.getWidth() - capLength, 4.0f);
    g.setColour(palette::displayInset);
    g.fillRoundedRectangle(groove, 2.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(groove, 2.0f, 1.0f);
    if (ticks > 1)
    {
        g.setColour(palette::border.brighter(0.2f));
        for (int i = 0; i < ticks; ++i)
        {
            const auto at = positionFor(static_cast<float>(i) / static_cast<float>(ticks - 1));
            const auto major = i == 0 || i == ticks - 1 || i * 2 == ticks - 1;
            if (vertical)
                g.fillRect(groove.getRight() + 3.0f, at - 0.5f, major ? 8.0f : 4.0f, 1.0f);
            else
                g.fillRect(at - 0.5f, groove.getBottom() + 3.0f, 1.0f, major ? 8.0f : 4.0f);
        }
    }
    if (detent >= 0.0f)
    {
        const auto at = positionFor(detent);
        g.setColour(palette::textDim);
        if (vertical) g.fillRect(bounds.getX(), at - 0.5f, bounds.getWidth(), 1.0f);
        else g.fillRect(at - 0.5f, bounds.getY(), 1.0f, bounds.getHeight());
    }
    const auto at = positionFor(current);
    const auto cap = vertical ? juce::Rectangle<float>(bounds.getX() + 2.0f, at - capLength * 0.5f, bounds.getWidth() - 4.0f, capLength)
                              : juce::Rectangle<float>(at - capLength * 0.5f, bounds.getY() + 2.0f, capLength, bounds.getHeight() - 4.0f);
    g.setColour(juce::Colour(0x66000000));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), 2.0f);
    g.setColour(dragging ? palette::hover.brighter(0.2f) : palette::hover);
    g.fillRoundedRectangle(cap, 2.0f);
    g.setColour(palette::border.brighter(0.3f));
    g.drawRoundedRectangle(cap, 2.0f, 1.0f);
    // The cap's line, in the accent: what the hand looks for.
    g.setColour(accent);
    if (vertical) g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
    else g.fillRect(cap.getCentreX() - 1.0f, cap.getY() + 2.0f, 2.0f, cap.getHeight() - 4.0f);
}

void DjFader::mouseDown(const juce::MouseEvent& event)
{
    dragging = true;
    startValue = current;
    startPoint = event.position;
    repaint();
}

void DjFader::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging) return;
    const auto travel = static_cast<float>(vertical ? getHeight() : getWidth()) - capLength;
    if (travel <= 0.0f) return;
    const auto delta = vertical ? startPoint.y - event.position.y : event.position.x - startPoint.x;
    const auto fine = event.mods.isShiftDown() ? 0.1f : 1.0f;
    setValue(startValue + fine * delta / travel, true);
}

void DjFader::mouseUp(const juce::MouseEvent&)
{
    if (!dragging) return;
    dragging = false;
    repaint();
}

void DjFader::mouseDoubleClick(const juce::MouseEvent&)
{
    setValue(resting, true);
}

float DjFader::positionFor(float v) const
{
    const auto length = static_cast<float>(vertical ? getHeight() : getWidth());
    const auto travel = length - capLength;
    return vertical ? length - capLength * 0.5f - v * travel : capLength * 0.5f + v * travel;
}
}
