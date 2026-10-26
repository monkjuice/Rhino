#include "DuckTestSupport.h"

#include <cmath>

namespace rhino::duck::tests
{
int failures = 0;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void requireClose(float actual, float expected, float tolerance, const char* message)
{
    if (std::abs(actual - expected) <= tolerance) return;
    std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual << ")\n";
    ++failures;
}

void setValue(Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr)
    {
        require(false, id);
        return;
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float value(const Processor& processor, const char* id)
{
    const auto* raw = processor.state.getRawParameterValue(id);
    return raw != nullptr ? raw->load() : 0.0f;
}

std::vector<float> constant(int frames, float level)
{
    return std::vector<float>(static_cast<size_t>(frames), level);
}

std::vector<float> sine(int frames, double hz, float amplitude)
{
    std::vector<float> signal(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        signal[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * hz * i / testRate));
    return signal;
}

void addKick(std::vector<float>& into, int at, float amplitude)
{
    const auto attack = static_cast<int>(testRate * 0.0005);
    const auto length = static_cast<int>(testRate * 0.3);
    for (int i = 0; i < length && at + i < static_cast<int>(into.size()); ++i)
    {
        const auto t = i / testRate;
        const auto rise = i < attack ? static_cast<float>(i) / static_cast<float>(attack) : 1.0f;
        const auto decay = static_cast<float>(std::exp(-t / 0.08));
        into[static_cast<size_t>(at + i)] += amplitude * rise * decay * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 55.0 * t));
    }
}

float rmsOf(const std::vector<float>& signal, int from, int to)
{
    auto sum = 0.0;
    auto count = 0;
    for (int i = std::max(0, from); i < std::min(to, static_cast<int>(signal.size())); ++i, ++count)
        sum += static_cast<double>(signal[static_cast<size_t>(i)]) * signal[static_cast<size_t>(i)];
    return count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
}

float meanOf(const std::vector<float>& signal, int from, int to)
{
    auto sum = 0.0;
    auto count = 0;
    for (int i = std::max(0, from); i < std::min(to, static_cast<int>(signal.size())); ++i, ++count)
        sum += signal[static_cast<size_t>(i)];
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

float decibels(float ratio)
{
    return ratio > 1.0e-9f ? 20.0f * std::log10(ratio) : -180.0f;
}
}
