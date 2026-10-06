#include "ContentLibrary.h"
#include "DevicePreset.h"
#include "DrumKitFile.h"
#include <algorithm>
#include <cstring>

namespace rhino
{
namespace
{
constexpr const char* libraryFolderName = "Library";
// How a kit or a project names a sound that lives in the library.
constexpr const char* libraryPrefix = "library:";

// The kinds of drum, and the words that name each, in the order the browser
// lists the kinds. A name is matched a word at a time, so "bd" finds "bd01" but
// not "lbd", and "hat" finds "ClosedHat" but not "that".
struct DrumKind
{
    const char* type;
    const char* words;
};

constexpr DrumKind drumKinds[] {
    { "Kick",       "kick kicks bd bassdrum kik" },
    { "Snare",      "snare snares sd snr" },
    { "Clap",       "clap claps handclap" },
    { "Hat",        "hat hats hh hihat hihats" },
    { "Tom",        "tom toms" },
    { "Cymbal",     "cymbal cymbals ride rides crash crashes china splash" },
    { "Percussion", "perc percs percussion rim rimshot bongo bongos conga congas cowbell shaker "
                    "tambourine tamb clave claves hit hits block timbale" },
};

// The words of a name, lowercased: split wherever it is not a letter or a
// digit, between a lowercase letter and an uppercase one, and between letters
// and digits, so "TR808ClosedHat" reads tr, 808, closed, hat.
juce::StringArray wordsOf(const juce::String& text)
{
    juce::StringArray words;
    juce::String word;
    const auto flush = [&words, &word]
    {
        if (word.isNotEmpty())
            words.add(word.toLowerCase());
        word.clear();
    };
    juce::juce_wchar previous = 0;
    for (auto pointer = text.getCharPointer(); !pointer.isEmpty();)
    {
        const auto character = pointer.getAndAdvance();
        if (!juce::CharacterFunctions::isLetterOrDigit(character))
        {
            flush();
            previous = 0;
            continue;
        }
        if (previous != 0
            && ((juce::CharacterFunctions::isLowerCase(previous) && juce::CharacterFunctions::isUpperCase(character))
                || juce::CharacterFunctions::isDigit(previous) != juce::CharacterFunctions::isDigit(character)))
            flush();
        word << juce::String::charToString(character);
        previous = character;
    }
    flush();
    return words;
}

int drumTypeOrder(const juce::String& type)
{
    const auto index = ContentLibrary::drumTypes().indexOf(type, true);
    return index >= 0 ? index : ContentLibrary::drumTypes().size();
}

std::vector<LibraryDrumFile> drumFilesIn(const juce::File& folder, const char* extension, bool byType, bool user)
{
    std::vector<LibraryDrumFile> found;
    if (!folder.isDirectory())
        return found;
    const auto pattern = juce::String("*") + extension;
    const auto add = [&found, &pattern, user] (const juce::File& directory, const juce::String& type)
    {
        for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, pattern))
            found.push_back({ file, DrumFiles::nameOf(file), type, user });
    };
    add(folder, {});
    if (byType)
        for (const auto& directory : folder.findChildFiles(juce::File::findDirectories, false))
            add(directory, directory.getFileName());
    std::sort(found.begin(), found.end(), [] (const LibraryDrumFile& a, const LibraryDrumFile& b)
    {
        if (a.type != b.type)
        {
            const auto orderA = drumTypeOrder(a.type), orderB = drumTypeOrder(b.type);
            return orderA != orderB ? orderA < orderB : a.type < b.type;
        }
        return a.name.compareNatural(b.name) < 0;
    });
    return found;
}

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
            const auto group = parent == pack ? juce::String() : parent.getFileName();
            const auto name = audio.getFileNameWithoutExtension();
            found.push_back({audio, pack.getFileName(), group, name, drumTypeOf(group, name)});
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

juce::File ContentLibrary::userPresets()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("RHINO_USER_PRESETS_DIR", {});
    if (overridePath.isNotEmpty())
        return juce::File(overridePath);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Rhino").getChildFile("Presets");
}

