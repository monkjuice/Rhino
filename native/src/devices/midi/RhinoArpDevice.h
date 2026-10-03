#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <cstdint>
#include <limits>

namespace rhino
{
namespace te = tracktion::engine;

// A tempo-synchronised MIDI effect. It owns no voices and produces no audio:
// held input notes are consumed here and the notes it generates continue down
// the track's device chain to whichever instrument follows it.
class RhinoArpDevice final : public te::Plugin
{
public:
    inline static const char* xmlTypeName = "rhino.arp.v1";
    static const char* getPluginName() { return "Rhino Arp"; }

    // The editor lays these out as one purpose-built face, and tests use the
    // same order through Session::deviceParameters. Append rather than
    // reorder: automation addresses parameters by their exposed index.
    enum ParameterIndex
    {
        styleParameter,
        rateParameter,
        gateParameter,
        distanceParameter,
        stepsParameter,
        offsetParameter,
        grooveParameter,
        holdParameter,
        retriggerParameter,
        intervalParameter,
        repeatsParameter,
        rootParameter,
        scaleParameter,
        parameterCount
    };

    explicit RhinoArpDevice(te::PluginCreationInfo);
    ~RhinoArpDevice() override;
    juce::String getName() const override { return getPluginName(); }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getVendor() override { return "Rhino"; }
    juce::String getSelectableDescription() override { return getName(); }
    BusLayout getBusses() const override { return {}; }
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    double getLatencySeconds() override { return 0.0; }
    int getNumOutputChannelsGivenInputs(int) override { return 0; }
    void getChannelNames(juce::StringArray*, juce::StringArray*) override {}
    bool canBeAddedToClip() override { return false; }
    void reset() override;
    void midiPanic() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

private:
    enum class Style { Up, Down, UpDown, DownUp, Chord };
    enum class Groove { Straight, Swing8, Swing16 };
    enum class Retrigger { Off, Note, Beat };
    enum class Scale { Chromatic, Major, Minor };

    struct HeldNote
    {
        bool active = false;
        bool physicallyHeld = false;
        int channel = 1;
        float velocity = 0.8f;
        te::MPESourceID source = {};
    };

    struct PendingOff
    {
        bool active = false;
        double beat = 0.0;
        int pitch = 60;
        int channel = 1;
        te::MPESourceID source = {};
    };

    double rateBeats() const;
    double intervalBeats() const;
    double stepLengthBeats(int step) const;
    int activeNoteCount() const;
    int physicallyHeldCount() const;
    int noteAtOrdinal(int ordinal) const;
    int styleLength(int noteCount) const;
    int ordinalForStep(int step, int noteCount) const;
    int transposedPitch(int pitch, int transposition) const;
    void clearActiveNotes();
    void releaseLatchedNotes();
    void resetSequence(double beat);
    void addPendingOff(double beat, int pitch, int channel, te::MPESourceID source);
    void flushPendingOffs(double blockStartSeconds, double blockEndSeconds,
                          te::MidiMessageArray& output);
    void emitNote(double tickBeat, double offBeat, int sourcePitch,
                  double blockStartSeconds, double blockEndSeconds,
                  te::MidiMessageArray& output);
    void generateUntil(double endBeat, double blockStartSeconds, double blockEndSeconds,
                       te::MidiMessageArray& output);

    juce::CachedValue<float> style, rateIndex, gatePercent, distance, steps, offset;
    juce::CachedValue<float> groove, hold, retrigger, interval, repeats, root, scale;
    te::AutomatableParameter::Ptr styleParam, rateParam, gateParam, distanceParam, stepsParam;
    te::AutomatableParameter::Ptr offsetParam, grooveParam, holdParam, retriggerParam;
    te::AutomatableParameter::Ptr intervalParam, repeatsParam, rootParam, scaleParam;

    std::array<HeldNote, 128> heldNotes;
    // A full MIDI chord can schedule one release per pitch. Four banks leave
    // room for overlapping gates without ever allocating in the callback.
    std::array<PendingOff, 512> pendingOffs;
    // The first swap hands one pre-sized array to Tracktion; the steady-state
    // array is used from the second block onward, when both sides of the swap
    // already own reserved storage.
    te::MidiMessageArray firstOutput, steadyOutput;
    bool firstRender = true;
    bool clockValid = false;
    double nextTickBeat = 0.0;
    double lastBlockEndBeat = 0.0;
    int sequenceStep = 0;
    std::int64_t lastBeatRetriggerCycle = std::numeric_limits<std::int64_t>::min();
    double sampleRate = 48000.0;
};
}
