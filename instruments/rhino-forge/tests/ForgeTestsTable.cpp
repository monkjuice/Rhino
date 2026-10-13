// The wavetable editor and the .wav files it loads.
#include "ForgeTestSupport.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTablePanel.h"
#include <cstring>

namespace rhino::forge::tests
{
namespace
{
// Forge's own factory tables, which the import test reads for real rather than
// writing a file of its own: the point is that the files that ship are loadable,
// not that some file is.
juce::File factoryTables()
{
    return juce::File(RHINO_FORGE_SOURCE_DIR).getChildFile("tables");
}

// --- The table editor ---------------------------------------------------------
//
// Everything M9b-2 and M9b-3 added: the frames as a person changes them, the
// hand-over to the audio thread, and reading a table out of a file.
void tableEditSuite()
{
    using rhino::forge::WavetableEdit;
    using rhino::forge::WavetableStore;
    using rhino::forge::wavetableFrameSize;
    using rhino::forge::maxEditableFrames;

    WavetableStore store;

    // A fresh store is the built-in ten, and what the editor draws is exactly
    // what the oscillator has been playing — the M9a guarantee, now that the
    // display reads the editable frames rather than the formulas.
    require(store.edit(0).frameCount() == rhino::forge::waveShapeCount,
            "a fresh table holds the built-in frames");
    require(store.edit(0).isUntouched(), "a fresh table counts as untouched");
    require(store.isBuiltIn(0) && store.frameCount(0) == rhino::forge::waveShapeCount,
            "what POSITION reads is published with the table");
    for (int oscillator = 0; oscillator < rhino::forge::oscillatorCount; ++oscillator)
        require(store.edit(oscillator).isUntouched() && store.table(oscillator) != nullptr,
                "every oscillator owns a published built-in table");
    for (const auto position : {0.0f, 0.23f, 0.5f, 0.77f, 1.0f})
        for (const auto phase : {0.0f, 0.1f, 0.37f, 0.62f, 0.99f})
            requireClose(store.edit(0).sample(position, phase), rhino::forge::waveAt(position, phase),
                         0.0005f, "the frames the editor draws are the frames the voice reads");

    // A stroke is a straight line between two points, and it touches nothing
    // outside the span it covers.
    auto& edit = store.edit(0);
    std::vector<float> before(edit.frame(2), edit.frame(2) + wavetableFrameSize);
    edit.draw(2, 0.25f, -1.0f, 0.75f, 1.0f);
    requireClose(edit.frame(2)[wavetableFrameSize / 4], -1.0f, 0.002f, "a stroke lands on its first point");
    requireClose(edit.frame(2)[wavetableFrameSize * 3 / 4], 1.0f, 0.002f, "a stroke lands on its last point");
    requireClose(edit.frame(2)[wavetableFrameSize / 2], 0.0f, 0.005f, "a stroke is straight between them");
    requireClose(edit.frame(2)[0], before[0], 0.0f, "a stroke leaves what it did not cross alone");
    requireClose(edit.frame(2)[wavetableFrameSize - 1], before[wavetableFrameSize - 1], 0.0f,
                 "a stroke leaves the end of the frame alone");
    // A stroke drawn right to left is the same stroke.
    edit.draw(3, 0.75f, 1.0f, 0.25f, -1.0f);
    requireClose(edit.frame(3)[wavetableFrameSize / 4], -1.0f, 0.002f,
                 "a stroke drawn backwards puts its ends where a forward one does");

    // The first edit takes the table's name and its frame names with it: "SAW"
    // is a lie about a frame somebody has drawn over.
    require(!edit.isUntouched(), "a stroke marks the table as edited");
    requireText(edit.title(), "CUSTOM", "an edited table is no longer the built-in one");
    requireText(edit.frameTitle(6), "7", "an edited table numbers its frames");

    // Init and normalise.
    edit.initFrame(4);
    requireClose(edit.frame(4)[0], 0.0f, 0.001f, "an initialised frame starts a sine at zero");
    requireClose(edit.frame(4)[wavetableFrameSize / 4], 1.0f, 0.005f, "an initialised frame is a sine");
    edit.draw(5, 0.0f, 0.25f, 0.5f, -0.25f);
    edit.draw(5, 0.5f, -0.25f, 1.0f, 0.25f);
    edit.normaliseFrame(5);
    auto peak = 0.0f;
    for (int i = 0; i < wavetableFrameSize; ++i) peak = juce::jmax(peak, std::abs(edit.frame(5)[i]));
    requireClose(peak, 1.0f, 0.001f, "a normalised frame reaches full scale");

    // Adding, copying and removing frames.
    const auto count = edit.frameCount();
    const auto added = edit.insertFrame(2, true);
    require(added == 3 && edit.frameCount() == count + 1, "a duplicated frame lands after the one it copies");
    for (int i = 0; i < wavetableFrameSize; i += 97)
        requireClose(edit.frame(3)[i], edit.frame(2)[i], 0.0f, "a duplicated frame is a copy");
    edit.removeFrame(3);
    require(edit.frameCount() == count, "removing a frame gives the count back");

    WavetableEdit single;
    std::vector<float> one(wavetableFrameSize, 0.5f);
    require(single.setFrames(one.data(), 1, "ONE"), "a one-frame table is allowed");
    require(single.removeFrame(0) == 0 && single.frameCount() == 1,
            "the last frame of a table cannot be removed");
    while (single.frameCount() < maxEditableFrames) single.insertFrame(single.frameCount() - 1, false);
    single.insertFrame(single.frameCount() - 1, false);
    require(single.frameCount() == maxEditableFrames, "a table stops growing at the ceiling");

    // The cheap path and the exact path have to agree. publishFrame rebuilds one
    // frame over a copy of the table; publish rebuilds every frame from scratch.
    // If these ever diverge, drawing would sound different from loading the same
    // table back, and only an ear would catch it.
    store.publish(0);
    require(store.table(0) != nullptr && store.table(0)->frameCount() == edit.frameCount(),
            "publishing hands over a table of the right size");

    // Sampled into a vector each time rather than held as a pointer across the
    // next publish: a replaced table is freed as soon as the store can prove
    // nothing is reading it, and with no block in flight that is immediately.
    const auto probe = [&store]
    {
        std::vector<float> taken;
        const auto* table = store.table(0);
        for (int level = 0; level < table->levelCount(); level += 3)
            for (const auto phase : {0.03f, 0.31f, 0.58f, 0.86f})
                taken.push_back(table->frameSample(level, 2, phase));
        return taken;
    };

    store.edit(0).draw(2, 0.1f, 0.4f, 0.9f, -0.4f);
    store.publishFrame(0, 2);
    const auto patched = probe();
    store.publish(0);
    const auto rebuilt = probe();
    auto agreed = !patched.empty() && patched.size() == rebuilt.size();
    for (size_t i = 0; i < patched.size() && i < rebuilt.size(); ++i)
        if (std::abs(patched[i] - rebuilt[i]) > 0.0005f) agreed = false;
    require(agreed, "rebuilding one frame gives what rebuilding the whole table gives");

    // The hand-over. A block that has already picked up a table goes on reading
    // it while the message thread publishes over the top — which is the case
    // that would be a use-after-free if a replaced table were freed on the spot.
    // Reading it afterwards is a canary rather than a proof: it is what a debug
    // allocator or a sanitiser has to be given something to catch.
    {
        const WavetableStore::ScopedBlock block(store);
        const auto* held = store.table(0);
        const auto frames = held->frameCount();
        store.edit(0).draw(1, 0.0f, 0.2f, 1.0f, -0.2f);
        store.publishFrame(0, 1);
        store.publish(0);
        require(held->frameCount() == frames, "a table picked up by a block survives being replaced");
        require(std::isfinite(held->sample(0, 0.5f, 0.25f)), "and is still readable through the block");
        require(store.table(0) != held, "while the next block is handed the new one");
    }
    store.collect();

    // Going back to the built-in ten really does go back: same frames, same
    // names, and no longer marked as edited.
    store.resetToBuiltIn(0);
    require(store.edit(0).isUntouched(), "resetting gives back an untouched table");
    require(store.isBuiltIn(0), "and says so to whatever is reading POSITION");
    requireText(store.edit(0).frameTitle(6), "SAW", "and gets the built-in frame names back");

    // What POSITION says depends on the table under it.
    requireText(rhino::forge::positionLabel(rhino::forge::waveShapeCount, true, 6.0f / 9.0f), "SAW",
                "POSITION names a built-in shape");
    requireText(rhino::forge::positionLabel(16, false, 0.0f), "1 / 16",
                "POSITION counts frames on a table nobody named");
    requireText(rhino::forge::positionLabel(16, false, 1.0f), "16 / 16",
                "POSITION counts to the last frame");
}

// Reading a wavetable out of a .wav, against the files Forge actually ships.
void tableFileSuite()
{
    rhino::forge::Processor processor;
    const auto folder = factoryTables();
    require(folder.isDirectory(), "the factory tables folder is where the tests expect it");

    const auto files = folder.findChildFiles(juce::File::findFiles, false, "*.wav");
    require(files.size() >= 10, "Forge ships at least ten factory tables");

    for (const auto& file : files)
    {
        const auto result = processor.importTable(0, file);
        require(result.wasOk(), "every factory table loads");
        if (!result.wasOk())
        {
            std::cerr << "       " << file.getFileName() << ": " << result.getErrorMessage() << '\n';
            continue;
        }
        const auto& loaded = processor.tableStore().edit(0);
        require(loaded.frameCount() == 16, "every factory table holds sixteen frames");
        requireText(loaded.title(), file.getFileNameWithoutExtension().toUpperCase(),
                    "a loaded table is named after its file");
        require(!loaded.isUntouched(), "a loaded table is not the built-in one");

        // Every frame bipolar, at full scale, and finite. A table that fails
        // this would change how loud the oscillator is as POSITION sweeps it.
        for (int frame = 0; frame < loaded.frameCount(); ++frame)
        {
            auto peak = 0.0f;
            auto finite = true;
            for (int i = 0; i < rhino::forge::wavetableFrameSize; ++i)
            {
                const auto sample = loaded.frame(frame)[i];
                finite = finite && std::isfinite(sample) && std::abs(sample) <= 1.0001f;
                peak = juce::jmax(peak, std::abs(sample));
            }
            require(finite, "a loaded frame stays finite and inside full scale");
            requireClose(peak, 1.0f, 0.01f, "a loaded frame reaches full scale");
        }
    }

    // What is refused. A table Forge cannot account for is better rejected than
    // half-loaded, because half a table is a sound nobody authored.
    require(!processor.importTable(0, folder.getChildFile("nothing-here.wav")).wasOk(),
            "a file that is not there is refused");
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-table-test", {}, true);
    require(directory.createDirectory(), "temporary table directory can be created");
    const auto notAudio = directory.getChildFile("not-a-table.wav");
    notAudio.replaceWithText("this is not a wav file");
    require(!processor.importTable(0, notAudio).wasOk(), "a file that is not audio is refused");
    directory.deleteRecursively();

    // And the table reaches the voice. With everything else switched off and
    // POSITION parked on the first frame, flattening that frame has to silence
    // the oscillator — which it can only do if the voice is reading the frames
    // the editor changed.
    rhino::forge::Processor voice;
    for (const auto* id : {"oscBEnable", "oscCEnable", "subEnable", "noiseEnable"})
        setValue(voice, id, 0.0f);
    setValue(voice, "oscAPosition", 0.0f);
    require(peakForNote(voice) > 0.01f, "the oscillator sounds before its frame is changed");
    voice.tableStore().edit(0).draw(0, 0.0f, 0.0f, 1.0f, 0.0f);
    voice.tableStore().publishFrame(0, 0);
    require(peakForNote(voice) < 0.001f, "flattening the frame POSITION is on silences the oscillator");
}

// A menu as lines of text, a submenu's items indented under it, so a whole
// nesting can be compared in one check. Every item is also held to the entry its
// id names, which is how a choice is turned back into a table.
juce::StringArray describeMenu(const juce::PopupMenu& menu, const std::vector<rhino::forge::TableEntry>& entries,
                               const juce::String& indent = {})
{
    juce::StringArray lines;
    juce::PopupMenu::MenuItemIterator items(menu);
    while (items.next())
    {
        const auto& item = items.getItem();
        if (item.isSeparator) { lines.add(indent + "---"); continue; }
        lines.add(indent + item.text + (item.subMenu != nullptr ? "/" : "") + (item.isTicked ? " *" : ""));
        if (item.subMenu != nullptr)
        {
            lines.addArray(describeMenu(*item.subMenu, entries, indent + "  "));
            continue;
        }
        const auto entry = item.itemID - 1;
        require(entry >= 0 && entry < static_cast<int>(entries.size())
                    && entries[static_cast<size_t>(entry)].name == item.text,
                "a table menu item's id names the entry it shows");
    }
    return lines;
}

// The tables on disk: the files NEW and SAVE write, the folder tree the table
// field lists, the menu that tree becomes, and the strip the field is seated in.
void tableLibrarySuite()
{
    using rhino::forge::wavetableFrameSize;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-table-library", {}, true);
    require(root.createDirectory(), "a temporary table root can be created");
    const auto own = rhino::forge::ownTableFolder(root);

    // --- Writing a table -------------------------------------------------------
    //
    // Three frames, each a sine at its own level, so a round trip is checked
    // frame by frame and a frame landing in the wrong place shows.
    const auto frameCount = 3;
    std::vector<float> frames(static_cast<size_t>(frameCount * wavetableFrameSize));
    for (int frame = 0; frame < frameCount; ++frame)
        for (int i = 0; i < wavetableFrameSize; ++i)
            frames[static_cast<size_t>(frame * wavetableFrameSize + i)] =
                0.25f * static_cast<float>(frame + 1)
                * std::sin(juce::MathConstants<float>::twoPi * static_cast<float>(i) / wavetableFrameSize);
    const auto written = root.getChildFile("written.wav");
    require(rhino::forge::writeTableFile(written, frames.data(), frameCount).wasOk(),
            "a table can be written to a file");
    // Written twice, because a second write over a file that is already there
    // is the case createOutputStream gets wrong by appending.
    require(rhino::forge::writeTableFile(written, frames.data(), frameCount).wasOk(),
            "a table can be written over its own file");

    // The frame size is named the way Serum names its own, so the file opens
    // there as well as here, and the audio is exactly the frames.
    juce::MemoryBlock bytes;
    require(written.loadFileAsData(bytes), "a written table can be read back as bytes");
    const auto* data = static_cast<const char*>(bytes.getData());
    const auto holds = [data, &bytes] (const char* text)
    {
        const auto length = std::strlen(text);
        return std::search(data, data + bytes.getSize(), text, text + length) != data + bytes.getSize();
    };
    require(holds("clm ") && holds("<!>2048"), "a written table names its frame size in a clm chunk");
    require(bytes.getSize() > static_cast<size_t>(frameCount * wavetableFrameSize) * sizeof(float)
                && bytes.getSize() < static_cast<size_t>(frameCount * wavetableFrameSize) * sizeof(float) + 200,
            "a written table holds its frames and a header, written once");

    rhino::forge::Processor processor;
    require(processor.importTable(0, written).wasOk(), "a written table loads");
    {
        const auto& loaded = processor.tableStore().edit(0);
        require(loaded.frameCount() == frameCount, "a written table loads with every frame");
        requireText(loaded.title(), "WRITTEN", "a written table is named after its file");
        auto worst = 0.0f;
        for (size_t i = 0; i < frames.size(); ++i)
            worst = juce::jmax(worst, std::abs(loaded.samples()[i] - frames[i]));
        requireClose(worst, 0.0f, 0.0f, "a written table loads back sample for sample");
    }

    // --- NEW -----------------------------------------------------------------
    const auto fresh = own.getChildFile("from-scratch.wav");
    require(processor.newTable(1, fresh).wasOk(), "NEW writes a table and loads it");
    require(fresh.existsAsFile(), "NEW leaves the file on disk, making the folder it goes in");
    {
        const auto& blank = processor.tableStore().edit(1);
        require(blank.frameCount() == 1, "a new table is one frame");
        requireText(blank.title(), "FROM-SCRATCH", "a new table is named after its file");
        auto loudest = 0.0f;
        for (const auto sample : blank.samples()) loudest = juce::jmax(loudest, std::abs(sample));
        requireClose(loudest, 0.0f, 0.0f, "a new table is blank");
    }

    // --- SAVE ----------------------------------------------------------------
    //
    // What is drawn is what is written, and loading the file back into another
    // oscillator gives the same frame.
    processor.tableStore().edit(1).draw(0, 0.0f, -0.5f, 1.0f, 0.5f);
    processor.tableStore().publish(1);
    require(processor.saveTable(1, fresh).wasOk(), "SAVE writes a table over its own file");
    require(processor.importTable(2, fresh).wasOk(), "a saved table loads");
    {
        auto worst = 0.0f;
        for (int i = 0; i < wavetableFrameSize; ++i)
            worst = juce::jmax(worst, std::abs(processor.tableStore().edit(2).frame(0)[i]
                                               - processor.tableStore().edit(1).frame(0)[i]));
        requireClose(worst, 0.0f, 0.0f, "a saved table is what was drawn");
    }
    // Saved somewhere new, the table takes that file's name, so the field reads
    // the file it is now in.
    require(processor.saveTable(0, own.getChildFile("renamed.wav")).wasOk(), "SAVE writes a table to a new file");
    requireText(processor.tableStore().edit(0).title(), "RENAMED", "a table saved as a new file takes its name");
    require(processor.tableStore().edit(0).frameCount() == frameCount, "and keeps every frame");
    // The built-in ten saved to a file are no longer the built-in ten, so
    // POSITION stops naming their frames as if they were.
    processor.tableStore().resetToBuiltIn(2);
    require(processor.saveTable(2, own.getChildFile("basic-copy.wav")).wasOk(), "the built-in table can be saved");
    require(!processor.tableStore().edit(2).isUntouched() && !processor.tableStore().isBuiltIn(2),
            "a saved built-in table is no longer the built-in one");

    // --- The list ------------------------------------------------------------
    //
    // A tree with a folder two deep, a file beside the folders, and something
    // that is not a table. Each folder's subfolders come before its files, and
    // both in natural order.
    require(root.getChildFile("Basic").getChildFile("Deep").createDirectory(), "folders can be made");
    require(written.copyFileTo(root.getChildFile("zeta.wav"))
                && written.copyFileTo(root.getChildFile("Basic").getChildFile("alpha.wav"))
                && written.copyFileTo(root.getChildFile("Basic").getChildFile("Deep").getChildFile("inner.wav")),
            "tables can be copied into the tree");
    root.getChildFile("notes.txt").replaceWithText("not a table");

    const auto entries = rhino::forge::listTables(root);
    juce::StringArray listed;
    for (const auto& entry : entries)
        listed.add(entry.folders.joinIntoString("/") + (entry.folders.isEmpty() ? "" : "/") + entry.name);
    const juce::StringArray expected {"BASIC SHAPES", "Basic/Deep/INNER", "Basic/ALPHA",
                                      "user/BASIC-COPY", "user/FROM-SCRATCH", "user/RENAMED",
                                      "WRITTEN", "ZETA"};
    if (listed != expected)
    {
        require(false, "the tables are listed depth first, folders before files");
        std::cerr << "       listed " << listed.joinIntoString(", ") << '\n';
    }
    require(entries.front().file == juce::File {}, "the built-in ten come first, and are not a file");

    // Which entry a table is.
    const auto indexOf = [&entries] (const juce::String& name)
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == name) return static_cast<int>(i);
        return -1;
    };
    require(rhino::forge::tableEntryIndex(entries, "BASIC SHAPES", true) == 0,
            "an untouched table is the built-in entry");
    require(rhino::forge::tableEntryIndex(entries, "ALPHA", false) == indexOf("ALPHA"),
            "a loaded table is the entry its file is");
    require(rhino::forge::tableEntryIndex(entries, "CUSTOM", false) == -1,
            "a table drawn over is none of the entries");

