#include "ForgePresetBank.h"
#include "ForgePresetImport.h"
#include "../core/ForgeTableFiles.h"

namespace rhino::forge
{
namespace
{
juce::File& rootOverride()
{
    static juce::File redirected;
    return redirected;
}

juce::Array<juce::File>& libraryOverride()
{
    static juce::Array<juce::File> redirected;
    return redirected;
}

bool& libraryOverridden()
{
    static bool overridden = false;
    return overridden;
}

// What a list needs out of a Vital preset sits at the top of the file: its keys
// are written in order, and author, comments and the style all sort ahead of
// the settings that follow them and make up nearly all of its megabyte. So the
// top-level object is walked a key at a time, every value is skipped without
// being built, and the walk stops at the settings or at the end of what was
// read. Strings are the only values kept, and only for the keys asked about.
class JsonHeadScanner
{
public:
    explicit JsonHeadScanner(const juce::String& source) : text(source.getCharPointer()) {}

    juce::StringPairArray scan(const juce::StringArray& wanted, const juce::String& stopAt)
    {
        juce::StringPairArray found;
        skipSpace();
        if (!take('{')) return found;
        for (;;)
        {
            skipSpace();
            juce::String key;
            if (!readString(key)) return found;
            skipSpace();
            if (!take(':')) return found;
            skipSpace();
            if (key == stopAt) return found;
            if (wanted.contains(key) && *text == '"')
            {
                juce::String value;
                if (!readString(value)) return found;
                found.set(key, value);
            }
            else if (!skipValue(0))
                return found;
            skipSpace();
            if (!take(',')) return found;
        }
    }

private:
    juce::String::CharPointerType text;

    void skipSpace()
    {
        while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') ++text;
    }

    bool take(juce::juce_wchar c)
    {
        if (*text != c) return false;
        ++text;
        return true;
    }

    // A string, with JSON's escapes undone. Written out rather than handed to
    // juce::JSON, whose parser takes only an object or an array whole.
    bool readString(juce::String& into, bool keep = true)
    {
        if (*text != '"') return false;
        ++text;
        juce::String result;
        while (!text.isEmpty() && *text != '"')
        {
            auto c = *text++;
            if (c == '\\')
            {
                if (text.isEmpty()) return false;
                c = *text++;
                switch (c)
                {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u':
                    {
                        juce::String hex;
                        for (int i = 0; i < 4 && !text.isEmpty(); ++i) hex << *text++;
                        c = static_cast<juce::juce_wchar>(hex.getHexValue32());
                        break;
                    }
                    default: break;  // \" \\ \/ stand for themselves
                }
            }
            if (keep) result << juce::String::charToString(c);
        }
        if (text.isEmpty()) return false;
        ++text;
        into = result;
        return true;
    }

    bool skipValue(int depth)
    {
        if (depth > 64) return false;
        skipSpace();
        if (*text == '"')
        {
            juce::String ignored;
            return readString(ignored, false);
        }
        if (*text == '{' || *text == '[')
        {
            const auto close = *text == '{' ? '}' : ']';
            const auto object = *text == '{';
            ++text;
            skipSpace();
            if (take(static_cast<juce::juce_wchar>(close))) return true;
            for (;;)
            {
                if (object)
                {
                    juce::String key;
                    skipSpace();
                    if (!readString(key)) return false;
                    skipSpace();
                    if (!take(':')) return false;
                }
                if (!skipValue(depth + 1)) return false;
                skipSpace();
                if (take(static_cast<juce::juce_wchar>(close))) return true;
                if (!take(',')) return false;
                skipSpace();
            }
        }
        // A number or a literal: everything up to the next delimiter.
        auto moved = false;
        while (!text.isEmpty() && *text != ',' && *text != '}' && *text != ']'
               && *text != ' ' && *text != '\n' && *text != '\r' && *text != '\t')
        {
            ++text;
            moved = true;
        }
        return moved;
    }
};

juce::String readHead(const juce::File& file, int bytes)
{
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return {};
    juce::MemoryBlock block;
    stream.readIntoMemoryBlock(block, bytes);
    return juce::String::fromUTF8(static_cast<const char*>(block.getData()), static_cast<int>(block.getSize()));
}

void readForgeHead(PresetEntry& entry)
{
    juce::XmlDocument document(entry.file);
    // The outer element only: a preset carrying a table or a sample is a
    // hundred kilobytes of base64 under it that a list has no use for.
    const auto wrapper = document.getDocumentElement(true);
    if (wrapper == nullptr || !wrapper->hasTagName("RhinoForgePreset")) return;
    entry.author = wrapper->getStringAttribute("author");
    entry.style = canonicalPresetStyle(wrapper->getStringAttribute("style"));
}

void readVitalHead(PresetEntry& entry)
{
    const auto head = readHead(entry.file, 1 << 15);
    const auto found = JsonHeadScanner(head).scan({"author", "preset_style"}, "settings");
    entry.author = found["author"];
    entry.style = canonicalPresetStyle(found["preset_style"]);
}

void readSerumHead(PresetEntry& entry)
{
    SerumPresetHeader header;
    if (!readSerumPresetHeader(entry.file, header)) return;
    entry.author = header.author;
    entry.style = canonicalPresetStyle(header.style);
}

bool isHiddenName(const juce::String& name) { return name.startsWithChar('.'); }

juce::File userDocuments()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
}
}

