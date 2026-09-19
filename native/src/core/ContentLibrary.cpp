#include "ContentLibrary.h"
#include <algorithm>

namespace rhino
{
namespace
{
constexpr const char* libraryFolderName = "Library";

// A root is only accepted if it actually holds content.  An empty directory
// that happens to sit next to the executable would otherwise shadow the real
// library in a development build and leave every device silent.
bool looksLikeLibrary(const juce::File& candidate)
{
    return candidate.isDirectory() && candidate.getChildFile("Samples").isDirectory();
}
}

juce::File ContentLibrary::resolveRoot()
{
    juce::Array<juce::File> candidates;

    // An explicit override comes first, so a packaged build or a test can point
    // at a library that is neither installed nor in the source tree.
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("RHINO_LIBRARY_DIR", {});
    if (overridePath.isNotEmpty())
        candidates.add(juce::File(overridePath));

    // An installed build ships the library beside the executable.  On macOS the
    // same folder lives inside the bundle, so look there before walking out of it.
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto executableDirectory = executable.getParentDirectory();
    candidates.add(executableDirectory.getChildFile(libraryFolderName));
   #if JUCE_MAC
    candidates.add(executable.getParentDirectory().getParentDirectory()
                             .getChildFile("Resources").getChildFile(libraryFolderName));
   #endif

    // A development build runs straight out of the build tree, where the only
    // copy of the library is the one in the repository.  This mirrors how the
    // Forge VST3 is found in SessionExternalPlugins.cpp.  RHINO_SOURCE_DIR is
    // the native/ folder, not the repository root, so the library is its sibling.
   #if defined(RHINO_SOURCE_DIR)
    candidates.add(juce::File(juce::String(RHINO_SOURCE_DIR)).getParentDirectory()
                       .getChildFile("library"));
   #endif

    for (const auto& candidate : candidates)
        if (looksLikeLibrary(candidate))
        {
            juce::Logger::writeToLog("Rhino: content library at " + candidate.getFullPathName());
            return candidate;
        }

    juce::String searched;
    for (const auto& candidate : candidates)
        searched << "\n  " << candidate.getFullPathName();
    juce::Logger::writeToLog("Rhino: no content library found. Searched:" + searched);
    return {};
}

const juce::File& ContentLibrary::root()
{
    // Resolved once: the answer cannot change while the process is running, and
    // devices ask for it from initialise(), which the engine may call often.
    static const juce::File resolved = resolveRoot();
    return resolved;
}

juce::File ContentLibrary::file(const juce::String& relativePath)
{
    const auto& base = root();
    if (base == juce::File())
        return {};
    return base.getChildFile(relativePath);
}

bool ContentLibrary::isAvailable()
{
    return root() != juce::File();
}

std::vector<LibrarySample> ContentLibrary::scanSamples()
{
    std::vector<LibrarySample> found;
    const auto samplesRoot = file("Samples");
    if (!samplesRoot.isDirectory())
        return found;

    // Extensions JUCE's basic formats can read. A pack may carry artwork or a
    // SOURCE.md beside its audio, and neither belongs in the browser.
    const auto* audioPattern = "*.wav;*.flac;*.aif;*.aiff;*.ogg;*.mp3";

    for (const auto& pack : samplesRoot.findChildFiles(juce::File::findDirectories, false))
        for (const auto& audio : pack.findChildFiles(juce::File::findFiles, true, audioPattern))
        {
            // One level of grouping inside a pack, which is how the packs here
            // are laid out: VinylDrums/Kick/... and a loose file for a small one.
            const auto parent = audio.getParentDirectory();
            found.push_back({audio, pack.getFileName(),
                             parent == pack ? juce::String() : parent.getFileName(),
                             audio.getFileNameWithoutExtension()});
        }

    std::sort(found.begin(), found.end(), [](const LibrarySample& a, const LibrarySample& b)
    {
        if (a.pack != b.pack)   return a.pack < b.pack;
        if (a.group != b.group) return a.group < b.group;
        return a.name < b.name;
    });
    return found;
}

const std::vector<LibrarySample>& ContentLibrary::samples()
{
    static const std::vector<LibrarySample> scanned = scanSamples();
    return scanned;
}

}
