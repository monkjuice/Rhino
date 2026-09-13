#include "SessionInternal.h"

namespace theta
{
namespace
{
bool looksLikeForge(const juce::PluginDescription& description)
{
    return description.name.containsIgnoreCase("Theta Forge")
        || (description.name.containsIgnoreCase("Forge")
            && description.manufacturerName.containsIgnoreCase("Theta"));
}
}

void Session::initialiseExternalPlugins(bool retry)
{
#if JUCE_PLUGINHOST_VST3
    auto& pluginManager = engine.getPluginManager();
    if (!retry)
        pluginManager.pluginFormatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());

    for (const auto& description : pluginManager.knownPluginList.getTypes())
        if (looksLikeForge(description)
            && description.pluginFormatName.equalsIgnoreCase("VST3")
            && juce::File(description.fileOrIdentifier).exists())
        {
            forgeDescription = description;
            juce::Logger::writeToLog("Theta: using registered Forge VST3 at " + description.fileOrIdentifier);
            return;
        }

    juce::AudioPluginFormat* vst3 = nullptr;
    for (auto* format : pluginManager.pluginFormatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase("VST3"))
            vst3 = format;

    if (vst3 == nullptr)
        return;

    juce::Array<juce::File> candidates;
    const auto sourceRoot = juce::File(juce::String(THETA_SOURCE_DIR));
    for (const auto& configuration : {juce::String("Release"), juce::String("Debug")})
        candidates.add(sourceRoot.getChildFile("../instruments/theta-forge/build/ThetaForge_artefacts")
                                  .getChildFile(configuration).getChildFile("VST3").getChildFile("Theta Forge.vst3"));

    const auto defaultLocations = vst3->getDefaultLocationsToSearch();
    for (int i = 0; i < defaultLocations.getNumPaths(); ++i)
        candidates.add(defaultLocations[i].getChildFile("Theta Forge.vst3"));

    for (const auto& candidate : candidates)
    {
        if (!candidate.exists())
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        vst3->findAllTypesForFile(found, candidate.getFullPathName());
        juce::Logger::writeToLog("Theta: Forge VST3 scan " + candidate.getFullPathName()
                                 + " found " + juce::String(found.size()) + " type(s)"
                                 + (retry ? " on retry" : ""));
        for (auto* description : found)
            if (description != nullptr && looksLikeForge(*description))
            {
                juce::Logger::writeToLog("Theta: discovered " + description->name + " by "
                                         + description->manufacturerName + " (UID "
                                         + juce::String(description->uniqueId) + ")");
                forgeDescription = *description;
                pluginManager.knownPluginList.addType(*description);
                return;
            }
    }
#endif
}
}
