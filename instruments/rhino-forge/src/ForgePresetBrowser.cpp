// What the browser lists and what it does: the folders, the presets of the one
// chosen, the filters, and every action on a file. What it draws is in
// ForgePresetBrowserViews.cpp.
#include "ForgePresetBrowserInternal.h"

namespace rhino::forge
{
namespace
{
juce::String styleCaption(const juce::String& style)
{
    return style.toUpperCase();
}

int depthBelow(const juce::File& folder, const juce::File& root)
{
    return juce::StringArray::fromTokens(folder.getRelativePathFrom(root), "\\/", {}).size();
}
}

PresetBrowser::PresetBrowser(Processor& p) : processor(p)
{
    setOpaque(true);
    setBufferedToImage(true);
    setWantsKeyboardFocus(true);
    favourites = Processor::readFavouritePresets();

    info = std::make_unique<InfoCard>(*this);
    folders = std::make_unique<FolderList>(*this);
    table = std::make_unique<PresetTable>(*this);
    addAndMakeVisible(*info);
    addAndMakeVisible(*folders);
    addAndMakeVisible(*table);

    browserLook::dressField(search, "SEARCH NAME, AUTHOR OR STYLE");
    search.addListener(this);
    addAndMakeVisible(search);

    for (const auto& style : presetStyles())
    {
        auto chip = std::make_unique<ui::ToggleChip>(styleCaption(style));
        chip->accent = ui::signalViolet;
        chip->setTooltip("Show only " + style.toLowerCase() + " presets. Several at once show any of them.");
        chip->onClick = [this] { refilter(); };
        addAndMakeVisible(*chip);
        styleChips.push_back(std::move(chip));
    }

    closeButton.setTooltip("Close the browser (Esc). LOAD opens it again.");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    openButton.setTooltip("Open a Forge, Serum 2 or Vital preset from anywhere on disk");
    openButton.onClick = [this] { chooseFileToOpen(); };
    initButton.setTooltip("Start again from the INIT patch: every control at its default");
    initButton.onClick = [this]
    {
        processor.initialisePatch();
        highlighted = {};
        info->refresh();
        table->repaint();
        if (onPresetLoaded) onPresetLoaded(juce::Result::ok(), {});
    };
    previousButton.setTooltip("Load the preset above this one in the list");
    previousButton.onClick = [this] { step(-1); };
    nextButton.setTooltip("Load the preset below this one in the list");
    nextButton.onClick = [this] { step(1); };
    for (auto* button : {&closeButton, &openButton, &initButton, &previousButton, &nextButton})
        addAndMakeVisible(*button);

    browserLook::dressField(nameField, "NAME");
    browserLook::dressField(authorField, "AUTHOR");
    browserLook::dressField(notesField, "NOTES", true);
    for (auto* field : {&nameField, &authorField, &notesField})
    {
        field->addListener(this);
        addChildComponent(*field);
    }
    styleField.setTooltip("The style the browser files this preset under");
    styleField.onClick = [this] { chooseStyleForSave(); };
    saveButton.setTooltip("Write the patch into the folder named above it");
    saveButton.onClick = [this] { commitSave(); };
    cancelButton.onClick = [this] { showSaveForm(false); };
    for (auto* button : {&styleField, &saveButton, &cancelButton}) addChildComponent(*button);

    browserLook::dressField(renameField, {});
    renameField.addListener(this);
    addChildComponent(renameField);

    // The library roots open folded: a Serum install is hundreds of folders,
    // and the bank's own are what the list is for.
    for (const auto& library : presetBank::libraries())
        if (!library.writable) collapsed.insert(library.root.getFullPathName());
}

PresetBrowser::~PresetBrowser() = default;

const PresetBrowser::FolderRow* PresetBrowser::chosenRow() const
{
    return chosenFolder >= 0 && chosenFolder < static_cast<int>(folderRows.size())
        ? &folderRows[static_cast<size_t>(chosenFolder)] : nullptr;
}

void PresetBrowser::openToBrowse()
{
    cancelRename();
    showSaveForm(false);
    scanned.clear();
    rebuildFolders();

    // Open on the folder the loaded patch came from, so the arrows walk its
    // neighbours. A patch from nowhere in particular opens on the whole bank.
    const auto current = processor.presetFile();
    auto row = 0;
    if (current != juce::File {})
        for (int i = 0; i < static_cast<int>(folderRows.size()); ++i)
        {
            const auto& candidate = folderRows[static_cast<size_t>(i)];
            if (candidate.kind == FolderRow::Kind::favourites) continue;
            if (current.getParentDirectory() == candidate.folder) row = i;
        }
    highlighted = current;
    // A folder found inside a folded library is shown by unfolding it.
    for (const auto& candidate : folderRows)
        if (folderRows[static_cast<size_t>(row)].folder.isAChildOf(candidate.folder))
            collapsed.erase(candidate.folder.getFullPathName());
    folders->rebuildDrawn();
    chooseFolder(row);
    revealHighlighted();
    info->refresh();
    table->grabKeyboardFocus();
}

void PresetBrowser::openToSave()
{
    openToBrowse();
    showSaveForm(true);
}

void PresetBrowser::rebuildFolders()
{
    const auto previous = chosenRow() != nullptr ? chosenRow()->folder : juce::File {};
    const auto previousKind = chosenRow() != nullptr ? chosenRow()->kind : FolderRow::Kind::all;
    folderRows.clear();
    presetBank::ensure();
    for (const auto& library : presetBank::libraries())
    {
        FolderRow top;
        top.kind = library.writable ? FolderRow::Kind::all : FolderRow::Kind::library;
        top.caption = library.writable ? juce::String("ALL PRESETS") : library.name;
        top.folder = library.root;
        top.writable = library.writable;
        folderRows.push_back(top);
        if (library.writable)
        {
            FolderRow favourite;
            favourite.kind = FolderRow::Kind::favourites;
            favourite.caption = "FAVOURITES";
            folderRows.push_back(favourite);
        }
        const auto topIndex = folderRows.size() - 1;
        for (const auto& folder : presetBank::foldersUnder(library.root))
        {
            FolderRow row;
            row.kind = FolderRow::Kind::folder;
            row.caption = folder.getFileName();
            row.folder = folder;
            row.depth = depthBelow(folder, library.root);
            row.writable = library.writable;
            folderRows.push_back(row);
        }
        // A library's own row folds its whole tree; the bank's ALL does not,
        // because the bank's folders are the list's reason to exist.
        folderRows[library.writable ? topIndex - 1 : topIndex].hasChildren =
            !library.writable && folderRows.size() > topIndex + 1;
    }
    for (size_t i = 0; i + 1 < folderRows.size(); ++i)
        if (folderRows[i].kind == FolderRow::Kind::folder)
            folderRows[i].hasChildren = folderRows[i + 1].kind == FolderRow::Kind::folder
                                     && folderRows[i + 1].folder.isAChildOf(folderRows[i].folder);

    chosenFolder = 0;
    for (int i = 0; i < static_cast<int>(folderRows.size()); ++i)
        if (folderRows[static_cast<size_t>(i)].kind == previousKind
            && folderRows[static_cast<size_t>(i)].folder == previous)
            chosenFolder = i;
    folders->rebuildDrawn();
    folders->repaint();
}

void PresetBrowser::toggleCollapsed(int row)
{
    if (row < 0 || row >= static_cast<int>(folderRows.size())) return;
    const auto key = folderRows[static_cast<size_t>(row)].folder.getFullPathName();
    if (collapsed.erase(key) == 0) collapsed.insert(key);
    folders->rebuildDrawn();
    folders->clampScroll();
    folders->repaint();
}

void PresetBrowser::chooseFolder(int row)
{
    if (row < 0 || row >= static_cast<int>(folderRows.size())) return;
    chosenFolder = row;
    folders->repaint();
    relist();
    if (saving) repaint(infoArea);
}

void PresetBrowser::relist()
{
    listed.clear();
    const auto* row = chosenRow();
    if (row == nullptr) { refilter(); return; }
    if (row->kind == FolderRow::Kind::favourites)
    {
        for (const auto& path : favourites)
        {
            const juce::File file(path);
            if (file.existsAsFile()) listed.push_back(readPresetEntry(file));
        }
    }
    else
    {
        // A folder lists everything under it, so a library's root is the
        // whole library and the bank's ALL is the whole bank. Kept once read:
        // a Serum install is six hundred files, and stepping back to a folder
        // already seen should not read them all again.
        const auto key = row->folder.getFullPathName();
        auto found = scanned.find(key);
        if (found == scanned.end())
            found = scanned.emplace(key, presetBank::presetsIn(row->folder, true)).first;
        listed = found->second;
    }
    refilter();
}

void PresetBrowser::refilter()
{
    shown.clear();
    juce::StringArray styles;
    for (size_t i = 0; i < styleChips.size(); ++i)
        if (styleChips[i]->getToggleState()) styles.add(presetStyles()[static_cast<int>(i)]);
    auto words = juce::StringArray::fromTokens(search.getText().toLowerCase(), " ", "\"");
    words.removeEmptyStrings();

    for (int i = 0; i < static_cast<int>(listed.size()); ++i)
    {
        const auto& entry = listed[static_cast<size_t>(i)];
        if (!styles.isEmpty() && !styles.contains(entry.style)) continue;
        const auto haystack = (entry.name + " " + entry.author + " " + entry.style).toLowerCase();
        auto matches = true;
        for (const auto& word : words)
            if (!haystack.contains(word)) { matches = false; break; }
        if (matches) shown.push_back(i);
    }

    const auto compare = [this] (int a, int b)
    {
        const auto& left = listed[static_cast<size_t>(a)];
        const auto& right = listed[static_cast<size_t>(b)];
        auto order = 0;
        switch (sortColumn)
        {
            case Column::favourite:
                order = static_cast<int>(isFavourite(right.file)) - static_cast<int>(isFavourite(left.file));
                break;
            case Column::style:  order = left.style.compareIgnoreCase(right.style); break;
            case Column::author: order = left.author.compareIgnoreCase(right.author); break;
            case Column::date:
                order = left.modified < right.modified ? -1 : (right.modified < left.modified ? 1 : 0);
                break;
            case Column::name: break;
        }
        if (order == 0) order = left.name.compareNatural(right.name);
        return sortAscending ? order < 0 : order > 0;
    };
    std::stable_sort(shown.begin(), shown.end(), compare);
    table->clampScroll();
    table->repaint();
}

void PresetBrowser::sortBy(Column column)
{
    if (sortColumn == column) sortAscending = !sortAscending;
    else
    {
        sortColumn = column;
        // Newest first is what a date is sorted for; everything else reads
        // from the top of the alphabet.
        sortAscending = column != Column::date;
    }
    refilter();
    revealHighlighted();
}

void PresetBrowser::revealHighlighted()
{
    for (int i = 0; i < static_cast<int>(shown.size()); ++i)
        if (listed[static_cast<size_t>(shown[static_cast<size_t>(i)])].file == highlighted)
        {
            table->reveal(i);
            return;
        }
}

bool PresetBrowser::isFavourite(const juce::File& file) const
{
    return favourites.contains(file.getFullPathName());
}

void PresetBrowser::toggleFavourite(const juce::File& file)
{
    const auto path = file.getFullPathName();
    if (favourites.contains(path)) favourites.removeString(path);
    else favourites.add(path);
    Processor::saveFavouritePresets(favourites);
    if (const auto* row = chosenRow(); row != nullptr && row->kind == FolderRow::Kind::favourites) relist();
    else table->repaint();
    info->repaint();
}

void PresetBrowser::loadPreset(const juce::File& file)
{
    const auto result = processor.loadPreset(file);
    if (result.wasOk()) highlighted = file;
    info->refresh();
    table->repaint();
    repaint(topBar);
    if (onPresetLoaded) onPresetLoaded(result, file);
}

void PresetBrowser::step(int delta)
{
    if (shown.empty()) return;
    auto at = -1;
    for (int i = 0; i < static_cast<int>(shown.size()); ++i)
        if (listed[static_cast<size_t>(shown[static_cast<size_t>(i)])].file == highlighted) at = i;
    const auto count = static_cast<int>(shown.size());
    // Round the end of the list to its start and back, as a hardware synth's
    // patch buttons go.
    const auto next = at < 0 ? (delta > 0 ? 0 : count - 1) : (at + delta + count) % count;
    loadPreset(listed[static_cast<size_t>(shown[static_cast<size_t>(next)])].file);
    table->reveal(next);
}

juce::File PresetBrowser::saveFolder() const
{
    const auto bank = presetBank::root();
    if (const auto* row = chosenRow(); row != nullptr && row->writable && row->folder.isDirectory())
        return row->folder;
    const auto current = processor.presetFile();
    if (current.isAChildOf(bank) && current.getParentDirectory().isDirectory()) return current.getParentDirectory();
    return bank;
}

juce::String PresetBrowser::folderCaption(const juce::File& folder) const
{
    const auto bank = presetBank::root();
    if (folder == bank) return "BANK";
    if (folder.isAChildOf(bank)) return "BANK / " + folder.getRelativePathFrom(bank).replaceCharacter('\\', '/');
    for (const auto& library : presetBank::libraries())
        if (folder == library.root || folder.isAChildOf(library.root))
            return library.name + (folder == library.root ? juce::String()
                                   : " / " + folder.getRelativePathFrom(library.root).replaceCharacter('\\', '/'));
    // Somewhere else on disk: the folder's own name says enough, and a whole
    // path does not fit a card.
    return folder.getFileName();
}

// --- Saving -------------------------------------------------------------------

void PresetBrowser::showSaveForm(bool show)
{
    saving = show;
    confirmReplace = {};
    saveButton.setButtonText("SAVE");
    if (show)
    {
        const auto current = processor.presetInfo();
        nameField.setText(current.name == "INIT" ? juce::String() : current.name, false);
        authorField.setText(current.author, false);
        notesField.setText(current.notes, false);
        chosenStyle = current.style;
        styleField.setButtonText(chosenStyle.isEmpty() ? "NO STYLE" : styleCaption(chosenStyle));
    }
    for (auto* component : std::initializer_list<juce::Component*> {&nameField, &authorField, &notesField,
                                                                     &styleField, &saveButton, &cancelButton})
        component->setVisible(show);
    info->setVisible(!show);
    resized();
    repaint();
    if (show)
    {
        nameField.grabKeyboardFocus();
        nameField.selectAll();
    }
}

void PresetBrowser::chooseStyleForSave()
{
    juce::PopupMenu menu;
    menu.addItem(1, "No style", true, chosenStyle.isEmpty());
    for (int i = 0; i < presetStyles().size(); ++i)
        menu.addItem(i + 2, presetStyles()[i], true, chosenStyle == presetStyles()[i]);
    const auto safe = juce::Component::SafePointer<PresetBrowser>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&styleField),
        [safe] (int chosen)
        {
            if (safe == nullptr || chosen <= 0) return;
            safe->chosenStyle = chosen == 1 ? juce::String() : presetStyles()[chosen - 2];
            safe->styleField.setButtonText(safe->chosenStyle.isEmpty() ? "NO STYLE" : styleCaption(safe->chosenStyle));
        });
}

