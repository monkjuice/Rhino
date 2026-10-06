#include "RhinoFmDevice.h"
#include <cmath>

namespace rhino
{
juce::StringArray RhinoFmDevice::algorithmNames()
{
    juce::StringArray names;
    for (int algorithm = 0; algorithm < FmEngine::algorithms; ++algorithm)
        names.add(FmEngine::algorithmName(algorithm));
    return names;
}

// Each operator's seven controls, declared in this order. The names carry the
// operator's number, because an automation lane is read away from the face.
RhinoFmDevice::OperatorControls RhinoFmDevice::operatorControls(int number, OperatorDefaults defaults)
{
    const auto prefix = "op" + juce::String(number);
    const auto label = "Op " + juce::String(number);
    // The four operators share one place on the face, a tab each.
    const juce::String operatorTabs = "Operators";
    OperatorControls controls;
    controls.ratio = param(prefix + "Ratio", label + " Ratio").range(0.5f, 16.0f, 0.5f).defaultValue(defaults.ratio)
                         .section(label, operatorTabs)
                         .format([] (float value)
                         {
                             return "x" + juce::String(value, value == std::floor(value) ? 0 : 1);
                         });
    controls.detune = param(prefix + "Detune", label + " Detune").range(-50.0f, 50.0f, 1.0f)
                          .defaultValue(defaults.detune).section(label, operatorTabs)
                          .format([] (float value)
                          {
                              return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " ct";
                          });
    controls.level = param(prefix + "Level", label + " Level").range(0.0f, 1.0f).defaultValue(defaults.level)
                         .unit(ParamUnit::percent).section(label, operatorTabs);
    controls.attack = param(prefix + "Attack", label + " Attack").range(0.0f, 10.0f).skewAround(0.1f)
                          .defaultValue(defaults.attack).unit(ParamUnit::seconds).section(label, operatorTabs);
    controls.decay = param(prefix + "Decay", label + " Decay").range(0.0f, 20.0f).skewAround(1.0f)
                         .defaultValue(defaults.decay).unit(ParamUnit::seconds).section(label, operatorTabs);
    controls.sustain = param(prefix + "Sustain", label + " Sustain").range(0.0f, 1.0f).defaultValue(defaults.sustain)
                           .unit(ParamUnit::percent).section(label, operatorTabs);
    controls.release = param(prefix + "Release", label + " Release").range(0.0f, 20.0f).skewAround(1.0f)
                           .defaultValue(defaults.release).unit(ParamUnit::seconds).section(label, operatorTabs);
    return controls;
}

void RhinoFmDevice::prepare(double rate, int)
{
    engine.prepare(rate);
}

void RhinoFmDevice::clear()
{
    engine.reset();
}

FmEngine::Settings RhinoFmDevice::currentSettings() const
{
    FmEngine::Settings settings;
    settings.algorithm = algorithm.index();
    settings.feedback = feedback.value();
    settings.depth = depth.value();
    settings.velocity = velocity.value();
    settings.mono = mono.on();
    settings.glide = glide.value();
    // The bottom of the Output knob is silence rather than -36 dB.
    settings.gain = juce::Decibels::decibelsToGain(outputDb.value(), -36.0f);
    for (size_t op = 0; op < ops.size(); ++op)
    {
        const auto& controls = ops[op];
        auto& target = settings.ops[op];
        target.ratio = controls.ratio.value();
        target.detuneCents = controls.detune.value();
        target.level = controls.level.value();
        target.attack = controls.attack.value();
        target.decay = controls.decay.value();
        target.sustain = controls.sustain.value();
        target.release = controls.release.value();
    }
    return settings;
}

void RhinoFmDevice::process(RenderBlock& block)
{
    if (block.numChannels == 0)
        return;
    engine.setSettings(currentSettings());
    // Envelopes fall towards zero for as long as a note is held, and a float
    // that small is slow on every x86 that does not flush it.
    juce::ScopedNoDenormals noDenormals;
    engine.render(block.channels[0], block.numChannels > 1 ? block.channels[1] : nullptr, block.numSamples);
}

// The routing as the engine has it: an operator is a carrier when it is heard
// and a modulator when it bends the operators it feeds. The algorithm decides
// which is which; no operator is a carrier by nature.
void RhinoFmDevice::describe(DeviceDisplay& display)
{
    const auto now = currentSettings();
    if (!pictured.has_value() || !(*pictured == now))
    {
        FmEngine::picture(now, peakPicture.data(), heldPicture.data());
        pictured = now;
    }
    display.traces.push_back({ "Peak", { peakPicture.begin(), peakPicture.end() } });
    display.traces.push_back({ "Sustain", { heldPicture.begin(), heldPicture.end() } });

    const auto lastOperator = FmEngine::operators - 1;
    for (int op = 0; op < FmEngine::operators; ++op)
    {
        DeviceDisplay::Block block;
        block.label = juce::String(op + 1);
        block.section = "Op " + juce::String(op + 1);
        block.level = now.ops[static_cast<size_t>(op)].level;
        block.output = FmEngine::isCarrier(now.algorithm, op);
        juce::StringArray fed;
        for (int to = 0; to < op; ++to)
            if (FmEngine::modulates(now.algorithm, op, to))
            {
                display.links.push_back({ op, to });
                fed.add(juce::String(to + 1));
            }
        if (block.output)
            block.role = "carrier";
        else if (fed.size() == 1)
            block.role = "modulates " + fed[0];
        else
            block.role = "modulates " + fed.joinIntoString(", ", 0, fed.size() - 1) + " and " + fed[fed.size() - 1];
        if (op == lastOperator && now.feedback > 0.0f)
            block.role << ", feeds back";
        display.blocks.push_back(block);
    }
    if (now.feedback > 0.0f)
        display.links.push_back({ lastOperator, lastOperator });
}

void RhinoFmDevice::noteOn(int note, float noteVelocity, int)
{
    engine.noteOn(note, noteVelocity);
}

void RhinoFmDevice::noteOff(int note, float, int)
{
    engine.noteOff(note);
}

void RhinoFmDevice::allNotesOff()
{
    engine.releaseAll();
}

void RhinoFmDevice::controller(int number, int value, int)
{
    if (number == 64)
        engine.setSustainPedal(value >= 64);
}

// Two semitones either way, the range a keyboard's wheel is expected to have.
void RhinoFmDevice::pitchWheel(int value, int)
{
    engine.setPitchBend(static_cast<float>(value - 8192) / 8192.0f * 2.0f);
}
}
