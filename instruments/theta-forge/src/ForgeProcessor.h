#pragma once

#include "../core/ForgeCore.h"
#include "../core/ForgeTableStore.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace theta::forge
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
    const juce::String getName() const override { return "Theta Forge"; }
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

    // ENV 1's live position, published once per block for the editor to draw.
    // The audio thread writes, the message thread reads; nothing else crosses.
    float envelopeLevel() const { return meterLevel.load(std::memory_order_relaxed); }
    int envelopeStage() const { return meterStage.load(std::memory_order_relaxed); }

    // Where LFO 1 is in its cycle, and what it last put out. The phase moves the
    // indicator across its display; the value is published too because a
    // sample-and-hold's step cannot be worked back out of the phase.
    float lfoPhase() const { return meterLfoPhase.load(std::memory_order_relaxed); }
    float lfoValue() const { return meterLfoValue.load(std::memory_order_relaxed); }

    // The rate LFO 1 actually runs at: the rate knob in free mode, a division of
    // the host's tempo in sync.
    float lfoRateHz() const;

    // How far the matrix is moving a destination right now, in that
    // destination's normalised space, so the knob pointed at it can draw where
    // its value actually is while a source plays it. Published the same way.
    float modulationOffset(int destination) const
    {
        return destination > 0 && destination < destinationCount
            ? meterOffsets[static_cast<size_t>(destination)].load(std::memory_order_relaxed)
            : 0.0f;
    }

private:
    // Not static: POSITION's readout closes over this Processor so it can name
    // the frame it is on in whichever table the oscillator is reading.
    juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    Core core;
    std::atomic<float> meterLevel {0.0f};
    std::atomic<int> meterStage {0};
    std::atomic<float> meterLfoPhase {0.0f};
    std::atomic<float> meterLfoValue {0.0f};
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
