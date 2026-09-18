#pragma once

#include "../core/ForgeCore.h"
#include "../core/ForgeTableStore.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace rhino::forge
{
class Processor final : public juce::AudioProcessor
{
    // Declared before `state`, and deliberately first in the class. Members are
    // initialised in declaration order, and POSITION's value formatter reads the
    // table's frame count to say which frame it has landed on — so the store has
    // to exist before parameterLayout() runs, which happens while `state` is
    // being constructed.
    WavetableStore tables;

public:
    Processor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Rhino Forge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::Result savePreset(const juce::File&, const juce::String& name);
    juce::Result loadPreset(const juce::File&);

    // The tables the oscillators read, and the frames behind them. The editor
    // draws on these directly; nothing else outside this class should.
    WavetableStore& tableStore() noexcept { return tables; }

    // Read a wavetable file into one oscillator's table: an ordinary .wav of
    // single-cycle frames laid end to end. Message thread only — it opens a
    // file and builds a table.
    juce::Result importTable(int oscillator, const juce::File&);
    juce::AudioProcessorValueTreeState state;
    // The editor's keyboard plays through this, so notes struck on screen reach
    // the voice the same way notes from the host do.
    juce::MidiKeyboardState keyboardState;

    // An envelope's live position, published once per block for the editor to
    // draw. The audio thread writes, the message thread reads; nothing else
    // crosses. All four report the loudest voice's copy of themselves, so the
    // panel shows one voice rather than four unrelated readings.
    float envelopeLevel(int env = ampEnv) const { return meter(meterLevel, env); }
    int envelopeStage(int env = ampEnv) const
    {
        return env >= 0 && env < envCount
            ? meterStage[static_cast<size_t>(env)].load(std::memory_order_relaxed) : 0;
    }

    // Where an LFO is in its cycle, and what it last put out. The phase moves
    // the indicator across its display; the value is published too because a
    // sample-and-hold's step cannot be worked back out of the phase. An LFO
    // that answers the keyboard reports the loudest voice's copy of itself,
    // exactly as ENV 1's display follows that voice.
    float lfoPhase(int lfo) const { return meter(meterLfoPhase, lfo); }
    float lfoValue(int lfo) const { return meter(meterLfoValue, lfo); }

    // The rate an LFO actually runs at: the rate knob when it is set in Hertz,
    // a division of the host's tempo when it is set in beats.
    float lfoRateHz(int lfo) const;

    // How far the matrix is moving a destination right now, in that
    // destination's normalised space, so the knob pointed at it can draw where
    // its value actually is while a source plays it. Published the same way.
    float modulationOffset(int destination) const
    {
        return destination > 0 && destination < destinationCount
            ? meterOffsets[static_cast<size_t>(destination)].load(std::memory_order_relaxed)
            : 0.0f;
    }

    // What one of a rack slot's general knobs reads as, in the unit whichever
    // type that slot holds gives it. Public because the panel asks the same
    // question to label the knob's bubble and to decide whether the knob is in
    // use at all. Message thread and audio thread both reach it; everything it
    // touches is an atomic parameter read.
    juce::String fxKnobText(int rack, int slot, int knob, float value) const;

    // Which type a slot holds, and therefore what its controls are.
    const FxTypeInfo& fxSlotType(int rack, int slot) const;

private:
    // Not static: POSITION's readout closes over this Processor so it can name
    // the frame it is on in whichever table the oscillator is reading, and a
    // rack knob's closes over it for the same reason.
    juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    Core core;
    std::array<std::atomic<float>, envCount> meterLevel {};
    std::array<std::atomic<int>, envCount> meterStage {};
    std::array<std::atomic<float>, lfoCount> meterLfoPhase {};
    std::array<std::atomic<float>, lfoCount> meterLfoValue {};
    // One reader for every bank of published meters, whatever it is a bank of:
    // an index outside the bank reads as nothing rather than off the end.
    template <size_t count>
    static float meter(const std::array<std::atomic<float>, count>& from, int index)
    {
        return index >= 0 && index < static_cast<int>(count)
            ? from[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 0.0f;
    }
    // The host's tempo as of the last block, for a synced LFO to divide.
    std::atomic<double> hostBpm {0.0};
    std::array<std::atomic<float>, destinationCount> meterOffsets {};
    Patch patch() const;
    Modulation modulation() const;
    juce::ValueTree migrated(const juce::ValueTree& savedState) const;
    // A table is not a parameter, so it travels beside the parameter state
    // rather than inside it: written as a child node when an oscillator's table
    // has been drawn on or loaded over, and read back before the parameters are
    // applied. A saved state that names no table resets that oscillator to the
    // built-in ten, for the same reason an omitted parameter returns to its
    // default rather than keeping the previous patch's value.
    void appendTables(juce::ValueTree& tree) const;
    void applyTables(const juce::ValueTree& tree);
};
}
