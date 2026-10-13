#pragma once
#include "ControlBarIcons.h"
#include "ui/shared/ClipDrag.h"
#include "ui/shared/Theme.h"
#include <functional>

namespace rhino
{

// Stop parks the playhead on the line the arrangement is working from, which
// is what makes play-stop-play repeat a passage. Double-clicking it means the
// top of the song instead. The second click is told apart here, where the
// system's own click counting is already available, rather than by timing
// clicks in the handler.
class StopButton final : public IconButton
{
public:
    StopButton() : IconButton("Stop") {}

    std::function<void()> onStop, onReturnToStart;

    void mouseDown(const juce::MouseEvent& event) override
    {
        secondClick = event.getNumberOfClicks() > 1;
        IconButton::mouseDown(event);
    }

    void clicked() override
    {
        // Cleared as it is read: a click arriving from the keyboard or from
        // triggerClick has no mouse event behind it and must not inherit the
        // last one's count.
        if (secondClick)
        {
            secondClick = false;
            if (onReturnToStart) onReturnToStart();
            return;
        }
        if (onStop) onStop();
    }

private:
    bool secondClick = false;
};

// The record button paints its own dot rather than borrowing a glyph. It has
// three things to say - nothing is armed, something is armed, and the transport
// is rolling or counting into it - and a colour on a circle says all three at a
// glance where a character would need a legend.
class RecordButton final : public juce::TextButton
{
public:
    enum class State { idle, armed, countingIn, recording };

    RecordButton() : juce::TextButton("Record") {}

    void setState(State next)
    {
        if (state == next) return;
        state = next;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool pressed) override
    {
        const auto bounds = getLocalBounds().toFloat();
        // No box. The dot sits on the bar like every other transport glyph,
        // and the wash under the pointer is what shows the hit area is larger
        // than the twelve pixels the dot occupies.
        if (isEnabled() && (highlighted || pressed))
        {
            g.setColour(juce::Colour(pressed ? 0x24ffffff : 0x14ffffff));
            g.fillRoundedRectangle(bounds.reduced(1.0f), 3.0f);
        }
        const auto dot = bounds.withSizeKeepingCentre(12.0f, 12.0f);
        // Red is the one colour the neutral chrome keeps, and it keeps it here:
        // record is the only control in the bar whose state is worth a colour.
        // Disabled - the DJ view showing - it is grey whatever the tracks say.
        const auto colour = !isEnabled()                 ? palette::disabled.withMultipliedAlpha(0.5f)
                          : state == State::recording  ? palette::recordAccent
                          : state == State::countingIn ? juce::Colour(0xffe0a03c)
                          : state == State::armed      ? palette::recordAccent.withMultipliedSaturation(0.72f).darker(0.25f)
                                                       : palette::disabled;
        g.setColour(colour);
        g.fillEllipse(dot);
        // A ring while it is actually capturing, so a rolling recording cannot
        // be mistaken for a track merely sitting armed.
        if (state == State::recording || state == State::countingIn)
        {
            g.setColour(colour.withAlpha(0.45f));
            g.drawEllipse(dot.expanded(3.0f), 1.6f);
        }
    }

private:
    State state = State::idle;
};

// The view switch: two icons at the right end of the bar, the view that is
// showing in green and the other in light grey, drawn from the SVGs in
// assets (arrangement_view.svg and dj_view.svg) compiled in beside the fonts.
// A toggle takes a drag too: a clip from the timeline, a console's cell or
// a browser item hovering it shows its view, so the drag can go on to the
// other view's targets - a console's grid, a lane - the way a drag crosses
// Live's view toggles. Nothing is dropped on the toggle itself.
class ViewToggle final : public IconButton,
                         public juce::DragAndDropTarget
{
public:
    ViewToggle(const juce::String& name, const char* svg, int svgSize) : IconButton(name)
    {
        // JUCE's SVG reader knows nothing of currentColor; black stands in for
        // it, and the painter swaps black for the state's colour.
        const auto text = juce::String::fromUTF8(svg, svgSize).replace("currentColor", "#000000");
        if (const auto xml = juce::XmlDocument::parse(text))
            glyph = juce::Drawable::createFromSVG(*xml);
        setGlyphInset(0.18f);
        setActiveColour(palette::viewActive);
        setIdleColour(palette::viewIdle);
        setPainter([this](juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
        {
            if (glyph == nullptr) return;
            auto tinted = glyph->createCopy();
            tinted->replaceColour(juce::Colours::black, colour);
            tinted->drawWithin(g, area, juce::RectanglePlacement::centred, 1.0f);
        });
    }
    std::function<void()> dragHovered;
    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return isCrossViewDrag(details.description.toString());
    }
    void itemDragEnter(const SourceDetails&) override { if (dragHovered) dragHovered(); }
    void itemDropped(const SourceDetails&) override {}

private:
    std::unique_ptr<juce::Drawable> glyph;
};

// The clip and device panes float over the foot of the arrangement, so the
// split between them has to be a component of its own: the arrangement is
// underneath it and would take every press meant for the handle. Positions are
// read in screen coordinates because the bar moves with the drag, and a
// position relative to a component that is being moved chases itself.
class SplitterBar final : public juce::Component
{
public:
    SplitterBar() { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    void paint(juce::Graphics& g) override
    {
        g.setColour(palette::border);
        g.fillRect(getLocalBounds().withSizeKeepingCentre(getWidth(), 4));
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (dragStarted) dragStarted(event.getScreenPosition().y);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragged) dragged(event.getScreenPosition().y);
    }
    std::function<void(int)> dragStarted, dragged;
};

}
