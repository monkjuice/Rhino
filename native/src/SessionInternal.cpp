#include "SessionInternal.h"
#include <algorithm>

namespace theta
{

const juce::Identifier starterPlaceholderID {"thetaStarterPlaceholder"};
const juce::Identifier editorStepsID {"thetaEditorSteps"};
const juce::Identifier clipAutomationID {"thetaClipAutomation"};
const juce::Identifier automationTrackID {"track"};
const juce::Identifier automationSlotID {"slot"};
const juce::Identifier automationParameterID {"parameter"};
const juce::Identifier automationStartID {"start"};
const juce::Identifier automationEndID {"end"};
const juce::Identifier automationStartValueID {"startValue"};
const juce::Identifier automationEndValueID {"endValue"};

void panicMidiOnTrack(te::ClipTrack* clipTrack)
{
    auto* track = dynamic_cast<te::AudioTrack*>(clipTrack);
    if (track == nullptr)
        return;
    for (auto* plugin : track->pluginList)
        if (plugin != nullptr)
            plugin->midiPanic();
}

juce::Colour presetColour(Session::PatternPreset preset)
{
    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return juce::Colour(0xff4f7d8f);
        case Session::PatternPreset::AcidSteps:  return juce::Colour(0xff2f6e78);
        case Session::PatternPreset::ArpRun:     return juce::Colour(0xff77659a);
        case Session::PatternPreset::ChordPad:   return juce::Colour(0xff5f718f);
        case Session::PatternPreset::SubBass:    return juce::Colour(0xff34535f);
        case Session::PatternPreset::ReeseBass:  return juce::Colour(0xff4a5f38);
        case Session::PatternPreset::SirenLead:  return juce::Colour(0xff8f4f67);
        case Session::PatternPreset::WavePad:    return juce::Colour(0xff5e55b8);
        case Session::PatternPreset::WaveBass:   return juce::Colour(0xff355a86);
        case Session::PatternPreset::WavePluck:  return juce::Colour(0xff4c7a95);
        case Session::PatternPreset::HouseKit:   return juce::Colour(0xff657844);
        case Session::PatternPreset::BreakKit:   return juce::Colour(0xff6f7f43);
        case Session::PatternPreset::MinimalKit: return juce::Colour(0xff506d45);
        case Session::PatternPreset::ClapKit:    return juce::Colour(0xff8a7a42);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour instrumentColour(Session::Instrument instrument)
{
    switch (instrument)
    {
        case Session::Instrument::FourOsc:    return juce::Colour(0xff3d6f8b);
        case Session::Instrument::ThetaWave:  return juce::Colour(0xff574ec8);
        case Session::Instrument::ThetaForge: return juce::Colour(0xff3a9aa9);
        case Session::Instrument::Drums:      return juce::Colour(0xff738044);
        case Session::Instrument::Utility:    return juce::Colour(0xff56636c);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour nextClipColour(juce::Colour current)
{
    static constexpr juce::uint32 palette[] {
        0xff3d6f8b, 0xff738044, 0xff8d5f42, 0xff7a5b8f,
        0xff9b4f67, 0xff4b7f68, 0xff8a7a42, 0xff56636c
    };
    int closest = -1;
    for (int i = 0; i < static_cast<int>(std::size(palette)); ++i)
        if (current == juce::Colour(palette[i]))
        {
            closest = i;
            break;
        }
    return juce::Colour(palette[static_cast<size_t>((closest + 1) % static_cast<int>(std::size(palette)))]);
}

bool effectTypeAndName(Session::AudioEffect effect, const char*& type, juce::String& name)
{
    switch (effect)
    {
        case Session::AudioEffect::Equaliser:  type = te::EqualiserPlugin::xmlTypeName;  name = "EQ"; break;
        case Session::AudioEffect::Reverb:     type = te::ReverbPlugin::xmlTypeName;     name = "Reverb"; break;
        case Session::AudioEffect::Delay:      type = te::DelayPlugin::xmlTypeName;      name = "Delay"; break;
        case Session::AudioEffect::Compressor: type = te::CompressorPlugin::xmlTypeName; name = "Compressor"; break;
        case Session::AudioEffect::ThetaSpace: type = ThetaSpaceDevice::xmlTypeName;     name = "Theta Space"; break;
        case Session::AudioEffect::ThetaBloom: type = ThetaBloomDevice::xmlTypeName;     name = "Theta Bloom"; break;
    }
    return type != nullptr;
}

void resetPluginList(te::PluginList* list)
{
    if (list == nullptr)
        return;
    for (auto* plugin : *list)
        if (plugin != nullptr)
        {
            plugin->midiPanic();
            plugin->reset();
        }
}

double stepDurationBeats(int steps)
{
    return 4.0 / static_cast<double>(juce::jlimit(Session::defaultSteps, Session::steps, steps));
}

bool sameDeviceTarget(Session::DeviceTarget a, Session::DeviceTarget b)
{
    return a.track == b.track && a.slot == b.slot && a.parameter == b.parameter;
}

bool hasClipAutomationTarget(const te::Edit& edit, Session::DeviceTarget target)
{
    for (auto* track : te::getAudioTracks(edit))
        for (auto* clip : track->getClips())
        {
            for (int i = 0; i < clip->state.getNumChildren(); ++i)
            {
                const auto state = clip->state.getChild(i);
                if (!state.hasType(clipAutomationID))
                    continue;
                const Session::DeviceTarget clipTarget {
                    static_cast<int>(state.getProperty(automationTrackID, -1)),
                    static_cast<int>(state.getProperty(automationSlotID, -1)),
                    static_cast<int>(state.getProperty(automationParameterID, -1))
                };
                if (sameDeviceTarget(clipTarget, target))
                    return true;
            }
        }
    return false;
}

te::Plugin* findPlugin(te::AudioTrack& track, const juce::String& type)
{
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && plugin->getPluginType() == type)
            return plugin;
    return nullptr;
}

te::FourOscPlugin* findFourOsc(te::AudioTrack& track)
{
    return dynamic_cast<te::FourOscPlugin*>(findPlugin(track, te::FourOscPlugin::xmlTypeName));
}

ThetaWaveDevice* findThetaWave(te::AudioTrack& track)
{
    return dynamic_cast<ThetaWaveDevice*>(findPlugin(track, ThetaWaveDevice::xmlTypeName));
}

DrumDevice* findDrumDevice(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* drums = dynamic_cast<DrumDevice*>(plugin))
            return drums;
    return nullptr;
}

Session::Instrument activeTrackInstrument(te::AudioTrack& track)
{
    if (auto* drums = findDrumDevice(track))
        if (drums->isEnabled())
            return Session::Instrument::Drums;
    if (auto* wave = findThetaWave(track))
        if (wave->isEnabled())
            return Session::Instrument::ThetaWave;
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && plugin->isEnabled() && isForgePlugin(*plugin))
            return Session::Instrument::ThetaForge;
    return Session::Instrument::FourOsc;
}

bool isForgePlugin(const te::Plugin& plugin)
{
    if (auto* processor = plugin.getWrappedAudioProcessor())
        return processor->getName().containsIgnoreCase("Theta Forge")
            || processor->getName().equalsIgnoreCase("Forge");
    return false;
}

tracktion::core::TimeRange firstFreeDuplicateRange(te::Clip& source)
{
    const auto old = source.getPosition().time;
    const auto length = old.getLength();
    auto start = old.getEnd();
    auto* owner = source.getClipTrack();
    if (owner == nullptr)
        return {start, start + length};

    bool moved = false;
    do
    {
        moved = false;
        const tracktion::core::TimeRange candidate {start, start + length};
        for (auto* clip : owner->getClips())
        {
            if (clip == nullptr || clip == &source)
                continue;
            const auto occupied = clip->getPosition().time;
            if (candidate.overlaps(occupied))
            {
                start = occupied.getEnd();
                moved = true;
                break;
            }
        }
    }
    while (moved);

    return {start, start + length};
}

juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed)
{
    if ((plugin = findPlugin(track, type)) != nullptr)
        return juce::Result::ok();

    auto created = edit.getPluginCache().createNewPlugin(type, {});
    if (created == nullptr)
        return juce::Result::fail("The target track device could not be created.");

    plugin = created.get();
    track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), insertIndex), nullptr);
    changed = true;
    return juce::Result::ok();
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument, bool& changed,
                                   const juce::PluginDescription* forgeDescription)
{
    te::Plugin* selected = nullptr;
    const auto selectedType = instrument == Session::Instrument::Drums ? juce::String(DrumDevice::xmlTypeName)
        : instrument == Session::Instrument::ThetaWave ? juce::String(ThetaWaveDevice::xmlTypeName)
        : juce::String(te::FourOscPlugin::xmlTypeName);
    if (instrument == Session::Instrument::ThetaForge)
    {
        for (auto* plugin : track.pluginList)
            if (plugin != nullptr && isForgePlugin(*plugin))
                selected = plugin;

        if (selected == nullptr)
        {
            if (forgeDescription == nullptr)
                return juce::Result::fail("Theta Forge.vst3 was not found. Build or install the Forge VST3 first.");
            auto created = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *forgeDescription);
            if (created == nullptr)
                return juce::Result::fail("Theta Forge.vst3 could not be loaded.");
            selected = created.get();
            track.pluginList.insertPlugin(created, 0, nullptr);
            changed = true;
        }
    }
    else
    {
        auto result = ensurePlugin(edit, track, selectedType, 0, selected, changed);
        if (result.failed())
            return result;
    }

    if (auto* fourOsc = findPlugin(track, te::FourOscPlugin::xmlTypeName))
        if (fourOsc->isEnabled() != (instrument == Session::Instrument::FourOsc))
        {
            fourOsc->setEnabled(instrument == Session::Instrument::FourOsc);
            changed = true;
        }

    if (auto* wave = findPlugin(track, ThetaWaveDevice::xmlTypeName))
        if (wave->isEnabled() != (instrument == Session::Instrument::ThetaWave))
        {
            wave->setEnabled(instrument == Session::Instrument::ThetaWave);
            changed = true;
        }
    for (auto* forge : track.pluginList)
        if (forge != nullptr && isForgePlugin(*forge)
            && forge->isEnabled() != (instrument == Session::Instrument::ThetaForge))
        {
            forge->setEnabled(instrument == Session::Instrument::ThetaForge);
            changed = true;
        }

    if (auto* drumDevice = findDrumDevice(track))
        if (drumDevice->isEnabled() != (instrument == Session::Instrument::Drums))
        {
            drumDevice->setEnabled(instrument == Session::Instrument::Drums);
            changed = true;
        }

    if (selected != nullptr && !selected->isEnabled())
    {
        selected->setEnabled(true);
        changed = true;
    }

    return juce::Result::ok();
}

}
