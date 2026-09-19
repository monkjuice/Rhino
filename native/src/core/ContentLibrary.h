#pragma once
#include <juce_core/juce_core.h>

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

private:
    static juce::File resolveRoot();
};
}
