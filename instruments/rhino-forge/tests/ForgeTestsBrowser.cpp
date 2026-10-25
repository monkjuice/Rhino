// The preset bank and the browser over it: what a folder lists, what a preset
// says about itself, moving files about, and the panel's LOAD, SAVE and plate.
// The bank is a scratch folder throughout, never the one in Documents.
#include "ForgeSuites.h"
#include "ForgeTestSupport.h"
#include "../src/ForgePresetBrowser.h"
#include "../ui/ForgePresetPlate.h"

namespace rhino::forge::tests
{
namespace
{
juce::File scratchBank()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-bank-test", {}, true);
    presetBank::setRootForTests(folder.getChildFile("Presets"));
    presetBank::setLibraryRootsForTests({folder.getChildFile("Serum"), folder.getChildFile("Vital")});
    return folder;
}

PresetInfo described(const char* name, const char* author, const char* style)
{
    PresetInfo info;
    info.name = name;
    info.author = author;
    info.style = style;
    info.notes = "Notes on " + juce::String(name);
    return info;
}


// The browser lists a folder on a thread of its own and takes the result on
// the message loop; with no loop running here, this takes it by hand.
void waitForListing(PresetBrowser& browser)
{
    const auto until = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (browser.listing && juce::Time::getMillisecondCounterHiRes() < until)
    {
        juce::Thread::sleep(2);
        browser.takeListings();
    }
    require(!browser.listing, "a folder's listing lands");
}

// A folder listed again is walked again, so a file added, changed or deleted
// from outside Forge is noticed, but a file whose date and size have not
// moved keeps the entry read last time rather than being opened again.
void listingSuite()
{
    const auto scratch = scratchBank();
    presetBank::ensure();
    Processor processor;
    const auto folder = presetBank::root().getChildFile("Relist");
    folder.createDirectory();
    const auto one = folder.getChildFile("One.forgepreset");
    const auto two = folder.getChildFile("Two.forgepreset");
    require(processor.savePreset(one, described("One", "First", "Bass")).wasOk(), "One saves");
    require(processor.savePreset(two, described("Two", "First", "Lead")).wasOk(), "Two saves");
    const auto first = presetBank::presetsIn(folder, true);
    require(first.size() == 2, "both list");

    // An entry handed back as the previous listing is trusted for a file that
    // has not moved, so a doctored one proves the file was not opened again.
    auto doctored = first;
    for (auto& entry : doctored)
        if (entry.file == one) entry.author = "Kept";
    // Two is rewritten with a new author, and dated well after, so the clock's
    // resolution cannot make the two writes look like one.
    require(processor.savePreset(two, described("Two", "Second", "Lead")).wasOk(), "Two saves again");
    two.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(30));
    // And a third appears from outside, by a plain copy.
    const auto three = folder.getChildFile("Three.forgepreset");
    require(one.copyFileTo(three), "a copy arrives from outside");

    const auto again = presetBank::presetsIn(folder, true, &doctored);
    require(again.size() == 3, "a file added from outside is listed");
    for (const auto& entry : again)
    {
        if (entry.file == one) requireText(entry.author, "Kept", "a file that has not moved keeps its entry");
        if (entry.file == two) requireText(entry.author, "Second", "a file whose date moved is read again");
        if (entry.file == three) requireText(entry.author, "First", "a new file is read");
    }

    require(three.deleteFile(), "the copy goes again");
    const auto gone = presetBank::presetsIn(folder, true, &again);
    require(gone.size() == 2, "a file deleted from outside leaves the list");

    const auto stopped = presetBank::presetsIn(folder, true, nullptr, [] { return true; });
    require(stopped.empty(), "a listing asked to stop before it starts lists nothing");

    presetBank::setRootForTests({});
    presetBank::setLibraryRootsForTests({});
    scratch.deleteRecursively();
}