void PresetBrowser::commitSave()
{
    const auto name = presetBank::legalName(nameField.getText());
    if (name.isEmpty())
    {
        if (announce) announce("A PRESET NEEDS A NAME", true);
        nameField.grabKeyboardFocus();
        return;
    }
    const auto file = saveFolder().getChildFile(name + ".forgepreset");
    // One more press to write over a preset of the same name: the hand that
    // meant a new one finds out before anything is lost.
    if (file.existsAsFile() && confirmReplace != file)
    {
        confirmReplace = file;
        saveButton.setButtonText("REPLACE");
        if (announce) announce(name.toUpperCase() + " IS ALREADY THERE - SAVE AGAIN TO REPLACE IT", true);
        return;
    }
    auto saved = processor.presetInfo();
    saved.name = name;
    saved.author = authorField.getText().trim();
    saved.style = chosenStyle;
    saved.notes = notesField.getText().trim();
    const auto result = processor.savePreset(file, saved);
    if (result.failed()) { reportFailure(result); return; }
    highlighted = file;
    showSaveForm(false);
    scanned.clear();
    rebuildFolders();
    relist();
    revealHighlighted();
    info->refresh();
    if (announce) announce("SAVED " + name.toUpperCase() + " TO " + folderCaption(file.getParentDirectory()).toUpperCase(), false);
}

