#pragma once
// The device library is reached through its catalog, never through a device
// header: what Session needs is a device's identity and metadata, not its DSP.
#include "DeviceCatalog.h"
#include "ClipGeometry.h"
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace rhino
{
struct PresetPattern;
// Held only as a pointer here, so the definition stays where it is used: the
// count-in click is an audio callback of Rhino's own, and only
// SessionRecording.cpp needs to see how it works.
class CountInClick;

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
    // The limits the control bar reads so that the fields it offers and the
    // values setTimeSignature accepts cannot drift apart. The signature range
    // is Live's: any numerator from 1 to 99 over a power of two up to 16.
    static constexpr double minimumTempo = 40.0, maximumTempo = 240.0;
    static constexpr int minimumTimeSignatureNumerator = 1, maximumTimeSignatureNumerator = 99;
    static constexpr std::array<int, 5> timeSignatureDenominators {1, 2, 4, 8, 16};
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
    // The way back to the top of the song without first clicking there. It
    // takes the playback start with it, so the next stop lands at zero too
    // rather than jumping back to a selection that is no longer where the
    // transport is.
    void returnToStart();
    // Where playback starts from: the line the arrangement's selection sits on.
    // Stop returns the transport here rather than to the top of the song, so
    // play after stop picks up where the last click put the line. The view
    // sets it; nothing here knows how it was chosen.
    void setPlaybackStart(double seconds);
    double playbackStart() const { return playbackStartSeconds; }
    void releasePlayingNotes();
    void panicReset(bool restartAudioDevice = true);
    void releaseAudioDevice();
    static constexpr int steps = 512, defaultSteps = 16, pitches = 16, lowestNote = 48;
    // The MIDI clip open in the note editor. A document with no MIDI track has
    // none, so ask hasPatternClip() before pattern(); every note edit below
    // refuses rather than reaching for a clip that is not there.
    bool hasPatternClip() const { return patternClip != nullptr; }
    te::MidiClip& pattern() const { jassert(patternClip != nullptr); return *patternClip; }
    int editorStepResolution() const;
    int editorStepCount() const;
    double patternLengthBeats() const;
    // A clip trimmed from its left edge - by a split, or by the region a copy
    // was taken from - keeps its whole sequence and shows a window into it.
    // These say where that window starts, in the sequence's own beats, and
    // every step-to-beat conversion in the note editor goes through them.
    // Without that the editor reads the sequence from its own zero and shows
    // notes the clip never plays, which is what the step grid's playhead has
    // always known and the notes beside it did not.
    double clipOffsetBeats(const te::Clip&) const;
    double patternOffsetBeats() const;
    double beatForStep(double step) const;
    double stepForBeat(double beat) const;
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
    // created deliberately instead, by double-click or Ctrl+A. The clip it made
    // is handed back through created, because the gesture that makes a clip
    // also opens it and the caller has no other way to name it.
    juce::Result createClip(int track, double startSeconds, te::EditItemID* created = nullptr);
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
    // What a track is for. A track used to become one or the other only when
    // something landed on it, which left a new track unable to hold a clip
    // until an instrument had been dropped on it. A track now says which it is
    // from the moment it is created, and the arrangement makes the person
    // choose rather than guessing on their behalf.
    //
    // The declaration is final. Nothing dropped on a track changes what it
    // is: an instrument, a drum kit, a MIDI effect and a MIDI clip go on a
    // MIDI track, audio files and audio clips go on an audio track, and audio
    // effects go on either. A track that changed kind under a drop made every
    // lane a guess about what it would accept next, and made "what is this
    // track" a question only the chain could answer.
    enum class TrackType { audio, midi };
    TrackType trackType(int track) const;
    juce::Result addTrack(TrackType);
    // Shorthand for addTrack(TrackType::audio), kept because most callers - an
    // import, a drop below the last lane - want a track for audio and say so.
    juce::Result addAudioTrack() { return addTrack(TrackType::audio); }
    juce::Result removeAudioTrack(int track);
    // What Ctrl+T adds: the kind last chosen from the add-track menu, MIDI
    // until one has been. It describes how the person works rather than the
    // song, so it is a preference of this machine and not part of a document.
    static TrackType lastAddedTrackType();
    static void setLastAddedTrackType(TrackType);
    static juce::String trackTypeName(TrackType);
    // How tall the track's row is drawn. Zero means the arrangement is still
    // choosing, so a project that has never been resized keeps following the
    // panel height; dragging a card's edge pins a height that outlives reopen.
    float trackLaneHeight(int track) const;
    juce::Result setTrackLaneHeight(int track, float height);
    juce::Result setTrackName(int track, const juce::String& name);
    // The card's own colour, and the colour its clips take unless one of them
    // has been given a colour of its own.
    juce::Colour trackColour(int track) const;
    juce::Result setTrackColour(int track, juce::Colour);
    static const std::vector<juce::Colour>& trackColourPalette();
    // What a track created right now would be coloured: a palette entry picked
    // at random from the ones no track is already wearing. Every path that
    // makes a track calls it, so a stack is legible the moment it exists
    // rather than after someone has coloured it by hand.
    juce::Colour pickTrackColour() const;
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
    // For a device whose editor needs more than a list of knobs -- a live
    // meter, or a scale. Null unless that slot holds a plugin.
    te::Plugin* devicePlugin(int track, int slot) const;
    DeviceTarget lastTouchedDeviceParameter() const { return lastTouchedParameter; }
    juce::Result beginDeviceParameterGesture(int track, int slot, int parameter);
    juce::Result setDeviceParameter(int track, int slot, int parameter, float value);
    juce::Result endDeviceParameterGesture(int track, int slot, int parameter);
    juce::Result toggleDeviceEnabled(int track, int slot);
    juce::Result deleteDevice(int track, int slot);
    // A reorder is expressed in positions within deviceSlots, not plugin
    // indices: the chain hides the channel strip and any dormant instrument,
    // and what is dragged is what is on screen. toDevice is where the device
    // ends up, so moveDevice(track, 2, 0) puts the third device first.
    juce::Result moveDevice(int track, int fromDevice, int toDevice);
    // A device that takes audio from another track as well as from its own -
    // a sidechain. Rhino Vocoder is the one that does today: the voice is on
    // the track and the carrier is whatever synth is chosen here. The tap sits
    // after the source track's devices and mixer and before its mute, so the
    // usual move of muting the synth leaves the carrier running.
    struct SidechainSource
    {
        int track = -1;
        juce::String name;
    };
    // Empty for a device that takes no sidechain. Never includes the track the
    // device is on: a track cannot play itself.
    std::vector<SidechainSource> deviceSidechainSources(int track, int slot) const;
    // The chosen source track, or -1 for none - which is also the answer once
    // the source track has been deleted.
    int deviceSidechainSource(int track, int slot) const;
    juce::Result setDeviceSidechainSource(int track, int slot, int sourceTrack);
    juce::Result clearDeviceSidechainSource(int track, int slot);

    double tempo() const;
    void setTempo(double bpm);
    TimeSignature timeSignature() const;
    juce::Result setTimeSignature(int numerator, int denominator);
    double beatsPerBar() const;
    // A tempo dragged across the control bar is one undo step, not one per
    // pixel. It matters more here than on a fader: every tempo change rescales
    // every automation lane and the loop range with it, so an unbracketed drag
    // fills the undo stack with hundreds of entries that each did that work.
    // The signature shares the bracket because the two are dragged side by side
    // and a change to either means the same kind of thing.
    void beginTempoGesture();
    void endTempoGesture();
    bool clickTrackEnabled() const;
    bool clickTrackEmphasiseBars() const;
    float clickTrackGain() const;
    void setClickTrackEnabled(bool enabled);
    void setClickTrackEmphasiseBars(bool enabled);
    void setClickTrackGain(float gainDb);
    // Recording. A track is armed, not an input: what a track records is
    // decided by what the track already is, so there is nothing to choose and
    // nothing that can disagree with the rest of the document. A track running
    // an instrument takes the MIDI input, a track without one takes the audio
    // input chosen in Audio settings, and a group bus or the main row takes
    // neither.
    enum class RecordInput { none, midi, audio };
    RecordInput trackRecordInput(int track) const;
    bool isTrackArmed(int track) const;
    juce::Result setTrackArmed(int track, bool armed);
    void toggleTrackArmed(int track);
    bool anyTrackArmed() const;
    bool isRecording() const;
    // Plays a note into the MIDI input, exactly where a controller's would
    // arrive. Everything downstream therefore treats it as one: arming decides
    // which track hears it, monitoring whether it is audible, and recording
    // captures it. Serves the computer keyboard, and would serve anything else
    // that wants to play without being a MIDI device.
    void sendMidiInputNote(int midiNote, int velocity, bool isNoteOn);
    // True when there is a MIDI input to send to at all.
    bool hasMidiInput() const;
    // Which MIDI input a track listens to, as Live's MIDI From chooser does.
    // It is a property of the track, so it travels with the track and is saved
    // with the project; absent means All Ins, which is what every document
    // written before this says and what a new track gets.
    //
    // The token is what is stored. All Ins is the empty string so that the
    // default writes nothing at all, and a named device is prefixed so that a
    // keyboard called "None" cannot mean anything but itself.
    struct InputChoice
    {
        juce::String token;
        juce::String name;
        bool available = true;
    };
    std::vector<InputChoice> midiInputChoices() const;
    juce::String trackMidiInput(int track) const;
    // What the card and the menu show: the device's name rather than its token.
    juce::String trackMidiInputName(int track) const;
    juce::Result setTrackMidiInput(int track, const juce::String& token);
    static juce::String midiInputAllInsToken();
    static juce::String midiInputKeyboardToken();
    static juce::String midiInputNoneToken();
    static juce::String midiInputDeviceToken(const juce::String& deviceName);
    // Which audio input a track records from, which is the same idea one
    // signal down: an audio track names a WaveInputDevice the way a MIDI track
    // names a MidiInputDevice, it is stored on the track, and it is saved with
    // the project. The empty token is the input Audio settings made the
    // default, so a track that has never been asked writes nothing at all --
    // and a project carried to another machine follows that machine's default
    // rather than naming an interface that is not plugged into it.
    std::vector<InputChoice> audioInputChoices() const;
    juce::String trackAudioInput(int track) const;
    juce::String trackAudioInputName(int track) const;
    juce::Result setTrackAudioInput(int track, const juce::String& token);
    static juce::String audioInputDefaultToken();
    static juce::String audioInputNoneToken();
    static juce::String audioInputDeviceToken(const juce::String& deviceName);
    // True when there is a MIDI keyboard on the machine - something other than
    // the two virtual devices Rhino and the engine make for themselves. False
    // means the typing keyboard is the only way to play a MIDI track, which is
    // worth saying out loud at the moment a track is armed.
    bool hasHardwareMidiInput() const;
    // Hearing yourself. These are Live's three Monitor settings under another
    // spelling, and the engine happens to carry exactly the same three.
    //
    //   Off  records the input without playing it back.
    //   Auto plays it back only while the track is armed.
    //   On   plays it back at all times, armed or not -- which is what makes
    //        it the setting you leave a track on while you set a level.
    //
    // It is a property of the *track*, because a track now names its own
    // input: both choosers live on the card and the pair of them is what a
    // track takes in. It travels with the track when the stack is reordered
    // and is saved with the project, exactly as the arm flag is.
    //
    // The default is per kind rather than one value. A MIDI track is `auto`,
    // which is what every armed instrument track did before this existed and
    // the only setting under which playing one makes a sound. An audio track
    // is `off`, where Live starts at `auto`: the machine most people run this
    // on has a microphone at one end and speakers at the other, so Live's
    // default is a feedback tone that lasts as long as the track stays armed.
    // Absent therefore writes nothing, and `trackMonitoring` is the one place
    // that resolves it.
    //
    // The engine carries the mode on the *device*, with the arm flag as the
    // only per-track term, so two tracks sharing one input cannot hold two
    // different modes at once. `applyRecordArming` gives such a device the
    // strongest mode any of its tracks asked for -- on beats auto beats off --
    // which is the only reconciliation that never silences a track that asked
    // to hear itself. Give the two tracks different inputs and each gets
    // exactly what its card says.
    enum class InputMonitoring { off, automatic, on };
    InputMonitoring trackMonitoring(int track) const;
    juce::Result setTrackMonitoring(int track, InputMonitoring);
    static juce::String inputMonitoringName(InputMonitoring);
    // Where the recording started, so the arrangement can draw the span being
    // recorded. Negative when nothing is being recorded.
    double recordingStartSeconds() const { return recordingStart; }
    // What is being played, while it is being played. The engine writes no
    // clip until the transport stops, so a take would otherwise be invisible
    // until it was over; it does keep a small fifo of the incoming notes for
    // exactly this purpose, and these turn that into something drawable.
    struct RecordingNote
    {
        double startSeconds = 0.0;
        // Negative while the key is still down, so a note being held is drawn
        // out to the playhead rather than given an end it does not have yet.
        double endSeconds = -1.0;
        int pitch = 0;
        bool isHeld() const { return endSeconds < 0.0; }
    };
    // Drains the engine's fifo into the lists below. Cheap, lock-free, and
    // safe to call when nothing is recording - the arrangement calls it once
    // a frame while a take is running.
    void pollRecordingNotes();
    const std::vector<RecordingNote>& recordingNotes(int track) const;
    // Bumped whenever a note starts or ends, so a view can tell that the
    // picture changed without comparing the lists.
    juce::int64 recordingNotesRevision() const { return liveNoteRevision; }
    // The count-in, in bars: zero is off and four is the longest. It counts
    // with the playhead standing still, so the transport starts on the beat
    // after the last one counted rather than rolling in from before it.
    static constexpr int maximumCountInBars = 4;
    int countInBars() const;
    void setCountInBars(int bars);
    bool isCountingIn() const;
    // Bars still to count, from the full count down to one. Zero when idle.
    int countInBarsRemaining() const;
    void cancelCountIn();
    // The record button and F9. Starts the count-in, or the recording when
    // there is no count-in, or stops the one already running.
    juce::Result toggleRecording();
    void stopRecording();
    // Polled by the shell: the engine starts and finishes a recording without
    // broadcasting either, so the clips it created have to be tidied and
    // announced once the transport has actually come to rest.
    void recordingStopped();
    // Recorded audio is written here: beside the project once it has been
    // saved, and in the application's own folder until then.
    juce::File recordingDirectory() const;
    // Merged audio follows the same rule, in a folder of its own: a take and a
    // flattened clip are different things to go looking for.
    juce::File mergedAudioDirectory() const;
    void undo();
    void redo();
    juce::Result setLoopRange(double startSeconds, double endSeconds);
    void clearManualLoopRange();
    bool hasManualLoopRange() const { return manualLoop; }
    // What the transport will actually turn at, whether or not a span was
    // dragged on the ruler. refreshLoop is what puts it on the transport.
    tracktion::core::TimeRange loopRange() const;
    void refreshLoop();
    te::Clip* findClip(te::EditItemID) const;
    te::WaveAudioClip* findAudioClip(te::EditItemID) const;
    bool shouldShowClipInArrangement(te::Clip&) const;
    // Lanes are addressed by the device they drive; the lane is displayed on
    // target.track, so an automation always sits under the track it belongs to.
    std::vector<TrackAutomation> trackAutomations(int track) const;
    AutomationLaneState trackAutomationState(DeviceTarget) const;
    // What a value anywhere on a lane's curve reads as on the knob it drives -
    // "2.0 dB", "12%" - so the arrangement can print the level under the
    // pointer rather than a bare number between the parameter's two limits.
    juce::String automationValueText(DeviceTarget, float value) const;
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
    // Every clip in the list cut at the same time, in one undo step, so a line
    // drawn across four lanes cuts all four and one Ctrl+Z puts all four back.
    // Clips the time misses are passed over rather than refusing the gesture:
    // the caller hands over what a lane or a selection holds, not a list it has
    // already filtered. splitCount, when asked for, reports how many were cut.
    juce::Result splitClips(const std::vector<te::EditItemID>&, double splitTimeSeconds,
                            int* splitCount = nullptr);
    juce::Result duplicateClip(te::EditItemID);
    // What a merge did, so the gesture that asked for it can select the clips
    // it made and say what went into them.
    struct MergeResult
    {
        // One per track merged, in the order the tracks were reached.
        std::vector<te::EditItemID> clips;
        // How many clips went in, which is not the size of the selection: a
        // clip the span reached is merged whether or not it was selected.
        int sourceCount = 0;
        // True when a source carried clip-local effects, which the engine
        // cannot render apart from the track's own - see SessionMerge.cpp.
        bool lostClipEffects = false;
    };
    // Flattens the selected audio clips down to one clip per track. The span
    // from the first clip's start to the last one's end is rendered to a new
    // audio file - gaps included, because the silence between two clips is
    // part of what is being merged - and everything inside that span is
    // replaced by a single clip reading that file.
    //
    // What the render carries is the clips and nothing else: each clip's own
    // gain, pan, pitch, fades, mute and reverse are applied, because those are
    // properties of the clip, while the track's devices and its fader are not,
    // because the merged clip still plays through them.
    juce::Result mergeClips(const std::vector<te::EditItemID>&, MergeResult* = nullptr);
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
    // What a clip was coloured with by hand, or nothing, in which case it is
    // drawn in its track's colour. Cycling runs through the palette and then
    // back to nothing, so a clip can always rejoin its lane.
    static juce::Colour clipColour(const te::Clip&);
    juce::Result cycleClipColour(te::EditItemID);
    int clipPluginCount(te::EditItemID) const;
    // An audio clip's own mix. Gain, pan, pitch, the two fades, mute and
    // reverse all live on the clip's state rather than its track, so they
    // travel with the clip, are undone with it, and leave every other clip on
    // that track alone - which is what makes this a clip editor and not a
    // second track mixer. Serves AudioClipPanel.
    struct AudioClipMix
    {
        bool valid = false;
        juce::String name;
        juce::File sourceFile;
        double startSeconds = 0.0, endSeconds = 0.0, offsetSeconds = 0.0;
        // The source material, and how fast the clip reads it. A clip plays
        // the span [offset, offset + length) of the source, scaled by speed.
        double sourceLengthSeconds = 0.0, speedRatio = 1.0;
        double fadeInSeconds = 0.0, fadeOutSeconds = 0.0;
        float gainDb = 0.0f, pan = 0.0f, pitchSemitones = 0.0f;
        bool muted = false, reversed = false;
        double lengthSeconds() const { return endSeconds > startSeconds ? endSeconds - startSeconds : 0.0; }
    };
    static constexpr float minimumClipGainDb = -60.0f, maximumClipGainDb = 24.0f;
    static constexpr float maximumClipPitchSemitones = 24.0f;
    AudioClipMix audioClipMix(te::EditItemID) const;
    juce::Result setAudioClipGainDb(te::EditItemID, float decibels);
    juce::Result setAudioClipPan(te::EditItemID, float pan);
    juce::Result setAudioClipPitch(te::EditItemID, float semitones);
    juce::Result setAudioClipFadeIn(te::EditItemID, double seconds);
    juce::Result setAudioClipFadeOut(te::EditItemID, double seconds);
    juce::Result setAudioClipMuted(te::EditItemID, bool muted);
    juce::Result setAudioClipReversed(te::EditItemID, bool reversed);
    // Time warp: making an audio clip follow the song's tempo instead of
    // playing at the speed it happened to be recorded at. It is the same idea
    // Live calls warping, and it has three parts.
    //
    // The switch (followsTempo) says the clip is measured in beats rather than
    // in seconds. Its content is `beats` beats long at `clipBpm`, so the clip
    // stretches as the song's tempo moves and its musical position holds.
    //
    // The mode says how the stretching is done. Repitch does not stretch at
    // all - it resamples, so the clip changes pitch with its speed, which is
    // what a record player does and what a lot of dance music wants. The
    // others hold the pitch and differ in how they treat what is in the file.
    //
    // The markers are the fine work. A marker pins one moment in the file to
    // one moment in beat time; the audio between two markers is stretched to
    // fit, so a take that drifts can be pulled back onto the grid a beat at a
    // time. Markers are off until someone asks for them, because a clip whose
    // tempo is simply steady needs none of it.
    enum class WarpMode { repitch, beats, tones, texture, complex };
    static constexpr int warpModeCount = 5;
    static juce::String warpModeName(WarpMode);
    // One line on what the mode is for, shown in the chooser.
    static juce::String warpModeBlurb(WarpMode);
    struct WarpMarkerView
    {
        // Where in the file, and where that moment is played. Both in seconds;
        // the beats the person reads come from the edit's tempo sequence.
        double sourceSeconds = 0.0, warpSeconds = 0.0;
    };
    struct ClipWarp
    {
        bool valid = false;
        bool followsTempo = false;
        bool markersEnabled = false;
        WarpMode mode = WarpMode::beats;
        double clipBpm = 0.0;
        double beats = 0.0;
        double sourceLengthSeconds = 0.0;
        // Always at least two: one at the start of the file and one at its
        // end, which is what the engine seeds a new warp with. Everything
        // between them is the person's.
        std::vector<WarpMarkerView> markers;
        // Where the file's own attacks are, so a marker dropped near one lands
        // on it. Detected on a worker thread the first time a clip is warped,
        // so the answer is not ready in the same frame it was asked for.
        std::vector<double> transients;
        bool transientsReady = false;
    };
    // Live's range, which is wider than the song tempo's: a loop can honestly
    // be recorded at 30 or at 400, and saying so is how it gets warped right.
    static constexpr double minimumClipBpm = 20.0, maximumClipBpm = 999.0;
    // How near a transient a new marker has to land to be pulled onto it.
    static constexpr double warpMarkerSnapSeconds = 0.05;
    ClipWarp clipWarp(te::EditItemID) const;
    // How fast a clip reads its source, in source seconds per played second.
    // For an unwarped clip that is the engine's own speed ratio; for a warped
    // one the engine leaves that at one and derives the speed from the clip's
    // tempo against the song's, so anything drawing the clip's waveform has to
    // ask here rather than reading getSpeedRatio and drawing the wrong part of
    // the file. Cheap: it reads two numbers off the clip and never builds a
    // warp manager, which is why the arrangement can call it per clip per sync.
    double clipPlaybackSpeed(const te::AudioClipBase&) const;
    juce::Result setClipFollowsTempo(te::EditItemID, bool);
    juce::Result setClipWarpMode(te::EditItemID, WarpMode);
    // The clip's own tempo. Changing it rescales the window the clip shows
    // into its source, so doubling it halves the clip on the timeline - which
    // is what the :2 and *2 buttons are, through scaleClipBpm.
    juce::Result setClipBpm(te::EditItemID, double bpm);
    juce::Result scaleClipBpm(te::EditItemID, double factor);
    // Asks the engine to work the tempo out from the audio. Blocking, and
    // honest about failing: a clip with no steady pulse has no answer.
    juce::Result detectClipBpm(te::EditItemID);
    juce::Result setClipWarpMarkersEnabled(te::EditItemID, bool);
    // sourceSeconds is a position in the file. The marker is created where the
    // clip currently plays that moment, so adding one changes nothing until it
    // is dragged.
    juce::Result addClipWarpMarker(te::EditItemID, double sourceSeconds);
    juce::Result moveClipWarpMarker(te::EditItemID, int index, double warpSeconds);
    // The first and last markers cannot be taken away - they are the ends of
    // the file - so removing either straightens it instead.
    juce::Result removeClipWarpMarker(te::EditItemID, int index);
    juce::Result resetClipWarpMarkers(te::EditItemID);
    // A slider drag is one undo step and one notification, not one per pixel:
    // the panel brackets the drag with these and the setters in between stay
    // quiet. Nested calls are counted, so a caller cannot end another's.
    void beginAudioClipGesture(const juce::String& actionName);
    void endAudioClipGesture();
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
    // SessionWarp.cpp - holds a warped clip's length in step with its content
    // after the content's length in beats has changed.
    static void rescaleWarpedClip(te::WaveAudioClip&, double previousContentSeconds);
    // The one place the engine's warp state is written, from the switch and
    // the mode Rhino records on the clip.
    void applyWarpState(te::WaveAudioClip&, bool on, WarpMode);
    // Repitch is a speed ratio rather than a beat-based clip, so it is the one
    // mode that has to be rewritten when the song's tempo moves.
    void updateRepitchedClips();
    // SessionAudioClips.cpp - find the clip, open a transaction unless a
    // gesture already has one open, apply, and notify once it is over.
    juce::Result applyAudioClipEdit(te::EditItemID, const juce::String& actionName,
                                    const std::function<void(te::WaveAudioClip&)>&);
    // SessionRegion.cpp - the region edit itself, without a transaction or a
    // notification, so paste can clear and insert inside one undo step.
    bool clearClipRegionInEdit(double startSeconds, double endSeconds, int firstTrack, int lastTrack,
                               const std::vector<te::EditItemID>& keep = {});
    // Two clips may never overlap on one track. A clip that has just been
    // dropped, dragged or trimmed is the one that wins the span it now covers:
    // the clips already there are split at its edges and the part underneath
    // it is removed. This is the only place that rule lives.
    void makeRoomForClip(te::Clip&, const std::vector<te::EditItemID>& alsoKeep = {});
    // Deleting clips, deleting a track, undo and reopening can all take the
    // clip the note editor is pointed at. This finds it again by id, and
    // otherwise puts the editor on the first MIDI track's first MIDI clip,
    // giving that track a hidden starter clip if it has none. A document with
    // no MIDI track leaves the editor with no clip at all.
    void repairPatternClip();
    // SessionRecording.cpp - arming is Rhino's state, held on the track, and
    // the engine's input destinations are rebuilt from it rather than being a
    // second place the answer lives.
    juce::Result applyRecordArming();
    // The MIDI input Rhino records from and plays into: the one chosen in
    // Audio settings, or the first that is there. One rule, so what a note is
    // played into is what a recording is captured from.
    te::MidiInputDevice* midiInputDevice() const;
    // SessionMidiInput.cpp - what a track's MIDI From setting resolves to.
    // Both virtual devices are the engine's own: it always makes the merge of
    // the physical inputs, and Rhino makes the typing keyboard's the first
    // time a track asks for it, because creating one rescans the device list.
    te::MidiInputDevice* allMidiInsDevice() const;
    te::MidiInputDevice* computerKeyboardDevice() const;
    te::MidiInputDevice* ensureComputerKeyboardDevice();
    te::MidiInputDevice* midiInputDeviceForTrack(int track) const;
    // SessionAudioInput.cpp - the same question for audio. The default is the
    // one Audio settings chose, or the first the device offers when nothing
    // has been; null means the track is set to None, or names a device this
    // machine does not have.
    te::WaveInputDevice* defaultWaveInputDevice() const;
    te::WaveInputDevice* audioInputDeviceForTrack(int track) const;
    // A physical input only feeds the All Ins merge while it is open, so
    // opening them is part of what All Ins means rather than a side effect.
    void enablePhysicalMidiInputs();
    // The engine rebuilds its MIDI device list on a timer, so a device created
    // here does not exist yet when the call that asked for it returns. This
    // watches for the rebuild and arms against what is then there - which also
    // means a keyboard plugged in while Rhino is running is picked up.
    struct MidiDeviceWatcher final : juce::ChangeListener
    {
        explicit MidiDeviceWatcher(Session& owner) : session(owner) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override;
        Session& session;
    };
    void midiDevicesChanged();
    std::unique_ptr<MidiDeviceWatcher> midiDeviceWatcher;
    // Set while a virtual device has been asked for and the rebuild that will
    // produce it has not landed. Arming does not complain about a missing
    // device in that window: it is on its way.
    bool awaitingMidiDeviceScan = false;
    bool reapplyingArming = false;
    void clearRecordArming();
    void beginTransportRecording();
    // The tidy-up and the notification, with no question about whether the
    // transport has got there yet. recordingStopped is the polled entry.
    void finishRecording();
    // makeRoomForClip over whatever the recording just added. The engine
    // inserts a recorded clip on top of what is already on the track; Rhino's
    // rule is that the newcomer wins the ground it landed on.
    bool tidyRecordedClips();
    CountInClick& countInClick();
    // Attaching is separate from building, so a count is configured while
    // nothing is registered on the device and no block can be in flight
    // through the state start() is rewriting.
    void attachCountIn();
    // And detaching is separate from cancelling, because a count that reached
    // its last beat has nothing left to cancel and still has to come off the
    // device: anything left registered there is rendered every block.
    void detachCountIn();
    // SessionPreview.cpp
    static bool readPreviewPreference();
    void ensurePreviewAttached();
    void releasePreview();
    void buildStarterEdit();
    void refreshAfterUndoRedo(bool changed);
    te::PluginList* pluginListForTrack(int track) const;
    // SessionSidechain.cpp - called as a track is deleted, so nothing is left
    // pointing at it. Inside the caller's transaction, so undo restores both.
    void clearSidechainSourcesNaming(te::EditItemID sourceTrack);
    te::ClipSlot* clipSlotAt(int track, int scene) const;
    te::VolumeAndPanPlugin* trackVolumePlugin(int track) const;
    void ensureTrackMixers();
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
    // Every path that makes a track goes through here, so no track can exist
    // without having said what it is for. It appends the row, names it,
    // colours it, writes its type and gives it its utility device; the caller
    // owns the undo transaction and whatever reconciling the new row needs.
    te::AudioTrack* appendTrack(TrackType);
    void initialiseExternalPlugins(bool retry = false);
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
    int audioClipGestureDepth = 0, tempoGestureDepth = 0;
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
    // Built on first use and, like the preview, detached before the device it
    // is registered with goes.
    std::unique_ptr<CountInClick> countIn;
    bool countInAttached = false;
    // The clips the armed tracks held when recording started, so the ones the
    // engine adds afterwards can be told apart from the ones already there.
    std::vector<te::EditItemID> clipsBeforeRecording;
    // One list per track, filled only for the tracks actually recording MIDI.
    std::vector<std::vector<RecordingNote>> liveNotes;
    juce::int64 liveNoteRevision = 0;
    double recordingStart = -1.0;
    // Where Stop puts the transport back to. A fresh document has never had a
    // line placed on it, so it is the top of the song until something moves it.
    double playbackStartSeconds = 0.0;
    // record() is asked for on the message thread and begins on the audio
    // thread, so there is a window in which a recording has been started and
    // the transport still reports that it is not recording. Without this the
    // poll would read that window as a recording that had already finished.
    bool recordingStarted = false;
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
int runArrangementGeometryTest();
// Not a test: times what the interface costs and prints medians. See
// tests/UiProfile.cpp.
int runUiProfile();
// Rhino Tune's and Rhino EQ's checks are their own translation units because
// they measure rendered audio rather than reading state back, and that needs
// a page of scaffolding -- a transform, a tone generator -- that nothing else
// wants. runSelfTest calls both.
void checkAutoTuneDsp(Session&);
void checkRhinoEqDsp(Session&);
void checkVocoderDsp(Session&);
}