void bankSuite()
{
    const auto scratch = scratchBank();
    require(presetBank::ensure(), "the bank is made the first time it is asked for");
    require(presetBank::root().isDirectory(), "and is a folder");
    const auto factory = presetBank::factorySource();
    if (factory.isDirectory())
        require(presetBank::root().getChildFile(presetBank::factoryFolderName).isDirectory(),
                "a new bank is given the factory set");
    require(presetBank::restoreFactory() == 0, "restoring a complete factory set copies nothing");

    Processor processor;
    const auto basses = presetBank::root().getChildFile("Basses");
    juce::File made;
    require(presetBank::createFolder(presetBank::root(), "Basses", made).wasOk() && made == basses,
            "a folder is made in the bank");
    require(presetBank::createFolder(presetBank::root(), "Basses", made).failed(), "but not twice");
    require(presetBank::createFolder(basses, "Dark", made).wasOk(), "a folder is made inside a folder");

    setValue(processor, "cutoff", 900.0f);
    require(processor.savePreset(basses.getChildFile("Wide Reese.forgepreset"), described("Wide Reese", "Monk", "Bass")).wasOk(),
            "a preset is saved into a folder");
    setValue(processor, "cutoff", 400.0f);
    require(processor.savePreset(basses.getChildFile("Dark/Sub Floor.forgepreset"), described("Sub Floor", "Monk", "bass")).wasOk(),
            "and into a folder inside it");

    const auto listed = presetBank::presetsIn(basses, true);
    require(listed.size() == 2, "a folder lists the presets under it");
    if (listed.size() == 2)
    {
        requireText(listed[0].name, "Sub Floor", "sorted by name");
        requireText(listed[1].author, "Monk", "with the author read from the file's head");
        requireText(listed[0].style, "Bass", "and the style spelled as the chips spell it");
    }
    require(presetBank::presetsIn(basses, false).size() == 1, "a folder alone lists only its own");
    const auto folders = presetBank::foldersUnder(presetBank::root());
    require(folders.indexOf(basses) >= 0 && folders.indexOf(basses.getChildFile("Dark")) == folders.indexOf(basses) + 1,
            "the folders come depth first");

    // What a preset says about itself rides with it, and its name is its file's.
    Processor reader;
    require(reader.loadPreset(basses.getChildFile("Wide Reese.forgepreset")).wasOk(), "the saved preset opens");
    const auto info = reader.presetInfo();
    requireText(info.name, "Wide Reese", "named after its file");
    requireText(info.author, "Monk", "with its author");
    requireText(info.style, "Bass", "its style");
    requireText(info.notes, "Notes on Wide Reese", "and its notes");
    requireClose(value(reader, "cutoff"), 900.0f, 1.0f, "and the sound it was saved with");

    // A project carries the patch's name, though not the file it came from.
    juce::MemoryBlock saved;
    reader.getStateInformation(saved);
    Processor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    requireText(restored.presetInfo().name, "Wide Reese", "a project reopens on the patch's name");
    require(restored.presetFile() == juce::File {}, "and claims no file");

    // Renaming and moving, with the loaded patch following its file.
    juce::File renamed;
    require(processor.savePreset(basses.getChildFile("Taken.forgepreset"), described("Taken", "", "")).wasOk(),
            "a second preset in the folder");
    require(presetBank::rename(basses.getChildFile("Wide Reese.forgepreset"), "Taken", renamed).failed(),
            "a rename never writes over another preset");
    require(presetBank::rename(basses.getChildFile("Wide Reese.forgepreset"), "Wider Reese", renamed).wasOk(),
            "a preset is renamed");
    reader.presetFileMoved(basses.getChildFile("Wide Reese.forgepreset"), renamed);
    requireText(reader.presetInfo().name, "Wider Reese", "and the loaded patch takes the new name");
    require(reader.presetFile() == renamed, "and follows the file");
    juce::File moved;
    require(presetBank::moveInto(renamed, basses.getChildFile("Dark"), moved).wasOk(), "a preset is moved into a folder");
    require(moved.existsAsFile() && !renamed.exists(), "and is there and only there");
    require(presetBank::moveInto(basses, basses.getChildFile("Dark"), moved).failed(), "a folder cannot go inside itself");

    // The favourites are a setting of the machine, by path.
    Processor::saveFavouritePresets({moved.getFullPathName()});
    require(Processor::readFavouritePresets().contains(moved.getFullPathName()), "a favourite is remembered");
    Processor::saveFavouritePresets({});

    presetBank::setRootForTests({});
    presetBank::setLibraryRootsForTests({});
    scratch.deleteRecursively();
}

template <typename Type>
Type* childOf(juce::Component& parent, std::function<bool(Type&)> matches = {})
{
    for (auto* child : parent.getChildren())
        if (auto* found = dynamic_cast<Type*>(child))
            if (!matches || matches(*found)) return found;
    return nullptr;
}

ui::PresetButton* presetButton(juce::Component& parent, const char* caption)
{
    return childOf<ui::PresetButton>(parent, [caption] (ui::PresetButton& button)
    {
        return button.getButtonText() == caption;
    });
}

