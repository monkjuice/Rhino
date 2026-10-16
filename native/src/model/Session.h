#pragma once
// The device library is reached through its catalog, never through a device
// header: what Session needs is a device's identity and metadata, not its DSP.
#include "DeviceCatalog.h"
#include "DeviceDisplay.h"
#include "ClipGeometry.h"
#include "VelocityCurve.h"
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace rhino
{
struct PresetPattern;
// A device preset (core/DevicePreset.h), held only by pointer here.
struct DevicePreset;
// Held only as a pointer here, so the definition stays where it is used: the
// count-in click is an audio callback of Rhino's own, and only
// SessionRecording.cpp needs to see how it works.
class CountInClick;
// A deck's material (core/DjTrack.h), handed out by pointer for the displays.
struct DjTrack;

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
        // The catalog id, so a face is chosen by what the device is rather
        // than by its engine type. Empty for a device the catalog lacks.
        juce::String deviceId;
        // Written on the device SDK: its controls declare their own shape,
        // and the rack generates a face from them.
        bool native = false;
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
        // The shape of the control, as far as the device says. A native
        // device says all of it; for anything else these keep their defaults
        // or what the engine parameter reports.
        juce::String section;
        juce::String tabGroup;      // sections sharing one shows one at a time
        juce::StringArray choices;  // a chooser's names, in value order
        bool toggle = false;
        std::optional<float> defaultValue;
        double skew = 1.0;          // the knob's travel, as NormalisableRange::skew
        float interval = 0.0f;      // the step between values; 0 is continuous
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
    // A note's velocity as MIDI has it, 1-127. A note-on of 0 is a note-off,
    // so nothing that takes one of these writes 0.
    struct NoteVelocity
    {
        juce::ValueTree state;
        int velocity = 100;
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
    // The document's state, to be written to `destination`. The open edit
    // names each clip's audio file as it was stored against the file the edit
    // was opened from; for any other destination the copy names each one again
    // for that file - relative to it where the engine would store it so,
    // absolute otherwise - and the live clips keep their names. With no
    // destination, the names are the edit's own.
    juce::ValueTree projectSnapshot(const juce::File& destination = {});
    // Discards the open document for an untitled starter one. Callers ask about
    // unsaved changes first; this does not.
    void newProject();
    juce::Result restoreProject(const juce::ValueTree&, const juce::File&);
    void projectSaved(const juce::ValueTree&, const juce::File&);
    bool hasUnsavedChanges() const { return changeRevision != savedRevision; }
    // Fails, naming the clip and its file, when a clip on the timeline plays an
    // audio file that is not there. A render waits for every such file to open,
    // so the WAV export asks this before it starts.
    juce::Result checkTimelineFiles();
    // The song as Export WAV writes it: the timeline of `edit` from the top for
    // `seconds`, through the main chain, at unity, as a 24-bit dithered WAV at
    // the rate and block size given, all before it returns. On whichever
    // thread calls it; the caller takes the edit off the device first. A
    // render waits for every clip's file to open, and one that is there but
    // cannot be read would hold it for good, so a render that has not moved
    // for `stallSeconds` is given up and what it wrote deleted.
    static juce::Result renderTimelineWav(te::Edit&, const juce::File& destination, double seconds, double sampleRate,
                                          int blockSize, double stallSeconds = 60.0);
    // Export WAV itself: that render of this document, at the audio device's
    // rate and block size, in steps, as the bounce runs, so the shell's
    // loading screen can draw and cancel while it renders. A start refuses an
    // empty song or a missing file, stops the transport and takes the edit off
    // the device; continueExport renders for about `milliseconds` and returns
    // true while there is more to do, replacing `destination` only once all of
    // it has rendered; cancelExport stops it with the file that was there left
    // alone. Once it is over, exportOutcome says how it went, failures as a
    // sentence to show. Nothing may edit the document while one runs: the
    // shell blocks input, undo and redo wait, and replacing the document
    // cancels it.
    juce::Result startExport(const juce::File& destination);
    bool continueExport(double milliseconds);
    void cancelExport();
    bool isExporting() const { return exportRun != nullptr; }
    float exportProgress() const;
    juce::Result exportOutcome() const;
    // What an edit changed, so that what is derived from the document - the
    // engine's automation curves, a DJ deck's clip - can tell whether it is
    // behind. Kinds combine with |. A change nobody has classified is
    // `unknown`, every kind at once, which is always safe: it costs a deck its
    // clip made again, never a wrong sound.
    enum class ModelChange : unsigned
    {
        none = 0,              // nothing anything renders: a name, a colour, a view setting
        parameters = 1u << 0,  // a device parameter's value, the track's fader and pan among them
        curves = 1u << 1,      // an automation lane
        notes = 1u << 2,       // what a MIDI track plays: its notes and the clips that hold them
        audioClips = 1u << 3,  // what an audio track plays: its clips and how each one plays
        devices = 1u << 4,     // a chain's devices, or a device's own settings
        tempo = 1u << 5,       // the tempo or the time signature
        unknown = (1u << 6) - 1
    };
    friend constexpr ModelChange operator|(ModelChange a, ModelChange b) noexcept
    {
        return static_cast<ModelChange>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }
    // Every edit ends here, naming the track it changed when it knows it and
    // what it changed. `none` marks the document and nothing else; anything
    // else leaves the lanes to be mirrored again, and a change to what a clip
    // plays leaves the DJ decks playing that track's clips - every deck, for
    // -1 - to make their clip again.
    void markModified(int changedTrack = -1, ModelChange changes = ModelChange::unknown);
    juce::File projectFile;
    juce::ListenerList<Listener> listeners;
    juce::Result importAudio(const juce::File&);
    juce::Result importAudioAt(const juce::File&, int track, double startSeconds);
    // Files dropped together, laid end to end on one lane from startSeconds
    // in the order given, as one undo step. Should any of them fail, none of
    // them lands.
    juce::Result importAudioFilesAt(const std::vector<juce::File>&, int track, double startSeconds);
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
    // Sets each listed note of the open clip to the velocity beside it, held to
    // 1-127. A state the clip does not hold is skipped, and so is a note already
    // at its value. False with no open clip, or when nothing changed. Opens no
    // undo transaction of its own: the caller brackets it, as addNote's callers
    // do. Outside a note-velocity gesture it announces the change; inside one
    // the gesture's end does, once. Playback needs no help: the engine rebuilds
    // after any note property changes.
    bool setNoteVelocities(const std::vector<NoteVelocity>&);
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
    // A velocity drag, a stroke painted in the velocity lane, or V held: every
    // step reaches the notes at once, the whole gesture is one undo step, and
    // it is announced once, at its end, and only if it changed something - an
    // announcement per pixel rebuilds the arrangement every time. Counted, so a
    // caller cannot end another's, as beginAudioClipGesture is.
    void beginNoteVelocityGesture(const juce::String& actionName = "Change velocity");
    void endNoteVelocityGesture();
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
    // What the instrument on the open clip's track calls a note, when it
    // names it: a Drum Rack names each of its pads after its sound. The note
    // editor labels its drum rows with it.
    std::optional<juce::String> patternNoteName(int pitch) const;
    // The lowest of the note editor's drum rows: the first note of the bank
    // of sixteen pads the open clip's Drum Rack shows, so the rows are the
    // pads on its face. lowestNote when it plays no rack.
    int patternDrumLowestNote() const;
    // Adding a device by its catalog id is the general form; the three enum
    // overloads below are shorthand for the devices that had an enum before
    // the catalog existed. A device added from now on needs no enum.
    // Given a preset, the device is added already set the way it says, in the
    // same undo step.
    juce::Result addDevice(const juce::String& deviceId, int track, const DevicePreset* preset = nullptr);
    juce::Result addClipDevice(const juce::String& deviceId, te::EditItemID);
    // Device presets, .rnd files (core/DevicePreset.h). A preset stores every
    // control by the id it is saved under, and only Rhino's own devices have
    // them. Loading one sets each control it names and puts every other back
    // to its default, so a preset sounds the same whatever was there before.
    juce::Result saveDevicePreset(int track, int slot, const juce::File&);
    // Into the device in a slot, which must be the device the preset is for.
    // One undo step.
    juce::Result loadDevicePreset(int track, int slot, const juce::File&);
    // Adds the device a preset is for to a track, already set: what dragging a
    // preset out of the browser does. An instrument the track already runs
    // takes the preset instead, as a second copy would replace it anyway.
    juce::Result addDeviceFromPreset(const juce::File&, int track);
    juce::Result addAudioEffect(AudioEffect, int track);
    juce::Result addClipAudioEffect(AudioEffect, te::EditItemID);
    juce::Result addInstrument(Instrument, int track);
    // The Drum Rack (SessionDrums.cpp). A kit (.rdk) is a whole rack and a
    // drum preset (.rdp) one pad's sound (core/DrumKitFile.h); both are files
    // under library/Drums and the person's own Drums folder.
    //
    // A kit dropped on a track behaves like any other instrument drop: the
    // track's instrument becomes a Drum Rack playing the kit, in one undo
    // step, and the track takes the kit's name. A rack the track already
    // runs takes the kit instead.
    juce::Result addDrumKit(const juce::File& kit, int track);
    juce::Result loadDrumKit(int track, int slot, const juce::File& kit);
    juce::Result saveDrumKit(int track, int slot, const juce::File& kit);
    // One pad of the rack in a slot, each one undo step. A sample is any audio
    // file; a preset is one drum sound.
    juce::Result loadDrumPadSample(int track, int slot, int pad, const juce::File& sample);
    juce::Result loadDrumPadPreset(int track, int slot, int pad, const juce::File& preset);
    juce::Result saveDrumPadPreset(int track, int slot, int pad, const juce::File& preset);
    // A drum sound dropped on a track rather than on a pad lands on the first
    // empty pad of the track's Drum Rack, from the bank its face shows. A drum
    // preset brings a blank rack to a MIDI track running something else or
    // nothing, as an instrument drop would; a sample needs the rack to be
    // there already.
    juce::Result addDrumSound(const juce::File& soundOrSample, int track);
    bool trackHasDrumRack(int track) const;
    // Which sixteen pads a rack's face shows, from a note that is a multiple
    // of four. View state, never an undo step, but announced: the note
    // editor's drum rows follow it.
    juce::Result showDrumBank(int track, int slot, int firstNote);
    // Cuts a sample pad's part where Slice says and puts each slice on a pad
    // of its own from this one up, as one undo step. `spread` says how many
    // pads it filled.
    juce::Result spreadDrumSlices(int track, int slot, int pad, int* spread = nullptr);
    // A pad dragged onto another: its sound moves there, trading places with
    // a sound already on it, as one undo step, and the pad it landed on is
    // selected. Clips and automation lanes keep their notes.
    juce::Result moveDrumPad(int track, int slot, int from, int to);
    // Auditions a drum preset exactly as a pad would play it.
    juce::Result previewDrumSound(const juce::File& preset);
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
    // Lands directly under `below`, in its group when it is in one, so a new
    // track arrives where the person is working; with no track named - the
    // main row, or -1 - it goes at the foot of the stack. A group folded away
    // shows as one row, so a track added under any part of it lands under
    // the whole group rather than out of sight inside it. `added`, when
    // given, receives the new track's index.
    juce::Result addTrack(TrackType, int below = -1, int* added = nullptr);
    // Shorthand for addTrack(TrackType::audio), kept because most callers - an
    // import, a drop below the last lane - want a track for audio and say so.
    juce::Result addAudioTrack() { return addTrack(TrackType::audio); }
    juce::Result removeAudioTrack(int track);
    // Whole tracks, as a track card's cut, copy, paste, duplicate and delete
    // move them (SessionTrackClipboard.cpp). A copy is everything the track
    // is - its kind, name, colour, devices, clips, cells and automation - held
    // as a value, so a cut, or an undo between the copy and the paste, cannot
    // take it away. A bus stands for its group in all of them, as it does for
    // a bounce: copying one copies its members, and deleting one deletes them
    // too, as Live does. Ungroup is the way to drop the bus alone, and
    // removeAudioTrack still does that for a bus it is handed.
    struct TrackClipboard
    {
        // In stack order, each group's bus directly before its members.
        std::vector<juce::ValueTree> tracks;
        bool isEmpty() const { return tracks.empty(); }
    };
    TrackClipboard copyTracks(const std::vector<int>& tracks) const;
    // Lands the copies directly under `below`, by addTrack's rule: under an
    // open group's bus or one of its members the copies join that group,
    // under a folded one they go below all of it, and a copied group, which
    // cannot go inside another, always goes below the whole group. Nothing
    // named lands them at the foot of the stack. One undo step; `pasted`
    // receives the new tracks' indices, top to bottom.
    juce::Result pasteTracks(const TrackClipboard&, int below, std::vector<int>* pasted = nullptr);
    // A copy of the tracks lands directly under the last of them, as one
    // block, by the same rule.
    juce::Result duplicateTracks(const std::vector<int>& tracks, std::vector<int>* duplicated = nullptr);
    // Deletes the tracks, and the members of any bus among them, in one undo
    // step. Refused when it would leave the document no track at all.
    juce::Result removeTracks(const std::vector<int>& tracks);
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
    // Choosing a height unfolds a folded row.
    juce::Result setTrackLaneHeight(int track, float height);
    // A row folded down to its one line by the arrow beside the card's name,
    // as Live's fold button does, keeping the height it unfolds back to. A
    // view setting, like the height. A group's bus folds its members instead
    // (setTrackGroupCollapsed).
    bool isTrackFolded(int track) const;
    juce::Result setTrackFolded(int track, bool folded);
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
    // Only the controls from `first`, `count` of them (fewer at the end), each
    // read as the whole list reads it. A Drum Rack has 768 controls and a face
    // shows six, and reading and formatting every one cost each frame of a
    // knob drag a millisecond.
    std::vector<DeviceParameter> deviceParameters(int track, int slot, int first, int count) const;
    std::optional<DeviceParameter> deviceParameter(int track, int slot, int index) const;
    int deviceParameterCount(int track, int slot) const;
    // What a native device's face shows beside its controls, worked out by
    // the device from its controls as they stand. Empty for anything else.
    DeviceDisplay deviceDisplay(int track, int slot) const;
    // For a device whose editor needs more than a list of knobs -- a live
    // meter, or a scale. Null unless that slot holds a plugin.
    te::Plugin* devicePlugin(int track, int slot) const;
    DeviceTarget lastTouchedDeviceParameter() const { return lastTouchedParameter; }
    // A device parameter moved inside a drag is announced here instead of to
    // the whole session. One value changed, and every view rebuilding itself
    // for each pixel of a knob turn is what made the turn stutter: the rack
    // listens here to keep the dragged device's readings live, and everything
    // else hears once, when the gesture ends. lastTouchedDeviceParameter()
    // says which parameter it was.
    juce::ChangeBroadcaster deviceParameterValues;
    juce::Result beginDeviceParameterGesture(int track, int slot, int parameter);
    // A device's own settings, the ones a face writes rather than a knob: an
    // EQ band switched on, Rhino Tune's scale. change writes them, and they
    // become one undo step of their own and are announced like any other edit.
    juce::Result editDeviceSettings(int track, int slot, const juce::String& actionName,
                                    const std::function<void()>& change);
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
    // The knobs an external plugin shows (SessionMappedParameters.cpp).
    // Rhino's own devices and the engine's built-ins show every control they
    // declare. An external plugin - Rhino Forge is the one there is - declares
    // hundreds, so its rack is a chosen list: the controls a synth is played
    // by until someone says otherwise, and from then on whatever has been
    // mapped from its own window. The list is kept on the device, by the
    // plugin's own parameter ids, so it is saved with the document, follows
    // the device along the chain and survives the plugin gaining parameters.
    // A lane names a knob by its place in the list, so a knob only ever joins
    // at the end, and taking one off moves the lanes above it down.
    static constexpr int maximumMappedParameters = 32;
    bool deviceHasMappedParameters(int track, int slot) const;
    // Puts the plugin's parameter `pluginParameter` - its index in the
    // plugin's own list - at the end of the rack, as one undo step. One
    // already there, or one the plugin says cannot be automated, is refused.
    juce::Result mapDeviceParameter(int track, int slot, int pluginParameter);
    // Takes the knob at `index` off the rack with its lane, moving the lanes
    // of the knobs after it down one, as one undo step.
    juce::Result unmapDeviceParameter(int track, int slot, int index);
    // Mapping from the window: while it is on, every control touched in the
    // device's own window joins its rack. One device at a time. It ends when
    // asked, when the device leaves the chain, and with the document.
    juce::Result beginParameterMapping(int track, int slot);
    void endParameterMapping();
    bool isMappingParameters(int track, int slot) const;
    // Applies what the window has reported since the last message. The queue
    // drains on the message loop by itself; a test, which runs no loop, calls
    // this.
    void flushParameterMapping();
    // What the last control touched in the window did - mapped, already on
    // the rack, refused - for the status line. The serial moves with every
    // touch, so a line can tell a new report from the one it has shown.
    struct MappingReport
    {
        int serial = 0;
        juce::String text;
    };
    MappingReport parameterMappingReport() const { return mappingReport; }

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
    // A hardware controller Rhino drives itself, the Ableton Push
    // (app/PushController). Its ports are reserved: the engine never opens
    // them, All Ins never merges them and no card offers them, because the
    // controller's own MIDI input holds them and a Windows MIDI port has one
    // owner. What it plays comes back in through sendControllerMidi, already
    // translated, as the typing keyboard's notes come in through
    // sendMidiInputNote. SessionControllers.cpp.
    void reserveMidiPorts(const juce::StringArray& portNames);
    bool isReservedMidiPort(const juce::String& portName) const;
    // While one is connected its input is offered on the cards as "Push",
    // and the machine counts as having a keyboard.
    void setControllerConnected(bool connected);
    bool isControllerConnected() const { return controllerConnected; }
    static juce::String midiInputPushToken();
    // The one Session call that is safe from any thread: the controller's
    // MIDI callback plays its pads through it, with no hop to the message
    // thread to add jitter. It reaches All Ins and the Push input, exactly as
    // a physical keyboard's notes reach All Ins and the keyboard's own entry.
    void sendControllerMidi(const juce::MidiMessage&);
    // The controller's own preferences - its scale, its octave - kept per
    // machine in the settings file beside the browser preview's, and never
    // read or written under the test runners.
    static juce::String controllerSetting(const juce::String& key, const juce::String& fallback);
    static void setControllerSetting(const juce::String& key, const juce::String& value);
    // A keyboard's velocity curve: how hard its keys have to be struck to play
    // loud, chosen per MIDI port in the preferences' MIDI tab and kept per
    // machine. A port with any curve but Linear is read by Rhino itself, which
    // bends each note-on before any track hears or records it, and is kept
    // from the engine as a controller's ports are - but it is still offered on
    // the cards, under its own name. Linear leaves the port to the engine.
    // SessionKeyboards.cpp.
    juce::StringArray keyboardPortNames() const;
    VelocityCurve velocityCurve(const juce::String& portName) const;
    void setVelocityCurve(const juce::String& portName, VelocityCurve curve);
    // The last note a port played: how hard it was struck and what its curve
    // made of that. `notes` counts them, so a panel can tell a new note from
    // the same one read twice; zero means none yet. A Linear port is read too,
    // through the engine, while the engine has it open.
    struct KeyboardNote
    {
        int struck = 0, played = 0;
        juce::uint32 notes = 0;
    };
    KeyboardNote lastKeyboardNote(const juce::String& portName) const;
    // engine: Linear, so the engine reads it. open: Rhino reads it. busy:
    // Rhino could not open it, which on Windows means another program has it.
    // absent: it has a curve and is not plugged in, or Rhino has not started
    // reading keyboards yet.
    enum class KeyboardPortState { engine, open, busy, absent };
    KeyboardPortState keyboardPortState(const juce::String& portName) const;
    // Opens the ports a curve is saved for, and any given one later. The shell
    // calls it once it is up; the test runners never do, so a test run never
    // takes a keyboard from whoever is playing it.
    void startKeyboardPorts();
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
    // Merged and bounced audio follow the same rule, in a folder of their own:
    // a take and a flattened clip are different things to go looking for.
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
    // A clip by id: on a track's timeline, or in one of its clip slots.
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
    // The engine plays the lanes. Every lane that is drawn, and that nobody
    // has taken over by hand, is mirrored onto its parameter's own automation
    // curve, which the audio graph reads every block - live and in a render
    // alike, and whatever the message thread is doing. This runs by itself
    // when a change to the document or to an override is announced; call it
    // directly only where something has to hear the curves before then.
    void mirrorAutomationToEngine();
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
    // Several clips carried by one drag: one undo step, one restart of
    // playback, and all or nothing - a clip that may not land where it is
    // going refuses the whole move rather than leaving the others moved.
    struct ClipMove
    {
        te::EditItemID id;
        ClipGeometry position;
        int track = -1;
    };
    juce::Result moveClips(const std::vector<ClipMove>&);
    // Every clip in the list, in one undo step.
    void deleteClips(const std::vector<te::EditItemID>&);
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
    // What a bounce made, so the gesture that asked for it can select it and
    // say what happened.
    struct BounceResult
    {
        // The new tracks, top to bottom, as they stand once all are in place.
        std::vector<int> tracks;
        // The clips on them, top to bottom and in time order on each.
        std::vector<te::EditItemID> clips;
        // The clips, or the tracks and groups, that went in.
        int sourceCount = 0;
        // True when a source was a group, whose bus is what was muted.
        bool group = false;
    };
    // Bounce to New Track, as Live has it (SessionBounce.cpp). What a source
    // plays is rendered to an audio file and laid on a new audio track
    // directly under it, named for it with " (Bounce)" and wearing its
    // colour, and the source is muted so the song sounds as it did - all one
    // undo step. The render runs on this thread, as a merge's does.
    //
    // A track is rendered through its devices and clip effects and before its
    // fader and pan, which the new track takes over, so the mix can still be
    // moved; a track in a group is rendered alone, and its new track joins the
    // group. A group is rendered after its bus, fader included, and its new
    // track sits under the whole group at unity. Neither carries the main
    // row's devices. A muted track, or a MIDI track with no instrument, is
    // refused: either would bounce to silence.
    //
    // Clips: one new track per source track, holding one clip for each
    // selected clip over the same span. The track is rendered once across
    // them, so a tail that rings into the next clip is in the bounce as it is
    // in the song. The source clips are muted.
    //
    // These two run the whole bounce before they return, for a caller with
    // nothing to draw meanwhile; the steps below are the same bounce a few
    // blocks at a time.
    juce::Result bounceClipsToNewTracks(const std::vector<te::EditItemID>&, BounceResult* = nullptr);
    // Tracks and groups: everything on the timeline from the first clip's
    // start to the last one's end becomes one clip, and the track, or the
    // group's bus, is muted. A group's bus bounces the group; a member listed
    // with its own bus is bounced as part of it rather than twice.
    juce::Result bounceTracksToNewTracks(const std::vector<int>& tracks, BounceResult* = nullptr);
    // The bounce in steps, so the window can draw while it renders - the
    // shell's loading screen drives it. A start makes every refusal at once
    // and takes the edit off the device; continueBounce renders for about
    // `milliseconds` and returns true while there is more to do, laying what
    // was made on new tracks as it ends; cancelBounce stops it with nothing
    // changed. Once it is over, bounceOutcome says how it went. Nothing else
    // may edit the document while one runs: the shell blocks input, undo and
    // redo wait, and opening or starting a document cancels it.
    juce::Result startBounceClips(const std::vector<te::EditItemID>&);
    juce::Result startBounceTracks(const std::vector<int>& tracks);
    bool continueBounce(double milliseconds);
    void cancelBounce();
    bool isBouncing() const { return bounceRun != nullptr; }
    struct BounceProgress
    {
        float fraction = 0.0f;  // of the whole bounce, by length rendered
        juce::String source;    // the track or group being rendered now
        int step = 0, steps = 0;
    };
    BounceProgress bounceProgress() const;
    juce::Result bounceOutcome(BounceResult* = nullptr) const;
    // Mutes or unmutes clips of either kind, as one undo step. A muted clip
    // stays on its lane, drawn grey, and plays nothing.
    juce::Result setClipsMuted(const std::vector<te::EditItemID>&, bool muted);
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
        // audio only: the clip's own mix and warp, so a copy plays what the
        // original plays rather than the raw file at speed one.
        float gainDb = 0.0f, pan = 0.0f, pitchSemitones = 0.0f;
        double fadeInSeconds = 0.0, fadeOutSeconds = 0.0;
        bool muted = false, reversed = false;
        // Held as plain values because the warp types are declared below:
        // the mode as its WarpMode value, each marker as (source, warp) seconds.
        bool warped = false, warpMarkers = false;
        int warpMode = 0;
        double warpBeats = 0.0;
        std::vector<std::pair<double, double>> markers;
        std::vector<Note> notes;                      // midi only
        // midi only: the catalog id of what its track played, or empty for a
        // track that played nothing - in which case the paste leaves the
        // destination's instrument alone.
        juce::String instrumentId;
        // midi only: when that was a Drum Rack, its kit, as a kit file's XML,
        // which a rack arriving blank where the paste lands takes.
        juce::String drumKit;
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
    // The same for a clip already in hand, which a caller walking every clip
    // has: asking by id finds it again by walking every clip in the edit.
    static int clipPluginCount(te::Clip&);
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
    // The mixer. The arrangement's track headers read and write these
    // per-track values through here, and a deck's bounce of a track passes
    // through the same fader.
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
    // What each track sends to the mix, for the meters on the cards: the
    // loudest sample on each side since the last call, in decibels, after the
    // track's fader and pan. It is read from the level meter every track
    // carries just behind its fader (ensureTrackMixers); index trackCount() is
    // the main output, after its own fader. A track the song cannot hear -
    // muted, or silenced by a solo elsewhere - reads as silence, because the
    // engine mutes a track after its whole chain, meter included; so does
    // everything while a bounce or an export renders. A read clears the
    // peaks, so there is one reader: the arrangement's frame clock.
    struct OutputLevel
    {
        float leftDb = -100.0f, rightDb = -100.0f;
    };
    std::vector<OutputLevel> takeOutputLevels();
    // A track's clip slots (SessionSlots.cpp): one per scene on every track,
    // each holding at most one clip. They are the cells of a DJ console's
    // clips grid, the DJ view's own clips, apart from the arrangement's: a
    // deck plays one cell's clip through the track's live chain
    // (loadDjDeckSlot).
    static constexpr int defaultScenes = 8;
    struct SlotClip
    {
        bool hasClip = false;
        bool isMidi = false;
        juce::String name;
        // The clip's own colour, transparent until someone gives it one:
        // until then it wears its track's, as a timeline clip does.
        juce::Colour colour;
        te::EditItemID clipID;
    };
    int sceneCount() const;
    SlotClip slotClip(int track, int scene) const;
    // Every path that puts a clip in a cell makes the scene it lands in when
    // that is past the last, inside the same undo step, so a drop on a
    // console's + row grows the grid only when something actually lands.
    juce::Result insertPatternPresetInSlot(PatternPreset, int track, int scene);
    juce::Result insertAudioFileInSlot(const juce::File&, int track, int scene);
    juce::Result insertBuiltInSampleInSlot(BuiltInSample, int track, int scene);
    juce::Result deleteSlotClip(int track, int scene);
    // An empty one-bar MIDI clip in a slot, as a double-click on an empty
    // cell makes; an audio track's cell takes a file instead.
    juce::Result createSlotClip(int track, int scene);
    // A cell's clip and a timeline clip are separate objects. These are the
    // ways across, and each copies: -1 for scene picks the first free slot,
    // -1 for targetTrack the clip's own track. A clip copied into another
    // track's slot is held to that track's kind.
    juce::Result copySlotClipToArrangement(int track, int scene, double startSeconds, int targetTrack = -1);
    juce::Result copyClipToSlot(te::EditItemID, int scene = -1);
    juce::Result copyClipToTrackSlot(te::EditItemID, int track, int scene);
    int firstFreeSlot(int track) const;
    // ---- The DJ booth (SessionDj.cpp, SessionDjSources.cpp) ----
    // Up to six decks into a mixer with a beat effect, rendered by Rhino's own
    // engine (core/DjEngine.h) as a second callback on the audio device,
    // beside the edit's, the way the library preview is. So every deck has a
    // transport of its own and none of them is the song's. A deck plays a
    // file, or one of a track's own clips - its clip slots, the console's
    // cells, never the arrangement's clips - heard live through the track's
    // chain: the clip's notes play the track's instrument and its sound goes
    // through the track's effects as the deck passes them, so every knob,
    // and an external instrument's own window, is heard as it turns. Nothing
    // is rendered on the way.
    static constexpr int maximumDjDecks = 6;
    // The booth's private state, defined in SessionDjInternal.h and held
    // below; declared here so the session's own units can name it.
    struct DjBooth;
    enum class DjSourceKind { none, file, track };
    // When a start or a hot cue lands: at once, or on the master deck's next
    // beat, bar or four bars - the mechanism Live launches clips with.
    enum class DjQuantise { off, beat, bar, fourBars };
    static juce::String djQuantiseName(DjQuantise);
    struct DjDeckInfo
    {
        DjSourceKind kind = DjSourceKind::none;
        juce::String name;        // what the deck shows
        juce::File file;          // the file a file deck reads
        te::EditItemID trackId;   // a track deck's track
        int slot = -1;            // the cell a track deck plays; -1 while it plays none yet
        int track = -1;           // where that track is now; -1 once it is gone
        bool loaded = false;      // material is on the deck
        bool loading = false;     // a read is in flight
        bool live = false;        // the track's input is heard too, so its instrument plays from the keys
        juce::String error;       // why the last load failed
        int generation = 0;       // bumped with every load, so a display can tell new material from old
    };
    struct DjLoop
    {
        double startSeconds = -1.0, endSeconds = -1.0;
        bool active = false;
        bool valid() const { return startSeconds >= 0.0 && endSeconds > startSeconds; }
    };
    struct DjDeckState
    {
        enum class Transport { empty, stopped, playing, waiting, cueing };
        Transport transport = Transport::empty;
        bool pendingJump = false;
        double positionSeconds = 0.0, lengthSeconds = 0.0;
        double bpm = 0.0, effectiveBpm = 0.0;   // the track's own, and at the rate it plays
        double firstBeatSeconds = 0.0;
        int beatsPerBar = 4;
        double beat = 0.0;                      // fractional beats from the first downbeat
        float tempoPercent = 0.0f;
        int tempoRange = 6;
        bool synced = false, master = false, reversed = false;
        float brakeSeconds = 0.0f;              // how long a stop winds down and a start spins up
        bool quantiseSnap = true;               // cue and loop points snap to the grid
        bool scratching = false;                // a hand is on the platter
        double cueSeconds = 0.0;
        std::array<double, 8> hotCueSeconds {}; // negative when unset
        DjLoop loop;
        float peak = 0.0f;
        int keyIndex = -1;
        juce::uint32 jumps = 0;
        bool isPlaying() const { return transport == Transport::playing || transport == Transport::cueing; }
    };
    // A channel of the mixer, as the DJM-V10 lays one out: trim, a
    // compressor, four EQ bands, the colour filter, a send, the fader with
    // its curve, cue, the effect assign and the crossfader side.
    static constexpr int djChannelBandCount = 4;   // low, low-mid, high-mid, high
    struct DjChannelState
    {
        float trimDb = 0.0f, comp = 0.0f;
        std::array<float, djChannelBandCount> eqDb {};
        float filter = 0.0f, resonance = 0.2f, fader = 1.0f, send = 0.0f;
        int faderCurve = 1;                     // 0 gentle, 1 normal, 2 steep
        bool cue = false, fx = false;
        int crossfaderSide = 1;                 // 0 A, 1 through, 2 B
        float meter = 0.0f;
    };
    struct DjMasterState
    {
        float crossfader = 0.0f, crossfaderCurve = 0.0f;
        float levelDb = 0.0f, lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;
        // The headphones: the cue against the master, their level, and
        // mono split (cue left, master right).
        float cueMix = 0.0f, cueLevelDb = 0.0f;
        bool monoSplit = false;
        float meterLeft = 0.0f, meterRight = 0.0f;
        double bpm = 0.0;                       // the master deck's, 0 for none
        double tapBpm = 128.0;                  // tapped in; times the effect when no deck plays
        int masterDeck = -1;
    };
    // The mic: the audio device's first input, with a two-band EQ, off, on
    // or on with talkover, which ducks the master while it is spoken into.
    struct DjMicState
    {
        float levelDb = 0.0f, lowDb = 0.0f, highDb = 0.0f;
        int mode = 0;                           // 0 off, 1 on, 2 talkover
        float meter = 0.0f;
    };
    // The send/return unit the channels' sends feed.
    struct DjSendFxState
    {
        int type = 2;                           // djSendFxTypeName
        float size = 0.5f, time = 0.5f, tone = 0.5f, mix = 0.5f;
    };
    struct DjFxState
    {
        int type = 1;
        float beats = 0.5f, depth = 0.5f;
        bool on = false;
        // -1 the master, else a channel. One past the console's decks is
        // where a deck taken off with the effect on it went, and the effect
        // is on nothing until a deck takes that channel and lights its key.
        int target = -1;
        std::array<bool, 3> bands {true, true, true};   // the bands the effect is on: low, mid, high
        bool autoTime = true;                   // from the beats, else from manualSeconds
        float manualSeconds = 0.25f;
    };
    static constexpr float djKillDb = -90.0f;  // an EQ band at or below this is off
    static juce::String djFxTypeName(int type);
    static int djFxTypeCount();
    static juce::String djSendFxTypeName(int type);
    static int djSendFxTypeCount();

    // Decks are named by their place on the console, and a mixer channel by
    // the same place. Removing a deck closes the console's gap; the decks
    // after it play on untouched, since each plays in an engine slot of its
    // own (SessionDjInternal.h).
    int djDeckCount() const;
    juce::Result addDjDeck();
    juce::Result removeDjDeck(int deck);
    // What a deck plays. A file is read and analysed on a worker thread and
    // djPoll installs it. A track makes the deck that track's: the deck is
    // heard through the track's chain from then on, and plays nothing until
    // one of the track's cells is chosen - the arrangement's clips are never
    // a deck's. A track another deck plays is refused, since one chain plays
    // one deck.
    juce::Result loadDjDeckFile(int deck, const juce::File&);
    juce::Result loadDjDeckTrack(int deck, int track);
    // One of the track's cells on the deck, heard through the track's chain:
    // a MIDI clip's notes at once, an audio clip's sound once the worker has
    // read it. A deck already playing that track keeps its beat.
    juce::Result loadDjDeckSlot(int deck, int track, int scene);
    void ejectDjDeck(int deck);
    DjDeckInfo djDeckInfo(int deck) const;
    DjDeckState djDeckState(int deck) const;
    // The material itself, for the displays. Valid until the next change
    // announcement; a display keys what it keeps on DjDeckInfo::generation.
    const DjTrack* djDeckTrack(int deck) const;
    // The deck's transport. Each is a command queued for the audio thread,
    // which is where every position is changed; the state reads it back.
    void djPlay(int deck);
    void djPause(int deck);
    void djTogglePlay(int deck);
    void djCueDown(int deck);
    void djCueUp(int deck);
    void djHotCue(int deck, int index);
    void djClearHotCue(int deck, int index);
    void djLoopIn(int deck);
    void djLoopOut(int deck);
    void djBeatLoop(int deck, double beats);
    void djReloopExit(int deck);
    void djLoopHalve(int deck);
    void djLoopDouble(int deck);
    void djClearLoop(int deck);
    void djBeatJump(int deck, int beats);
    void djSeek(int deck, double seconds);
    void djSetSynced(int deck, bool);
    void djSetMaster(int deck);
    void djSetReversed(int deck, bool);
    void djSetTempoPercent(int deck, float percent);
    void djSetTempoRange(int deck, int range);     // 6, 10, 16 or 100
    void djNudge(int deck, float percent);          // held: 0 lets go
    // The hand on the platter: the deck plays at this rate, backwards for a
    // negative one, until the hand lets go.
    void djScratch(int deck, double rate, bool active);
    void djSetBrake(int deck, float seconds);
    void djSetQuantiseSnap(int deck, bool);
    // The previous or next cue point from where the deck stands, hot cues
    // and the cue point alike; back past the first is the top of the track.
    void djJumpToCue(int deck, bool forward);
    void djStopAll();
    DjQuantise djQuantise() const;
    void setDjQuantise(DjQuantise);
    bool djPhaseLock() const;
    void setDjPhaseLock(bool);
    // The mixer. Knob moves are performance, not edits: no undo step, and
    // the document is not marked modified by them.
    DjChannelState djChannel(int channel) const;
    void setDjChannelTrim(int channel, float decibels);
    void setDjChannelComp(int channel, float amount);
    void setDjChannelEq(int channel, int band, float decibels);   // 0 low, 1 low-mid, 2 high-mid, 3 high
    void setDjChannelFilter(int channel, float amount);
    void setDjChannelResonance(int channel, float amount);
    void setDjChannelFader(int channel, float position);
    void setDjChannelFaderCurve(int channel, int curve);
    void setDjChannelSend(int channel, float level);
    void setDjChannelCue(int channel, bool);
    void setDjChannelFx(int channel, bool);
    void setDjChannelCrossfaderSide(int channel, int side);
    DjMasterState djMaster() const;
    void setDjCrossfader(float position);
    void setDjCrossfaderCurve(float curve);
    void setDjMasterLevel(float decibels);
    void setDjMasterEq(int band, float decibels);                 // 0 low, 1 mid, 2 high
    void setDjCueMix(float mix);
    void setDjCueLevel(float decibels);
    void setDjMonoSplit(bool);
    DjMicState djMic() const;
    void setDjMicLevel(float decibels);
    void setDjMicEq(int band, float decibels);                    // 0 low, 1 high
    void setDjMicMode(int mode);
    DjSendFxState djSendFx() const;
    void setDjSendFxType(int type);
    void setDjSendFxSize(float);
    void setDjSendFxTime(float);
    void setDjSendFxTone(float);
    void setDjSendFxMix(float);
    DjFxState djFx() const;
    void setDjFxType(int type);
    void setDjFxBeats(float beats);
    void setDjFxDepth(float depth);
    void setDjFxOn(bool);
    void setDjFxTarget(int target);
    void setDjFxBand(int band, bool on);                          // 0 low, 1 mid, 2 high
    void setDjFxAutoTime(bool);
    void setDjFxTime(float seconds);
    // A tap on the effect's TAP key: two or more within two seconds set
    // the tempo the effect runs at when no deck plays.
    void djTapTempo();
    // Playing over a deck. A track deck's track can take its input too: it
    // is monitored, so a Drum Rack on it plays from the keys through the
    // deck's channel. A file has no instrument to play.
    juce::Result setDjDeckLive(int deck, bool live);
    bool djDeckHasDrumRack(int deck) const;
    // The clip the lower pane opens for a track deck: the cell it plays, or
    // an invalid id while it plays none.
    te::EditItemID djDeckEditClip(int deck) const;
    // Polled by the shell at 30 Hz: installs what a worker has read, hands
    // over what waited for room, and makes a deck's clip again once it has
    // changed and nothing is held.
    void djPoll();
    bool djBusy() const;
    // Drives the booth without an audio device, for the tests, through the
    // booth's own callback: what a device would hand it is what the test
    // hands it, the inputs (the first is the mic) included, and the last form
    // hands back what the device would play.
    void djProcessOffline(int frames);
    void djProcessOffline(const float* const* inputs, int inputChannels, int frames);
    void djProcessOffline(const float* const* inputs, int inputChannels, float* const* outputs, int outputChannels, int frames);
    bool djEngineAttached() const;

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
    juce::Result addInstrumentDevice(const DeviceDescriptor&, int track, const DevicePreset*);
    juce::Result addMidiEffectDevice(const DeviceDescriptor&, int track, const DevicePreset*);

    // The arrangement workflow test reaches engine-level slot state through
    // this, the same way it does for StepGrid and Arrangement.
    friend int runArrangementTest();
    struct AutomationRuntime
    {
        DeviceTarget target;
        // The device's key, so a knob taken over by hand stays taken over
        // when its device is dragged to another place in the chain. Empty for
        // a device no lane has ever referred to, found by target instead.
        juce::String deviceKey;
        float baseValue = 0.0f;
        bool hasBaseValue = false;
        bool overridden = false;
        bool active = false;
    };
    // A lane written onto an engine curve, with the points it was written
    // with, so the next mirror can leave an unchanged curve alone. The plugin
    // is held so a curve can be handed back only while its device is still in
    // the edit.
    struct MirroredCurve
    {
        DeviceTarget target;
        te::Plugin::Ptr plugin;
        te::AutomatableParameter::Ptr parameter;
        std::vector<AutomationPoint> points;
    };
    // Mirrors the lanes whenever the session announces a change that left
    // them stale.
    struct AutomationMirror final : juce::ChangeListener
    {
        explicit AutomationMirror(Session& owner) : session(owner) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override;
        Session& session;
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
    // SessionClips.cpp - one clip's move or trim without a transaction, a
    // notification or a playback restart, so moveClips can carry several
    // inside one undo step. trackChanged says whether it changed lanes,
    // changed whether it moved at all, and devicesChanged whether the lane it
    // reached changed devices for it: a MIDI clip's instrument brought there
    // or switched back on, or a blank Drum Rack given the clip's kit.
    juce::Result editClipInEdit(te::EditItemID, ClipGeometry, ClipGesture, int targetTrack,
                                const std::vector<te::EditItemID>& movingWith, bool& trackChanged,
                                bool& changed, bool& devicesChanged);
    // SessionTransport.cpp - an import without a transaction, so importAudio
    // can add the lane it lands on in the same undo step.
    juce::Result importAudioInEdit(const juce::File&, int track, double startSeconds, double* endSeconds = nullptr);
    // SessionRegion.cpp - the region edit itself, without a transaction or a
    // notification, so paste can clear and insert inside one undo step.
    bool clearClipRegionInEdit(double startSeconds, double endSeconds, int firstTrack, int lastTrack,
                               const std::vector<te::EditItemID>& keep = {});
    // SessionBounce.cpp - one render each, defined there; the start every
    // kind of bounce shares; the edit made once the last has rendered; and
    // the way out that leaves the document as it was.
    struct BounceJob;
    juce::Result startBounce(std::vector<BounceJob>&);
    void finishBounce();
    void abandonBounce(const juce::String& why);
    // A clip edit marks each track it reached with what that track plays -
    // notes on a MIDI track, audio clips on an audio track - and with `also`,
    // whatever else it changed there. With no track known, every track is
    // marked. Session.cpp.
    void markClipsModified(std::vector<int> tracks, ModelChange also = ModelChange::none);
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
    // Armed, or monitoring On: the one rule for whether a track's input is
    // routed to it, shared by arming and by the MIDI device rescan.
    bool trackWantsInput(int track) const;
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
    // Arms again, against the device list as it is now, when any MIDI track is
    // listening, and announces it. Never under a take, which keeps it for
    // finishRecording.
    void rearmListeningMidiTracks();
    std::unique_ptr<MidiDeviceWatcher> midiDeviceWatcher;
    // SessionKeyboards.cpp - the velocity curves, and the ports Rhino reads
    // for them, defined there with its deleter so that nothing else need see
    // it. Made by the constructor, gone before the engine is.
    struct Keyboards;
    struct KeyboardsDeleter
    {
        void operator()(Keyboards*) const;
    };
    std::unique_ptr<Keyboards, KeyboardsDeleter> keyboards;
    void initialiseKeyboards();
    // Takes the ports with a curve from the engine and opens them, hands back
    // the ones set to Linear, and points each at the devices the list holds
    // now. Message thread; run on every rebuild of the device list too. True
    // when a port changed hands, which moves the tracks that play from it onto
    // another device, so the caller arms again. Under a take nothing changes
    // hands until finishRecording runs this once more.
    bool syncKeyboardPorts();
    void closeKeyboardPorts();
    // A port Rhino reads for its curve, and the virtual device its notes are
    // delivered to, which a track that names the port listens to instead.
    bool isCurvedMidiPort(const juce::String& portName) const;
    te::MidiInputDevice* curvedInputDevice(const juce::String& portName) const;
    // What a curved port's MIDI callback does with a message, for a test that
    // has no keyboard to play.
    void receiveKeyboardMidi(const juce::String& portName, const juce::MidiMessage&);
    // SessionControllers.cpp - the Push's own virtual input, made the first
    // time a track asks for it, as the typing keyboard's is.
    te::MidiInputDevice* pushInputDevice() const;
    te::MidiInputDevice* ensurePushInputDevice();
    // Points sendControllerMidi at the devices the list holds now. Message
    // thread only; run whenever the engine has rebuilt the list.
    void refreshControllerTargets();
    // Switches off whatever reserved port the engine has switched on.
    void disableReservedMidiPorts();
    juce::StringArray reservedMidiPorts;
    bool controllerConnected = false;
    // What sendControllerMidi delivers to, from whatever thread calls it. The
    // lock is held across the delivery, so the last reference to a device the
    // engine has dropped is always let go here, on the message thread.
    juce::SpinLock controllerTargetLock;
    std::shared_ptr<te::MidiInputDevice> controllerAllIns, controllerPush;
    // Set while a virtual device has been asked for and the rebuild that will
    // produce it has not landed. Arming does not complain about a missing
    // device in that window: it is on its way.
    bool awaitingMidiDeviceScan = false;
    bool reapplyingArming = false;
    // An arming, monitoring or device change that arrived during a take, kept
    // off the inputs until finishRecording so as not to cut the take.
    bool armingDeferred = false;
    void clearRecordArming();
    void beginTransportRecording();
    // The tidy-up and the notification, with no question about whether the
    // transport has got there yet. recordingStopped is the polled entry.
    void finishRecording();
    // makeRoomForClip over whatever the recording just added. The engine
    // inserts a recorded clip on top of what is already on the track; Rhino's
    // rule is that the newcomer wins the ground it landed on. Returns the
    // tracks a take landed on, empty when nothing was recorded.
    std::vector<int> tidyRecordedClips();
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
    // Plays a sound through the preview, in place of whatever it was playing.
    void startPreview(std::unique_ptr<juce::PositionableAudioSource>, double sampleRate);
    void buildStarterEdit();
    void refreshAfterUndoRedo(bool changed);
    te::PluginList* pluginListForTrack(int track) const;
    // SessionSidechain.cpp - called as a track is deleted, so nothing is left
    // pointing at it. Inside the caller's transaction, so undo restores both.
    void clearSidechainSourcesNaming(te::EditItemID sourceTrack);
    te::ClipSlot* clipSlotAt(int track, int scene) const;
    // SessionSlots.cpp - why a lane may not take a clip of this kind, or ok.
    juce::Result clipLaneRefusal(int track, bool midi) const;
    te::VolumeAndPanPlugin* trackVolumePlugin(int track) const;
    // SessionMixer.cpp - a fader or pan change is announced at once outside a
    // drag and once at the drag's end inside one.
    void announceMixerChange(int track, ModelChange);
    void endMixerGesture();
    void ensureTrackMixers();
    // The reorder itself, without a transaction or a notification, so the group
    // calls can move several tracks inside one of their own.
    void moveTrackInEdit(int track, int destination);
    void arrangeTrackOrder(const std::vector<te::EditItemID>& desired);
    juce::Result assignTracksToGroup(std::vector<int> tracks, int groupId);
    // A group's members let go and routed to the main output, inside the
    // caller's transaction and before its bus is deleted.
    void dissolveGroupInEdit(int groupId);
    // A group is a bus followed by one run of members, and a member's audio
    // goes to its bus. Anything that reorders or removes tracks can break both,
    // so this repairs the runs and then makes the routing follow them.
    void reconcileTrackGroups();
    void migrateLegacyTrackGroups();
    void ensureSceneSlots(int minimumScenes = defaultScenes);
    // SessionPresets.cpp - the instrument and sound a pattern preset plays its
    // notes with, put on the track. devicesChanged says whether that changed
    // the track's chain: an instrument switched, the synth's patch set, or a
    // blank rack given the default kit. A track already set up changes none.
    juce::Result preparePresetTrack(int trackIndex, const PresetPattern&, bool& devicesChanged);
    // Every path that makes a track goes through here, so no track can exist
    // without having said what it is for. It adds the row - directly under
    // `after` when that names a track, at the foot of the stack otherwise -
    // names it, colours it, writes its type and gives it its utility device;
    // the caller owns the undo transaction and whatever reconciling the new
    // row needs.
    te::AudioTrack* appendTrack(TrackType, int after = -1);
    // SessionTrackClipboard.cpp - the half of a paste and a duplicate that
    // differs only in what the undo step is called.
    juce::Result insertTrackCopies(const TrackClipboard&, int below, std::vector<int>* inserted,
                                   const juce::String& action);
    void initialiseExternalPlugins(bool retry = false);
    juce::ValueTree automationOwnerState(int track) const;
    juce::ValueTree findTrackAutomationState(DeviceTarget) const;
    juce::ValueTree ensureTrackAutomationState(DeviceTarget, bool ownLane, bool keepExistingLane);
    std::vector<TrackAutomation> readTrackAutomations(int track, bool resolveParameterInfo) const;
    // The same, from a track's lanes and devices already found: a caller
    // walking every track finds them all at once rather than through the track
    // list each time.
    std::vector<TrackAutomation> readTrackAutomations(const juce::ValueTree& owner, const te::PluginList* devices,
                                                      int track, bool resolveParameterInfo) const;
    AutomationRuntime& automationRuntimeFor(DeviceTarget);
    AutomationRuntime* findAutomationRuntime(DeviceTarget);
    const AutomationRuntime* findAutomationRuntime(DeviceTarget) const;
    const AutomationRuntime* findAutomationRuntime(const juce::String& deviceKey, int parameter) const;
    te::MidiClip* patternClip = nullptr; // owned by edit
    te::EditItemID patternClipID;
    // Engine initialization also changes its edit flag asynchronously. Track
    // user commands separately so startup cannot dirty an untouched document.
    juce::int64 changeRevision = 0, savedRevision = 0;
    int audioClipGestureDepth = 0, tempoGestureDepth = 0;
    // An open note-velocity gesture, and whether anything inside it changed.
    int noteVelocityGestureDepth = 0;
    bool noteVelocitiesChangedInGesture = false;
    // Open knob and fader drags. Inside one, a value change is not announced
    // to the whole session; the gesture's end announces it once.
    int parameterGestureDepth = 0, mixerGestureDepth = 0;
    // The dragged device's latency when the drag began, so its end can tell
    // whether the playback graph has to be rebuilt for delay compensation.
    double latencyAtGestureStart = 0.0;
    bool manualLoop = false;
    tracktion::core::TimeRange manualLoopRange;
    DeviceTarget lastTouchedParameter;
    std::vector<AutomationRuntime> automationRuntime;
    std::vector<MirroredCurve> mirroredCurves;
    // The plugins in the edit when it was last searched for stray curves.
    // Emptied with the edit, so a document just opened is always searched.
    std::vector<te::EditItemID> mirroredPlugins;
    // Set by every document change; the next announcement mirrors the lanes.
    bool automationMirrorStale = true;
    AutomationMirror automationMirror {*this};
    // SessionMappedParameters.cpp. Hears the window of the device being
    // mapped from while mapping is on. A control touched there reports a
    // gesture, or only a value for the controls that send none; both are
    // queued and applied on the next message, never inside the plugin's own
    // callback and never from the audio thread.
    struct ParameterMapper final : juce::AudioProcessorListener, juce::AsyncUpdater
    {
        explicit ParameterMapper(Session& owner) : session(owner) {}
        void audioProcessorParameterChanged(juce::AudioProcessor*, int parameter, float) override;
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int parameter) override;
        void handleAsyncUpdate() override;
        Session& session;
        std::vector<int> gestured, changed;
    };
    ParameterMapper parameterMapper {*this};
    // The device being mapped from, by id, and the processor its listener
    // went on: compared and never followed, since a device brought back by
    // undo is a new processor the listener was never on.
    std::optional<te::EditItemID> mappingDevice;
    const juce::AudioProcessor* mappingProcessor = nullptr;
    MappingReport mappingReport;
    // The map itself, without the announcement, so the window's mapping can
    // say what it did and announce once. `alreadyMapped` reports that kind
    // of refusal apart, which the window's mapping passes over in silence.
    juce::Result mapDeviceParameterInEdit(int track, int slot, int pluginParameter, juce::String& name,
                                          bool* alreadyMapped = nullptr);
    // Where a device sits now, as (track, slot), or nothing once it has gone.
    std::optional<std::pair<int, int>> locateDevice(te::EditItemID) const;
    // Takes the listener off the device's processor and forgets the mapping,
    // without a word: for a device or a document on its way out.
    void detachParameterMapping();
    void dropParameterMapping();
    // Called by markModified for a change to a chain, which may have taken
    // the device being mapped from, and its processor, with it.
    void dropParameterMappingIfDeviceGone();
    std::optional<juce::PluginDescription> forgeDescription;
    // Declared after the engine, so the preview is torn down before the device
    // manager it is registered with. The destructor detaches it either way.
    juce::TimeSliceThread previewThread {"Rhino preview"};
    juce::AudioFormatManager previewFormats;
    juce::AudioTransportSource previewTransport;
    juce::AudioSourcePlayer previewPlayer;
    // A file being read, or a drum sound rendered into memory.
    std::unique_ptr<juce::PositionableAudioSource> previewSource;
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
    // The DJ booth: the engine, its decks' sources and the loads in flight.
    // Built the first time a deck is added, so a session that never opens
    // the DJ view registers no callback. SessionDjInternal.h.
    std::unique_ptr<DjBooth> dj;
    DjBooth& booth();
    void ensureDjAttached();
    void releaseDj();
    // A deck's info found by the engine slot it plays in, which stays its own
    // however the console moves around it (DjBooth::slotOfDeck).
    DjDeckInfo djSlotInfo(int slot) const;
    // SessionDjSources.cpp. These take a deck's engine slot, which a load in
    // flight keeps: a deck taken off the console moves no other deck's slot.
    // The deck's cell made into material: a MIDI clip's notes at once, an
    // audio clip's sound on the worker.
    juce::Result loadDjClip(int slot, bool keepBeat);
    // The deck in `slot` heard through the track at `track`, or through none
    // for -1: the track's hidden ends point at the slot's port. Idempotent,
    // so the poll asserts it again for a track whose devices were made anew.
    void bindDjTrack(int slot, int track);
    // Every deck let go of its track, before the booth or the document goes.
    void djUnbindAll();
    juce::Result startDjFileRead(int slot);
    void finishDjLoad(int slot, int generation, std::unique_ptr<DjTrack>, const juce::String& error, bool keepBeat);
    // Hands the deck the source its last load or eject left waiting, when
    // the booth's queue has room: false while it waits on.
    bool handOverDjSource(int slot);
    void applyDjDeckSettings(int slot);
    // A change to the document, or to one track of it (-1 is every track):
    // the decks playing a clip of it make their clip again when what a clip
    // plays changed. A device, a knob or a curve is heard live and asks for
    // nothing.
    void djDocumentChanged(int track, ModelChange);
    // True while a knob, fader, tempo or clip drag is held, or any mouse
    // button is down: the person is in the middle of something. A deck's clip
    // is not made again then, so a note being dragged is not cut on every
    // step of the drag.
    bool gestureHeld() const;
    // SessionNotes.cpp: the track the note editor's clip sits on, or -1.
    int patternTrackIndex() const;
    void djReset();
    void writeDjState();
    void readDjState();
    void markDjModified();
    // The bounce in flight (SessionBounce.cpp), defined there with its
    // deleter so that nothing else need see it. After the edit, so it would
    // go first; the destructor cancels it before then anyway.
    struct BounceRun;
    struct BounceRunDeleter
    {
        void operator()(BounceRun*) const;
    };
    std::unique_ptr<BounceRun, BounceRunDeleter> bounceRun;
    juce::Result lastBounce = juce::Result::ok();
    BounceResult lastBounceResult;
    // The WAV export in flight (SessionExport.cpp), kept the same way.
    struct ExportRun;
    struct ExportRunDeleter
    {
        void operator()(ExportRun*) const;
    };
    void finishExport();
    void endExport(const juce::Result&);
    std::unique_ptr<ExportRun, ExportRunDeleter> exportRun;
    juce::Result lastExport = juce::Result::ok();
    // Where takeOutputLevels reads (SessionMeters.cpp): a place on each
    // track's meter, by the meter's id, and one on the main output's. Each
    // holds the meter it is on, which belongs to the edit, so all of them go
    // before the edit does (releaseMeterTaps).
    struct MeterTap;
    struct MeterTapDeleter
    {
        void operator()(MeterTap*) const;
    };
    std::map<te::EditItemID, std::unique_ptr<MeterTap, MeterTapDeleter>> meterTaps;
    std::unique_ptr<MeterTap, MeterTapDeleter> mainMeterTap;
    juce::uint32 meterPass = 0;
    void releaseMeterTaps();
};
int runSelfTest();
int runPatternTest();
int runArrangementTest();
int runArrangementGeometryTest();
// Every device Rhino makes, held to the device standard. See
// tests/DeviceConformance.cpp.
int runDeviceConformance();
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
void checkFmDsp(Session&);
// The Drum Rack: its engine measured, its two files, the library's drums and
// the session's paths onto pads and tracks. See tests/DrumRackTest.cpp.
void checkDrumRack(Session&);
// Device presets: the .rnd format, every factory preset against its device,
// and saving, loading and adding through the session. Run by --device-test.
void checkDevicePresets(Session&);
// The DJ booth's engine, measured offline: decks, loops, cues, quantised
// starts and sync, the mixer's bands and faders, the beat effect, and the
// analysis of a synthesised beat. See tests/DjTest.cpp.
void checkDjCore(Session&);
}
