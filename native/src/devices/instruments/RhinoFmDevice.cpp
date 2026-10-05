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
    OperatorControls controls;
    controls.ratio = param(prefix + "Ratio", label + " Ratio").range(0.5f, 16.0f, 0.5f).defaultValue(defaults.ratio)
                         .section(label)
                         .format([] (float value)
                         {
                             return "x" + juce::String(value, value == std::floor(value) ? 0 : 1);
                         });
    controls.detune = param(prefix + "Detune", label + " Detune").range(-50.0f, 50.0f, 1.0f)
                          .defaultValue(defaults.detune).section(label)
                          .format([] (float value)
                          {
                              return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " ct";
                          });
    controls.level = param(prefix + "Level", label + " Level").range(0.0f, 1.0f).defaultValue(defaults.level)
                         .unit(ParamUnit::percent).section(label);
    controls.attack = param(prefix + "Attack", label + " Attack").range(0.0f, 10.0f).skewAround(0.1f)
                          .defaultValue(defaults.attack).unit(ParamUnit::seconds).section(label);
    controls.decay = param(prefix + "Decay", label + " Decay").range(0.0f, 20.0f).skewAround(1.0f)
                         .defaultValue(defaults.decay).unit(ParamUnit::seconds).section(label);
    controls.sustain = param(prefix + "Sustain", label + " Sustain").range(0.0f, 1.0f).defaultValue(defaults.sustain)
                           .unit(ParamUnit::percent).section(label);
    controls.release = param(prefix + "Release", label + " Release").range(0.0f, 20.0f).skewAround(1.0f)
                           .defaultValue(defaults.release).unit(ParamUnit::seconds).section(label);
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

void RhinoFmDevice::process(RenderBlock& block)
{
    if (block.numChannels == 0)
        return;
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
    engine.setSettings(settings);
    // Envelopes fall towards zero for as long as a note is held, and a float
    // that small is slow on every x86 that does not flush it.
    juce::ScopedNoDenormals noDenormals;
    engine.render(block.channels[0], block.numChannels > 1 ? block.channels[1] : nullptr, block.numSamples);
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