void browserSuite()
{
    const auto scratch = scratchBank();
    presetBank::ensure();
    Processor processor;
    const auto folder = presetBank::root().getChildFile("Leads");
    folder.createDirectory();
    for (const auto* name : {"Alpha", "Beta", "Gamma"})
    {
        setValue(processor, "cutoff", 500.0f + 100.0f * static_cast<float>(std::strlen(name)));
        processor.savePreset(folder.getChildFile(juce::String(name) + ".forgepreset"), described(name, "Tester", "Lead"));
    }
    processor.initialisePatch();
    requireText(processor.presetInfo().name, "INIT", "INIT is the patch with nothing loaded");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
    auto* load = presetButton(*editor, "LOAD");
    auto* save = presetButton(*editor, "SAVE");
    require(load != nullptr && save != nullptr, "LOAD and SAVE are on the panel");
    if (load == nullptr || save == nullptr) return;

    // The plate names the patch where the maker's legend was.
    auto* plate = childOf<ui::PresetPlate>(*editor);
    require(plate != nullptr && ui::identityPlateBounds(editor->getLocalBounds()).contains(plate->getBounds()),
            "the preset's name is on the maker plate");

    load->onClick();
    auto* browser = childOf<PresetBrowser>(*editor);
    require(browser != nullptr && browser->isVisible(), "LOAD opens the browser");
    if (browser == nullptr) return;
    waitForListing(*browser);
    require(browser->getBounds() == ui::contentBounds(editor->getLocalBounds()),
            "standing over the modules, between the title bar and the keys");
    require(load->getToggleState(), "with LOAD lit while it is open");

    auto leads = -1;
    for (int row = 0; row < static_cast<int>(browser->folderRows.size()); ++row)
        if (browser->folderRows[static_cast<size_t>(row)].folder == folder) leads = row;
    require(leads >= 0, "the bank's folders are listed");
    browser->chooseFolder(leads);
    waitForListing(*browser);
    require(browser->shown.size() == 3, "a folder shows its presets");
    browser->step(1);
    require(processor.presetFile() == folder.getChildFile("Alpha.forgepreset"), "the arrows load the first preset");
    browser->step(1);
    require(processor.presetFile() == folder.getChildFile("Beta.forgepreset"), "and the next");
    browser->step(-2);
    require(processor.presetFile() == folder.getChildFile("Gamma.forgepreset"), "and wrap round the list");
    requireClose(value(processor, "cutoff"), 1000.0f, 1.0f, "each one's sound arriving with it");

    browser->toggleFavourite(folder.getChildFile("Beta.forgepreset"));
    require(browser->isFavourite(folder.getChildFile("Beta.forgepreset")), "a star makes a favourite");
    browser->toggleFavourite(folder.getChildFile("Beta.forgepreset"));

    // SAVE opens the form, filled from the patch, into the folder on show.
    load->onClick();
    require(!browser->isVisible() && !load->getToggleState(), "LOAD closes it again");
    save->onClick();
    require(browser->isVisible() && browser->isSaving(), "SAVE opens it on the save form");
    require(browser->saveFolder() == folder, "saving into the folder the patch came from");
    auto* name = childOf<juce::TextEditor>(*browser, [] (juce::TextEditor& field)
    {
        return field.getTextToShowWhenEmpty() == "NAME";
    });
    require(name != nullptr && name->getText() == "Gamma", "the name field holds the patch's name");
    if (name != nullptr) name->setText("Delta", false);
    setValue(processor, "cutoff", 4321.0f);
    if (auto* commit = presetButton(*browser, "SAVE")) commit->onClick();
    const auto delta = folder.getChildFile("Delta.forgepreset");
    require(delta.existsAsFile(), "SAVE in the form writes the preset");
    waitForListing(*browser);
    require(browser->shown.size() == 4, "and the folder lists it");
    require(!browser->isSaving(), "and puts the form away");
    requireText(processor.presetInfo().name, "Delta", "the patch is now the one saved");
    Processor check;
    require(check.loadPreset(delta).wasOk(), "the saved preset opens");
    requireClose(value(check, "cutoff"), 4321.0f, 1.0f, "with the sound that was saved");
    requireText(check.presetInfo().author, "Tester", "and the author it was saved with");

    // A second SAVE under a taken name asks before replacing.
    browser->openToSave();
    if (auto* field = childOf<juce::TextEditor>(*browser, [] (juce::TextEditor& f)
        { return f.getTextToShowWhenEmpty() == "NAME"; }))
        field->setText("Alpha", false);
    const auto alphaBefore = folder.getChildFile("Alpha.forgepreset").loadFileAsString();
    if (auto* commit = presetButton(*browser, "SAVE")) commit->onClick();
    require(folder.getChildFile("Alpha.forgepreset").loadFileAsString() == alphaBefore,
            "the first SAVE over a taken name writes nothing");
    require(presetButton(*browser, "REPLACE") != nullptr, "it asks to be pressed again instead");

    editor.reset();
    presetBank::setRootForTests({});
    presetBank::setLibraryRootsForTests({});
    scratch.deleteRecursively();
}
}

void browserTests()
{
    listingSuite();
    bankSuite();
    browserSuite();
}
}
