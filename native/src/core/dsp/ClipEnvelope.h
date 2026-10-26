#pragma once
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

// An audio clip's own envelopes: Live's clip envelopes for an audio clip. A
// clip carries one each for its Gain, its Pan and its Pitch, drawn over its
// waveform, and each one moves the knob of the same name over the clip's
// length rather than replacing it - which is what lets a clip's knob still
// mean something once an envelope is drawn on it.
//
//   Gain   a share of the clip's gain, 0 to 100%: at 100% the clip is as loud
//          as its Gain knob says, and the envelope can only take it down.
//   Pan    a balance on what the clip's own pan made: centre leaves both
//          sides as they were, and turning one way fades the other side out.
//   Pitch  semitones added to the clip's Pitch knob.
//
// A pure header with no JUCE and no engine, like EqFilter.h, because three
// places read these rules and have to agree: the model, which stores and
// clamps the points; the hidden device that plays gain and pan
// (devices/infra/ClipEnvelopeDevice.h); and the clip editor, which draws the
// line the audio follows.
namespace rhino
{
enum class ClipEnvelopeKind
{
    gain,
    pan,
    pitch
};
inline constexpr int clipEnvelopeKindCount = 3;

struct ClipEnvelopeRange
{
    float minimum = 0.0f, maximum = 1.0f;
    // Where an envelope with no points sits, and what it does there: nothing.
    float neutral = 0.0f;
};

// Pitch reaches as far as the clip's own Pitch knob does, either way.
inline constexpr float clipEnvelopeMaximumSemitones = 24.0f;

constexpr ClipEnvelopeRange clipEnvelopeRange(ClipEnvelopeKind kind)
{
    switch (kind)
    {
        case ClipEnvelopeKind::gain:  return {0.0f, 1.0f, 1.0f};
        case ClipEnvelopeKind::pan:   return {-1.0f, 1.0f, 0.0f};
        case ClipEnvelopeKind::pitch: return {-clipEnvelopeMaximumSemitones, clipEnvelopeMaximumSemitones, 0.0f};
    }
    return {};
}

// What the document stores for each kind. Written into saved projects, so
// never renamed.
constexpr const char* clipEnvelopeId(ClipEnvelopeKind kind)
{
    switch (kind)
    {
        case ClipEnvelopeKind::gain:  return "gain";
        case ClipEnvelopeKind::pan:   return "pan";
        case ClipEnvelopeKind::pitch: return "pitch";
    }
    return "";
}

constexpr const char* clipEnvelopeName(ClipEnvelopeKind kind)
{
    switch (kind)
    {
        case ClipEnvelopeKind::gain:  return "Gain";
        case ClipEnvelopeKind::pan:   return "Pan";
        case ClipEnvelopeKind::pitch: return "Pitch";
    }
    return "";
}

inline float clampClipEnvelopeValue(ClipEnvelopeKind kind, float value)
{
    const auto range = clipEnvelopeRange(kind);
    if (!std::isfinite(value))
        return range.neutral;
    return std::clamp(value, range.minimum, range.maximum);
}

// Pitch lands on whole semitones unless asked not to, as Live's Transposition
// envelope does without Shift: tuning a note by hand is the rare case.
inline float snapClipEnvelopeValue(ClipEnvelopeKind kind, float value, bool fine)
{
    value = clampClipEnvelopeValue(kind, value);
    if (kind == ClipEnvelopeKind::pitch && !fine)
        return clampClipEnvelopeValue(kind, std::round(value));
    return value;
}

// The envelope's value at a moment, as the engine plays it: a straight line
// between neighbouring points, level before the first and after the last, and
// neutral with none. Points must be in time order; two at one moment are a
// step, the later one winning from that moment on. Any type with
// `timeSeconds` and `value` will do, so the model's own points are read as
// they are.
template <typename Points>
float clipEnvelopeValueAt(const Points& points, double seconds, float neutral)
{
    const auto count = static_cast<size_t>(std::size(points));
    if (count == 0)
        return neutral;
    const auto& first = *std::begin(points);
    if (seconds < first.timeSeconds)
        return first.value;
    auto previous = std::begin(points);
    for (auto next = std::next(previous); next != std::end(points); previous = next, ++next)
    {
        if (seconds >= next->timeSeconds)
            continue;
        const auto span = next->timeSeconds - previous->timeSeconds;
        if (!(span > 0.0))
            return next->value;
        const auto amount = static_cast<float>((seconds - previous->timeSeconds) / span);
        return previous->value + (next->value - previous->value) * amount;
    }
    return previous->value;
}

// The value just before a moment: the same line, except that a step falling
// exactly on the moment has not happened yet. What a clip ending on a step
// plays up to its last sample.
template <typename Points>
float clipEnvelopeValueBefore(const Points& points, double seconds, float neutral)
{
    const auto count = static_cast<size_t>(std::size(points));
    if (count == 0)
        return neutral;
    const auto& first = *std::begin(points);
    if (seconds <= first.timeSeconds)
        return first.value;
    auto previous = std::begin(points);
    for (auto next = std::next(previous); next != std::end(points); previous = next, ++next)
    {
        if (seconds > next->timeSeconds)
            continue;
        // Past the previous point and not past this one, so the span is open.
        const auto amount = static_cast<float>((seconds - previous->timeSeconds)
                                               / (next->timeSeconds - previous->timeSeconds));
        return previous->value + (next->value - previous->value) * amount;
    }
    return previous->value;
}

// The balance the Pan envelope applies to each side, both 1 at centre.
inline void clipEnvelopeBalance(float pan, float& left, float& right)
{
    pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
    left = pan > 0.0f ? 1.0f - pan : 1.0f;
    right = pan < 0.0f ? 1.0f + pan : 1.0f;
}

// A Gain envelope's share in decibels: 0 at 100%, minus infinity at none.
inline float clipEnvelopeGainDecibels(float share)
{
    if (!(share > 0.0f))
        return -std::numeric_limits<float>::infinity();
    return 20.0f * std::log10(std::min(share, 1.0f));
}
}
