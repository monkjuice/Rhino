// Direct manipulation of the envelope display. The picture and the knobs are
// two views of the same four host parameters; nothing is cached in the editor.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
namespace
{
const char* suffixFor(ui::EnvelopeNode node, int part)
{
    switch (node)
    {
        case ui::EnvelopeNode::attack:       return part == 0 ? "Attack" : nullptr;
        case ui::EnvelopeNode::decaySustain: return part == 0 ? "Decay" : part == 1 ? "Sustain" : nullptr;
        case ui::EnvelopeNode::release:      return part == 0 ? "Release" : nullptr;
        case ui::EnvelopeNode::none:         break;
    }
    return nullptr;
}
}

ui::EnvelopeShape Editor::envelopeShapeFor(int env) const
{
    const auto display = envelopeDisplayBounds();
    return ui::envelopeShape(ui::envelopeCurveBounds(display),
                             value(envParameterId(env, "Attack")),
                             value(envParameterId(env, "Decay")),
                             value(envParameterId(env, "Sustain")),
                             value(envParameterId(env, "Release")),
                             envelopeZoom[static_cast<size_t>(env)]);
}

ui::EnvelopeNode Editor::envelopeNodeAt(juce::Point<int> at) const
{
    const auto display = envelopeDisplayBounds();
    if (display.isEmpty() || !ui::envelopePlotBounds(display).contains(at))
        return ui::EnvelopeNode::none;
    return ui::envelopeNodeAt(envelopeShapeFor(shownEnv()), at.toFloat());
}

void Editor::beginEnvelopeNodeDrag(ui::EnvelopeNode node)
{
    if (node == ui::EnvelopeNode::none) return;
    envelopeDragNode = node;
    envelopeDragBank = shownEnv();
    envelopeDragMoved = false;
    for (int part = 0; part < 2; ++part)
        if (const auto* suffix = suffixFor(node, part))
            if (auto* parameter = processor.state.getParameter(
                    envParameterId(envelopeDragBank, suffix)))
                parameter->beginChangeGesture();
}

void Editor::editEnvelopeNode(juce::Point<int> at)
{
    if (envelopeDragNode == ui::EnvelopeNode::none
        || envelopeDragBank < 0 || envelopeDragBank >= envCount)
        return;

    const auto shape = envelopeShapeFor(envelopeDragBank);
    const ui::EnvelopeValues before {
        value(envParameterId(envelopeDragBank, "Attack")),
        value(envParameterId(envelopeDragBank, "Decay")),
        value(envParameterId(envelopeDragBank, "Sustain")),
        value(envParameterId(envelopeDragBank, "Release"))};
    const auto after = ui::envelopeValuesForNodeDrag(
        shape, before, envelopeDragNode, at.toFloat());

    const auto set = [this] (const juce::String& id, float plain)
    {
        auto* parameter = processor.state.getParameter(id);
        if (parameter == nullptr) return;
        const auto legal = parameter->getNormalisableRange().snapToLegalValue(plain);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(legal));
    };

    switch (envelopeDragNode)
    {
        case ui::EnvelopeNode::attack:
            set(envParameterId(envelopeDragBank, "Attack"), after.attack);
            break;
        case ui::EnvelopeNode::decaySustain:
            set(envParameterId(envelopeDragBank, "Decay"), after.decay);
            set(envParameterId(envelopeDragBank, "Sustain"), after.sustain);
            break;
        case ui::EnvelopeNode::release:
            set(envParameterId(envelopeDragBank, "Release"), after.release);
            break;
        case ui::EnvelopeNode::none:
            break;
    }
    repaint(envelopeDisplayBounds());
}

void Editor::endEnvelopeNodeDrag()
{
    if (envelopeDragBank >= 0 && envelopeDragBank < envCount)
        for (int part = 0; part < 2; ++part)
            if (const auto* suffix = suffixFor(envelopeDragNode, part))
                if (auto* parameter = processor.state.getParameter(
                        envParameterId(envelopeDragBank, suffix)))
                    parameter->endChangeGesture();
    envelopeDragNode = ui::EnvelopeNode::none;
    envelopeDragBank = -1;
    envelopeDragMoved = false;
}
}
