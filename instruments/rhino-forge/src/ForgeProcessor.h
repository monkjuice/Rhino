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
    LfoTable lfoTable(int lfo) const;
    bool lfoTableIsCustom(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount
            && lfoCustom[static_cast<size_t>(lfo)].load(std::memory_order_relaxed);
    }
    void setLfoTable(int lfo, const LfoTable&, const juce::String& name = "Custom");
    juce::String lfoTableName(int lfo) const;
    juce::Result saveLfoTable(int lfo, const juce::File&) const;
    juce::Result loadLfoTable(int lfo, const juce::File&);

    // The rate an LFO actually runs at: the rate knob when it is set in Hertz,
    // a division of the host's tempo when it is set in beats.
    float lfoRateHz(int lfo) const;

    // How long one arp step lasts, in seconds. Worked out rather than
    // published, exactly as an LFO's rate is: it depends only on parameters and
    // the tempo, both of which the message thread can read for itself, so the
    // panel and the arp cannot end up quoting different rates. The triplet and
    // dotted switches are applied here, which is the one place they are.
    float arpStepSeconds() const;

    // The tempo as of the last block. A synced delay draws its repeats where
    // they will actually land, which needs the same tempo the engine divides.
    double hostTempo() const { return hostBpm.load(std::memory_order_relaxed); }

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

    // The colour a module's panel is drawn in, as a PanelColour's index.
    //
    // Not a parameter. Nothing about it is heard, nothing automates it, and a
    // host that found it in the automation list would be offering to draw a
    // curve that sweeps a plate from red to green. It rides on the state
    // tree's own properties instead, which is what carries it into a preset
    // and into a project without it becoming something a DAW can sequence.
    int panelColour(const juce::String& moduleId) const;
    void setPanelColour(const juce::String& moduleId, int choice);

    // What a macro has been named, or an empty string for one that has not
    // been. Not a parameter, for the same reasons a plate colour is not: it is
    // text, it has no range, and a host offered it in an automation list would
    // be offering to sweep a word. It rides on the state tree's properties, so
    // a preset and a project carry it without a DAW being able to sequence it.
    //
    // The matrix goes on calling this macro by its number whatever it is named:
    // a slot's SOURCE is a choice parameter whose list is fixed when the
    // parameters are built, and text that followed a rename would move under
    // every automation lane already pointed at it.
    juce::String macroName(int macro) const;
    // Trimmed, upper-cased and cut to maxMacroNameLength. Empty clears it.
    void setMacroName(int macro, const juce::String& name);
    // A name is a tag rather than a caption: the strip under a macro's knob
    // holds about a dozen characters, and this is the point past which more
    // text is only text nobody can read on the panel.
    static constexpr int maxMacroNameLength = 24;

private:
    // Not static: POSITION's readout closes over this Processor so it can name
    // the frame it is on in whichever table the oscillator is reading, and a
    // rack knob's closes over it for the same reason.
    juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    Core core;
    // In front of the voices rather than inside them: it consumes the notes
    // arriving and hands Core the ones the pattern actually plays.
    Arp arp;
    ArpSettings arpSettings() const;
    // What the arp was doing last block, so the two transitions that need
    // acting on can be spotted: switched off mid-phrase, which has to let go of
    // the notes its own gate clock was still holding, and the latch released,
    // which has to drop the chord no finger is on.
    bool arpWasEnabled = false;
    bool arpWasLatched = false;
    // The rate prepareToPlay was given, kept rather than read back from
    // AudioProcessor::getSampleRate(). That one is set by the host calling
    // setRateAndBufferSizeDetails, which nothing does when prepareToPlay is
    // called directly — so it reads zero, and an arp dividing by it stepped
    // once per sample and stacked every voice the synth had.
    double preparedSampleRate = 44100.0;
    // Where the host's transport is, for LAUNCH QUANT. An arp started off the
    // grid waits for the next division of the bar, and the only way to know
    // where that is is the position the host reports.
    double hostPpq = 0.0;
    bool hostPlaying = false;
    // Hold the arp's first step until the next LAUNCH QUANT boundary. Worked
    // out here rather than in the arp, which counts in steps and knows nothing
    // about bars.
    void quantiseArpStart(int sampleIndex, const ArpSettings&);
    std::array<std::atomic<float>, envCount> meterLevel {};
    std::array<std::atomic<int>, envCount> meterStage {};
    std::array<std::atomic<float>, lfoCount> meterLfoPhase {};
    std::array<std::atomic<float>, lfoCount> meterLfoValue {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPointX {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPointY {};
    std::array<std::atomic<int>, lfoCount> lfoPointCount {};
    std::array<std::atomic<int>, lfoCount> lfoColumns {};
    std::array<std::atomic<int>, lfoCount> lfoRows {};
    std::array<std::atomic<bool>, lfoCount> lfoCustom {};
    mutable juce::CriticalSection lfoNameLock;
    std::array<juce::String, lfoCount> lfoNames {};
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
    void appendLfoTables(juce::ValueTree& tree) const;
    void applyLfoTables(const juce::ValueTree& tree);
};
}
