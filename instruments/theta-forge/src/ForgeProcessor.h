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

    // ENV 1's live position, published once per block for the editor to draw.
    // The audio thread writes, the message thread reads; nothing else crosses.
    float envelopeLevel() const { return meterLevel.load(std::memory_order_relaxed); }
    int envelopeStage() const { return meterStage.load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    Core core;
    std::atomic<float> meterLevel {0.0f};
    std::atomic<int> meterStage {0};
    Patch patch() const;
    juce::ValueTree migrated(const juce::ValueTree& savedState) const;
};
}
