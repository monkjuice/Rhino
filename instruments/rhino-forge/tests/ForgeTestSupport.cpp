#include "ForgeTestSupport.h"

#include <algorithm>
#include <cmath>

namespace rhino::forge::tests
{
int failures = 0;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void requireClose(float actual, float expected, float tolerance, const char* message)
{
    if (std::abs(actual - expected) <= tolerance) return;
    std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual << ")\n";
    ++failures;
}

void requireText(const juce::String& actual, const juce::String& expected, const char* message)
{
    if (actual == expected) return;
    std::cerr << "FAIL: " << message << " (expected \"" << expected << "\", got \"" << actual << "\")\n";
    ++failures;
}

void setValue(rhino::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return; }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float value(const rhino::forge::Processor& processor, const char* id)
{
    const auto* raw = processor.state.getRawParameterValue(id);
    if (raw == nullptr) { require(false, id); return 0.0f; }
    return raw->load();
}

juce::String textFor(const rhino::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return {}; }
    return parameter->getText(parameter->convertTo0to1(plainValue), 32);
}

// Render a held note and report the loudest sample. Used to prove a source is
// silent rather than merely quiet.
float peakForNote(rhino::forge::Processor& processor, int samples)
{
    processor.prepareToPlay(48000.0, samples);
    juce::AudioBuffer<float> buffer(2, samples);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    processor.processBlock(buffer, midi);
    return buffer.getMagnitude(0, samples);
}

void renderNote(rhino::forge::Processor& processor, juce::AudioBuffer<float>& buffer, int note)
{
    processor.prepareToPlay(48000.0, buffer.getNumSamples());
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    processor.processBlock(buffer, midi);
}

// Counted on the settled part of the render, past the envelope attack.
int zeroCrossings(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto crossings = 0;
    for (int i = from + 1; i < buffer.getNumSamples(); ++i)
        if ((buffer.getSample(channel, i - 1) < 0.0f) != (buffer.getSample(channel, i) < 0.0f))
            ++crossings;
    return crossings;
}

// How far the level swings over the course of a render, in dB, measured on the
// short-term RMS rather than on single samples. A detuned stack is supposed to
// move - that movement is the chorus - but it is supposed to move continuously.
// A stack whose members are evenly spaced instead swings in and out of phase
// all together on one slow period, and that reads as a throb rather than as
// chorus. The depth of the swing is what tells the two apart.
float envelopeDepthDb(const juce::AudioBuffer<float>& buffer, int channel, int from,
                      int window)
{
    std::vector<float> levels;
    for (auto start = from; start + window <= buffer.getNumSamples(); start += window)
    {
        auto sum = 0.0;
        for (auto i = start; i < start + window; ++i)
        {
            const auto value = static_cast<double>(buffer.getSample(channel, i));
            sum += value * value;
        }
        levels.push_back(static_cast<float>(std::sqrt(sum / window)));
    }
    if (levels.size() < 8) return 0.0f;
    std::sort(levels.begin(), levels.end());
    // Trimmed, so one stray window cannot stand for the whole render.
    const auto low = levels[levels.size() / 20];
    const auto high = levels[levels.size() - 1 - levels.size() / 20];
    return juce::Decibels::gainToDecibels(high / juce::jmax(low, 1.0e-9f));
}

float rms(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto sum = 0.0;
    const auto count = buffer.getNumSamples() - from;
    for (int i = from; i < buffer.getNumSamples(); ++i)
        sum += static_cast<double>(buffer.getSample(channel, i)) * buffer.getSample(channel, i);
    return count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
}

// High-frequency content relative to overall level. Amplitude-independent, so
// it measures how open a filter is without being fooled by a quieter note.
float brightness(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto edges = 0.0, total = 0.0;
    for (int i = from + 1; i < buffer.getNumSamples(); ++i)
    {
        const auto sample = static_cast<double>(buffer.getSample(channel, i));
        const auto step = sample - buffer.getSample(channel, i - 1);
        edges += step * step;
        total += sample * sample;
    }
    return total > 0.0 ? static_cast<float>(std::sqrt(edges / total)) : 0.0f;
}

bool allSamplesFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(buffer.getSample(channel, i))) return false;
    return true;
}

bool identical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(channel, i) != b.getSample(channel, i)) return false;
    return true;
}

// ---------------------------------------------------------------- engine ---

// Reduce a processor to one clean sine from oscillator A, so the waveform can
// be measured directly.
void soloSineOnA(rhino::forge::Processor& processor)
{
    for (const auto* id : {"oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(processor, id, 0.0f);
    setValue(processor, "oscAEnable", 1.0f);
    setValue(processor, "oscAPosition", 0.0f);
    setValue(processor, "oscAUnison", 1.0f);
    setValue(processor, "oscADetune", 0.0f);
    setValue(processor, "oscAPan", 0.0f);
    setValue(processor, "oscALevel", 1.0f);
    setValue(processor, "oscAOctave", 0.0f);
    setValue(processor, "oscASemitone", 0.0f);
    setValue(processor, "oscAFine", 0.0f);
    setValue(processor, "drive", 0.0f);
    setValue(processor, "env1Attack", 0.001f);
}

// A patch with a filter that is closed enough for cutoff modulation to be
// plainly audible, and no other slot interfering.
void closedFilterOnA(rhino::forge::Processor& processor)
{
    soloSineOnA(processor);
    // A saw, for the harmonics the filter is there to remove. Named through the
    // table rather than written as a bare number: the position a shape sits at
    // moves whenever the table gains a frame, and a test that hard-codes one is
    // silently measuring a different sound afterwards.
    setValue(processor, "oscAPosition", 6.0f / 9.0f);
    setValue(processor, "filterEnable", 1.0f);
    setValue(processor, "routeA", 1.0f);
    setValue(processor, "cutoff", 300.0f);
    setValue(processor, "resonance", 0.0f);
    for (int slot = 1; slot <= rhino::forge::modSlotCount; ++slot)
        setSlot(processor, slot, srcOff, destOff, 0.0f);
}

void setSlot(rhino::forge::Processor& processor, int slot, float source, float destination, float depth)
{
    const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
    setValue(processor, id("Source").toRawUTF8(), source);
    setValue(processor, id("Dest").toRawUTF8(), destination);
    setValue(processor, id("Depth").toRawUTF8(), depth);
}

}