PresetFormat presetFormatOf(const juce::File& file)
{
    const auto extension = file.getFileExtension();
    if (extension.equalsIgnoreCase(".forgepreset")) return PresetFormat::forge;
    if (extension.equalsIgnoreCase(".SerumPreset")) return PresetFormat::serum;
    if (extension.equalsIgnoreCase(".vital")) return PresetFormat::vital;
    return PresetFormat::unknown;
}

juce::String presetFormatName(PresetFormat format)
{
    switch (format)
    {
        case PresetFormat::serum: return "Serum 2";
        case PresetFormat::vital: return "Vital";
        case PresetFormat::forge: return "Forge";
        case PresetFormat::unknown: break;
    }
    return {};
}

const juce::StringArray& presetStyles()
{
    static const juce::StringArray styles {"Bass", "Lead", "Keys", "Pad", "Percussion",
                                           "Sequence", "Experiment", "SFX", "Template"};
    return styles;
}

juce::String canonicalPresetStyle(const juce::String& raw)
{
    const auto style = raw.trim().toLowerCase();
    if (style.isEmpty()) return {};
    for (const auto& known : presetStyles())
        if (style == known.toLowerCase()) return known;
    // Other synths' words for the same shelves. Serum 2 files by category, and
    // its packs name the usual ones in their own spellings.
    static const std::array<std::pair<const char*, const char*>, 22> synonyms {{
        {"basses", "Bass"},     {"sub", "Bass"},            {"808", "Bass"},
        {"leads", "Lead"},      {"synth", "Lead"},          {"keys", "Keys"},
        {"pluck", "Keys"},      {"plucks", "Keys"},         {"bell", "Keys"},
        {"piano", "Keys"},      {"pads", "Pad"},            {"chord", "Pad"},
        {"drum", "Percussion"}, {"drums", "Percussion"},    {"perc", "Percussion"},
        {"arp", "Sequence"},    {"seq", "Sequence"},        {"sequence", "Sequence"},
        {"fx", "SFX"},          {"sfx", "SFX"},             {"init", "Template"},
        {"experimental", "Experiment"},
    }};
    for (const auto& [word, shelf] : synonyms)
        if (style == word) return shelf;
    return {};
}

PresetEntry readPresetEntry(const juce::File& file)
{
    PresetEntry entry;
    entry.file = file;
    entry.name = file.getFileNameWithoutExtension();
    entry.modified = file.getLastModificationTime();
    entry.format = presetFormatOf(file);
    switch (entry.format)
    {
        case PresetFormat::forge: readForgeHead(entry); break;
        case PresetFormat::vital: readVitalHead(entry); break;
        case PresetFormat::serum: readSerumHead(entry); break;
        case PresetFormat::unknown: break;
    }
    return entry;
}

namespace presetBank
{
juce::File root()
{
    if (rootOverride() != juce::File {}) return rootOverride();
    return userDocuments().getChildFile("Rhino Forge").getChildFile("Presets");
}

void setRootForTests(const juce::File& folder) { rootOverride() = folder; }

void setLibraryRootsForTests(const juce::Array<juce::File>& folders)
{
    libraryOverride() = folders;
    libraryOverridden() = !folders.isEmpty();
}

juce::File factorySource()
{
    const auto found = sourceFolderAbove(juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                                         juce::String("presets/") + factoryFolderName);
    return found;
}

int restoreFactory()
{
    const auto source = factorySource();
    if (!source.isDirectory()) return 0;
    const auto target = root().getChildFile(factoryFolderName);
    if (!target.createDirectory()) return 0;
    auto copied = 0;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true, "*.forgepreset"))
    {
        const auto destination = target.getChildFile(file.getRelativePathFrom(source));
        if (destination.exists()) continue;
        destination.getParentDirectory().createDirectory();
        if (file.copyFileTo(destination)) ++copied;
    }
    return copied;
}

