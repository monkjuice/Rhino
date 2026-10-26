#include "SessionInternal.h"

// Finding the VST3 behind each external entry of the catalog: Rhino Forge
// and Rhino Duck, built beside Rhino rather than into it. Each is looked for
// in the engine's known-plugin list, then in its development build under
// the repository, then in the platform's VST3 folders, and the first
// description that matches the entry is kept by catalog id.
namespace rhino
{
namespace
{
// A scan's description belongs to an entry when it carries the plugin's
// name, or its last word with Rhino as the manufacturer ("Forge" by "Rhino").
bool looksLike(const DeviceDescriptor& device, const juce::PluginDescription& description)
{
    if (device.pluginName.isEmpty())
        return false;
    const auto lastWord = device.pluginName.fromLastOccurrenceOf(" ", false, false);
    return description.name.containsIgnoreCase(device.pluginName)
        || (description.name.containsIgnoreCase(lastWord) && description.manufacturerName.containsIgnoreCase("Rhino"));
}
}

bool Session::isExternalAvailable(const juce::String& deviceId) const
{
    return externalPlugins.find(deviceId) != externalPlugins.end();
}

const juce::PluginDescription* Session::externalDescription(const juce::String& deviceId) const
{
    const auto found = externalPlugins.find(deviceId);
    return found != externalPlugins.end() ? &found->second : nullptr;
}

void Session::initialiseExternalPlugins(bool retry)
{
#if JUCE_PLUGINHOST_VST3
    auto& pluginManager = engine.getPluginManager();
    if (!retry)
        pluginManager.pluginFormatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());

    juce::AudioPluginFormat* vst3 = nullptr;
    for (auto* format : pluginManager.pluginFormatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase("VST3"))
            vst3 = format;

    const auto sourceRoot = juce::File(juce::String(RHINO_SOURCE_DIR));
    for (const auto& device : DeviceCatalog::all())
    {
        if (!device.external || isExternalAvailable(device.id))
            continue;

        auto registered = false;
        for (const auto& description : pluginManager.knownPluginList.getTypes())
            if (looksLike(device, description)
                && description.pluginFormatName.equalsIgnoreCase("VST3")
                && juce::File(description.fileOrIdentifier).exists())
            {
                externalPlugins[device.id] = description;
                juce::Logger::writeToLog("Rhino: using registered " + device.displayName + " VST3 at " + description.fileOrIdentifier);
                registered = true;
                break;
            }
        if (registered || vst3 == nullptr)
            continue;

        juce::Array<juce::File> candidates;
        if (device.developmentArtefacts.isNotEmpty() && device.bundle.isNotEmpty())
            for (const auto& configuration : {juce::String("Release"), juce::String("Debug")})
                candidates.add(sourceRoot.getChildFile("../" + device.developmentArtefacts)
                                         .getChildFile(configuration).getChildFile("VST3").getChildFile(device.bundle));
        if (device.bundle.isNotEmpty())
        {
            const auto defaultLocations = vst3->getDefaultLocationsToSearch();
            for (int i = 0; i < defaultLocations.getNumPaths(); ++i)
                candidates.add(defaultLocations[i].getChildFile(device.bundle));
        }

        for (const auto& candidate : candidates)
        {
            if (!candidate.exists())
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            vst3->findAllTypesForFile(found, candidate.getFullPathName());
            juce::Logger::writeToLog("Rhino: " + device.displayName + " VST3 scan " + candidate.getFullPathName()
                                     + " found " + juce::String(found.size()) + " type(s)"
                                     + (retry ? " on retry" : ""));
            auto matched = false;
            for (auto* description : found)
                if (description != nullptr && looksLike(device, *description))
                {
                    juce::Logger::writeToLog("Rhino: discovered " + description->name + " by "
                                             + description->manufacturerName + " (UID "
                                             + juce::String(description->uniqueId) + ")");
                    externalPlugins[device.id] = *description;
                    pluginManager.knownPluginList.addType(*description);
                    matched = true;
                    break;
                }
            if (matched)
                break;
        }
    }
#else
    juce::ignoreUnused(retry);
#endif
}
}
