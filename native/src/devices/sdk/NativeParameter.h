#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <functional>
#include <optional>

namespace rhino
{
namespace te = tracktion::engine;

// How a control's value reads on screen, and how a typed reading is parsed
// back. Values are stored in these units, never normalised.
enum class ParamUnit
{
    none,
    percent,       // 0 to 1, read as 0% to 100%
    decibels,
    hertz,
    seconds,
    milliseconds,
    semitones,
    ratio          // read as 1.25x
};

// One control, as a device declares it. A device writes this once, as a
// member, and everything else is derived from it: the engine parameter that
// automation drives, the property it is saved under, its text, the face that
// shows it and the checks every device must pass.
struct ParamSpec
{
    // Saved in documents and device files, so never renamed.
    juce::String id;
    juce::String name;
    // Groups controls on a generated face. Empty is the device's main row.
    juce::String section;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    // The step between values; 0 is continuous.
    float interval = 0.0f;
    // The value at the middle of a knob's travel. Unset keeps the travel
    // linear; zero cannot mean that, since many ranges straddle it.
    std::optional<float> skewCentre;
    ParamUnit unit = ParamUnit::none;
    // A chooser when not empty. Its values are 0 to choices.size() - 1, and
    // the list is append-only: a saved value is a position in it.
    juce::StringArray choices;
    bool toggle = false;
    // How long Param::smoothed() takes to follow a change; 0 does not smooth.
    float smoothingSeconds = 0.0f;
    // Replaces the unit's reading when set.
    std::function<juce::String(float)> format;

    bool isChoice() const { return !choices.isEmpty(); }
    bool isDiscrete() const { return isChoice() || toggle || interval > 0.0f; }
    float clamp(float value) const;
    juce::String text(float value) const;
    float parse(const juce::String&) const;
};

class NativeDevice;
struct ParamSlot;

// What a device holds for each control it declared. Reading it is lock-free
// and safe from any thread; it is a handle onto storage the device owns.
class Param
{
public:
    Param() = default;

    // The value now, automation included.
    float value() const noexcept;
    // A chooser's position.
    int index() const noexcept;
    // A toggle's state.
    bool on() const noexcept;
    // The next sample of the smoothed value. Audio thread, inside process().
    // A control declared without smoothing answers value().
    float smoothed() noexcept;

    const ParamSpec& spec() const;
    te::AutomatableParameter& automatable() const;

private:
    friend class NativeDevice;
    friend class ParamBuilder;
    explicit Param(ParamSlot& s) : slot(&s) {}
    ParamSlot* slot = nullptr;
};

// Built by NativeDevice::param(id, name) and turned into a Param where a
// device declares a member; that conversion is what registers the control,
// so declaration order is the order automation addresses controls by.
class ParamBuilder
{
public:
    ParamBuilder& range(float minimum, float maximum, float interval = 0.0f);
    ParamBuilder& defaultValue(float);
    ParamBuilder& skewAround(float centre);
    ParamBuilder& unit(ParamUnit);
    ParamBuilder& choices(juce::StringArray, int defaultChoice = 0);
    ParamBuilder& toggle(bool defaultOn = false);
    ParamBuilder& section(juce::String);
    ParamBuilder& smoothing(float seconds);
    ParamBuilder& format(std::function<juce::String(float)>);

    operator Param();

private:
    friend class NativeDevice;
    ParamBuilder(NativeDevice& owner, juce::String id, juce::String name);
    NativeDevice& device;
    ParamSpec spec;
};
}
