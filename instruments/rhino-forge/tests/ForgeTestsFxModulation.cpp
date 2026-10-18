// Sources reach the actual controls inside the clipped FX rack, including
// after a bank switch or a scroll. No editor-private access is needed.
#include "ForgeTestSupport.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeMacroCell.h"
#include <algorithm>

namespace rhino::forge::tests
{
namespace
{
constexpr auto secondMacro = static_cast<ModSource>(static_cast<int>(ModSource::macro1) + 1);

juce::MouseEvent mouseEvent(juce::Component& on, juce::Point<int> at,
                            juce::Point<int> start, bool dragged = false)
{
    const auto when = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(),
            juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &on, &on, when, start.toFloat(), when, 1, dragged};
}

void selectRack(juce::AudioProcessorEditor& editor, int rack)
{
    for (auto* child : editor.getChildren())
        if (auto* card = dynamic_cast<ui::BankCard*>(child))
            if (card->isVisible() && card->getButtonText() == rackName(rack) && card->onClick)
            {
                card->onClick();
                return;
            }
    require(false, "the FX page offers the requested rack bank");
}

// Labels span the whole cell and knobs occupy its centre. Match neighbours
// in the real component tree, so this does not merely repeat layout arithmetic.
std::vector<ui::ModKnob*> rackKnobs(juce::AudioProcessorEditor& editor, const char* caption)
{
    std::vector<ui::ModKnob*> found;
    const auto visit = [&] (auto&& self, juce::Component& parent) -> void
    {
        for (auto* child : parent.getChildren())
        {
            if (&parent != &editor)
                if (auto* label = dynamic_cast<juce::Label*>(child))
                    if (label->isVisible() && label->getText() == caption)
                        for (auto* neighbour : parent.getChildren())
                            if (auto* knob = dynamic_cast<ui::ModKnob*>(neighbour))
                                if (knob->isVisible()
                                    && std::abs(knob->getBounds().getCentreX()
                                                - label->getBounds().getCentreX()) <= 1
                                    && knob->getY() >= label->getY()
                                    && knob->getY() - label->getY() <= ui::knobLabelHeight + 2)
                                    found.push_back(knob);
            self(self, *child);
        }
    };
    visit(visit, editor);
    std::sort(found.begin(), found.end(), [] (const auto* a, const auto* b)
    {
        return a->getY() < b->getY();
    });
    return found;
}

bool dragSource(juce::AudioProcessorEditor& editor, ModSource source, juce::Point<int> target)
{
    for (auto* child : editor.getChildren())
        if (auto* handle = dynamic_cast<ui::SourceHandle*>(child))
            if (handle->isVisible() && handle->source == static_cast<int>(source))
            {
                // The macro's lower half opens its routing menu once assigned.
                const juce::Point<int> grip {handle->getWidth() / 2, 2};
                const auto end = handle->getLocalPoint(&editor, target);
                editor.mouseDown(mouseEvent(*handle, grip, grip));
                editor.mouseDrag(mouseEvent(*handle, end, grip, true));
                editor.mouseUp(mouseEvent(*handle, end, grip, true));
                return true;
            }
    require(false, "the selected modulation source has a visible drag handle");
    return false;
}

int routeCount(const Processor& processor)
{
    int count = 0;
    for (int slot = 0; slot < modSlotCount; ++slot)
        if (value(processor, modParameterId(slot, "Source").toRawUTF8()) > 0.0f
            && value(processor, modParameterId(slot, "Dest").toRawUTF8()) > 0.0f) ++count;
    return count;
}

void requireRoute(const Processor& processor, ModSource source, int destination)
{
    for (int slot = 0; slot < modSlotCount; ++slot)
        if (juce::roundToInt(value(processor, modParameterId(slot, "Source").toRawUTF8()))
                == static_cast<int>(source)
            && juce::roundToInt(value(processor, modParameterId(slot, "Dest").toRawUTF8()))
                == destination)
        {
            requireClose(value(processor, modParameterId(slot, "Depth").toRawUTF8()),
                         0.5f, 0.001f, "a source dropped on an FX control starts at audible depth");
            return;
        }
    require(false, "a source dropped on an FX control routes to that rack and slot");
}
}

