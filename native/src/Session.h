#pragma once
// The device library is reached through its catalog, never through a device
// header: what Session needs is a device's identity and metadata, not its DSP.
#include "DeviceCatalog.h"
#include "ClipGeometry.h"
#include <optional>
#include <vector>

namespace rhino
{
struct PresetPattern;
// Held only as a pointer here, so the definition stays in the device library.
class UtilityDevice;

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
    // Declared by the device catalog so there is one spelling of the idea.
    using DeviceKind = rhino::DeviceKind;
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
    ~Session();
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
    // Auditioning a library sound. The preview is mixed alongside the edit
    // rather than routed through it, so it chooses no track, opens no undo
    // transaction and changes nothing about the project.
    juce::Result previewSample(const juce::File&);
    juce::Result previewBuiltInSample(BuiltInSample);
    void stopPreview();
    bool isPreviewing() const;
    bool previewEnabled() const { return browserPreview; }
    void setPreviewEnabled(bool enabled);
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
    // Adding a device by its catalog id is the general form; the three enum
    // overloads below are shorthand for the devices that had an enum before
    // the catalog existed. A device added from now on needs no enum.
    juce::Result addDevice(const juce::String& deviceId, int track);
    juce::Result addClipDevice(const juce::String& deviceId, te::EditItemID);
    juce::Result addAudioEffect(AudioEffect, int track);
    juce::Result addClipAudioEffect(AudioEffect, te::EditItemID);
    juce::Result addInstrument(Instrument, int track);
    // A drum kit is the drum instrument plus a kit selection, so these behave
    // like any other instrument drop: the track switches to Rhino Drums and
    // takes that kit's name.
    juce::Result addDrumKit(DrumKit, int track);
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
    // A group is a bus. Its own track carries the group's name, colour, fader,
    // mute, solo and devices, and every member's output is routed into it. That
    // track is an ordinary audio track, so every track-indexed call here
    // reaches a bus without a second code path - but it takes audio effects
    // only: a sum has nothing for an instrument to play, no sequence for a MIDI
    // effect to act on, and nowhere to put a clip.
    struct TrackGroup
    {
        int id = 0;
        int busTrack = -1;
        juce::String name;
        juce::Colour colour;
        bool collapsed = false;
        int firstTrack = 0;  // the first member, always busTrack + 1
        int trackCount = 0;  // the members, not counting the bus
        int lastTrack() const { return firstTrack + trackCount - 1; }
        bool contains(int track) const { return trackCount > 0 && track >= firstTrack && track <= lastTrack(); }
        bool covers(int track) const { return track == busTrack || contains(track); }
    };
    // In track order, one entry per bus. A bus with no members is still a group.
    std::vector<TrackGroup> trackGroups() const;
    std::optional<TrackGroup> trackGroup(int groupId) const;
    // Zero is "not in a group", so it can never be mistaken for a group index.
    // A bus is not a member of its own group: it answers the second of these.
    int trackGroupId(int track) const;
    int trackGroupBusId(int track) const;
    bool isGroupBusTrack(int track) const;
    // Grouping makes a bus and carries the tracks under it: they end up as one
    // run starting where the topmost of them already was.
    juce::Result groupTracks(std::vector<int> tracks, const juce::String& name = {});
    juce::Result addTrackToGroup(int track, int groupId);
    juce::Result addTracksToGroup(std::vector<int> tracks, int groupId);
    juce::Result removeTrackFromGroup(int track);
    juce::Result removeTracksFromGroup(std::vector<int> tracks);
    // Deletes the bus and hands its members back to the main output. The tracks
    // themselves survive: what goes is the mixing point.
    juce::Result ungroupTracks(int groupId);
    juce::Result setTrackGroupCollapsed(int groupId, bool collapsed);
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
    // movingWith names the other clips travelling in the same gesture, so a
    // clip carried onto ground another selected clip is still vacating cannot
    // delete it on the way past.
    juce::Result editClip(te::EditItemID, ClipGeometry, ClipGesture, int targetTrack = -1,
                          const std::vector<te::EditItemID>& movingWith = {});
    juce::Result splitClip(te::EditItemID, double splitTimeSeconds);
    juce::Result duplicateClip(te::EditItemID);
    void deleteClip(te::EditItemID);
    // A region is a span of time across a run of tracks - the rectangle the
    // arrangement highlights - and it is what copy, cut, paste, duplicate and
    // delete all act on. A clip crossing an edge of one is cut at that edge
    // rather than taken whole, which is what makes a region an edit of the
    // timeline instead of a selection of clips.
    struct ClipSnapshot
    {
        struct Note
        {
            double startBeats = 0.0, lengthBeats = 0.0;
            int pitch = 60, velocity = 100, colour = 0;
        };
        // Complete enough to be rebuilt without the clip it came from: cut
        // deletes the source, and undo can take it away long after the copy
        // was made. Nothing here is a pointer into the edit.
        bool midi = false;
        juce::String name;
        juce::Colour colour;
        juce::File sourceFile;                        // audio only
        double speed = 1.0;                           // audio only
        std::vector<Note> notes;                      // midi only
        Instrument instrument = Instrument::Utility;  // midi only: what its track played
        // Relative to the copied region's corner, so pasting is a translation
        // and nothing else.
        int track = 0;
        double start = 0.0, end = 0.0, offset = 0.0;
    };
    // A copied region is its own size plus what was inside it. The size is
    // carried rather than measured back off the clips, because an empty lane
    // or a trailing rest is part of what was copied even though no clip
    // records it - and pasting has to clear that part of the destination too.
    struct ClipRegion
    {
        double spanSeconds = 0.0;
        int trackSpan = 0;  // zero is one track
        std::vector<ClipSnapshot> clips;
        bool isEmpty() const { return spanSeconds <= 0.0; }
    };
    ClipRegion copyClipRegion(double startSeconds, double endSeconds, int firstTrack, int lastTrack) const;
    // Empties the region: clips inside it go, clips crossing an edge are cut
    // at that edge, and a clip spanning it is left with a hole.
    juce::Result clearClipRegion(double startSeconds, double endSeconds, int firstTrack, int lastTrack);
    // Drops a copied region at a new corner, replacing what is already there
    // the way Live does, so what lands is what was copied and nothing else.
    juce::Result pasteClipRegion(const ClipRegion&, double destinationStart,
                                 int destinationTrack, std::vector<te::EditItemID>& pasted);
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
    juce::Result insertDeviceClipInSlot(const juce::String& deviceId, int track, int scene);
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
    // Reached only through addDevice, which resolves the id and dispatches
    // on the device's kind.
    juce::Result addInstrumentDevice(const DeviceDescriptor&, int track);
    juce::Result addMidiEffectDevice(const DeviceDescriptor&, int track);

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
    // SessionRegion.cpp - the region edit itself, without a transaction or a
    // notification, so paste can clear and insert inside one undo step.
    bool clearClipRegionInEdit(double startSeconds, double endSeconds, int firstTrack, int lastTrack,
                               const std::vector<te::EditItemID>& keep = {});
    // Two clips may never overlap on one track. A clip that has just been
    // dropped, dragged or trimmed is the one that wins the span it now covers:
    // the clips already there are split at its edges and the part underneath
    // it is removed. This is the only place that rule lives.
    void makeRoomForClip(te::Clip&, const std::vector<te::EditItemID>& alsoKeep = {});
    // Deleting clips can take the clip the note editor is pointed at. This puts
    // the editor back on a clip of track one, making a starter one if the track
    // has none left, and is what keeps `pattern()` safe to call.
    void repairPatternClip();
    // SessionPreview.cpp
    static bool readPreviewPreference();
    void ensurePreviewAttached();
    void releasePreview();
    void buildStarterEdit();
    void refreshAfterUndoRedo(bool changed);
    te::PluginList* pluginListForTrack(int track) const;
    te::ClipSlot* clipSlotAt(int track, int scene) const;
    te::VolumeAndPanPlugin* trackVolumePlugin(int track) const;
    void ensureTrackMixers();
    void refreshUtilityPointers();
    // The reorder itself, without a transaction or a notification, so the group
    // calls can move several tracks inside one of their own.
    void moveTrackInEdit(int track, int destination);
    void arrangeTrackOrder(const std::vector<te::EditItemID>& desired);
    juce::Result assignTracksToGroup(std::vector<int> tracks, int groupId);
    // A group is a bus followed by one run of members, and a member's audio
    // goes to its bus. Anything that reorders or removes tracks can break both,
    // so this repairs the runs and then makes the routing follow them.
    void reconcileTrackGroups();
    void migrateLegacyTrackGroups();
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
    // Declared after the engine, so the preview is torn down before the device
    // manager it is registered with. The destructor detaches it either way.
    juce::TimeSliceThread previewThread {"Rhino preview"};
    juce::AudioFormatManager previewFormats;
    juce::AudioTransportSource previewTransport;
    juce::AudioSourcePlayer previewPlayer;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    bool browserPreview = readPreviewPreference();
    bool previewAttached = false;
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
int runArrangementGeometryTest();
}