// --- Opening from anywhere ----------------------------------------------------

void PresetBrowser::chooseFileToOpen()
{
    chooser = std::make_unique<juce::FileChooser>("Open a preset", presetBank::root(), presetFilePatterns(), true);
    const auto safe = juce::Component::SafePointer<PresetBrowser>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe] (const juce::FileChooser& picked)
        {
            if (safe == nullptr) return;
            const auto file = picked.getResult();
            if (file == juce::File {}) return;
            safe->loadPreset(file);
        });
}

// --- Renaming, moving, deleting -----------------------------------------------

void PresetBrowser::beginRename(const juce::File& item, juce::Component& over, juce::Rectangle<int> where)
{
    cancelRename();
    renaming = item;
    renameField.setBounds(getLocalArea(&over, where).reduced(1));
    renameField.setText(item.isDirectory() ? item.getFileName() : item.getFileNameWithoutExtension(), false);
    renameField.setVisible(true);
    renameField.toFront(true);
    renameField.grabKeyboardFocus();
    renameField.selectAll();
}

void PresetBrowser::commitRename()
{
    if (renaming == juce::File {}) return;
    const auto from = renaming;
    renaming = {};
    renameField.setVisible(false);
    juce::File renamed;
    const auto result = presetBank::rename(from, renameField.getText(), renamed);
    if (result.failed()) { reportFailure(result); return; }
    rememberMove(from, renamed);
}