    // Where SAVE may write without asking: one of yours, and only one.
    require(rhino::forge::tableSaveTarget(entries, "FROM-SCRATCH", false, own) == fresh,
            "SAVE writes straight to a table of yours");
    require(rhino::forge::tableSaveTarget(entries, "ZETA", false, own) == juce::File {},
            "SAVE never writes over a table that is not yours");
    require(rhino::forge::tableSaveTarget(entries, "BASIC SHAPES", true, own) == juce::File {},
            "SAVE asks where to put the built-in ten");
    require(own.getChildFile("More").createDirectory()
                && fresh.copyFileTo(own.getChildFile("More").getChildFile("from-scratch.wav")),
            "a second table of the same name can be made");
    require(rhino::forge::tableSaveTarget(rhino::forge::listTables(root), "FROM-SCRATCH", false, own)
                == juce::File {},
            "SAVE asks rather than guess between two tables of one name");

    // --- The menu ------------------------------------------------------------
    //
    // The built-in ten on their own, then the tree with each folder a submenu,
    // and the way to the table playing ticked all the way down.
    const auto lines = describeMenu(rhino::forge::ui::tableMenu(entries, indexOf("INNER")), entries);
    const juce::StringArray menuExpected {"BASIC SHAPES", "---", "Basic/ *", "  Deep/ *", "    INNER *",
                                          "  ALPHA", "user/", "  BASIC-COPY", "  FROM-SCRATCH",
                                          "  RENAMED", "WRITTEN", "ZETA"};
    if (lines != menuExpected)
    {
        require(false, "the table menu nests each folder as a submenu");
        std::cerr << "       menu " << lines.joinIntoString(" | ") << '\n';
    }

