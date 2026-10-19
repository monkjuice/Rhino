// Rendering a Vital wavetable into Forge frames.
//
// A Vital table is groups of components, each a list of keyframes at positions
// 0..256 that the oscillator's WAVE FRAME travels. A source component makes a
// frame at a position: a drawn cycle (Wave Source), a line (Line Source) or a
// window of a recording (Audio File Source), morphing between its keyframes in
// time or in frequency. A modifier component bends what the sources made. The
// groups are averaged, then the table is cleared of DC and normalised when it
// says so. Worked from the format's description, not from Vital's code.
//
// Forge holds 64 frames to Vital's 257, so the table is sampled at evenly spaced
// positions and a POSITION is the WAVE FRAME over the last position. The
// modifiers are not rendered; a table that uses them is said to have lost them.
#include "ForgePresetImportInternal.h"
#include <complex>
#include <numeric>

namespace rhino::forge::import
{
namespace
{
constexpr int frameSize = wavetableFrameSize;

std::vector<float> decodeFloats(const juce::String& base64)
{
    const auto block = fromBase64(base64);
    const auto count = block.getSize() / sizeof(float);
    std::vector<float> values(count);
    const auto* bytes = static_cast<const juce::uint8*>(block.getData());
    for (size_t i = 0; i < count; ++i)
    {
        const auto bits = juce::ByteOrder::littleEndianInt(bytes + i * 4);
        float value;
        std::memcpy(&value, &bits, sizeof(float));
        values[i] = std::isfinite(value) ? value : 0.0f;
    }
    return values;
}

std::vector<float> decodeShorts(const juce::String& base64)
{
    const auto block = fromBase64(base64);
    const auto count = block.getSize() / 2;
    std::vector<float> values(count);
    const auto* bytes = static_cast<const juce::uint8*>(block.getData());
    for (size_t i = 0; i < count; ++i)
        values[i] = static_cast<float>(static_cast<juce::int16>(juce::ByteOrder::littleEndianShort(bytes + i * 2)))
                  / 32767.0f;
    return values;
}

using Frame = std::array<float, frameSize>;

}

float vitalLineValue(const juce::var& line, float x)
{
    const auto* points = line.getProperty("points", {}).getArray();
    const auto* powers = line.getProperty("powers", {}).getArray();
    const auto smooth = static_cast<bool>(line.getProperty("smooth", false));
    if (points == nullptr || points->size() < 4) return 0.5f;
    const auto count = points->size() / 2;
    const auto pointX = [points] (int i) { return static_cast<float>((*points)[i * 2]); };
    const auto pointY = [points] (int i) { return static_cast<float>((*points)[i * 2 + 1]); };
    if (x <= pointX(0)) return pointY(0);
    for (int i = 0; i + 1 < count; ++i)
    {
        const auto x0 = pointX(i), x1 = pointX(i + 1);
        if (x > x1) continue;
        auto t = x1 > x0 ? (x - x0) / (x1 - x0) : 1.0f;
        if (smooth) t = 0.5f * std::sin((t - 0.5f) * juce::MathConstants<float>::pi) + 0.5f;
        const auto power = powers != nullptr && i < powers->size() ? static_cast<float>((*powers)[i]) : 0.0f;
        t = vitalPowerScale(t, power);
        return pointY(i) + (pointY(i + 1) - pointY(i)) * t;
    }
    return pointY(count - 1);
}

namespace
{
// The keyframes of one component, sorted by position.
struct Keyframes
{
    std::vector<int> positions;
    std::vector<juce::var> frames;

