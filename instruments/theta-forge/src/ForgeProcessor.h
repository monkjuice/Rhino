#pragma once

#include "../core/ForgeCore.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace theta::forge
{
class Processor final : public juce::AudioProcessor
{
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
    juce::AudioProcessorValueTreeState state;
    // The editor's keyboard plays through this, so notes struck on screen reach
    // the voice the same way notes from the host do.
    juce::MidiKeyboardState keyboardState;

    // ENV 1's live position, published once per block for the editor to draw.
    // The audio thread writes, the message thread reads; nothing else crosses.
    float envelopeLevel() const { return meterLevel.load(std::memory_order_relaxed); }
    int envelopeStage() const { return meterStage.load(std::memory_order_relaxed); }

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
    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    Core core;
    std::atomic<float> meterLevel {0.0f};
    std::atomic<int> meterStage {0};
    std::array<std::atomic<float>, destinationCount> meterOffsets {};
    Patch patch() const;
    Modulation modulation() const;
    juce::ValueTree migrated(const juce::ValueTree& savedState) const;
};
}
