#pragma once
#include "Session.h"
#include "DeviceIds.h"
// Session's public header reaches the device library through its catalog
// only. These two are included here, in the private header, because the
// Session implementation genuinely manipulates them: it sets a drum kit and
// reads wave parameters. Nothing outside Session's own .cpp files sees this.
#include "audio/UtilityDevice.h"
#include "instruments/DrumDevice.h"
#include "instruments/RhinoWaveDevice.h"

// Shared internals of the Session implementation.
//
// Session is defined across several translation units (Session.cpp,
// SessionTransport.cpp, SessionPatches.cpp, DeviceMacros.cpp and friends).
// Anything those files need in common lives here rather than in a per-file
// anonymous namespace. This header is internal: nothing outside the Session
// implementation should include it.

namespace rhino
{

// ValueTree property identifiers owned by the session document.
extern const juce::Identifier starterPlaceholderID;
extern const juce::Identifier editorStepsID;
extern const juce::Identifier trackAutomationID;
extern const juce::Identifier automationPointID;
extern const juce::Identifier automationSlotID;
extern const juce::Identifier automationParameterID;
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
extern const juce::Identifier countInBarsID;

// True while the app is running a --self-test style command line. Persistent
// preferences are neither read nor written then, so a developer's settings
// cannot decide what the suite does.
bool isCommandLineTestMode();

// The application's own preferences - what belongs to this machine rather than
// to the project. Named here rather than in one feature's .cpp now that the
// browser preview and input monitoring both read it.
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
    bool useRhinoWave = false;
    SynthPatch synthPatch = SynthPatch::Default;
};

// Pattern and preset data (SessionPatches.cpp)
PresetPattern presetPattern(Session::PatternPreset preset);
void fillMidiClip(te::MidiClip& clip, const PresetPattern& preset, juce::UndoManager& undoManager);
void setPluginParameter(te::AutomatableParameter::Ptr parameter, float value);
void applySynthPatch(SynthPatch patch, te::FourOscPlugin& synth, juce::UndoManager& undoManager);
void applyRhinoWavePatch(Session::PatternPreset preset, RhinoWaveDevice& wave);

// Device parameter and macro mapping (DeviceMacros.cpp)
te::AutomatableParameter* activeParameterAt(te::Plugin& plugin, int index);
te::AutomatableParameter* fourOscMacroParameterAt(te::FourOscPlugin& synth, int index);
te::AutomatableParameter* rhinoWaveMacroParameterAt(RhinoWaveDevice& wave, int index);
juce::String fourOscMacroName(int index);
juce::String rhinoWaveMacroName(int index);
juce::String formatFourOscMacroValue(int index, float value, te::AutomatableParameter& parameter);
juce::String formatRhinoWaveMacroValue(int index, float value, te::AutomatableParameter& parameter);
te::AutomatableParameter* exposedParameterAt(te::Plugin& plugin, int index);
float exposedParameterMaximum(te::Plugin& plugin, int index, float maximum);

// Engine and model helpers (SessionInternal.cpp)
// The pattern track is the first track that plays, which is not the same as
// the first track in the stack: a group's bus is an ordinary te::AudioTrack
// and keeps its place in getAudioTracks(), so a document whose stack opens
// with a group has a bus at index zero. A bus runs no instrument and holds no
// clips, so everything that used to index track zero asks this instead.
te::AudioTrack* patternTrackOf(te::Edit& edit);
void panicMidiOnTrack(te::ClipTrack* clipTrack);
juce::Colour presetColour(Session::PatternPreset preset);
juce::Colour instrumentColour(const DeviceDescriptor& device);
juce::Colour instrumentColour(Session::Instrument instrument);
juce::Colour nextClipColour(juce::Colour current);
const DeviceDescriptor* audioEffectDescriptor(Session::AudioEffect effect);
void resetPluginList(te::PluginList* list);
double stepDurationBeats(int steps);
bool sameDeviceTarget(Session::DeviceTarget a, Session::DeviceTarget b);
bool hasActiveTrackAutomation(const te::Edit& edit, Session::DeviceTarget target);
te::Plugin* findPlugin(te::AudioTrack& track, const juce::String& type);
te::FourOscPlugin* findFourOsc(te::AudioTrack& track);
RhinoWaveDevice* findRhinoWave(te::AudioTrack& track);
DrumDevice* findDrumDevice(te::AudioTrack& track);
Session::Instrument activeTrackInstrument(te::AudioTrack& track);
bool isForgePlugin(const te::Plugin& plugin);
bool isInstrumentPlugin(te::Plugin& plugin);
te::Plugin* trackInstrument(te::AudioTrack& track);
void collapseStackedInstruments(te::Edit& edit);
juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed);
// Takes a catalog entry, so an instrument that has no Session::Instrument
// value can still be switched to. The enum overload is shorthand for it.
juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, const DeviceDescriptor& device,
                                   bool& changed, const juce::PluginDescription* forgeDescription = nullptr);
juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument, bool& changed,
                                   const juce::PluginDescription* forgeDescription = nullptr);

}