std::vector<LibraryPreset> ContentLibrary::presetsIn(const juce::File& presetsRoot, bool user)
{
    std::vector<LibraryPreset> found;
    if (!presetsRoot.isDirectory())
        return found;
    const auto pattern = juce::String("*") + DevicePreset::extension;
    for (const auto& device : presetsRoot.findChildFiles(juce::File::findDirectories, false))
        for (const auto& preset : device.findChildFiles(juce::File::findFiles, false, pattern))
            found.push_back({ preset, device.getFileName(), DevicePreset::nameOf(preset), user });
    std::sort(found.begin(), found.end(), [] (const LibraryPreset& a, const LibraryPreset& b)
    {
        return a.deviceId != b.deviceId ? a.deviceId < b.deviceId : a.name.compareNatural(b.name) < 0;
    });
    return found;
}

std::vector<LibraryPreset> ContentLibrary::presets()
{
    auto found = presetsIn(file("Presets"), false);
    const auto own = presetsIn(userPresets(), true);
    found.insert(found.end(), own.begin(), own.end());
    return found;
}

const juce::StringArray& ContentLibrary::drumTypes()
{
    static const juce::StringArray types = []
    {
        juce::StringArray list;
        for (const auto& kind : drumKinds)
            list.add(kind.type);
        return list;
    }();
    return types;
}

juce::String ContentLibrary::drumTypeOf(const juce::String& group, const juce::String& name)
{
    // The folder first: a pack that files its hits by kind has already said
    // what each one is, whatever else its name mentions -- a kick called
    // "Crash" is still a kick.
    for (const auto* text : { &group, &name })
    {
        const auto words = wordsOf(*text);
        for (const auto& kind : drumKinds)
            for (const auto& word : juce::StringArray::fromTokens(kind.words, " ", ""))
                if (words.contains(word))
                    return kind.type;
    }
    return {};
}

std::vector<LibraryDrumFile> ContentLibrary::drumKitsIn(const juce::File& drumsRoot, bool user)
{
    return drumFilesIn(drumsRoot.getChildFile("Kits"), DrumFiles::kitExtension, false, user);
}

std::vector<LibraryDrumFile> ContentLibrary::drumPresetsIn(const juce::File& drumsRoot, bool user)
{
    return drumFilesIn(drumsRoot.getChildFile("Presets"), DrumFiles::soundExtension, true, user);
}

std::vector<LibraryDrumFile> ContentLibrary::drumKits()
{
    auto found = drumKitsIn(file("Drums"), false);
    const auto own = drumKitsIn(userDrums(), true);
    found.insert(found.end(), own.begin(), own.end());
    return found;
}

std::vector<LibraryDrumFile> ContentLibrary::drumPresets()
{
    auto found = drumPresetsIn(file("Drums"), false);
    const auto own = drumPresetsIn(userDrums(), true);
    found.insert(found.end(), own.begin(), own.end());
    return found;
}

juce::File ContentLibrary::userDrums()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("RHINO_USER_DRUMS_DIR", {});
    if (overridePath.isNotEmpty())
        return juce::File(overridePath);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Rhino").getChildFile("Drums");
}

juce::String ContentLibrary::storedPath(const juce::File& sound)
{
    const auto& base = root();
    if (base != juce::File() && sound.isAChildOf(base))
        return libraryPrefix + sound.getRelativePathFrom(base).replaceCharacter('\\', '/');
    return sound.getFullPathName();
}

juce::File ContentLibrary::resolveStoredPath(const juce::String& stored)
{
    if (stored.startsWith(libraryPrefix))
    {
        const auto relative = stored.substring(static_cast<int>(std::strlen(libraryPrefix)));
        return relative.isEmpty() ? juce::File() : file(relative);
    }
    return juce::File::isAbsolutePath(stored) ? juce::File(stored) : juce::File();
}

}