void PresetBrowser::cancelRename()
{
    renaming = {};
    renameField.setVisible(false);
}

void PresetBrowser::rememberMove(const juce::File& from, const juce::File& to)
{
    if (from == to) return;
    // Favourites are kept by path, so a moved file or folder takes its
    // favourites with it.
    for (auto& path : favourites)
    {
        const juce::File file(path);
        if (file == from) path = to.getFullPathName();
        else if (file.isAChildOf(from)) path = to.getChildFile(file.getRelativePathFrom(from)).getFullPathName();
    }
    Processor::saveFavouritePresets(favourites);
    processor.presetFileMoved(from, to);
    if (highlighted == from) highlighted = to;
    else if (highlighted.isAChildOf(from)) highlighted = to.getChildFile(highlighted.getRelativePathFrom(from));
    if (from.isDirectory() || to.isDirectory())
        if (collapsed.erase(from.getFullPathName()) > 0) collapsed.insert(to.getFullPathName());
    scanned.clear();
    rebuildFolders();
    relist();
    revealHighlighted();
    info->refresh();
    repaint(topBar);
}

void PresetBrowser::movePreset(const juce::File& preset, const juce::File& folder)
{
    const auto bank = presetBank::root();
    const auto ownPreset = presetFormatOf(preset) == PresetFormat::forge && preset.isAChildOf(bank);
    if (ownPreset)
    {
        juce::File moved;
        const auto result = presetBank::moveInto(preset, folder, moved);
        if (result.failed()) { reportFailure(result); return; }
        rememberMove(preset, moved);
        if (announce) announce("MOVED TO " + folderCaption(folder).toUpperCase(), false);
        return;
    }
    // Anything else is copied in, a foreign preset converted on the way: the
    // bank never takes a file out of someone else's folder.
    const auto target = folder.getNonexistentChildFile(preset.getFileNameWithoutExtension(), ".forgepreset", true);
    const auto result = presetFormatOf(preset) == PresetFormat::forge
        ? (preset.copyFileTo(target) ? juce::Result::ok() : juce::Result::fail("Forge could not copy that preset."))
        : processor.convertPreset(preset, target);
    if (result.failed()) { reportFailure(result); return; }
    scanned.clear();
    rebuildFolders();
    relist();
    if (announce) announce("COPIED " + target.getFileNameWithoutExtension().toUpperCase() + " INTO "
                           + folderCaption(folder).toUpperCase(), false);
}

