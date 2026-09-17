#pragma once
#include "Session.h"

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
void panicMidiOnTrack(te::ClipTrack* clipTrack);
juce::Colour presetColour(Session::PatternPreset preset);
juce::Colour instrumentColour(Session::Instrument instrument);
juce::Colour nextClipColour(juce::Colour current);
bool effectTypeAndName(Session::AudioEffect effect, const char*& type, juce::String& name);
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
tracktion::core::TimeRange firstFreeDuplicateRange(te::Clip& source);
juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed);
juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument, bool& changed,
                                   const juce::PluginDescription* forgeDescription = nullptr);

}
