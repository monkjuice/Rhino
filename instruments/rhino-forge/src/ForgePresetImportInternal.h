#pragma once

#include "ForgePresetImport.h"
#include "../core/ForgeCore.h"
#include <map>
#include <optional>

// Private to the converters' translation units: the writer both of them fill a
// Forge state tree through, and the arithmetic they share.
namespace rhino::forge::import
{
// Builds an ImportedPreset's state a setting at a time, in Forge's own terms.
//
// Every value is a plain value in the parameter's own unit, the one a
// .forgepreset stores, and is clamped into the parameter's range on the way in.
// A value that had to be clamped is noted when the caller names what it was, so
// an eight-second attack that became Forge's four says so. Parameters nothing
// sets are left out, and open on their defaults like a preset that predates them.
class PatchWriter
{
public:
    PatchWriter(const juce::AudioProcessorValueTreeState& parameters, ImportedPreset& result);

    void set(const juce::String& id, float plain, const juce::String& what = {});
    void setOn(const juce::String& id, bool on) { set(id, on ? 1.0f : 0.0f); }
    void setIndex(const juce::String& id, int index) { set(id, static_cast<float>(index)); }
    // What set() last wrote, or the parameter's default.
    float valueOf(const juce::String& id) const;
    // A plain value as the parameter's own 0..1, the space the matrix adds
    // depths in, and back.
    float normalised(const juce::String& id, float plain) const;
    float plainFrom(const juce::String& id, float normalised) const;

    // A routing in the next free slot, depth in the destination's own 0..1 as
    // the matrix reads it. Refused, with a warning, once all thirty are taken.
    bool route(int source, const juce::String& destinationId, float depth, bool bipolar,
               const juce::String& what);
    int routedCount() const { return slotsUsed; }

    void table(int oscillator, const std::vector<float>& samples, int frames, const juce::String& name);
    void lfoShape(int lfo, const LfoTable&, const juce::String& name);
    void macroName(int macro, const juce::String& name);
    void warn(const juce::String& message);

    // Writes everything set into the state tree. Called once, last.
    void finish();

private:
    const juce::AudioProcessorValueTreeState& parameters;
    ImportedPreset& result;
    std::map<juce::String, float> values;
    int slotsUsed = 0;
};

// The index a destination id has in the matrix's list, or 0 for one the matrix
// cannot reach.
int destinationIndexOf(const juce::String& parameterId);

// A curve through points, resampled onto Forge's LFO table: at most
// maxLfoPoints, endpoints at 0 and 1, values -1..1, and each segment bent by
// the parabola Forge draws with, chosen to pass through the source's own
// midpoint. `sample` is the source shape at a phase, -1..1.
LfoTable lfoTableFrom(const std::vector<float>& xs, const std::function<float(float)>& sample);

// The nearest of Forge's LFO divisions to a cycle this many beats long, as an
// index into lfoDivisions().
int nearestLfoDivision(float beats);

// Standard base64, which is what JSON carries; juce::MemoryBlock's own
// encoding is a different alphabet.
juce::MemoryBlock fromBase64(const juce::String&);

// A Vital wavetable (one entry of a preset's `wavetables`) rendered to at most
// `maxFrames` frames of Forge's size, spread evenly over the positions Vital's
// oscillator travels. `lastPosition` is the last of those positions, which is
// what a WAVE FRAME setting is divided by to become a Forge POSITION. Notes
// what it could not render in `lost`. False when there is nothing to play.
bool renderVitalWavetable(const juce::var& table, int maxFrames, std::vector<float>& samples, int& frames,
                          int& lastPosition, juce::StringArray& lost);

// Vital's curve, which its envelopes, LFO segments and modulation powers all
// share: (e^(kx) - 1) / (e^k - 1), the identity when k is near nothing.
float vitalPowerScale(float x, float k);

// Serum 2's effects racks, onto Forge's three: the main output's and the two
// busses', module by module in processing order. Each place records the Forge
// slot a Serum module became, or -1 for one Forge has no type for.
struct SerumFxPlace
{
    juce::String type;
    int rack = 0, position = 0;
    int slot = -1;
    juce::var params;
};
std::vector<SerumFxPlace> convertSerumFx(const juce::var& patch, PatchWriter&);

// Where a routing into a Serum effect lands: the Forge parameter, the Serum
// knob's position as stored, and how a knob position becomes Forge's value.
// A module is addressed as its rack times a hundred plus its place in it.
struct SerumFxTarget
{
    juce::String id;
    float knob = 0.0f;
    std::function<float(float)> toForge;
};
std::optional<SerumFxTarget> serumFxTarget(const std::vector<SerumFxPlace>&, const juce::String& type, int moduleId,
                                           const juce::String& param);

// A line as Vital stores one — an LFO's shape, a Line Source's keyframe:
// points x0, y0, x1, y1... with y measured down from the top, a power per
// segment and an optional sine ease. The stored y at x, 0 at the top.
float vitalLineValue(const juce::var& line, float x);
}
