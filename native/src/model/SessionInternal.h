#pragma once
#include "Session.h"
#include "DeviceIds.h"
// Session's public header reaches the device library through its catalog
// only. These are included here, in the private header, because the Session
// implementation genuinely uses them: it inserts a Utility, fills a Drum
// Rack's pads, and reads what a native device declares about its controls.
// Nothing outside Session's own .cpp files sees this.
#include "audio/UtilityDevice.h"
#include "instruments/DrumRackDevice.h"
#include "infra/DjTrackPortDevices.h"
#include "sdk/NativeDevice.h"

// Shared internals of the Session implementation.
//
// Session is defined across several translation units (Session.cpp,
// SessionTransport.cpp, SessionPatches.cpp, DeviceMacros.cpp and friends).
// Anything those files need in common lives here rather than in a per-file
// anonymous namespace. This header is internal: nothing outside the Session
// implementation should include it.

namespace rhino
{

// Rebuilds a clip inside a new owner - a slot or a track, in this document or
// in a copy of it. SessionSlots.cpp.
te::Clip* copyClipInto(te::ClipOwner& destination, te::Clip& source, tracktion::core::TimeRange range,
                       const juce::String& name);

// ValueTree property identifiers owned by the session document.
extern const juce::Identifier starterPlaceholderID;
// A clip the person has coloured by hand. Absent means the clip wears its
// track's colour, which is what every clip does until someone says otherwise.
// It is Rhino's own property because the engine has no way to say "no colour":
// te::Clip::getColour substitutes a default per clip type for anything unset
// or transparent, so a clip can never be asked whether it was ever coloured.
extern const juce::Identifier clipColourID;
extern const juce::Identifier editorStepsID;
extern const juce::Identifier trackAutomationID;
extern const juce::Identifier automationPointID;
// Which device a lane drives, by the device's key rather than its place in
// the chain: a place changes whenever a device is moved, added or deleted.
extern const juce::Identifier automationDeviceID;
extern const juce::Identifier automationParameterID;
extern const juce::Identifier deviceKeyID;
extern const juce::Identifier automationOwnLaneID;
extern const juce::Identifier automationTimeID;
extern const juce::Identifier automationValueID;
extern const juce::Identifier trackGroupBusID;
extern const juce::Identifier trackGroupMemberID;
extern const juce::Identifier trackGroupCollapsedID;
// Only the migration reads these: they describe the group table written before
// a group was a bus track of its own.
extern const juce::Identifier legacyTrackGroupID;
extern const juce::Identifier legacyTrackGroupIdID;
extern const juce::Identifier legacyTrackGroupNameID;
extern const juce::Identifier legacyTrackGroupColourID;
extern const juce::Identifier trackArmedID;
// Whether you hear the input a track is taking in. Beside the arm flag
// because the two describe one thing - what a track does with its input - and
// SessionRecording.cpp reads both. Written only when it is not the default for
// the track's kind; which input that is stays private to the file that owns
// it, the way rhinoMidiInput and rhinoAudioInput both do.
extern const juce::Identifier trackMonitorID;
// Written only for a track created as a MIDI track. Absent means audio,
// which is what every track written before this says and what an audio
// track still says - so the property never has to be migrated in.
extern const juce::Identifier trackTypeID;
extern const juce::Identifier countInBarsID;

// True while the app is running a --self-test style command line. Persistent
// preferences are neither read nor written then, so a developer's settings
// cannot decide what the suite does.
bool isCommandLineTestMode();

// The application's own preferences - what belongs to this machine rather than
// to the project. Named here rather than in one feature's .cpp now that the
// browser preview and the last track kind chosen both read it.
juce::PropertiesFile::Options rhinoSettingsOptions();

// The one thing Rhino tells the engine about itself.
//
// Left to its own devices the engine names a recorded file from a pattern whose
// %projectdir% resolves to nothing for a document that has never been saved,
// which lands the take in the process's working directory. getFileForNewAudioRecording
// is checked before any of that and short-circuits it, so Rhino simply says
// where the file goes. The folder is asked for through a callback because the
// behaviour is built with the engine, before there is a Session to ask.
struct RhinoEngineBehaviour final : te::EngineBehaviour
{
    juce::File getFileForNewAudioRecording(te::Track& track, const juce::String& fileExtension) override;
    // A recording covers what is under it - the same rule every other clip
    // here follows - so the material it replaces is silent while it is made.
    bool muteTrackContentsWhilstRecording() override { return true; }
    // A muted track - or one left out by another's solo - still runs its
    // devices, and only its output to the song is silenced, by the muting
    // node after them. A DJ deck takes its track's sound from inside the
    // chain, so the song's mute and solo leave the deck its track; without
    // this the engine skipped the whole chain of a muted track. The price is
    // the CPU a muted track's devices take.
    bool shouldProcessMutedTracks() override { return true; }
    std::function<juce::File()> recordingDirectory;
};

struct PresetNote { int step, pitch, length; };

enum class SynthPatch { Default, ChordPad, SubBass, ReeseBass };

struct PresetPattern
{
    const PresetNote* notes = nullptr;
    int count = 0;
    juce::String name;
    bool useDrums = false;
    SynthPatch synthPatch = SynthPatch::Default;
};

// Pattern and preset data (SessionPatches.cpp)
PresetPattern presetPattern(Session::PatternPreset preset);
void fillMidiClip(te::MidiClip& clip, const PresetPattern& preset, juce::UndoManager& undoManager);
void setPluginParameter(te::AutomatableParameter::Ptr parameter, float value);
void applySynthPatch(SynthPatch patch, te::FourOscPlugin& synth, juce::UndoManager& undoManager);

// Device parameter and macro mapping (DeviceMacros.cpp)
te::AutomatableParameter* activeParameterAt(te::Plugin& plugin, int index);
te::AutomatableParameter* fourOscMacroParameterAt(te::FourOscPlugin& synth, int index);
juce::String fourOscMacroName(int index);
juce::String formatFourOscMacroValue(int index, float value, te::AutomatableParameter& parameter);
juce::String formatExposedParameterValue(te::Plugin& plugin, int index, float value,
                                         te::AutomatableParameter& parameter);
te::AutomatableParameter* exposedParameterAt(te::Plugin& plugin, int index);
float exposedParameterMaximum(te::Plugin& plugin, int index, float maximum);

// Engine and model helpers (SessionInternal.cpp)
// An edit made from a document's state, given the document's file and its
// role before it is built. A clip resolves its source file while the edit is
// built and keeps what it found, so a source stored relative to the document
// is found only by an edit that knows the file from the start: told
// afterwards, as both the reopened document and a deck's copy once were, the
// clip kept an empty file and played nothing. A state with no project id is
// given a new one, as te::loadEditFromState gives it.
std::unique_ptr<te::Edit> loadDocumentEdit(te::Engine&, const juce::ValueTree& state, const juce::File& document,
                                           te::Edit::EditRole);
// Where the note editor goes when the clip it had is gone: the first MIDI
// track, or null when there is none. Never a group bus, which holds no clips,
// and never an audio track, which may not hold a MIDI clip at all. Nothing
// else about a document singles out a track by position.
te::AudioTrack* firstMidiTrackOf(te::Edit& edit);
void panicMidiOnTrack(te::ClipTrack* clipTrack);
// The audio track holding a clip, on its timeline or in one of its slots.
te::AudioTrack* trackHoldingClip(te::Edit& edit, const te::Clip& clip);
// Its index among the audio tracks, or -1: what an edit of the clip names
// when it marks the document modified.
int clipTrackIndex(te::Edit& edit, const te::Clip& clip);
juce::Colour nextClipColour(juce::Colour current);
void setClipColour(te::Clip& clip, juce::Colour colour, juce::UndoManager* undoManager);
const DeviceDescriptor* audioEffectDescriptor(Session::AudioEffect effect);
void resetPluginList(te::PluginList* list);
double stepDurationBeats(int steps);
bool sameDeviceTarget(Session::DeviceTarget a, Session::DeviceTarget b);
// A device's own key: Rhino's, written onto the plugin's state the first time
// something has to refer to the device wherever it sits, and saved with it.
// Outside the undo history, because naming a device is not an edit to it.
// Empty for a device nothing has referred to yet.
juce::String deviceKeyOf(const te::Plugin&);
juce::String ensureDeviceKey(te::Plugin&);
// Where the device with a key sits in a chain now, or -1 if it is not there.
int slotOfDevice(const te::PluginList&, const juce::String& key);
// A device's lanes, taken off the track they are stored on as the device
// leaves it: left behind they name a device that is nowhere and drive nothing.
// Through the caller's transaction, so one undo brings device and lanes back.
void removeDeviceLanes(juce::ValueTree owner, const te::Plugin&, juce::UndoManager*);
bool hasActiveTrackAutomation(te::Edit& edit, Session::DeviceTarget target);
te::Plugin* findPlugin(te::AudioTrack& track, const juce::String& type);
// The DJ booth's hidden input sits at the very head of a track's chain while a
// deck plays the track (devices/infra/DjTrackPortDevices.h): 1 when it is
// there, else 0. Every path that puts a device at the front of a chain starts
// after it, so a MIDI effect or an instrument never lands ahead of the notes
// it is meant to play.
int djInputEnd(const te::AudioTrack&);
// Gives a track the booth's hidden input at the head of its chain and its
// hidden output where its own devices end and its channel strip begins, when
// it lacks either.
// Outside the undo history, as the channel strip and the mixer are: it runs
// as a track is made, inside the step that makes it, and as a document opens,
// before its history begins, so it never moves a place an undo recorded.
void ensureDjPortDevices(te::Edit&, te::AudioTrack&);
te::FourOscPlugin* findFourOsc(te::AudioTrack& track);
DrumRackDevice* findDrumRack(te::AudioTrack& track);
// A drum pattern on a blank rack would play nothing, so the rack takes the
// kit the patterns were written for. In SessionDrums.cpp.
void loadDefaultDrumKit(DrumRackDevice&);
// A drum clip moved or pasted onto another track brings its sounds: the kit of
// the Drum Rack it came from, which a rack that arrives blank takes. A rack
// already holding sounds keeps them. Returns whether the rack took the kit.
// In SessionDrums.cpp.
std::optional<DrumKit> drumKitOf(te::AudioTrack&);
bool fillBlankDrumRack(te::AudioTrack&, const DrumKit&);
// The instrument a MIDI clip carries when it is moved or pasted onto another
// track: whatever its own track plays, bypassed or not, by catalog entry so a
// device with no Session::Instrument value is carried too. Null for a track
// that plays nothing, which leaves the destination's instrument alone rather
// than installing one.
const DeviceDescriptor* carriedInstrument(te::AudioTrack& track);
bool isForgePlugin(const te::Plugin& plugin);
bool isInstrumentPlugin(te::Plugin& plugin);
te::Plugin* trackInstrument(te::AudioTrack& track);
void collapseStackedInstruments(te::Edit& edit);
juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed);
// What switching a track's instrument did: `devices` when its chain changed -
// the instrument made, any other removed, the one kept switched back on - and
// `renamed` when the track took the instrument's name, which on its own
// changes nothing the track plays. A clip carried onto a track already
// running its instrument, as a clip duplicated on its own track always is,
// changes no device there.
struct InstrumentSwitch
{
    bool devices = false;
    bool renamed = false;
};
// Takes a catalog entry, so an instrument that has no Session::Instrument
// value can still be switched to. The enum overload is shorthand for it.
juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, const DeviceDescriptor& device,
                                   InstrumentSwitch& switched, const juce::PluginDescription* forgeDescription = nullptr);
juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument,
                                   InstrumentSwitch& switched, const juce::PluginDescription* forgeDescription = nullptr);
// Fails, naming the clip and its file, when the clip plays an audio file that
// is not there: moved or deleted, or stored beside a document that has since
// been moved away from it. An offline render waits until every clip's file
// opens, muted clip or not (WaveNode::isReadyToProcess at 4536d8a), so a
// render holding such a clip never finishes; the bounce and the merge ask
// this of every clip they render before they build the render.
juce::Result checkClipFile(te::Clip&);

// Sets every control a preset names and every other to its default, clamped
// and snapped to each control's range, inside whatever undo transaction is
// open. Returns whether anything moved. In SessionDevicePresets.cpp.
bool applyDevicePreset(te::Plugin&, const DevicePreset&);

}
