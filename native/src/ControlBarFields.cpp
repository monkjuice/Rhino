#include "ControlBarFields.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// A field on the control bar is a raised control, not a recessed one: the
// display is the only well in the bar, and two kinds of inset next to each
// other read as a mistake rather than as a hierarchy.
const juce::Colour panelColour {palette::control};
const juce::Colour panelEdgeColour {palette::border};
const juce::Colour valueColour {palette::text};
const juce::Colour captionColour {palette::textDim};
}

ValueDragBox::ValueDragBox()
{
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

ValueDragBox::~ValueDragBox() = default;

void ValueDragBox::setRange(double newMinimum, double newMaximum, double coarse, double fine)
{
    minimum = newMinimum;
    maximum = std::max(newMinimum, newMaximum);
    coarseStep = coarse > 0.0 ? coarse : 1.0;
    fineStep = fine > 0.0 ? fine : coarseStep;
    ladder.clear();
    current = clamped(current);
}

void ValueDragBox::setSteps(std::vector<double> steps)
{
    ladder = std::move(steps);
    std::sort(ladder.begin(), ladder.end());
    if (!ladder.empty())
    {
        minimum = ladder.front();
        maximum = ladder.back();
    }
    current = clamped(current);
}

double ValueDragBox::clamped(double next) const
{
    if (!std::isfinite(next)) return current;
    if (ladder.empty())
        return juce::jlimit(minimum, maximum, next);
    // Nearest rung, so a typed value that is not on the ladder lands on the
    // closest one rather than being refused.
    auto best = ladder.front();
    for (const auto rung : ladder)
        if (std::abs(rung - next) < std::abs(best - next))
            best = rung;
    return best;
}

void ValueDragBox::setValue(double next, juce::NotificationType notification)
{
    const auto settled = clamped(next);
    if (settled == current && notification == juce::dontSendNotification)
        return;
    const auto moved = settled != current;
    current = settled;
    repaint();
    if (moved && notification != juce::dontSendNotification && onValueChange)
        onValueChange(current);
}

void ValueDragBox::applyValue(double next)
{
    const auto settled = clamped(next);
    if (settled == current) return;
    current = settled;
    repaint();
    if (onValueChange) onValueChange(current);
}

void ValueDragBox::setCaption(juce::String text) { caption = std::move(text); repaint(); }
void ValueDragBox::setSuffix(juce::String text) { suffix = std::move(text); repaint(); }
void ValueDragBox::setDecimalPlaces(int places) { decimals = std::max(0, places); repaint(); }
void ValueDragBox::setTextForValue(std::function<juce::String(double)> f) { formatter = std::move(f); repaint(); }
void ValueDragBox::setJustification(juce::Justification j) { justification = j; repaint(); }
void ValueDragBox::setDrawsBackground(bool draws) { drawsBackground = draws; repaint(); }

void ValueDragBox::setFontSize(float value, float captionSize)
{
    valueEm = value;
    captionEm = captionSize;
    repaint();
}

juce::String ValueDragBox::valueText() const
{
    if (formatter) return formatter(current);
    return juce::String(current, decimals);
}

void ValueDragBox::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    if (drawsBackground)
    {
        g.setColour(panelColour);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(panelEdgeColour);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
    }
    auto area = bounds.reduced(6, 3);
    if (caption.isNotEmpty())
    {
        g.setColour(captionColour);
        g.setFont(uiFontBold(captionEm));
        drawSnappedText(g, caption, area.removeFromTop(juce::roundToInt(captionEm) + 4), justification, true);
    }
    if (suffix.isNotEmpty())
    {
        g.setFont(uiFontBold(captionEm));
        const auto width = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), suffix) + 5;
        auto tail = area.removeFromRight(width);
        g.setColour(captionColour);
        drawSnappedText(g, suffix, tail, juce::Justification::centredRight);
    }
    if (editor != nullptr) return;
    g.setColour(valueColour);
    g.setFont(uiFontBold(valueEm));
    drawSnappedText(g, valueText(), area, justification, true);
}

void ValueDragBox::resized()
{
    if (editor != nullptr)
        editor->setBounds(getLocalBounds().reduced(3));
}

void ValueDragBox::mouseDown(const juce::MouseEvent& event)
{
    if (editor != nullptr || event.mods.isPopupMenu() || event.getNumberOfClicks() > 1)
        return;
    dragging = true;
    dragStart = current;
    dragStartIndex = 0;
    if (!ladder.empty())
        for (int i = 0; i < static_cast<int>(ladder.size()); ++i)
            if (ladder[static_cast<size_t>(i)] == current)
                dragStartIndex = i;
    // Unbound so the gesture survives the edge of the display, and hidden so
    // nothing is left sitting still beside digits that are moving.
    event.source.enableUnboundedMouseMovement(true, false);
    setMouseCursor(juce::MouseCursor::NoCursor);
    if (onDragStart) onDragStart();
}

void ValueDragBox::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging) return;
    // Up is more, which is the direction every DAW reads and the opposite of
    // the screen's own y axis.
    const auto travel = static_cast<float>(-event.getDistanceFromDragStartY());
    if (!ladder.empty())
    {
        const auto steps = juce::roundToInt(travel / pixelsPerLadderStep);
        const auto index = juce::jlimit(0, static_cast<int>(ladder.size()) - 1, dragStartIndex + steps);
        applyValue(ladder[static_cast<size_t>(index)]);
        return;
    }
    const auto step = event.mods.isCtrlDown() ? fineStep : coarseStep;
    const auto steps = static_cast<double>(juce::roundToInt(travel / pixelsPerStep));
    // Quantised to the step from the value the drag began on, so a field
    // dragged out and back reads exactly what it started at.
    applyValue(dragStart + steps * step);
}