void fxModulationTests()
{
    Processor processor;
    for (int rack = 0; rack < rackCount; ++rack)
        for (int slot = 0; slot < fxSlotCount; ++slot)
            setValue(processor, fxParameterId(rack, slot, "Type").toRawUTF8(),
                     static_cast<float>(FxType::filter));
    for (int slot = 1; slot <= modSlotCount; ++slot)
        setSlot(processor, slot, 0.0f, 0.0f, 0.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
    for (auto* child : editor->getChildren())
        if (auto* tab = dynamic_cast<ui::PageTab*>(child))
            if (tab->getButtonText() == "FX" && tab->onClick) tab->onClick();

    struct Assignment { const char* label; ModSource source; int control; };
    const std::array<Assignment, 4> assignments {{{"CUTOFF", ModSource::env1, 0},
        {"MIX", ModSource::lfo1, fxKnobCount}, {"RES", ModSource::macro1, 1},
        {"LEVEL", secondMacro, -1}}};
    for (int rack = 0; rack < rackCount; ++rack)
    {
        selectRack(*editor, rack);
        for (const auto& assignment : assignments)
        {
            const auto knobs = rackKnobs(*editor, assignment.label);
            require(!knobs.empty(), "the rack exposes a nested knob with its displayed label");
            if (knobs.empty()) continue;
            auto& knob = *knobs.front();
            const auto target = editor->getLocalArea(&knob, knob.getLocalBounds()).getCentre();
            const auto before = routeCount(processor);
            if (!dragSource(*editor, assignment.source, target)) continue;
            require(routeCount(processor) == before + 1, "an FX knob drop adds exactly one routing");
            const auto destination = assignment.control < 0 ? fxLevelDestinationOf(rack, 0)
                : fxDestinationOf(rack, 0, assignment.control);
            requireRoute(processor, assignment.source, destination);

            // Returning to a bank refreshes the rings without a timer or a peer.
            selectRack(*editor, (rack + 1) % rackCount);
            selectRack(*editor, rack);
            require(knob.ringDraggable, "a single FX routing exposes its depth ring");
            requireClose(knob.ringDepth, 0.5f, 0.001f, "the FX ring follows its assigned depth");
            dragSource(*editor, assignment.source, target);
            require(routeCount(processor) == before + 1, "dropping the same source reuses its FX routing");
        }
    }

    const ui::Module* fxModule = nullptr;
    for (const auto& module : ui::modules())
        if (juce::String(module.id) == "fx") fxModule = &module;
    require(fxModule != nullptr, "the FX gesture test finds the rack module");
    if (fxModule == nullptr) return;
    selectRack(*editor, 0);
    const auto area = ui::fxModuleBounds(editor->getLocalBounds(), *fxModule, false);
    const auto last = ui::fxListItemBounds(area, fxSlotCount - 1, true).getCentre();
    editor->mouseDown(mouseEvent(*editor, last, last));
    editor->mouseUp(mouseEvent(*editor, last, last));
    const auto scrolled = rackKnobs(*editor, "CUTOFF");
    require(!scrolled.empty(), "scrolling to the final effect leaves its knobs reachable");
    if (!scrolled.empty())
    {
        auto& knob = *scrolled.back();
        dragSource(*editor, ModSource::macro1,
                   editor->getLocalArea(&knob, knob.getLocalBounds()).getCentre());
        requireRoute(processor, ModSource::macro1, fxDestinationOf(0, fxSlotCount - 1, 0));
    }

    juce::MouseWheelDetails up;
    up.deltaY = 4.0f;
    const auto rackCentre = ui::fxSlotViewportBounds(area, true).getCentre();
    editor->mouseWheelMove(mouseEvent(*editor, rackCentre, rackCentre), up);
    auto clippedTargetFound = false;
    for (auto* knob : rackKnobs(*editor, "CUTOFF"))
    {
        const auto bounds = editor->getLocalArea(knob, knob->getLocalBounds());
        auto* parent = knob->getParentComponent();
        const auto viewport = editor->getLocalArea(parent, parent->getLocalBounds());
        if (bounds.getBottom() <= viewport.getBottom()) continue;
        clippedTargetFound = true;
        const juce::Point<int> hidden {bounds.getCentreX(),
                                      juce::jmax(bounds.getY(), viewport.getBottom())};
        const auto before = routeCount(processor);
        dragSource(*editor, secondMacro, hidden);
        require(routeCount(processor) == before,
                "the clipped portion of an FX knob cannot capture a source drop");
        break;
    }
    require(clippedTargetFound, "the gesture regression includes a row clipped by the rack viewport");
}
}