    root.deleteRecursively();

    // --- The strip -----------------------------------------------------------
    //
    // Every oscillator seats the field along the top of its display, and the
    // tube a wavetable oscillator draws on starts below it and ends where the
    // display does.
    const juce::Rectangle<int> panel(0, 0, rhino::forge::ui::defaultPanelWidth,
                                     rhino::forge::ui::defaultPanelHeight);
    auto oscillators = 0;
    for (const auto& module : rhino::forge::ui::modules())
    {
        if (module.display != rhino::forge::ui::Display::oscillator) continue;
        ++oscillators;
        const auto row = rhino::forge::ui::tableStripRow(module);
        require(row >= 0, "every oscillator has a strip for its table field");
        if (row < 0) continue;
        const auto area = rhino::forge::ui::moduleBounds(panel, module);
        const auto strip = rhino::forge::ui::rowBounds(area, module, row);
        const auto display = rhino::forge::ui::displayBounds(area, module);
        const auto tube = rhino::forge::ui::wavetableTubeBounds(area, module);
        require(display.contains(strip) && strip.getY() <= display.getY() + 2,
                "the table field sits along the top of the display");
        require(!tube.intersects(strip) && tube.getBottom() == display.getBottom()
                    && tube.getX() == display.getX() && tube.getWidth() == display.getWidth(),
                "the tube starts below the field and keeps the display's sides and foot");
        require(strip.getWidth() > 100 && strip.getHeight() >= 20, "the table field has room for a name");
    }
    require(oscillators == rhino::forge::oscillatorCount, "every oscillator is laid out");
}
}

void tableTests()
{
    tableEditSuite();
    tableFileSuite();
    tableLibrarySuite();
}
}
