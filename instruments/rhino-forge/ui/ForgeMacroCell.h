#pragma once

#include "ForgeControls.h"

// The two things a macro has that no other control does: the plate beside its
// knob, and the name under it.
//
// The number on the plate stays a number, and the name never replaces it. The
// matrix's SOURCE column is a host-facing choice parameter whose list is built
// once, when the parameters are, so it will always read MACRO 1 and cannot
// follow a rename without breaking every automation lane pointed at it. Keeping
// the number where the hand goes is what lets a renamed macro still be found in
// the matrix — and a grip wants a small, constant target anyway, which a name
// of any length is not.
namespace rhino::forge::ui
{
// A macro's drag handle: two storeys, the number you take hold of on top and
// under it how many slots this macro is driving.
class MacroPlate final : public SourceHandle
{
public:
    using SourceHandle::SourceHandle;

    // Set by the editor on every modulation refresh. Zero is drawn as a dash
    // rather than as a figure: eight zeroes down the column is eight things to
    // read and nothing to learn, and what the eye is looking for is the macros
    // that are doing something.
    int destinations = 0;

    // The lower storey, which is clicked rather than dragged.
    juce::Rectangle<int> countBounds() const
    {
        const auto area = getLocalBounds();
        return area.withTrimmedTop(area.getHeight() * numberShare / 100);
    }

    // Whether a press here opens this macro's routings instead of starting a
    // drag. The count is a door as well as a figure — it is the only place that
    // can say what a macro reaches from the macro's own end — but a plate this
    // small cannot afford a dead zone, so the lower storey takes the press only
    // while it has something to show. A right-click anywhere on the plate
    // always does, so the gesture still answers on a macro driving nothing.
    //
    // Split by where the press lands, the same way ModKnob splits its ring off
    // its body: it is what lets the count become a control without the plate
    // losing the grab a hand already knows.
    bool opensMenu(juce::Point<int> where, bool popup) const
    {
        return popup || (destinations > 0 && countBounds().contains(where));
    }

    // Which of the two this half of the plate is, said by the cursor, the same
    // way a knob's says where its ring stops and its body begins. Without it
    // the whole plate offers the dragging hand and half of it does not drag.
    void mouseMove(const juce::MouseEvent& event) override
    {
        setMouseCursor(opensMenu(event.getPosition(), false)
                           ? juce::MouseCursor::PointingHandCursor
                           : juce::MouseCursor::DraggingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const auto lit = dragging || isMouseOver();
        const auto live = destinations > 0;
        const auto area = getLocalBounds().toFloat().reduced(0.6f);

        // Quieter at rest than a source tag in a module header. Eight of these
        // stand in one narrow column beside the knobs they belong to, and at
        // the tag's own weight the column read as eight blue boxes with knobs
        // between them rather than as eight macros.
        g.setColour(lit ? accent.withAlpha(0.3f) : juce::Colour(0xff0a0d18));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(lit ? accent : line.withAlpha(0.9f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);

        const auto counter = countBounds().toFloat();
        const auto number = area.withBottom(counter.getY());

        // The seam between the storeys stops short of both edges, so it reads
        // as the plate being divided rather than as a line drawn across it.
        g.setColour(line.withAlpha(lit ? 0.9f : 0.6f));
        g.fillRect(juce::Rectangle<float>(number.getX() + 2.0f, counter.getY(),
                                          number.getWidth() - 4.0f, 1.0f));

        g.setColour(lit ? juce::Colours::white : accent.withAlpha(0.9f));
        g.setFont(panelFont(Face::emphasis, juce::jmin(13.0f, number.getHeight() * 0.74f)));
        g.drawText(caption, number, juce::Justification::centred);

        g.setColour(live ? accent.withAlpha(lit ? 1.0f : 0.8f) : mutedText.withAlpha(0.55f));
        g.setFont(panelFont(Face::reading, juce::jmin(11.0f, counter.getHeight() * 0.82f)));
        g.drawText(live ? juce::String(destinations) : juce::String("-"), counter,
                   juce::Justification::centred);
    }

private:
    // How the plate divides. The number is the grip and is read at a glance;
    // the count is read when you go looking for it, so it takes the smaller
    // storey.
    static constexpr int numberShare = 58;
};

// The name strip under a macro's knob. Empty until you give it one, and it says
// so only under the pointer: eight copies of the word NAME down the column is
// clutter, where one under the hand is an invitation.
class MacroName final : public juce::Label
{
public:
    MacroName()
    {
        setJustificationType(juce::Justification::centred);
        // Double-click edits and losing focus commits, which is the gesture
        // asked for. Note that it is also the gesture that takes a knob back to
        // its default — the macro cell is the one place on the panel where a
        // double-click means two things, and which one it means is decided by
        // the pixel it lands on.
        setEditable(false, true, false);
        setBorderSize(juce::BorderSize<int>(0, 2, 0, 2));
        setColour(juce::Label::textColourId, mutedText);
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::outlineWhenEditingColourId, electricBlue);
        setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff05070e));
        setColour(juce::Label::textWhenEditingColourId, text);
        setFont(panelFont(Face::label, nameSize));
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    }

    // Small enough that the strip holds a name rather than a word and a half,
    // and the panel's own label size would not fit inside it at all.
    static constexpr float nameSize = 9.5f;

    void paint(juce::Graphics& g) override
    {
        if (isBeingEdited() || getText().isNotEmpty())
        {
            juce::Label::paint(g);
            return;
        }
        if (!isMouseOver()) return;
        g.setColour(mutedText.withAlpha(0.45f));
        g.setFont(getFont());
        g.drawText("NAME", getLocalBounds(), juce::Justification::centred);
    }
};
}
