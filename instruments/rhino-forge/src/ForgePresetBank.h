#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

// Where presets live, what is in a folder of them, and moving them about.
//
// Forge's bank is one folder tree, Documents/Rhino Forge/Presets, and it is the
// player's: they save into it and file it into folders however they like. The
// factory set is copied into Factory/ under it the first time the bank is made,
// rather than read from where it ships, so it can be renamed, moved and deleted
// like anything the player saved — and Restore puts back whatever went missing.
//
// The libraries of the other synths found on this machine are listed beside the
// bank and only ever read: a Serum 2 or Vital preset is converted when it is
// opened (ForgePresetImport.h), never written back where it came from.
//
// Nothing here is on the audio thread or knows about the panel, and nothing
// here opens a whole preset: a list needs a name, an author, a style and a
// date, and reading only those is what keeps a folder of six hundred quick.
namespace rhino::forge
{
enum class PresetFormat { forge, serum, vital, unknown };

PresetFormat presetFormatOf(const juce::File&);
inline bool isPresetFile(const juce::File& file) { return presetFormatOf(file) != PresetFormat::unknown; }
// "Forge", "Serum 2", "Vital": what a preset is said to come from on the plate.
juce::String presetFormatName(PresetFormat);
// The wildcard a file chooser offers for every format Forge opens.
inline const char* presetFilePatterns() { return "*.forgepreset;*.SerumPreset;*.vital"; }

// The styles a preset is filed under: Vital's nine, which is also what a Vital
// preset carries, so one imported from there keeps its own. The browser's chips
// are these, in this order.
const juce::StringArray& presetStyles();
// A style as the list spells it, or empty for one the list does not have. Takes
// the names other synths use for the same thing — "Arp" is a Sequence, "Pluck"
// is Keys — so an imported preset lands under a chip rather than under none.
juce::String canonicalPresetStyle(const juce::String&);

// What the plate says about the patch that is loaded, and what a preset file
// says about itself. Not a parameter: it is text, nothing about it is heard,
// and it rides on the state tree's own properties the way a macro's name does,
// so a project reopens showing the patch it was saved with.
struct PresetInfo
{
    juce::String name, author, style, notes;
    // What the patch was converted from — "Serum 2", "Vital" — or empty for
    // one that was always Forge's.
    juce::String source;
    // What the conversion could not carry across, one line each. Kept with the
    // patch, so a converted preset saved into the bank still says what it lost.
    juce::StringArray warnings;
};

// What a list shows of one preset.
struct PresetEntry
{
    juce::File file;
    juce::String name, author, style;
    // When the file was last written and how long it is: together, what
    // says whether an entry read before still describes the file.
    juce::Time modified;
    juce::int64 size = 0;
    PresetFormat format = PresetFormat::unknown;
};

// Reads only the head of the file: a Forge preset's wrapper attributes, a Serum
// preset's metadata block, a Vital preset's top-level keys before its settings.
// A file that cannot be read still makes an entry, named after itself.
PresetEntry readPresetEntry(const juce::File&);

// One tree of presets the browser lists.
struct PresetLibrary
{
    juce::String name;
    juce::File root;
    // Only the bank. Everything else is someone else's folder.
    bool writable = false;
};

namespace presetBank
{
// Documents/Rhino Forge/Presets, or wherever a test has pointed it.
juce::File root();
void setRootForTests(const juce::File&);
// The folders a library search is pointed at instead of the machine's own, for
// the tests; an empty array puts back the real ones.
void setLibraryRootsForTests(const juce::Array<juce::File>& serumAndVital);

// The factory set as it ships: presets/Factory in the source tree, found upward
// from the binary as the tables are. Nothing in a build with no source tree.
juce::File factorySource();
inline constexpr const char* factoryFolderName = "Factory";

// Makes the bank if it is not there, filling Factory/ the first time. True when
// the bank exists afterwards. Cheap when it already does.
bool ensure();
// Copies into Factory/ every factory preset the bank no longer has there, and
// says how many. What the player changed is left alone.
int restoreFactory();

// The bank first, then Serum 2's and Vital's preset folders where this machine
// has them.
std::vector<PresetLibrary> libraries();

// Every folder under a root, depth first, each level by name.
juce::Array<juce::File> foldersUnder(const juce::File& root);
// The presets in a folder, and in every folder under it when asked.
//
// Given the folder's previous listing, a file whose date and size have not
// moved keeps its entry rather than being opened again, so a library of six
// hundred costs one walk the second time rather than six hundred opens; a
// file added, changed or deleted from outside Forge is still noticed, because
// the walk itself always happens. `shouldStop`, when given, is asked between
// files and ends the listing early with what it has: the browser lists on a
// thread of its own and may want that thread back.
std::vector<PresetEntry> presetsIn(const juce::File& folder, bool recursive,
                                   const std::vector<PresetEntry>* previous = nullptr,
                                   const std::function<bool()>& shouldStop = {});

// A name a file can carry: the characters Windows refuses taken out, trimmed.
juce::String legalName(const juce::String&);

// Folder and file housekeeping for the browser. Every one refuses a name that
// is already taken rather than writing over it, and says why in the Result.
juce::Result createFolder(const juce::File& parent, const juce::String& name, juce::File& made);
juce::Result rename(const juce::File& item, const juce::String& newName, juce::File& renamed);
juce::Result moveInto(const juce::File& item, const juce::File& folder, juce::File& moved);
// To the recycle bin, so a slip of the hand is recoverable.
juce::Result remove(const juce::File& item);
}
}