bool ensure()
{
    const auto bank = root();
    if (bank.isDirectory()) return true;
    if (!bank.createDirectory()) return false;
    restoreFactory();
    return true;
}

std::vector<PresetLibrary> libraries()
{
    std::vector<PresetLibrary> found;
    found.push_back({"FORGE", root(), true});
    const auto add = [&found] (const juce::String& name, const juce::File& folder)
    {
        if (folder.isDirectory()) found.push_back({name, folder, false});
    };
    if (libraryOverridden())
    {
        const auto& roots = libraryOverride();
        if (roots.size() > 0) add("SERUM 2", roots[0]);
        if (roots.size() > 1) add("VITAL", roots[1]);
        return found;
    }
    // Where each installer puts its user-facing presets. Serum 2 keeps its
    // tables and its presets side by side under one root; Vital keeps one
    // folder per pack, Factory among them.
    add("SERUM 2", userDocuments().getChildFile("Xfer").getChildFile("Serum 2 Presets").getChildFile("Presets"));
    add("VITAL", userDocuments().getChildFile("Vital"));
    return found;
}

juce::Array<juce::File> foldersUnder(const juce::File& folder)
{
    juce::Array<juce::File> found;
    auto children = folder.findChildFiles(juce::File::findDirectories, false);
    children.sort();
    for (const auto& child : children)
    {
        if (isHiddenName(child.getFileName())) continue;
        found.add(child);
        found.addArray(foldersUnder(child));
    }
    return found;
}

std::vector<PresetEntry> presetsIn(const juce::File& folder, bool recursive)
{
    std::vector<PresetEntry> found;
    for (const auto& item : juce::RangedDirectoryIterator(folder, recursive, "*",
                                                          juce::File::findFiles))
    {
        const auto& file = item.getFile();
        if (!isPresetFile(file) || isHiddenName(file.getFileName())) continue;
        found.push_back(readPresetEntry(file));
    }
    std::sort(found.begin(), found.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        return a.name.compareNatural(b.name) < 0;
    });
    return found;
}

juce::String legalName(const juce::String& name)
{
    return juce::File::createLegalFileName(name.trim()).trim();
}

juce::Result createFolder(const juce::File& parent, const juce::String& name, juce::File& made)
{
    const auto legal = legalName(name);
    if (legal.isEmpty()) return juce::Result::fail("A folder needs a name.");
    const auto folder = parent.getChildFile(legal);
    if (folder.exists()) return juce::Result::fail("There is already a " + legal + " here.");
    const auto created = folder.createDirectory();
    if (created.failed()) return created;
    made = folder;
    return juce::Result::ok();
}

juce::Result rename(const juce::File& item, const juce::String& newName, juce::File& renamed)
{
    const auto legal = legalName(newName);
    if (legal.isEmpty()) return juce::Result::fail("That name is empty.");
    const auto target = item.getSiblingFile(legal + (item.isDirectory() ? juce::String() : item.getFileExtension()));
    if (target == item) { renamed = item; return juce::Result::ok(); }
    // A case-only rename is the same file on Windows, and moveFileTo handles it.
    if (target.exists() && target.getFullPathName().compareIgnoreCase(item.getFullPathName()) != 0)
        return juce::Result::fail("There is already a " + legal + " here.");
    if (!item.moveFileTo(target)) return juce::Result::fail("Forge could not rename " + item.getFileName() + ".");
    renamed = target;
    return juce::Result::ok();
}

juce::Result moveInto(const juce::File& item, const juce::File& folder, juce::File& moved)
{
    if (!folder.isDirectory()) return juce::Result::fail("That folder is not there.");
    if (item.getParentDirectory() == folder) { moved = item; return juce::Result::ok(); }
    if (item.isDirectory() && (folder == item || folder.isAChildOf(item)))
        return juce::Result::fail("A folder cannot go inside itself.");
    const auto target = folder.getChildFile(item.getFileName());
    if (target.exists()) return juce::Result::fail("There is already a " + item.getFileName() + " there.");
    if (!item.moveFileTo(target)) return juce::Result::fail("Forge could not move " + item.getFileName() + ".");
    moved = target;
    return juce::Result::ok();
}

juce::Result remove(const juce::File& item)
{
    if (!item.exists()) return juce::Result::ok();
    if (!item.moveToTrash()) return juce::Result::fail("Forge could not delete " + item.getFileName() + ".");
    return juce::Result::ok();
}
}
}
