#pragma once

#include "ForgeStyle.h"
#include <BinaryData.h>
#include <functional>

namespace rhino::forge::ui
{
// The wheel's art, cut from its photograph and scaled down once rather than on
// every paint, and kept in the ImageCache rather than in a static (see
// cachedImage in ForgeChrome.h).
inline juce::Image performanceWheelArt()
{
    return cachedImage(wheelImageKey, []
    {
        return juce::ImageCache::getFromMemory(BinaryData::performance_wheel_png,
                                               BinaryData::performance_wheel_pngSize)
            .getClippedImage({245, 65, 530, 1385})
            .rescaled(76, 160, juce::Graphics::highResamplingQuality);
    });
}

// The metal stays put while the ribbing scrolls inside its slot. Keeping each
// wheel a child lets a drag repaint only this small plate, not the whole panel.
class PerformanceWheel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PerformanceWheel(bool springsBack) : spring(springsBack)
    {
        setTooltip(spring ? "Pitch bend (returns to centre, +/- 2 semitones)"
                          : "Modulation wheel (MIDI CC 1; route MOD WHEEL in the matrix)");
        value = spring ? 0.5f : 0.0f;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    std::function<void(float)> onChange;

    void setDisplayValue(float next)
    {
        if (dragging) return;
        next = juce::jlimit(0.0f, 1.0f, next);
        if (std::abs(next - value) > 0.001f) { value = next; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        const auto wheel = performanceWheelArt();
        const auto width = getWidth();
        const auto art = juce::Rectangle<int>(2, 10, width - 4, getHeight() - 11);
        g.setColour(mutedText);
        g.setFont(panelFont(Face::label, 8.0f));
        g.drawText(spring ? "PITCH" : "MOD", 0, 0, width, 10, juce::Justification::centred);
        g.drawImage(wheel, art.toFloat(), juce::RectanglePlacement::stretchToFit);
        // The wheel art is brushed steel, the one cold metal on Merli's warm
        // panel. A wash of the knobs' bronze over the art's own pixels brings
        // it into the set: drawn through the image's alpha, so the plate
        // around the wheel is untouched, and in a saved state, because an
        // image takes the current colour's alpha and the tyre drawn next must
        // not inherit the wash's.
        if (isMerli())
        {
            juce::Graphics::ScopedSaveState wash(g);
            g.setColour(knobRimLit.withAlpha(0.24f));
            g.drawImage(wheel, art.toFloat(), juce::RectanglePlacement::stretchToFit, true);
        }

        // Slide only the tyre's ribbed middle over the stationary metal. The
        // narrow clip avoids moving the housing or its four screw heads.
        const auto tyre = art.toFloat().withTrimmedLeft(art.getWidth() * 0.27f)
                                       .withTrimmedRight(art.getWidth() * 0.27f)
                                       .withTrimmedTop(art.getHeight() * 0.13f)
                                       .withTrimmedBottom(art.getHeight() * 0.12f);
        const auto shift = (0.5f - value) * 8.0f;
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(tyre.getSmallestIntegerContainer());
        g.drawImage(wheel, art.toFloat().translated(0.0f, shift),
                    juce::RectanglePlacement::stretchToFit);
        // The tyre is rubber, near-black in the mockup between bronze rims,
        // so it is taken down toward the knobs' dark bronze rather than lit.
        if (isMerli())
        {
            juce::Graphics::ScopedSaveState rubber(g);
            g.setColour(knobRimDark.withAlpha(0.45f));
            g.drawImage(wheel, art.toFloat().translated(0.0f, shift), juce::RectanglePlacement::stretchToFit, true);
        }

        // The small luminous notch makes the current position legible at the
        // panel's minimum size, even where the tyre texture is only a few px.
        const auto markerY = juce::jmap(value, tyre.getBottom() - 3.0f, tyre.getY() + 3.0f);
        g.setColour(electricBlue.withAlpha(0.8f));
        g.fillRoundedRectangle(tyre.getX() + 1.0f, markerY, tyre.getWidth() - 2.0f, 1.6f, 0.8f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragging = true;
        dragStartValue = value;
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        change(dragStartValue - event.getDistanceFromDragStartY() / 90.0f);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        dragging = false;
        if (spring) change(0.5f);
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { change(spring ? 0.5f : 0.0f); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (!spring) change(value + wheel.deltaY * 0.15f);
    }

private:
    void change(float next)
    {
        next = juce::jlimit(0.0f, 1.0f, next);
        if (next == value) return;
        value = next;
        if (onChange) onChange(value);
        repaint();
    }

    bool spring = false;
    bool dragging = false;
    float value = 0.0f;
    float dragStartValue = 0.0f;
};
}