void PresetBrowser::createFolderIn(const juce::File& parent)
{
    juce::File made;
    const auto result = presetBank::createFolder(parent, parent.getNonexistentChildFile("New Folder", {}, true)
                                                             .getFileName(), made);
    if (result.failed()) { reportFailure(result); return; }
    collapsed.erase(parent.getFullPathName());
    rebuildFolders();
    for (int i = 0; i < static_cast<int>(folderRows.size()); ++i)
        if (folderRows[static_cast<size_t>(i)].folder == made && folderRows[static_cast<size_t>(i)].kind == FolderRow::Kind::folder)
        {
            chooseFolder(i);
            folders->reveal(i);
            beginRename(made, *folders, folders->rowBounds(i));
            return;
        }
}

void PresetBrowser::deleteItem(const juce::File& item)
{
    const auto folder = item.isDirectory();
    const auto name = folder ? item.getFileName() : item.getFileNameWithoutExtension();
    const auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
        juce::MessageBoxIconType::QuestionIcon, folder ? "Delete folder" : "Delete preset",
        folder ? "Send the folder \"" + name + "\" and every preset in it to the recycle bin?"
               : "Send \"" + name + "\" to the recycle bin?",
        "Delete", "Keep", this);
    const auto safe = juce::Component::SafePointer<PresetBrowser>(this);
    juce::AlertWindow::showAsync(options, [safe, item] (int chosen)
    {
        if (safe == nullptr || chosen != 1) return;
        const auto result = presetBank::remove(item);
        if (result.failed()) { safe->reportFailure(result); return; }
        for (int i = safe->favourites.size(); --i >= 0;)
        {
            const juce::File file(safe->favourites[i]);
            if (file == item || file.isAChildOf(item)) safe->favourites.remove(i);
        }
        Processor::saveFavouritePresets(safe->favourites);
        safe->scanned.clear();
        safe->rebuildFolders();
        safe->relist();
        if (safe->announce) safe->announce("SENT " + item.getFileName().toUpperCase() + " TO THE RECYCLE BIN", false);
    });
}

