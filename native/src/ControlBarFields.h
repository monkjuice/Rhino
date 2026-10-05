#pragma once
#include "Theme.h"
#include <functional>
#include <vector>

// The fields the control bar is built from: a number you drag, and the pair of
// them that a time signature is. Nothing here knows about Session - the shell
// wires them to the model, exactly as it does every other control in the bar.

namespace rhino
{
// A number you set by dragging on it, which is how every DAW's tempo and time
// signature fields work and what a spin-button slider is a poor substitute for.
//
// Three ways in, all of them expected from a field like this:
//   - press and move the mouse up or down; Ctrl makes the step fine
//   - the wheel, one step a notch
//   - double-click to type the number
//
// The pointer is unbound while dragging, so the gesture is not cut short by the
// edge of the display, and hidden, so the eye stays on the digits rather than
// following an arrow that is not moving with them. JUCE puts it back inside the
// field when the button comes up.
//
// It is a plain Component rather than a styled juce::Slider because a slider
// carries a track, a thumb and a text box that all have to be painted away
// again, and its own drag sensitivity is expressed in a way that cannot say
// "one BPM per three pixels, or one hundredth with Ctrl held".
class ValueDragBox final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    ValueDragBox();
    ~ValueDragBox() override;

    // A continuous range. coarse is the step a plain drag moves by, fine the
    // step with Ctrl held; passing fine == coarse simply turns Ctrl off.
    void setRange(double minimum, double maximum, double coarse, double fine);
    // A fixed ladder instead of a range: a time signature's lower number is 1,
    // 2, 4, 8 or 16 and nothing in between, so a drag steps through the list.
    void setSteps(std::vector<double>);
    double value() const { return current; }
    void setValue(double next, juce::NotificationType = juce::dontSendNotification);
    // The caption above the digits. Empty gives the whole box to the value.
    void setCaption(juce::String);
    // How the number is written. The default is the value at the number of
    // decimal places set below; a field that spells its value differently -
    // "4/4", "1/16" - hands over a formatter instead.
    void setDecimalPlaces(int places);
    void setTextForValue(std::function<juce::String(double)>);
    void setFontSize(float valueEm, float captionEm);
    // Drawn to the right of the digits at caption size: "BPM" and the like.
    void setSuffix(juce::String);
    void setJustification(juce::Justification);
    // Whether the box paints its own panel. A pair of boxes that read as one
    // field - a time signature - lets the parent draw one panel behind both.
    void setDrawsBackground(bool);

    std::function<void(double)> onValueChange;
    // Bracket a drag so the model can fold it into one undo step. Both are
    // called once per gesture, never per pixel.
    std::function<void()> onDragStart, onDragEnd;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::String valueText() const;
    // Applies a value, clamped or snapped to the ladder, and reports it only
    // when it has actually moved.
    void applyValue(double next);
    double clamped(double next) const;
    void beginTyping();
    void endTyping(bool keep);
    // How far one drag step is, in pixels. Steady rather than accelerated: an
    // accelerating field cannot be used to set an exact tempo, which is the
    // one thing a tempo field has to be good at.
    static constexpr float pixelsPerStep = 3.0f;
    static constexpr float pixelsPerLadderStep = 10.0f;

    double current = 0.0, minimum = 0.0, maximum = 1.0, coarseStep = 1.0, fineStep = 1.0;
    std::vector<double> ladder;
    double dragStart = 0.0;
    int dragStartIndex = 0;
    bool dragging = false;
    // Where the drag was, and whether it was fine, when the step size last
    // changed: Ctrl pressed or let go part way carries on from the value on
    // screen rather than re-reading the whole drag at the new step.
    float travelAtStepChange = 0.0f, lastTravel = 0.0f;
    bool draggingFine = false;
    int decimals = 0;
    float valueEm = 15.0f, captionEm = 8.0f;
    juce::String caption, suffix;
    juce::Justification justification {juce::Justification::centred};
    bool drawsBackground = true;
    std::function<juce::String(double)> formatter;
    std::unique_ptr<juce::TextEditor> editor;
};

// Two numbers over one rule, read and dragged separately: 4 / 4. It is one
// field to look at and two to edit, which is why the slash and the panel are
// drawn here rather than by either box.
//
// The limits are the caller's to set, because they belong to the model: Rhino
// takes any numerator from 1 to 99 over 1, 2, 4, 8 or 16, which is what Live
// takes too.
class TimeSignatureField final : public juce::Component
{
public:
    TimeSignatureField();
    void setLimits(int minimumNumerator, int maximumNumerator, const std::vector<double>& denominators);
    void setSignature(int numerator, int denominator);
    int numerator() const;
    int denominator() const;
    void setFontSize(float em);
    // Called once per edit with the whole signature, because changing either
    // half means setting both: the model takes them together.
    std::function<void(int, int)> onChange;
    std::function<void()> onDragStart, onDragEnd;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ValueDragBox upper, lower;
    float em = 15.0f;
};
}
