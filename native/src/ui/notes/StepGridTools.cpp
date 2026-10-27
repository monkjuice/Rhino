#include "StepGridInternal.h"

// The tools panel's commands, Live's Pitch and Time Utilities. Each hands the
// selection to the model's tool, which takes every note in the clip when
// nothing is selected; the rules - what a span is, where the next note is, how
// far a note may drift - are all the model's (SessionNoteTools.cpp). The
// panel's Loop switch is here too.

namespace rhino
{
bool StepGrid::reverseNotes()
{
    return applyNoteTool("Reverse notes", "Reversed", {}, [this](const auto& states)
    {
        return session.reverseNotes(states);
    });
}

bool StepGrid::legatoNotes()
{
    return applyNoteTool("Legato", "Made", " legato", [this](const auto& states)
    {
        return session.legatoNotes(states);
    });
}

bool StepGrid::humanizeNotes(double amount)
{
    return applyNoteTool("Humanize notes", "Humanized", {}, [this, amount](const auto& states)
    {
        return session.humanizeNotes(states, amount, juce::Random::getSystemRandom().nextInt64());
    });
}

// The clip's own switch rather than a note tool: it acts on the clip whatever
// is selected, and the model brackets its own undo step (SessionClipLoop.cpp).
bool StepGrid::toggleClipLoop()
{
    if (gesture != Gesture::none)
        return false;
    finishSubdivision();
    finishVelocityAdjustment();
    const auto looping = !session.isPatternLooping();
    if (const auto result = session.setPatternLooping(looping); result.failed())
    {
        report(result.getErrorMessage());
        return false;
    }
    report(looping ? "Clip loops: drag its end out in the arrangement to repeat it" : "Clip plays once");
    return true;
}

bool StepGrid::applyNoteTool(const juce::String& actionName, const juce::String& done, const juce::String& after,
                             const std::function<juce::Result(const std::vector<juce::ValueTree>&)>& tool)
{
    // A held gesture or modal tool owns the open undo step, and a tool landing
    // inside it would be taken back with it.
    if (gesture != Gesture::none)
        return false;
    finishSubdivision();
    finishVelocityAdjustment();
    const auto states = selectedStates();
    session.beginNoteGesture(actionName);
    const auto result = tool(states);
    session.endNoteGesture();
    if (result.failed())
    {
        report(result.getErrorMessage());
        return false;
    }
    const auto count = static_cast<int>(states.size());
    report(done + " " + (states.empty() ? juce::String("every note in the clip")
                                        : juce::String(count) + (count == 1 ? " note" : " notes")) + after);
    return true;
}
}
