#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace rhino
{
// Rhino's content -- samples now, presets and patterns later -- lives in files
// under one root and is never compiled into the executable.  JUCE turns an
// embedded asset into a C++ array, which cost roughly three bytes of source per
// byte of audio: the 700 KB that used to be embedded here expanded to 2.1 MB of
// generated code, and a sample library measured in tens of megabytes would have
// to be recompiled in full on every clean build.
//
// Nothing in here knows about Session, the UI, or the engine, so both the app
// and the device library can resolve content without depending on each other.
// One audio file in the library, already split the way a browser wants it.
struct LibrarySample
{
    juce::File file;
    juce::String pack;   // the folder under Samples/, e.g. "VinylDrums"
    juce::String group;  // the folder inside the pack, empty if the file is loose
    juce::String name;   // the file name without its extension
};

// One device preset on disk (see DevicePreset.h). The folder it sits in is
// named for its device's catalog id, so a browser can file it under its device
// without opening it.
struct LibraryPreset
{
    juce::File file;
    juce::String deviceId;
    juce::String name;   // the file name without its extension
    bool user = false;   // saved by the person, rather than shipped with Rhino
};

class ContentLibrary
{
public:
    // The library root, or a non-existent File if no candidate was found.
    // Resolved once on first use; the search order is reported to the log.
    static const juce::File& root();

    // A path below the root, e.g. file("Samples/TR808/TR808Kick.wav").
    // Returns a non-existent File when the library is missing, which every
    // caller must handle -- content is data on disk and can be absent.
    static juce::File file(const juce::String& relativePath);

    static bool isAvailable();

    // Every audio file under Samples/, sorted by pack, then group, then name.
    // Scanned once on first use: the library does not change while Rhino runs,
    // and the browser asks for this while it is being built.
    static const std::vector<LibrarySample>& samples();

    // Device presets: the factory set under Presets/ in the library, then the
    // person's own under userPresets(), each in a folder named for its
    // device's catalog id, sorted by name within each. Read from disk on every
    // call, unlike samples: there are few, and one just saved must show.
    static std::vector<LibraryPreset> presets();
    // The presets under one root, as presets() reads each of its two.
    static std::vector<LibraryPreset> presetsIn(const juce::File& root, bool user);

    // Where the person's own presets are kept: Documents/Rhino/Presets, or
    // RHINO_USER_PRESETS_DIR when that is set, as a test sets it.
    static juce::File userPresets();

private:
    static juce::File resolveRoot();
    static std::vector<LibrarySample> scanSamples();
};
}