void ValueDragBox::mouseUp(const juce::MouseEvent& event)
{
    if (!dragging) return;
    dragging = false;
    event.source.enableUnboundedMouseMovement(false, false);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    if (onDragEnd) onDragEnd();
}

void ValueDragBox::mouseDoubleClick(const juce::MouseEvent&)
{
    beginTyping();
}

void ValueDragBox::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (editor != nullptr) return;
    const auto notches = wheel.deltaY > 0.0f ? 1 : wheel.deltaY < 0.0f ? -1 : 0;
    if (notches == 0) return;
    if (onDragStart) onDragStart();
    if (!ladder.empty())
    {
        auto index = 0;
        for (int i = 0; i < static_cast<int>(ladder.size()); ++i)
            if (ladder[static_cast<size_t>(i)] == current) index = i;
        index = juce::jlimit(0, static_cast<int>(ladder.size()) - 1, index + notches);
        applyValue(ladder[static_cast<size_t>(index)]);
    }
    else
    {
        applyValue(current + notches * (event.mods.isCtrlDown() ? fineStep : coarseStep));
    }
    if (onDragEnd) onDragEnd();
}

void ValueDragBox::beginTyping()
{
    if (editor != nullptr) return;
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont(uiFontBold(valueEm));
    editor->setJustification(justification);
    editor->setColour(juce::TextEditor::backgroundColourId, palette::appBackground);
    editor->setColour(juce::TextEditor::textColourId, valueColour);
    editor->setColour(juce::TextEditor::outlineColourId, palette::border);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, palette::activeNeutral);
    editor->setColour(juce::TextEditor::highlightColourId, palette::hover);
    editor->setText(valueText(), false);
    editor->setSelectAllWhenFocused(true);
    editor->onReturnKey = [this] { endTyping(true); };
    editor->onEscapeKey = [this] { endTyping(false); };
    editor->onFocusLost = [this] { endTyping(true); };
    addAndMakeVisible(editor.get());
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
    repaint();
}

void ValueDragBox::endTyping(bool keep)
{
    if (editor == nullptr) return;
    // Moved out first: the editor is destroyed from inside its own callback,
    // and applying the value can run anything the owner has attached.
    auto finished = std::move(editor);
    finished->onFocusLost = nullptr;
    const auto typed = finished->getText().trim();
    removeChildComponent(finished.get());
    finished.reset();
    if (keep && typed.isNotEmpty())
    {
        if (onDragStart) onDragStart();
        applyValue(typed.getDoubleValue());
        if (onDragEnd) onDragEnd();
    }
    repaint();
}

//==============================================================================
TimeSignatureField::TimeSignatureField()
{
    for (auto* box : {&upper, &lower})
    {
        box->setDrawsBackground(false);
        box->setDecimalPlaces(0);
        box->setJustification(juce::Justification::centred);
        box->onDragStart = [this] { if (onDragStart) onDragStart(); };
        box->onDragEnd = [this] { if (onDragEnd) onDragEnd(); };
        box->onValueChange = [this] (double)
        {
            if (onChange) onChange(numerator(), denominator());
        };
        addAndMakeVisible(box);
    }
    upper.setRange(1.0, 99.0, 1.0, 1.0);
    lower.setSteps({1.0, 2.0, 4.0, 8.0, 16.0});
    upper.setValue(4.0);
    lower.setValue(4.0);
    upper.setTooltip("Beats in a bar - drag up or down, or double-click to type");
    lower.setTooltip("Which note is the beat - drag up or down through 1, 2, 4, 8 and 16");
}

void TimeSignatureField::setLimits(int minimumNumerator, int maximumNumerator,
                                   const std::vector<double>& denominators)
{
    upper.setRange(minimumNumerator, maximumNumerator, 1.0, 1.0);
    lower.setSteps(denominators);
}

void TimeSignatureField::setSignature(int n, int d)
{
    upper.setValue(n);
    lower.setValue(d);
}

int TimeSignatureField::numerator() const { return juce::roundToInt(upper.value()); }
int TimeSignatureField::denominator() const { return juce::roundToInt(lower.value()); }

void TimeSignatureField::setFontSize(float size)
{
    em = size;
    upper.setFontSize(size, size);
    lower.setFontSize(size, size);
    repaint();
}

void TimeSignatureField::paint(juce::Graphics& g)
{
    g.setColour(panelColour);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
    g.setColour(panelEdgeColour);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    // The rule between the two numbers. Leaned over rather than upright so it
    // reads as a signature and not as a fraction bar with nothing under it.
    const auto centre = getLocalBounds().withSizeKeepingCentre(14, getHeight()).toFloat();
    g.setColour(palette::textDim);
    g.drawLine(centre.getCentreX() + 3.0f, centre.getY() + 8.0f,
               centre.getCentreX() - 3.0f, centre.getBottom() - 8.0f, 1.4f);
}

void TimeSignatureField::resized()
{
    auto area = getLocalBounds();
    const auto half = std::max(1, (area.getWidth() - 14) / 2);
    upper.setBounds(area.removeFromLeft(half));
    lower.setBounds(area.removeFromRight(half));
}
}
