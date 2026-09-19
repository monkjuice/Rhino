#pragma once
#include "audio/UtilityDevice.h"
#include "instruments/DrumDevice.h"
#include "audio/RhinoSpaceDevice.h"
#include "audio/RhinoBloomDevice.h"
#include "midi/RhinoArpDevice.h"
#include "instruments/RhinoWaveDevice.h"
#include "ClipGeometry.h"
#include <optional>
#include <vector>

namespace rhino
{
struct PresetPattern;

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
        RhinoSpace,
        RhinoBloom
    };
    enum class Instrument
    {
        FourOsc,
        RhinoWave,
        RhinoForge,
        Drums,
        Utility
    };
    enum class MidiEffect
    {
        RhinoArp
    };
    // Rendered into a WAV cache before import, so these remain ordinary audio clips.
    enum class BuiltInSample
    {
        Whistle,
        Siren
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
    // Automation belongs to a track and spans the whole timeline, as it does in
    // every other DAW. A lane with fewer than two points has never been drawn:
    // it renders as a dotted line at the knob's resting value and drives
    // nothing, which is how a revealed-but-unused lane stays out of the way.
    struct AutomationPoint
    {
        double timeSeconds = 0.0;
        float value = 0.0f;
    };
    struct TrackAutomation
    {
        DeviceTarget target;
        juce::String parameterName;
        juce::String deviceName;
        float minimum = 0.0f;
        float maximum = 1.0f;
        float restingValue = 0.0f;
        bool ownLane = false;
        std::vector<AutomationPoint> points;
        bool active() const { return points.size() >= 2; }
        float valueAt(double seconds) const;
    };
    struct AutomationLaneState
    {
        bool visible = false;
        bool ownLane = false;
        bool active = false;
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
    struct TimeSignature
    {
        int numerator = 4;
        int denominator = 4;
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
    // Discards the open document for an untitled starter one. Callers ask about
    // unsaved changes first; this does not.
    void newProject();
    juce::Result restoreProject(const juce::ValueTree&, const juce::File&);
    void projectSaved(const juce::ValueTree&, const juce::File&);
    bool hasUnsavedChanges() const { return changeRevision != savedRevision; }
    void markModified();
    juce::File projectFile;
    juce::ListenerList<Listener> listeners;
    juce::Result importAudio(const juce::File&);
    juce::Result importAudioAt(const juce::File&, int track, double startSeconds);
    juce::Result importBuiltInSample(BuiltInSample, int track = 1, double startSeconds = -1.0);
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
    // Dropping an instrument changes the track, never its clips. Clips are
    // created deliberately instead, by double-click or Ctrl+A.
    juce::Result createClip(int track, double startSeconds);
    bool trackHasInstrument(int track) const;
    juce::Result selectPatternClip(te::EditItemID);
    bool isPatternDrums() const;
    juce::Result addAudioEffect(AudioEffect, int track);
    juce::Result addClipAudioEffect(AudioEffect, te::EditItemID);
    juce::Result addInstrument(Instrument, int track);
    // A drum kit is the drum instrument plus a kit selection, so these behave
    // like any other instrument drop: the track switches to Rhino Drums and
    // takes that kit's name.
    juce::Result addDrumKit(DrumDevice::Kit, int track);
    bool isForgeAvailable() const { return forgeDescription.has_value(); }
    juce::Result addMidiEffect(MidiEffect, int track);
    int trackCount() const;
    juce::String trackName(int track) const;
    juce::Result addAudioTrack();
    juce::Result removeAudioTrack(int track);
    // How tall the track's row is drawn. Zero means the arrangement is still
    // choosing, so a project that has never been resized keeps following the
    // panel height; dragging a card's edge pins a height that outlives reopen.
    float trackLaneHeight(int track) const;
    juce::Result setTrackLaneHeight(int track, float height);
    juce::Result setTrackName(int track, const juce::String& name);
    // The card's own colour. Clips keep the colours they were given.
    juce::Colour trackColour(int track) const;
    juce::Result setTrackColour(int track, juce::Colour);
    static const std::vector<juce::Colour>& trackColourPalette();
    juce::Result moveTrack(int track, int destination);
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
    TimeSignature timeSignature() const;
    juce::Result setTimeSignature(int numerator, int denominator);
    double beatsPerBar() const;
    bool clickTrackEnabled() const;
    bool clickTrackEmphasiseBars() const;
    float clickTrackGain() const;
    void setClickTrackEnabled(bool enabled);
    void setClickTrackEmphasiseBars(bool enabled);
    void setClickTrackGain(float gainDb);
    void undo();
    void redo();
    juce::Result setLoopRange(double startSeconds, double endSeconds);
    void clearManualLoopRange();
    bool hasManualLoopRange() const { return manualLoop; }
    void refreshLoop();
    te::Clip* findClip(te::EditItemID) const;
    te::WaveAudioClip* findAudioClip(te::EditItemID) const;
    bool shouldShowClipInArrangement(te::Clip&) const;
    // Lanes are addressed by the device they drive; the lane is displayed on
    // target.track, so an automation always sits under the track it belongs to.
    std::vector<TrackAutomation> trackAutomations(int track) const;
    AutomationLaneState trackAutomationState(DeviceTarget) const;
    juce::Result showTrackAutomation(DeviceTarget, bool ownLane);
    juce::Result hideTrackAutomation(DeviceTarget);
    juce::Result setTrackAutomationPoints(DeviceTarget, std::vector<AutomationPoint>);
    juce::Result clearTrackAutomationPoints(DeviceTarget);
    juce::Result toggleParameterAutomationOverride(int track, int slot, int parameter);
    void applyTrackAutomationAt(double timelineSeconds);
    // Playback sweeps the lanes from the UI timer, which an offline render
    // never runs. These mirror the lanes into the engine's own curves so the
    // render reads them, and hand the parameters back afterwards.
    void beginOfflineAutomation();
    void endOfflineAutomation();
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
    // The mixer. Session view and the arrangement are two presentations of
    // these same per-track values, so both read and write them through here.
    struct TrackMixer
    {
        float volumeDb = 0.0f;
        float pan = 0.0f;
        bool muted = false;
        bool soloed = false;
    };
    static constexpr float minimumVolumeDb = -60.0f, maximumVolumeDb = 6.0f;
    TrackMixer trackMixer(int track) const;
    juce::Result setTrackVolumeDb(int track, float decibels);
    juce::Result setTrackPan(int track, float pan);
    void beginTrackVolumeGesture(int track);
    void endTrackVolumeGesture(int track);
    void beginTrackPanGesture(int track);
    void endTrackPanGesture(int track);
    void setTrackMuted(int track, bool muted);
    void setTrackSoloed(int track, bool soloed);
    // The master track is addressed as one past the last audio track, so every
    // track-indexed call reaches it without a second code path or a sentinel
    // that DeviceTarget would read as invalid.
    int masterTrackIndex() const { return trackCount(); }
    bool isMasterTrack(int track) const { return track == trackCount(); }
    float masterVolumeDb() const;
    void setMasterVolumeDb(float decibels);
    void beginMasterVolumeGesture();
    void endMasterVolumeGesture();
    float masterPan() const;
    void setMasterPan(float pan);
    void beginMasterPanGesture();
    void endMasterPanGesture();
    // Session view: scenes are rows of clip slots across every track. The engine
    // owns launch timing; these calls only queue state changes from the message
    // thread and report back what the launch handles currently hold.
    static constexpr int defaultScenes = 8;
    struct SlotClip
    {
        bool hasClip = false;
        bool playing = false;
        bool playQueued = false;
        bool stopQueued = false;
        bool isMidi = false;
        juce::String name;
        juce::Colour colour;
        te::EditItemID clipID;
    };
    int sceneCount() const;
    juce::String sceneName(int scene) const;
    SlotClip slotClip(int track, int scene) const;
    bool trackHasActiveSlot(int track) const;
    juce::Result launchSlot(int track, int scene);
    juce::Result launchScene(int scene);
    void stopTrackSlots(int track);
    void stopAllSlots();
    juce::Result addScene();
    juce::Result deleteScene(int scene);
    juce::Result insertPatternPresetInSlot(PatternPreset, int track, int scene);
    juce::Result insertInstrumentClipInSlot(Instrument, int track, int scene);
    juce::Result insertAudioFileInSlot(const juce::File&, int track, int scene);
    juce::Result insertBuiltInSampleInSlot(BuiltInSample, int track, int scene);
    juce::Result deleteSlotClip(int track, int scene);
    // Session clips and arrangement clips are separate, as they are in Live.
    // These are the two ways across: -1 for scene picks the first free slot.
    juce::Result copySlotClipToArrangement(int track, int scene, double startSeconds);
    juce::Result copyClipToSlot(te::EditItemID, int scene = -1);
    int firstFreeSlot(int track) const;
    bool anyTrackPlayingSlots() const;
    void returnToArrangement();
    te::LaunchQType launchQuantisation() const;
    void setLaunchQuantisation(te::LaunchQType);
    te::SceneWatcher* sceneWatcher() const;
    te::Engine engine;
    std::unique_ptr<te::Edit> edit;
    UtilityDevice* utility = nullptr; // owned by edit's plugin list
    UtilityDevice* audioUtility = nullptr; // owned by edit's plugin list
    // A track has one instrument, so there is nothing stable to cache: switching
    // removes the previous plugin. Ask the track instead.
    te::Plugin* patternInstrument() const;
    te::Plugin* patternInstrumentForTrack(int track) const;
    Instrument patternInstrumentKind() const;
private:
    // The arrangement workflow test reaches engine-level slot state through
    // this, the same way it does for StepGrid and Arrangement.
    friend int runArrangementTest();
    struct AutomationRuntime
    {
        DeviceTarget target;
        float baseValue = 0.0f;
        bool hasBaseValue = false;
        bool overridden = false;
        bool active = false;
    };
    struct OfflineAutomation
    {
        te::AutomatableParameter::Ptr parameter;
        float restoreValue = 0.0f;
    };
    void buildStarterEdit();
    void refreshAfterUndoRedo(bool changed);
    te::PluginList* pluginListForTrack(int track) const;
    te::ClipSlot* clipSlotAt(int track, int scene) const;
    te::VolumeAndPanPlugin* trackVolumePlugin(int track) const;
    void ensureTrackMixers();
    void refreshUtilityPointers();
    void ensureSceneSlots(int minimumScenes = defaultScenes);
    std::optional<te::MonotonicBeat> nextLaunchBeat() const;
    void startTransportForLaunch();
    juce::Result preparePresetTrack(int trackIndex, const PresetPattern&, PatternPreset);
    void initialiseExternalPlugins(bool retry = false);
    void setPatternInstrument(bool useDrums);
    void ensureEditablePatternClip();
    juce::ValueTree automationOwnerState(int track) const;
    juce::ValueTree findTrackAutomationState(DeviceTarget) const;
    juce::ValueTree ensureTrackAutomationState(DeviceTarget, bool ownLane, bool keepExistingLane);
    std::vector<TrackAutomation> readTrackAutomations(int track, bool resolveParameterInfo) const;
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
    std::vector<OfflineAutomation> offlineAutomation;
    std::optional<juce::PluginDescription> forgeDescription;
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
int runArrangementGeometryTest();
}
