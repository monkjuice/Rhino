#include "audio/CompressorDevice.h"
#include <cmath>
#include <functional>
#include <source_location>
#include <stdexcept>

// Rhino's Compressor is the engine's own, sounding the same, that also says how
// far it turns the audio down. Run by --device-test after the conformance
// loop, which holds it to the device standard like any other device.
//
// The reading is checked against what can be worked out without it: a steady
// input settles the compressor on its static curve, threshold plus the excess
// times the ratio, and the rendered audio itself says what gain was applied.
namespace rhino
{
namespace
{
constexpr double rate = 48000.0;
// The live device's block: no piece of the meter's 64 divides it.
constexpr int block = 480;

void require(bool valid, const juce::String& what, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error(("Compressor meter: " + what + " (line " + juce::String(static_cast<int>(location.line()))
                                  + ")").toStdString());
}

void set(te::Plugin& plugin, const juce::String& id, float value)
{
    auto parameter = plugin.getAutomatableParameterByID(id);
    require(parameter != nullptr, "the compressor has a parameter " + id);
    parameter->setParameter(value, juce::sendNotificationSync);
}

// Plays `audio` through the plugin from a fresh start, in blocks of `size`.
void play(te::Plugin& plugin, juce::AudioBuffer<float>& audio, int size)
{
    plugin.baseClassInitialise({ {}, rate, size });
    for (int start = 0; start < audio.getNumSamples(); start += size)
    {
        const auto count = std::min(size, audio.getNumSamples() - start);
        const auto from = tracktion::core::TimePosition::fromSeconds(start / rate);
        const auto to = tracktion::core::TimePosition::fromSeconds((start + count) / rate);
        plugin.applyToBuffer(te::PluginRenderContext(&audio, start, count, nullptr, 0.0, { from, to },
                                                     true, false, true, false));
    }
    plugin.baseClassDeinitialise();
}

juce::AudioBuffer<float> filled(int channels, int frames, const std::function<float(int, int)>& sample)
{
    juce::AudioBuffer<float> audio(channels, frames);
    for (int channel = 0; channel < channels; ++channel)
        for (int i = 0; i < frames; ++i)
            audio.setSample(channel, i, sample(channel, i));
    return audio;
}
}

void checkCompressorMeter(te::Edit& edit)
{
    const auto make = [&edit] (const char* type) -> te::Plugin::Ptr
    {
        auto plugin = edit.getPluginCache().createNewPlugin(type, {});
        require(plugin != nullptr, juce::String("the engine makes a ") + type);
        // 4:1 from -20 dB, so a steady -6 dB settles 7.96 dB down.
        set(*plugin, "threshold", 0.1f);
        set(*plugin, "ratio", 0.25f);
        return plugin;
    };
    const auto ours = [] (te::Plugin& plugin) -> CompressorDevice&
    {
        auto* compressor = dynamic_cast<CompressorDevice*>(&plugin);
        require(compressor != nullptr, "the catalog's Compressor is Rhino's");
        return *compressor;
    };
    const auto second = static_cast<int>(rate);
    const auto expected = -juce::Decibels::gainToDecibels((0.1f + (0.5f - 0.1f) * 0.25f) / 0.5f);

    // A steady -6 dB: the reading is the static curve's, and the gain the
    // audio itself shows.
    {
        auto plugin = make(CompressorDevice::xmlTypeName);
        auto audio = filled(2, second, [] (int, int) { return 0.5f; });
        play(*plugin, audio, block);
        const auto reading = ours(*plugin).takeGainReductionDb();
        const auto applied = -juce::Decibels::gainToDecibels(audio.getSample(0, second - 1) / 0.5f);
        require(std::abs(reading - expected) < 0.02f,
                "a steady -6 dB over a 4:1 knee at -20 dB reads " + juce::String(reading, 3) + " dB, not "
                    + juce::String(expected, 3));
        require(std::abs(reading - applied) < 0.01f,
                "the reading is the gain the audio took, " + juce::String(applied, 3) + " dB");
        require(ours(*plugin).takeGainReductionDb() == 0.0f, "a reading is spent once taken");
    }
    // Make-up gain is not reduction.
    {
        auto plugin = make(CompressorDevice::xmlTypeName);
        set(*plugin, "output gain", 6.0f);
        auto audio = filled(2, second, [] (int, int) { return 0.5f; });
        play(*plugin, audio, block);
        const auto reading = ours(*plugin).takeGainReductionDb();
        require(audio.getSample(0, second - 1) > 0.39f, "the output gain lifts what comes out");
        require(std::abs(reading - expected) < 0.02f, "and the reading leaves it out: " + juce::String(reading, 3) + " dB");
    }
    // One channel measures the same as two.
    {
        auto plugin = make(CompressorDevice::xmlTypeName);
        auto audio = filled(1, second, [] (int, int) { return 0.5f; });
        play(*plugin, audio, block);
        const auto reading = ours(*plugin).takeGainReductionDb();
        require(std::abs(reading - expected) < 0.02f, "a mono input reads " + juce::String(reading, 3) + " dB");
    }
    // Under the threshold, and in silence, it takes nothing away.
    {
        auto plugin = make(CompressorDevice::xmlTypeName);
        auto quiet = filled(2, second, [] (int, int i) { return 0.05f * std::sin(0.05f * static_cast<float>(i)); });
        play(*plugin, quiet, block);
        require(ours(*plugin).takeGainReductionDb() == 0.0f, "a signal under the threshold reads no reduction");
        auto silence = filled(2, second, [] (int, int) { return 0.0f; });
        play(*plugin, silence, block);
        require(ours(*plugin).takeGainReductionDb() == 0.0f, "silence reads no reduction");
    }

    // Measuring changes nothing: Rhino's compressor and the engine's come out
    // bit for bit alike, on their own input and on a sidechain, in blocks
    // the meter's pieces do and do not divide.
    const auto loud = [] (int channel, int i)
    {
        const auto burst = (i / 4800) % 2 == 0 ? 0.9f : 0.08f;
        return burst * std::sin(0.031f * static_cast<float>(i) + static_cast<float>(channel));
    };
    for (const auto sidechain : { false, true })
        for (const auto size : { block, 1000, 4096 })
        {
            auto mine = make(CompressorDevice::xmlTypeName);
            auto engines = make(te::CompressorPlugin::xmlTypeName);
            for (auto* plugin : { mine.get(), engines.get() })
            {
                set(*plugin, "attack", 2.0f);
                set(*plugin, "release", 40.0f);
                // Outside any undo history: these plugins are on no track.
                plugin->state.setProperty(te::IDs::sidechainTrigger, sidechain, nullptr);
            }
            // The sidechain arrives as a third channel; on its own input the
            // compressor hears a pair.
            const auto channels = sidechain ? 3 : 2;
            const auto input = [&loud, sidechain] (int channel, int i)
            {
                return sidechain && channel < 2 ? 0.3f * std::sin(0.007f * static_cast<float>(i)) : loud(channel, i);
            };
            auto heardByMine = filled(channels, second, input);
            auto heardByEngine = filled(channels, second, input);
            play(*mine, heardByMine, size);
            play(*engines, heardByEngine, size);
            for (int channel = 0; channel < channels; ++channel)
                for (int i = 0; i < second; ++i)
                    require(heardByMine.getSample(channel, i) == heardByEngine.getSample(channel, i),
                            "in blocks of " + juce::String(size) + (sidechain ? " on a sidechain" : "")
                                + " the two differ at frame " + juce::String(i));
            require(dynamic_cast<te::CompressorPlugin&>(*mine).useSidechainTrigger.get() == sidechain,
                    "the compressor hears the sidechain only when told to");
            require(ours(*mine).takeGainReductionDb() > 3.0f,
                    "and the bursts read as reduction" + juce::String(sidechain ? " on a sidechain" : ""));
        }
    juce::Logger::writeToLog("Rhino: the Compressor's meter reads the gain it applies, and measuring changes nothing");
}
}
