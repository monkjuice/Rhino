#include "ForgePresetImportInternal.h"
#include "ForgeProcessor.h"

namespace rhino::forge
{
juce::Result importPreset(const juce::File& file, const juce::AudioProcessorValueTreeState& parameters,
                          ImportedPreset& result)
{
    if (!file.existsAsFile()) return juce::Result::fail("That preset is not there.");
    result = {};
    result.info.name = file.getFileNameWithoutExtension();
    switch (presetFormatOf(file))
    {
        case PresetFormat::serum:
            result.info.source = presetFormatName(PresetFormat::serum);
            return importSerumPreset(file, parameters, result);
        case PresetFormat::vital:
            result.info.source = presetFormatName(PresetFormat::vital);
            return importVitalPreset(file, parameters, result);
        case PresetFormat::forge:
        case PresetFormat::unknown:
            break;
    }
    return juce::Result::fail("Forge cannot convert that kind of file.");
}
}

namespace rhino::forge::import
{
PatchWriter::PatchWriter(const juce::AudioProcessorValueTreeState& p, ImportedPreset& r)
    : parameters(p), result(r)
{
}

void PatchWriter::set(const juce::String& id, float plain, const juce::String& what)
{
    const auto* parameter = parameters.getParameter(id);
    jassert(parameter != nullptr);
    if (parameter == nullptr || !std::isfinite(plain)) return;
    const auto& range = parameter->getNormalisableRange();
    const auto clamped = juce::jlimit(range.start, range.end, plain);
    // A hair past either end is rounding, not a setting Forge cannot reach.
    const auto span = range.end - range.start;
    // A parameter can only print values inside its range, so the setting
    // Forge cannot reach is told by the end it is held at.
    if (what.isNotEmpty() && std::abs(clamped - plain) > span * 0.01f)
        warn(what + (plain > range.end ? ": past Forge's maximum, set to " : ": below Forge's minimum, set to ")
             + parameter->getText(range.convertTo0to1(clamped), 32).trim());
    values[id] = clamped;
}

float PatchWriter::valueOf(const juce::String& id) const
{
    if (const auto found = values.find(id); found != values.end()) return found->second;
    const auto* parameter = parameters.getParameter(id);
    return parameter == nullptr ? 0.0f : parameter->convertFrom0to1(parameter->getDefaultValue());
}

float PatchWriter::normalised(const juce::String& id, float plain) const
{
    const auto* parameter = parameters.getParameter(id);
    if (parameter == nullptr) return 0.0f;
    const auto& range = parameter->getNormalisableRange();
    return range.convertTo0to1(juce::jlimit(range.start, range.end, plain));
}

float PatchWriter::plainFrom(const juce::String& id, float unit) const
{
    const auto* parameter = parameters.getParameter(id);
    return parameter == nullptr ? 0.0f : parameter->convertFrom0to1(juce::jlimit(0.0f, 1.0f, unit));
}

bool PatchWriter::route(int source, const juce::String& destinationId, float depth, bool bipolar,
                        const juce::String& what)
{
    const auto destination = destinationIndexOf(destinationId);
    if (destination <= 0 || source <= 0) return false;
    depth = juce::jlimit(-1.0f, 1.0f, depth);
    if (std::abs(depth) < 0.0005f) return false;
    if (slotsUsed >= modSlotCount)
    {
        warn(what + ": all " + juce::String(modSlotCount) + " of Forge's modulation slots are in use, left out");
        return false;
    }
    const auto slot = slotsUsed++;
    setIndex(modParameterId(slot, "Source"), source);
    setIndex(modParameterId(slot, "Dest"), destination);
    set(modParameterId(slot, "Depth"), depth);
    setOn(modParameterId(slot, "Bipolar"), bipolar);
    return true;
}

void PatchWriter::table(int oscillator, const std::vector<float>& samples, int frames, const juce::String& name)
{
    if (frames < 1 || samples.size() < static_cast<size_t>(frames) * wavetableFrameSize) return;
    result.state.addChild(wavetableStateNode(samples.data(), frames, name, oscillator), -1, nullptr);
}

void PatchWriter::lfoShape(int lfo, const LfoTable& shape, const juce::String& name)
{
    if (!shape.custom || !shape.valid()) return;
    result.state.addChild(lfoTableStateNode(lfo, shape, name), -1, nullptr);
}

void PatchWriter::macroName(int macro, const juce::String& name)
{
    const auto tag = name.trim().toUpperCase().substring(0, 24);
    if (macro < 0 || macro >= macroCount || tag.isEmpty()) return;
    // The default names say nothing a numbered plate does not.
    if (tag == "MACRO " + juce::String(macro + 1) || tag == "MACRO" + juce::String(macro + 1)) return;
    result.state.setProperty("macroName_" + juce::String(macro + 1), tag, nullptr);
}

void PatchWriter::warn(const juce::String& message)
{
    result.info.warnings.addIfNotAlreadyThere(message);
}

void PatchWriter::finish()
{
    // Slot 1 is pre-wired to LFO 1 into the cutoff at no depth on a fresh
    // patch; a converted one starts from an empty matrix, so the slots past
    // what was routed are written as off rather than left to that default.
    for (int slot = slotsUsed; slot < modSlotCount; ++slot)
    {
        values.emplace(modParameterId(slot, "Source"), 0.0f);
        values.emplace(modParameterId(slot, "Dest"), 0.0f);
        values.emplace(modParameterId(slot, "Depth"), 0.0f);
    }
    for (const auto& [id, value] : values)
    {
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", id, nullptr);
        entry.setProperty("value", value, nullptr);
        result.state.addChild(entry, -1, nullptr);
    }
}

int destinationIndexOf(const juce::String& parameterId)
{
    const auto& list = destinations();
    for (int i = 1; i < static_cast<int>(list.size()); ++i)
        if (parameterId == list[static_cast<size_t>(i)].id) return i;
    return 0;
}

LfoTable lfoTableFrom(const std::vector<float>& breakpoints, const std::function<float(float)>& sample)
{
    // The corners the source has, or an even spread of as many as Forge holds
    // when it has more than that.
    std::vector<float> xs;
    if (static_cast<int>(breakpoints.size()) <= maxLfoPoints && breakpoints.size() >= 2)
        xs = breakpoints;
    else
        for (int i = 0; i < maxLfoPoints; ++i)
            xs.push_back(static_cast<float>(i) / static_cast<float>(maxLfoPoints - 1));
    for (auto& x : xs) x = juce::jlimit(0.0f, 1.0f, x);
    std::sort(xs.begin(), xs.end());
    xs.front() = 0.0f;
    xs.back() = 1.0f;

    // A vertical step is two corners at one x. Forge's corners have to move
    // forwards, so the first of the pair steps back by the smallest gap the
    // editor itself leaves, and reads the value from just before the step.
    LfoTable table;
    table.custom = true;
    table.count = static_cast<int>(xs.size());
    for (size_t i = 0; i < xs.size(); ++i)
    {
        auto x = xs[i];
        auto y = sample(x);
        if (i + 1 < xs.size() && xs[i + 1] - x < lfoStepGap && i > 0)
        {
            x = juce::jmax(xs[i - 1] + lfoStepGap, xs[i + 1] - lfoStepGap);
            y = sample(juce::jmax(0.0f, xs[i + 1] - lfoStepGap * 0.5f));
        }
        table.points[i] = {x, juce::jlimit(-1.0f, 1.0f, y), 0.0f, 0.5f};
    }
    for (int i = 1; i < table.count; ++i)
        if (table.points[static_cast<size_t>(i)].x <= table.points[static_cast<size_t>(i - 1)].x)
            table.points[static_cast<size_t>(i)].x = table.points[static_cast<size_t>(i - 1)].x + lfoStepGap * 0.5f;
    table.points[static_cast<size_t>(table.count - 1)].x = 1.0f;

    // Each segment's bend is the parabola that best fits the source across it,
    // by least squares over a handful of points inside it.
    for (int i = 0; i + 1 < table.count; ++i)
    {
        auto& left = table.points[static_cast<size_t>(i)];
        const auto& right = table.points[static_cast<size_t>(i + 1)];
        const auto width = right.x - left.x;
        if (width <= lfoStepGap * 2.0f) continue;
        auto numerator = 0.0f, denominator = 0.0f;
        for (int k = 1; k < 8; ++k)
        {
            const auto m = static_cast<float>(k) / 8.0f;
            const auto straight = left.y + (right.y - left.y) * m;
            const auto basis = 4.0f * m * (1.0f - m);
            numerator += (sample(left.x + width * m) - straight) * basis;
            denominator += basis * basis;
        }
        left.curve = juce::jlimit(-maxLfoCurve, maxLfoCurve, denominator > 0.0f ? numerator / denominator : 0.0f);
        if (std::abs(left.curve) < 0.002f) left.curve = 0.0f;
    }
    if (!table.valid()) return {};
    return table;
}

int nearestLfoDivision(float beats)
{
    const auto& divisions = lfoDivisions();
    auto best = 2;
    auto bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(divisions.size()); ++i)
    {
        const auto distance = std::abs(std::log2(divisions[static_cast<size_t>(i)].beats / juce::jmax(1.0e-4f, beats)));
        if (distance < bestDistance) { bestDistance = distance; best = i; }
    }
    return best;
}

juce::MemoryBlock fromBase64(const juce::String& text)
{
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, text)) return {};
    return decoded.getMemoryBlock();
}

float vitalPowerScale(float x, float k)
{
    if (std::abs(k) < 0.005f) return x;
    return (std::exp(k * x) - 1.0f) / (std::exp(k) - 1.0f);
}
}
