#pragma once
#include "UtilityDevice.h"
#include "DrumDevice.h"
#include "ThetaSpaceDevice.h"
#include "ThetaBloomDevice.h"
#include "ThetaArpDevice.h"
#include "ThetaWaveDevice.h"
#include "ClipGeometry.h"
#include <optional>
#include <vector>

namespace theta
{
// Message-thread facade. The engine owns scheduling, streaming and playback.
// Member order keeps the engine alive until its edit and devices are released.
class Session : public juce::ChangeBroadcaster
{
public:
    enum class PatternPreset
    {
        WarmPulse,
        AcidSteps,
        ArpRun,
        ChordPad,
        SubBass,
        ReeseBass,
        SirenLead,
        WavePad,
        WaveBass,
        WavePluck,
        HouseKit,
        BreakKit,
        MinimalKit,
        ClapKit
    };
    enum class AudioEffect
    {
        Equaliser,
        Reverb,
        Delay,
        Compressor,
        ThetaSpace,
        ThetaBloom
    };
    enum class Instrument
    {
        FourOsc,
        ThetaWave,
        ThetaForge,
        Drums,
        Utility
    };
    enum class MidiEffect
    {
        ThetaArp
    };
    enum class DeviceKind
    {
        MidiEffect,
        Instrument,
        AudioEffect
    };
    struct DeviceSlot
    {
        juce::String name;
        juce::String type;
        DeviceKind kind = DeviceKind::AudioEffect;
        int pluginIndex = -1;
        bool enabled = true;
        bool removable = false;
    };
    struct DeviceParameter
    {
        juce::String name;
        juce::String valueText;
        float value = 0.0f;
        float minimum = 0.0f;
        float maximum = 1.0f;
        bool discrete = false;
        bool automated = false;
        bool automationOverridden = false;
    };
    struct DeviceTarget
    {
        int track = -1;
        int slot = -1;
        int parameter = -1;
        bool isValid() const { return track >= 0 && slot >= 0 && parameter >= 0; }
    };
    struct ClipAutomation
    {
        bool active = false;
        DeviceTarget target;
        juce::String parameterName;
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        float startValue = 0.0f;
        float endValue = 0.0f;
        float minimum = 0.0f;
        float maximum = 1.0f;
    };
    struct EditorNote
    {
        juce::ValueTree state;
        double startSteps = 0.0;
        double lengthSteps = 1.0;
        int pitch = 0;
        int velocity = 100;
        int colour = 0;
    };
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editWillChange() = 0;
        virtual void editDidChange() = 0;
    };
    Session();
    static void setCommandLineTestMode(bool enabled);
    juce::ValueTree projectSnapshot();
    juce::Result restoreProject(const juce::ValueTree&, const juce::File&);
    void projectSaved(const juce::ValueTree&, const juce::File&);
    bool hasUnsavedChanges() const { return changeRevision != savedRevision; }
    void markModified();
    juce::File projectFile;
    juce::ListenerList<Listener> listeners;
    juce::Result importAudio(const juce::File&);
    juce::Result importAudioAt(const juce::File&, int track, double startSeconds);
    void togglePlayback();
    void stop();
    void releasePlayingNotes();
    void panicReset(bool restartAudioDevice = true);
    void releaseAudioDevice();
    static constexpr int steps = 512, defaultSteps = 16, pitches = 16, lowestNote = 48;
    te::MidiClip& pattern() const { return *patternClip; }
    int editorStepResolution() const;
    int editorStepCount() const;
    double patternLengthBeats() const;
    void setEditorStepCount(int newSteps);
    bool hasNote(int step, int pitch) const;
    std::vector<EditorNote> editorNotes() const;
    juce::Result addNote(double startSteps, int pitch, double lengthSteps,
                         juce::ValueTree* addedState = nullptr, int velocity = 100);
    bool removeNotes(const std::vector<juce::ValueTree>&);
    bool adjustNoteVelocities(const std::vector<juce::ValueTree>&, int percentageDelta);
    void setNote(int step, int pitch, bool enabled);
    int noteLengthSteps(int step, int pitch) const;
    juce::Result resizeNote(int step, int pitch, int lengthSteps);
    juce::Result resizeNote(int step, int pitch, double lengthSteps);
    juce::Result resizeNote(const juce::ValueTree&, double lengthSteps);
    juce::Result resizeNoteFromLeft(double startStep, int pitch, double newStartStep);
    juce::Result resizeNoteFromLeft(const juce::ValueTree&, double newStartStep);
    bool ensurePatternLengthSteps(int requiredSteps);
    juce::Result fillNoteToClipEnd(int step, int pitch);
    juce::Result fillNoteToClipEnd(const juce::ValueTree&);
    void beginNoteGesture(juce::String actionName = "Draw notes");
    void endNoteGesture();
    void clearPattern();
    void applyPatternPreset(PatternPreset);
    juce::Result insertPatternPreset(PatternPreset, int track, double startSeconds);
    juce::Result insertInstrumentClip(Instrument, int track, double startSeconds);
    juce::Result selectPatternClip(te::EditItemID);
    bool isPatternDrums() const;
    juce::Result addAudioEffect(AudioEffect, int track = 1);
    juce::Result addClipAudioEffect(AudioEffect, te::EditItemID);
    juce::Result addInstrument(Instrument, int track);
    bool isForgeAvailable() const { return forgeDescription.has_value(); }
    juce::Result addMidiEffect(MidiEffect, int track);
    int trackCount() const;
    juce::String trackName(int track) const;
    juce::Result addAudioTrack();
    juce::Result removeAudioTrack(int track);
    std::vector<DeviceSlot> deviceSlots(int track) const;
    std::vector<DeviceParameter> deviceParameters(int track, int slot) const;
    DeviceTarget lastTouchedDeviceParameter() const { return lastTouchedParameter; }
    juce::Result beginDeviceParameterGesture(int track, int slot, int parameter);
    juce::Result setDeviceParameter(int track, int slot, int parameter, float value);
    juce::Result endDeviceParameterGesture(int track, int slot, int parameter);
    juce::Result toggleDeviceEnabled(int track, int slot);
    juce::Result deleteDevice(int track, int slot);
    double tempo() const;
    void setTempo(double bpm);
    void undo();
    void redo();
    juce::Result setLoopRange(double startSeconds, double endSeconds);
    void clearManualLoopRange();
    bool hasManualLoopRange() const { return manualLoop; }
    void refreshLoop();
    te::Clip* findClip(te::EditItemID) const;
    te::WaveAudioClip* findAudioClip(te::EditItemID) const;
    bool shouldShowClipInArrangement(te::Clip&) const;
    ClipAutomation clipAutomation(te::EditItemID) const;
    std::vector<ClipAutomation> clipAutomations(te::EditItemID) const;
    juce::Result setClipAutomationRamp(te::EditItemID, DeviceTarget, double startSeconds, double endSeconds,
                                       float startValue, float endValue);
    juce::Result deleteClipAutomation(te::EditItemID, DeviceTarget);
    juce::Result toggleParameterAutomationOverride(int track, int slot, int parameter);
    void applyClipAutomationAt(double timelineSeconds);
    juce::Result moveNote(int sourceStep, int sourcePitch, int targetStep, int targetPitch);
    juce::Result moveNotes(const std::vector<std::pair<int, int>>&, int stepDelta, int pitchDelta);
    juce::Result moveNotes(const std::vector<juce::ValueTree>&, double stepDelta, int pitchDelta);
    juce::Result redistributeNotes(const std::vector<juce::ValueTree>&, int divisions,
                                   std::vector<juce::ValueTree>& replacementStates);
    juce::Result editClip(te::EditItemID, ClipGeometry, ClipGesture, int targetTrack = -1);
    juce::Result splitClip(te::EditItemID, double splitTimeSeconds);
    juce::Result duplicateClip(te::EditItemID);
    juce::Result pasteClips(const std::vector<te::EditItemID>& source, double destinationStart,
                            int destinationTrack, std::vector<te::EditItemID>& pasted);
    void deleteClip(te::EditItemID);
    juce::Result cycleClipColour(te::EditItemID);
    int clipPluginCount(te::EditItemID) const;
    void toggleTrackMute(int track);
    void toggleTrackSolo(int track);
    te::Engine engine;
    std::unique_ptr<te::Edit> edit;
    UtilityDevice* utility = nullptr; // owned by edit's plugin list
    UtilityDevice* audioUtility = nullptr; // owned by edit's plugin list
    te::FourOscPlugin* synth = nullptr; // owned by edit's plugin list
    ThetaWaveDevice* thetaWave = nullptr; // owned by edit's plugin list
    DrumDevice* drums = nullptr; // owned by edit's plugin list
private:
    struct AutomationRuntime
    {
        DeviceTarget target;
        float baseValue = 0.0f;
        bool hasBaseValue = false;
        bool overridden = false;
        bool active = false;
    };
    void refreshAfterUndoRedo(bool changed);
    void initialiseExternalPlugins(bool retry = false);
    void setPatternInstrument(bool useDrums);
    void ensureEditablePatternClip();
    AutomationRuntime& automationRuntimeFor(DeviceTarget);
    AutomationRuntime* findAutomationRuntime(DeviceTarget);
    const AutomationRuntime* findAutomationRuntime(DeviceTarget) const;
    te::MidiClip* patternClip = nullptr; // owned by edit
    te::EditItemID patternClipID;
    // Engine initialization also changes its edit flag asynchronously. Track
    // user commands separately so startup cannot dirty an untouched document.
    juce::int64 changeRevision = 0, savedRevision = 0;
    bool manualLoop = false;
    tracktion::core::TimeRange manualLoopRange;
    DeviceTarget lastTouchedParameter;
    std::vector<AutomationRuntime> automationRuntime;
    std::optional<juce::PluginDescription> forgeDescription;
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
int runArrangementGeometryTest();
}