void PresetBrowser::reportFailure(const juce::Result& result)
{
    if (announce) announce("ERROR // " + result.getErrorMessage(), true);
}

// --- Menus --------------------------------------------------------------------

void PresetBrowser::showPresetMenu(const juce::File& file)
{
    const auto bank = presetBank::root();
    const auto inBank = presetFormatOf(file) == PresetFormat::forge && file.isAChildOf(bank);
    juce::PopupMenu menu, into;
    std::vector<juce::File> targets {bank};
    into.addItem(1000, "BANK", true, inBank && file.getParentDirectory() == bank);
    for (const auto& folder : presetBank::foldersUnder(bank))
    {
        targets.push_back(folder);
        into.addItem(1000 + static_cast<int>(targets.size()) - 1,
                     juce::String::repeatedString("    ", depthBelow(folder, bank) - 1) + folder.getFileName(),
                     true, inBank && file.getParentDirectory() == folder);
    }
    menu.addItem(1, "Load");
    menu.addItem(2, isFavourite(file) ? "Remove from favourites" : "Add to favourites");
    if (inBank)
    {
        menu.addItem(3, "Rename");
        menu.addSubMenu("Move to", into);
        menu.addItem(4, "Delete");
    }
    else
        menu.addSubMenu(presetFormatOf(file) == PresetFormat::forge ? "Copy into the bank"
                                                                     : "Convert into the bank", into);
    menu.addSeparator();
    menu.addItem(5, "Show in Explorer");

    const auto safe = juce::Component::SafePointer<PresetBrowser>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, file, targets] (int chosen)
    {
        if (safe == nullptr || chosen <= 0) return;
        if (chosen >= 1000)
        {
            const auto index = static_cast<size_t>(chosen - 1000);
            if (index < targets.size()) safe->movePreset(file, targets[index]);
            return;
        }
        switch (chosen)
        {
            case 1: safe->loadPreset(file); break;
            case 2: safe->toggleFavourite(file); break;
            case 3:
                for (int i = 0; i < static_cast<int>(safe->shown.size()); ++i)
                    if (safe->listed[static_cast<size_t>(safe->shown[static_cast<size_t>(i)])].file == file)
                    {
                        safe->table->reveal(i);
                        safe->beginRename(file, *safe->table,
                                          safe->table->columnBounds(Column::name, safe->table->rowBounds(i)));
                    }
                break;
            case 4: safe->deleteItem(file); break;
            case 5: file.revealToUser(); break;
            default: break;
        }
    });
}