    explicit Keyframes(const juce::var& component)
    {
        if (const auto* list = component.getProperty("keyframes", {}).getArray())
        {
            std::vector<std::pair<int, juce::var>> sorted;
            for (const auto& keyframe : *list)
                sorted.emplace_back(static_cast<int>(keyframe.getProperty("position", 0)), keyframe);
            std::stable_sort(sorted.begin(), sorted.end(),
                             [] (const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& [position, keyframe] : sorted)
            {
                positions.push_back(position);
                frames.push_back(keyframe);
            }
        }
    }

    // Which keyframe a position falls on or after, and how far it is toward
    // the next one, 0..1. Past either end it is the end keyframe, held.
    std::pair<int, float> at(int position, bool interpolate) const
    {
        if (positions.empty()) return {-1, 0.0f};
        auto index = 0;
        for (int i = 0; i < static_cast<int>(positions.size()); ++i)
            if (positions[static_cast<size_t>(i)] <= position) index = i;
        if (!interpolate || index + 1 >= static_cast<int>(positions.size()) || position <= positions.front())
            return {index, 0.0f};
        const auto from = positions[static_cast<size_t>(index)];
        const auto to = positions[static_cast<size_t>(index + 1)];
        return {index, to > from ? static_cast<float>(position - from) / static_cast<float>(to - from) : 0.0f};
    }
};

// Two cycles morphed in frequency rather than in time: each harmonic's
// amplitude blended on its square root and its phase turned the short way
// round, so a morph between two shapes passes through shapes rather than
// through the cancellation of two.
void morphInFrequency(const Frame& a, const Frame& b, float t, Frame& out)
{
    static constexpr int order = 11;
    static_assert((1 << order) == frameSize, "a frame is one FFT");
    juce::dsp::FFT fft(order);
    std::vector<float> left(frameSize * 2, 0.0f), right(frameSize * 2, 0.0f), mixed(frameSize * 2, 0.0f);
    std::copy(a.begin(), a.end(), left.begin());
    std::copy(b.begin(), b.end(), right.begin());
    fft.performRealOnlyForwardTransform(left.data(), true);
    fft.performRealOnlyForwardTransform(right.data(), true);
    for (int bin = 0; bin <= frameSize / 2; ++bin)
    {
        const std::complex<float> from(left[static_cast<size_t>(bin * 2)], left[static_cast<size_t>(bin * 2 + 1)]);
        const std::complex<float> to(right[static_cast<size_t>(bin * 2)], right[static_cast<size_t>(bin * 2 + 1)]);
        std::complex<float> value;
        if (bin == 0 || bin == frameSize / 2)
            value = from + (to - from) * t;
        else
        {
            const auto amplitude = std::pow((1.0f - t) * std::sqrt(std::abs(from)) + t * std::sqrt(std::abs(to)), 2.0f);
            const auto phase = std::abs(from) > 0.0f ? std::arg(from) + t * std::arg(std::conj(from) * to)
                                                     : std::arg(to);
            value = std::polar(amplitude, phase);
        }
        mixed[static_cast<size_t>(bin * 2)] = value.real();
        mixed[static_cast<size_t>(bin * 2 + 1)] = value.imag();
    }
    fft.performRealOnlyInverseTransform(mixed.data());
    std::copy_n(mixed.begin(), frameSize, out.begin());
}

bool waveSource(const juce::var& component, const Keyframes& keys, int position, Frame& out)
{
    const auto style = static_cast<int>(component.getProperty("interpolation_style", 1));
    const auto [index, t] = keys.at(position, style != 0);
    if (index < 0) return false;
    const auto load = [&keys] (int i, Frame& into)
    {
        const auto values = decodeFloats(keys.frames[static_cast<size_t>(i)].getProperty("wave_data", {}).toString());
        into.fill(0.0f);
        std::copy_n(values.begin(), std::min<size_t>(values.size(), frameSize), into.begin());
    };
    Frame from {};
    load(index, from);
    if (t <= 0.0f) { out = from; return true; }
    Frame to {};
    load(index + 1, to);
    // Cubic interpolation between keyframes is read as linear: the tween only
    // reshapes the path between two shapes, and it starts and ends on them.
    if (static_cast<int>(component.getProperty("interpolation", 0)) == 1)
        morphInFrequency(from, to, t, out);
    else
        for (int i = 0; i < frameSize; ++i) out[static_cast<size_t>(i)] = from[static_cast<size_t>(i)] + (to[static_cast<size_t>(i)] - from[static_cast<size_t>(i)]) * t;
    return true;
}

bool lineSource(const juce::var& component, const Keyframes& keys, int position, Frame& out)
{
    const auto style = static_cast<int>(component.getProperty("interpolation_style", 1));
    const auto [index, t] = keys.at(position, style != 0);
    if (index < 0) return false;
    const auto render = [&keys] (int i, Frame& into)
    {
        const auto line = keys.frames[static_cast<size_t>(i)].getProperty("line", {});
        for (int s = 0; s < frameSize; ++s)
            into[static_cast<size_t>(s)] = 1.0f - 2.0f * vitalLineValue(line, static_cast<float>(s) / static_cast<float>(frameSize - 1));
    };
    Frame from {};
    render(index, from);
    if (t <= 0.0f) { out = from; return true; }
    Frame to {};
    render(index + 1, to);
    for (int i = 0; i < frameSize; ++i) out[static_cast<size_t>(i)] = from[static_cast<size_t>(i)] + (to[static_cast<size_t>(i)] - from[static_cast<size_t>(i)]) * t;
    return true;
}

// A window of a recording, a cycle long, starting where the keyframes say:
// between two keyframes the start slides from one to the other.
bool audioSource(const juce::var& component, const Keyframes& keys, int position, const std::vector<float>& audio,
                 Frame& out)
{
    if (audio.size() < 4) return false;
    const auto [index, t] = keys.at(position, true);
    if (index < 0) return false;
    const auto startOf = [&keys] (int i)
    {
        return static_cast<double>(keys.frames[static_cast<size_t>(i)].getProperty("start_position", 0.0));
    };
    auto start = startOf(index);
    if (t > 0.0f) start += (startOf(index + 1) - start) * t;
    const auto window = juce::jmax(4.0, static_cast<double>(component.getProperty("window_size", 2048.0)));
    for (int i = 0; i < frameSize; ++i)
    {
        const auto at = start + window * static_cast<double>(i) / frameSize;
        const auto whole = static_cast<size_t>(juce::jlimit(0.0, static_cast<double>(audio.size() - 2), std::floor(at)));
        const auto fraction = static_cast<float>(juce::jlimit(0.0, 1.0, at - static_cast<double>(whole)));
        out[static_cast<size_t>(i)] = audio[whole] + (audio[whole + 1] - audio[whole]) * fraction;
    }
    return true;
}
}

bool renderVitalWavetable(const juce::var& table, int maxFrames, std::vector<float>& samples, int& frames,
                          int& lastPosition, juce::StringArray& lost)
{
    const auto* groups = table.getProperty("groups", {}).getArray();
    if (groups == nullptr || groups->isEmpty()) return false;

    struct Source
    {
        juce::var component;
        Keyframes keys;
        juce::String type;
        std::vector<float> audio;
    };
    std::vector<std::vector<Source>> built;
    lastPosition = 0;
    juce::StringArray modifiers;
    for (const auto& group : *groups)
    {
        std::vector<Source> sources;
        if (const auto* components = group.getProperty("components", {}).getArray())
            for (const auto& component : *components)
            {
                const auto type = component.getProperty("type", {}).toString();
                Keyframes keys(component);
                for (const auto position : keys.positions) lastPosition = juce::jmax(lastPosition, position);
                if (type == "Wave Source" || type == "Line Source" || type == "Audio File Source")
                {
                    Source source {component, std::move(keys), type, {}};
                    if (type == "Audio File Source")
                        source.audio = decodeShorts(component.getProperty("audio_file", {}).toString());
                    sources.push_back(std::move(source));
                }
                else
                    modifiers.addIfNotAlreadyThere(type);
            }
        if (!sources.empty()) built.push_back(std::move(sources));
    }
    if (built.empty()) return false;
    if (!modifiers.isEmpty())
        lost.add("its wavetable's " + modifiers.joinIntoString(", ") + (modifiers.size() == 1 ? " modifier is" : " modifiers are")
                 + " not rendered");

    lastPosition = juce::jlimit(0, 256, lastPosition);
    frames = juce::jlimit(1, juce::jmax(1, maxFrames), lastPosition + 1);
    samples.assign(static_cast<size_t>(frames) * frameSize, 0.0f);
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto position = frames == 1 ? 0 : juce::roundToInt(static_cast<double>(frame) * lastPosition / (frames - 1));
        auto* into = samples.data() + static_cast<size_t>(frame) * frameSize;
        auto groupsUsed = 0;
        for (const auto& sources : built)
        {
            // Within a group each source writes over the last, as a chain of
            // components does; the modifiers that would sit between them are
            // the part left out.
            Frame made {};
            auto any = false;
            for (const auto& source : sources)
            {
                Frame next {};
                const auto ok = source.type == "Wave Source" ? waveSource(source.component, source.keys, position, next)
                              : source.type == "Line Source" ? lineSource(source.component, source.keys, position, next)
                              : audioSource(source.component, source.keys, position, source.audio, next);
                if (ok) { made = next; any = true; }
            }
            if (!any) continue;
            ++groupsUsed;
            for (int i = 0; i < frameSize; ++i) into[i] += made[static_cast<size_t>(i)];
        }
        if (groupsUsed > 1)
            for (int i = 0; i < frameSize; ++i) into[i] /= static_cast<float>(groupsUsed);
    }

    if (static_cast<bool>(table.getProperty("remove_all_dc", false)))
        for (int frame = 0; frame < frames; ++frame)
        {
            auto* into = samples.data() + static_cast<size_t>(frame) * frameSize;
            const auto mean = std::accumulate(into, into + frameSize, 0.0f) / frameSize;
            for (int i = 0; i < frameSize; ++i) into[i] -= mean;
        }
    // Normalised to full scale when the table asks, as Vital does; otherwise
    // only pulled back when it would clip, since Forge reads a frame as it is.
    auto widest = 0.0f, loudest = 0.0f;
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto* from = samples.data() + static_cast<size_t>(frame) * frameSize;
        const auto [low, high] = std::minmax_element(from, from + frameSize);
        widest = juce::jmax(widest, *high - *low);
        loudest = juce::jmax(loudest, std::abs(*low), std::abs(*high));
    }
    const auto scale = static_cast<bool>(table.getProperty("full_normalize", false)) && widest > 1.0e-6f
        ? 2.0f / widest : (loudest > 1.0f ? 1.0f / loudest : 1.0f);
    if (scale != 1.0f)
        for (auto& sample : samples) sample *= scale;
    return true;
}
}
