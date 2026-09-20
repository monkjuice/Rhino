// The wavetable editor and the .wav files it loads.
#include "ForgeTestSupport.h"

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
    for (const auto* id : {"oscBEnable", "subEnable", "noiseEnable"}) setValue(voice, id, 0.0f);
    setValue(voice, "oscAPosition", 0.0f);
    require(peakForNote(voice) > 0.01f, "the oscillator sounds before its frame is changed");
    voice.tableStore().edit(0).draw(0, 0.0f, 0.0f, 1.0f, 0.0f);
    voice.tableStore().publishFrame(0, 0);
    require(peakForNote(voice) < 0.001f, "flattening the frame POSITION is on silences the oscillator");
}
}

void tableTests()
{
    tableEditSuite();
    tableFileSuite();
}
}