void PresetBrowser::showFolderMenu(int row)
{
    if (row < 0 || row >= static_cast<int>(folderRows.size())) return;
    const auto target = folderRows[static_cast<size_t>(row)];
    juce::PopupMenu menu;
    const auto bankFolder = target.writable && target.kind != FolderRow::Kind::favourites;
    if (bankFolder) menu.addItem(1, "New folder");
    if (bankFolder && target.kind == FolderRow::Kind::folder)
    {
        menu.addItem(2, "Rename");
        menu.addItem(3, "Delete");
    }
    if (target.kind == FolderRow::Kind::all)
        menu.addItem(4, "Restore factory presets", presetBank::factorySource().isDirectory());
    if (target.folder.isDirectory())
    {
        menu.addSeparator();
        menu.addItem(5, "Show in Explorer");
    }
    if (menu.getNumItems() == 0) return;

    const auto safe = juce::Component::SafePointer<PresetBrowser>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, target, row] (int chosen)
    {
        if (safe == nullptr) return;
        switch (chosen)
        {
            case 1: safe->createFolderIn(target.folder); break;
            case 2:
                safe->folders->reveal(row);
                safe->beginRename(target.folder, *safe->folders, safe->folders->rowBounds(row));
                break;
            case 3: safe->deleteItem(target.folder); break;
            case 4:
            {
                const auto restored = presetBank::restoreFactory();
                safe->scanned.clear();
                safe->rebuildFolders();
                safe->relist();
                if (safe->announce)
                    safe->announce(restored > 0 ? "RESTORED " + juce::String(restored) + " FACTORY PRESETS"
                                                : juce::String("EVERY FACTORY PRESET IS ALREADY THERE"), false);
                break;
            }
            case 5: target.folder.revealToUser(); break;
            default: break;
        }
    });
}

// --- Dragging a preset onto a folder ------------------------------------------

void PresetBrowser::dragTo(juce::Point<int> at)
{
    const auto local = folders->getLocalPoint(this, at);
    auto row = folders->getLocalBounds().contains(local) ? folders->rowAt(local) : -1;
    // Only the bank takes a drop; a library row or the favourites is not a
    // place a file can go.
    if (row >= 0)
    {
        const auto& target = folderRows[static_cast<size_t>(row)];
        if (!target.writable || target.kind == FolderRow::Kind::favourites) row = -1;
    }
    if (row != dropRow)
    {
        dropRow = row;
        folders->repaint();
    }
}

void PresetBrowser::dropAt(juce::Point<int> at)
{
    dragTo(at);
    const auto row = dropRow;
    const auto preset = dragging;
    dropRow = -1;
    dragging = {};
    folders->repaint();
    if (row >= 0 && preset != juce::File {}) movePreset(preset, folderRows[static_cast<size_t>(row)].folder);
}

// --- Keys and text ------------------------------------------------------------

bool PresetBrowser::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (renaming != juce::File {}) cancelRename();
        else if (saving) showSaveForm(false);
        else if (onClose) onClose();
        return true;
    }
    if (saving) return false;
    if (key == juce::KeyPress::upKey) { step(-1); return true; }
    if (key == juce::KeyPress::downKey) { step(1); return true; }
    if (key == juce::KeyPress::returnKey) { if (onClose) onClose(); return true; }
    if (key == juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0))
    {
        search.grabKeyboardFocus();
        return true;
    }
    return false;
}

