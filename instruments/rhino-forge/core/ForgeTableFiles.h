#pragma once

#include "ForgeShapes.h"

#include <algorithm>
#include <vector>

// Where Forge's wavetables are on disk, which ones there are, and writing one
// back out: what the table field on every oscillator and on the TABLE tab steps
// through, and what NEW and SAVE write into.
//
// The tables live under one root, and the list is that root's tree: a folder is
// a submenu and a .wav in it is a table, as deep as the folders go. The tables
// made here -- NEW, and SAVE on a table that has no file of its own yet -- go
// into `user` under the root. In a development build that is tables/user in the
// source tree, which git ignores, so a developer's experiments sit beside the
// factory tables without ever reaching a commit, and the tree shows them as one
// more submenu with nothing special needed to list them.
//
// Nothing here is on the audio thread or knows about the panel. A table is
// tied back to its entry by name: importTable titles a table after its file, so
// the title of the table an oscillator is playing is what says which entry it
// came from.
namespace rhino::forge
{
struct TableEntry
{
    // What the field shows, and what a table loaded from the file is called:
    // the file's name, upper-cased, exactly as importTable titles it.
    juce::String name;
    // Empty for the built-in ten, which are not a file.
    juce::File file;
    // The folders between the root and the file, outermost first: the
    // submenus it is reached through.
    juce::StringArray folders;
};

// The folder under the root that NEW and SAVE write into.
inline constexpr const char* ownTableFolderName = "user";

// The root. tables/ in the source tree in a development build, found upward
// from the binary the way LOAD has always found it: the standalone and the
// plugin both sit several directories under the source tree. A packaged build
// has no such folder, and keeps its tables in Documents beside the LFO shapes.
inline juce::File tableRoot()
{
    auto here = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (int step = 0; step < 8 && here != juce::File {}; ++step)
    {
        const auto candidate = here.getChildFile("tables");
        if (candidate.isDirectory()) return candidate;
        here = here.getParentDirectory();
    }
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Rhino Forge").getChildFile("Tables");
}

inline juce::File ownTableFolder(const juce::File& root) { return root.getChildFile(ownTableFolderName); }

// What a table read from this file is called. importTable names a table with
// this, so the two cannot drift apart.
inline juce::String tableNameOf(const juce::File& file)
{
    return file.getFileNameWithoutExtension().toUpperCase();
}

namespace detail
{
// Depth first, each folder's subfolders before its own files and both in
// natural order, so the list is the order the menu reads in and the arrows walk
// the menu top to bottom.
inline void collectTables(const juce::File& folder, const juce::StringArray& folders,
                          std::vector<TableEntry>& into, int depth)
{
    // A folder linked back into itself would otherwise never end.
    if (depth > 8) return;
    const auto byName = [] (const juce::File& a, const juce::File& b)
    { return a.getFileName().compareNatural(b.getFileName()) < 0; };

    auto subfolders = folder.findChildFiles(juce::File::findDirectories, false);
    std::sort(subfolders.begin(), subfolders.end(), byName);
    for (const auto& subfolder : subfolders)
    {
        if (subfolder.isHidden() || subfolder.getFileName().startsWithChar('.')) continue;
        auto deeper = folders;
        deeper.add(subfolder.getFileName());
        collectTables(subfolder, deeper, into, depth + 1);
    }

    auto files = folder.findChildFiles(juce::File::findFiles, false, "*.wav");
    std::sort(files.begin(), files.end(), byName);
    for (const auto& file : files) into.push_back({tableNameOf(file), file, folders});
}
}

// Every table there is: the built-in ten first, then the root's tree.
inline std::vector<TableEntry> listTables(const juce::File& root)
{
    std::vector<TableEntry> entries;
    entries.push_back({builtInWavetable().title(), {}, {}});
    if (root.isDirectory()) detail::collectTables(root, {}, entries, 0);
    return entries;
}

// Which entry a table is, or -1 when it is none of them: a table drawn over, a
// preset's own, one loaded from somewhere outside the root. Only the built-in
// ten are untouched, so an untouched table is the first entry whatever it is
// called; anything else is the first entry carrying its name.
inline int tableEntryIndex(const std::vector<TableEntry>& entries, const juce::String& title,
                           bool untouched)
{
    if (untouched) return entries.empty() ? -1 : 0;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file != juce::File {} && entries[i].name == title) return static_cast<int>(i);
    return -1;
}

// The file SAVE may write over without asking: a table of this name in the
// folder of tables made here, if there is exactly one. A factory table is never
// written over, and neither is anything that would have to be guessed at --
// those are saved as a new file instead.
inline juce::File tableSaveTarget(const std::vector<TableEntry>& entries, const juce::String& title,
                                  bool untouched, const juce::File& ownFolder)
{
    if (untouched) return {};
    juce::File found;
    for (const auto& entry : entries)
    {
        if (entry.name != title || !entry.file.isAChildOf(ownFolder)) continue;
        if (found != juce::File {}) return {};
        found = entry.file;
    }
    return found;
}

// A table written the way the factory tables are: RIFF, fmt, a `clm ` chunk
// naming the frame size the way Serum names its own, then the frames end to
// end as 32-bit float mono. tables/make-tables.py writes the same bytes, so a
// table saved here opens in Serum and Vital as readily as the factory ones do.
//
// Written beside the target and moved into place, because createOutputStream
// appends to a file that is already there.
inline juce::Result writeTableFile(const juce::File& file, const float* samples, int frames)
{
    if (samples == nullptr || frames < 1) return juce::Result::fail("there is no table to write.");
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("Forge could not make the folder " + file.getParentDirectory().getFullPathName());

    constexpr int sampleRate = 44100;
    const auto count = static_cast<size_t>(frames) * static_cast<size_t>(wavetableFrameSize);
    const auto marker = "<!>" + juce::String(wavetableFrameSize).paddedLeft('0', 4)
                      + " 00000000 wavetable (rhino forge)";
    juce::MemoryBlock clm(marker.toRawUTF8(), marker.getNumBytesAsUTF8());
    // A chunk is padded to an even length, as RIFF requires.
    if (clm.getSize() % 2 != 0) clm.append("\0", 1);
    const auto dataBytes = static_cast<int>(count * sizeof(float));
    const auto chunks = (8 + 16) + (8 + static_cast<int>(clm.getSize())) + (8 + dataBytes);

    juce::TemporaryFile temporary(file);
    {
        const auto out = temporary.getFile().createOutputStream();
        if (out == nullptr) return juce::Result::fail("Forge could not write " + file.getFileName());
        out->write("RIFF", 4);
        out->writeInt(4 + chunks);
        out->write("WAVE", 4);
        out->write("fmt ", 4);
        out->writeInt(16);
        out->writeShort(3);   // IEEE float
        out->writeShort(1);
        out->writeInt(sampleRate);
        out->writeInt(sampleRate * 4);
        out->writeShort(4);
        out->writeShort(32);
        out->write("clm ", 4);
        out->writeInt(static_cast<int>(clm.getSize()));
        out->write(clm.getData(), clm.getSize());
        out->write("data", 4);
        out->writeInt(dataBytes);
        for (size_t i = 0; i < count; ++i) out->writeFloat(samples[i]);
        out->flush();
        if (out->getStatus().failed()) return juce::Result::fail("Forge could not write " + file.getFileName());
    }
    if (!temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Forge could not write " + file.getFileName());
    return juce::Result::ok();
}
}
