#pragma once

#include "../core/DuckEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <vector>

namespace rhino::duck
{
// Every parameter's id, spelled once. Saved in projects and presets, so
// never renamed.
namespace id
{
inline constexpr const char* mix = "mix";
inline constexpr const char* curve = "curve";
inline constexpr const char* length = "length";
inline constexpr const char* trigger = "trigger";
inline constexpr const char* loop = "loop";
inline constexpr const char* end = "end";
inline constexpr const char* band = "band";
inline constexpr const char* cutoff = "cutoff";
inline constexpr const char* bandSolo = "bandSolo";
inline constexpr const char* antiClick = "antiClick";
inline constexpr const char* midiChannel = "midiChannel";
inline constexpr const char* midiNote = "midiNote";
}

// The longest Attack a curve takes, in milliseconds.
inline constexpr float longestAttackMs = 100.0f;

// Rhino Duck as a host sees it: the parameters, the buses (a stereo pair in
// and out, and a sidechain pair the Audio trigger listens to), the state, and
// the engine fed once a block with the host's clock and the notes that
// trigger.
//
// The sixteen curves' Hold, Release, Attack, Depth and Shift are not
// parameters. They are kept per curve on the state tree, saved with the
// project, and written only by the panel, as the curve buttons' own memory
// rather than something to automate.
class Processor final : public juce::AudioProcessor,
                        private juce::AsyncUpdater
{
public:
    Processor();
    ~Processor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Rhino Duck"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState state;

    // ---- the curves' own memory, message thread ----------------------------
    // A curve's adjustments as the audio reads them now, Attack converted to
    // phase at the current cycle's length.
    CurveAdjust curveAdjust(int curve) const noexcept;
    float curveAttackMs(int curve) const noexcept;
    // Where the sloped part starts, and how long it runs, in phase; each kept
    // within the room the other leaves.
    void setCurveHold(int curve, float start);
    void setCurveRelease(int curve, float length);
    void setCurveAttack(int curve, float milliseconds);
    void setCurveDepth(int curve, float depth);
    void setCurveShift(int curve, float shift);
    void resetCurve(int curve);
    void resetAllCurves();
    // One cycle in seconds, at the host's last tempo and the Length chosen:
    // what turns the curve's phases into the milliseconds the panel shows.
    double cycleSeconds() const noexcept;
    // Moves on every adjustment and every loaded state, so the panel can
    // notice by comparing one number.
    int curveRevision() const noexcept { return revision.load(std::memory_order_relaxed); }

    // ---- what the panel reads -----------------------------------------------
    const Engine& engine() const noexcept { return duck; }
    // The window's size as a factor of the panel's, kept with the state.
    float uiScale() const;
    void setUiScale(float);

    // A latency change is reported to the host on the message thread. The
    // tests call this where a message loop would.
    void applyPendingLatency() { handleUpdateNowIfNeeded(); }

private:
    void handleAsyncUpdate() override;
    Settings settingsNow() const noexcept;
    void writeCurves(juce::ValueTree&) const;
    void readCurves(const juce::ValueTree&);

    Engine duck;
    // The rate the engine was prepared at. The host's own figure is read
    // through getSampleRate(), which a test driving prepareToPlay directly
    // leaves at zero.
    double preparedRate = 48000.0;
    // A curve's memory, each a field of CurveAdjust except Attack, which is
    // kept in milliseconds so a change of tempo leaves it alone.
    struct Memory
    {
        std::atomic<float> hold { 0.0f }, shift { 0.0f }, release { 1.0f }, attackMs { 0.0f }, depth { 1.0f };

        // Back to the curve as designed.
        void forget() noexcept
        {
            const CurveAdjust design;
            hold.store(design.hold, std::memory_order_relaxed);
            shift.store(design.shift, std::memory_order_relaxed);
            release.store(design.release, std::memory_order_relaxed);
            attackMs.store(0.0f, std::memory_order_relaxed);
            depth.store(design.depth, std::memory_order_relaxed);
        }
    };
    std::array<Memory, curveCount> memory;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> revision { 0 };
    std::atomic<int> wantedLatency { 0 };

    const std::atomic<float>* mixValue = nullptr;
    const std::atomic<float>* curveValue = nullptr;
    const std::atomic<float>* lengthValue = nullptr;
    const std::atomic<float>* triggerValue = nullptr;
    const std::atomic<float>* loopValue = nullptr;
    const std::atomic<float>* endValue = nullptr;
    const std::atomic<float>* bandValue = nullptr;
    const std::atomic<float>* cutoffValue = nullptr;
    const std::atomic<float>* bandSoloValue = nullptr;
    const std::atomic<float>* antiClickValue = nullptr;
    const std::atomic<float>* midiChannelValue = nullptr;
    const std::atomic<float>* midiNoteValue = nullptr;

    // Reserved at construction so no block allocates.
    std::vector<int> midiFrames;
};
}