void PresetBrowser::textEditorTextChanged(juce::TextEditor& editor)
{
    if (&editor == &search) refilter();
    if (&editor == &nameField && confirmReplace != juce::File {})
    {
        confirmReplace = {};
        saveButton.setButtonText("SAVE");
    }
}

void PresetBrowser::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
    if (&editor == &renameField) commitRename();
    else if (&editor == &nameField || &editor == &authorField) commitSave();
    else if (&editor == &search) table->grabKeyboardFocus();
}

void PresetBrowser::textEditorEscapeKeyPressed(juce::TextEditor& editor)
{
    if (&editor == &renameField) cancelRename();
    else if (&editor == &search && search.getText().isNotEmpty()) search.clear();
    else if (saving) showSaveForm(false);
    else if (onClose) onClose();
}

void PresetBrowser::textEditorFocusLost(juce::TextEditor& editor)
{
    if (&editor == &renameField && renaming != juce::File {}) commitRename();
}

// --- Layout -------------------------------------------------------------------

void PresetBrowser::resized()
{
    using namespace browserLook;
    auto area = getLocalBounds().reduced(12, 10);
    topBar = area.removeFromTop(topBarHeight);
    area.removeFromTop(gap);

    {
        auto bar = topBar.reduced(8, 5);
        closeButton.setBounds(bar.removeFromRight(70));
        bar.removeFromRight(6);
        openButton.setBounds(bar.removeFromRight(96));
        bar.removeFromRight(6);
        initButton.setBounds(bar.removeFromRight(96));
        // The arrows either side of the name, which is painted between them,
        // centred on the bar.
        const auto middle = topBar.withSizeKeepingCentre(juce::jmin(440, topBar.getWidth() - 520), bar.getHeight());
        previousButton.setBounds(middle.withWidth(30));
        nextButton.setBounds(middle.withTrimmedLeft(middle.getWidth() - 30));
    }

    leftColumn = area.removeFromLeft(juce::jlimit(250, 340, area.getWidth() * 26 / 100));
    area.removeFromLeft(gap);
    table->setBounds(area);

    auto left = leftColumn;
    infoArea = left.removeFromTop(saving ? juce::jlimit(250, 330, left.getHeight() * 56 / 100)
                                         : juce::jlimit(160, 280, left.getHeight() * 38 / 100));
    info->setBounds(infoArea);
    if (saving)
    {
        auto form = infoArea.reduced(10, 8);
        form.removeFromTop(20);
        nameField.setBounds(form.removeFromTop(25));
        form.removeFromTop(5);
        authorField.setBounds(form.removeFromTop(25));
        form.removeFromTop(5);
        styleField.setBounds(form.removeFromTop(25));
        auto buttons = form.removeFromBottom(26);
        form.removeFromBottom(22);
        form.removeFromTop(5);
        notesField.setBounds(form);
        cancelButton.setBounds(buttons.removeFromRight(80));
        buttons.removeFromRight(6);
        saveButton.setBounds(buttons.removeFromRight(80));
    }
    left.removeFromTop(gap);
    search.setBounds(left.removeFromTop(26));
    left.removeFromTop(6);
    const auto chipGap = 4;
    const auto chipWidth = (left.getWidth() - chipGap * 2) / 3;
    for (size_t i = 0; i < styleChips.size(); ++i)
    {
        const auto column = static_cast<int>(i % 3), line = static_cast<int>(i / 3);
        styleChips[i]->setBounds(left.getX() + column * (chipWidth + chipGap), left.getY() + line * (22 + chipGap),
                                 chipWidth, 22);
    }
    left.removeFromTop(3 * 22 + 2 * chipGap);
    left.removeFromTop(gap);
    foldersTitle = left.removeFromTop(18);
    folders->setBounds(left);
    folders->clampScroll();
    table->clampScroll();
}
}
