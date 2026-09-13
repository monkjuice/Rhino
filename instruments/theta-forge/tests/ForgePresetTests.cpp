#include "../src/ForgeProcessor.h"
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void setValue(theta::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    require(parameter != nullptr, "expected Forge parameter exists");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float value(const theta::forge::Processor& processor, const char* id)
{
    const auto* raw = processor.state.getRawParameterValue(id);
    require(raw != nullptr, "expected Forge parameter value exists");
    return raw->load();
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    theta::forge::Processor processor;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("theta-forge-preset-test", {}, true);
    require(directory.createDirectory(), "temporary preset directory can be created");
    const auto preset = directory.getChildFile("Round Trip.forgepreset");

    setValue(processor, "cutoff", 1320.0f);
    setValue(processor, "macroWeight", 0.73f);
    setValue(processor, "macroSpace", 0.41f);
    require(processor.savePreset(preset, "Round Trip").wasOk(), "preset saves");
    setValue(processor, "cutoff", 9000.0f);
    setValue(processor, "macroWeight", 0.0f);
    require(processor.loadPreset(preset).wasOk(), "preset loads");
    require(std::abs(value(processor, "cutoff") - 1320.0f) < 1.0f, "preset restores cutoff");
    require(std::abs(value(processor, "macroWeight") - 0.73f) < 0.001f, "preset restores macro");
    require(std::abs(value(processor, "macroSpace") - 0.41f) < 0.001f, "preset restores FX macro");

    const auto invalid = directory.getChildFile("Invalid.forgepreset");
    require(invalid.replaceWithText("<NotForge />"), "invalid fixture writes");
    const auto before = value(processor, "cutoff");
    require(processor.loadPreset(invalid).failed(), "foreign preset is rejected");
    require(value(processor, "cutoff") == before, "rejected preset leaves state untouched");

    directory.deleteRecursively();
    std::cout << "Theta Forge preset round-trip passed\n";
    return 0;
}
