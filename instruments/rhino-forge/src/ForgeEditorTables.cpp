// The table field on every oscillator and on the TABLE tab: the wavetables on
// disk, the menu of their folders they are chosen from, and NEW and SAVE.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
// A table field wears the component the warp and the filter's TYPE wear, and
// differs from them in one way: what it steps through is not a parameter. The
// table travels in a preset as frames, so the list is whatever is on disk now,
// and a table is tied back to its entry by its title (see tableEntryIndex).
// When the table playing is none of them -- a preset's own, one drawn over --
// the field names it anyway and its arrows step onto the list from the top.
//
// The list is read off the disk when the panel opens, when a menu opens and
// after NEW or SAVE, and not on the tick: a folder walk at 24Hz would be the
// panel's single largest cost for a list that changes when a person changes it.
void Editor::buildTableFields()
{
    for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
    {
        auto field = std::make_unique<ui::FxSelector>();
        field->forceList = true;
        field->wheelSteps = true;
        field->onChoose = [this, oscillator] (int entry)
        {
            const auto result = chooseTable(oscillator, entry);
            if (result.failed()) showTableStatus("COULD NOT LOAD // " + result.getErrorMessage(), true);
            tableChanged();
        };
        auto* held = field.get();
        field->onOpenList = [this, oscillator, held] { showTableMenu(oscillator, *held); };
        addChildComponent(*field);
        tableFields[static_cast<size_t>(oscillator)] = std::move(field);
    }
    // In each oscillator's own colour, as the controls declared on it are from
    // the moment they are built. applyPanelColours keeps it so.
    for (const auto& module : moduleUis)
    {
        const auto oscillator = oscillatorIndexFromId(juce::String(module.descriptor->id));
        if (module.descriptor->display == ui::Display::oscillator && oscillator >= 0)
            tableFields[static_cast<size_t>(oscillator)]->accent = accentOf(*module.descriptor);
    }

    if (tablePanel != nullptr)
    {
        auto& field = tablePanel->tableField;
        // Through the panel, so a table chosen here is a step its undo can take
        // back, exactly as LOAD is.
        field.onChoose = [this] (int entry)
        {
            tablePanel->remember();
            tablePanel->report(chooseTable(tablePanel->editingOscillator(), entry));
        };
        field.onOpenList = [this] { showTableMenu(tablePanel->editingOscillator(), tablePanel->tableField); };
        tablePanel->onNewTable = [this] (int oscillator) { newTable(oscillator); };
        tablePanel->onSaveTable = [this] (int oscillator) { saveTable(oscillator); };
    }
    rescanTables();
}

void Editor::rescanTables()
{
    tableEntries = listTables(tableRoot());
    const auto point = [this] (ui::FxSelector& field)
    {
        field.choices.clear();
        for (const auto& entry : tableEntries) field.choices.push_back(entry.name.toRawUTF8());
        // Forced to be filled again below, whatever it was showing: an entry's
        // index can have moved under it.
        field.chosen = -2;
    };
    for (auto& field : tableFields)
        if (field != nullptr) point(*field);
    if (tablePanel != nullptr) point(tablePanel->tableField);
    refreshTableFields();
}

// What each field is showing. Cheap enough for the tick: a title against the
// names already listed, and a repaint only for a field that has moved.
void Editor::refreshTableFields()
{
    const auto fill = [this] (ui::FxSelector& field, int oscillator)
    {
        const auto& edit = processor.tableStore().edit(oscillator);
        const auto entry = tableEntryIndex(tableEntries, edit.title(), edit.isUntouched());
        const auto caption = entry < 0 ? edit.title() : juce::String();
        if (field.chosen == entry && field.caption == caption) return;
        field.chosen = entry;
        field.caption = caption;
        field.setTooltip("Oscillator " + oscillatorLetter(oscillator) + "'s wavetable. Step through "
                         "the tables with the arrows, or click for the list: each folder in the "
                         "tables folder is a submenu");
        field.repaint();
    };
    for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
        if (auto& field = tableFields[static_cast<size_t>(oscillator)]) fill(*field, oscillator);
    if (tablePanel != nullptr) fill(tablePanel->tableField, tablePanel->editingOscillator());
}

// Seated in the strip each oscillator declares along the top of its display,
// the way the filter's TYPE is seated in its own.
void Editor::placeTableFields()
{
    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        const auto row = ui::tableStripRow(descriptor);
        const auto oscillator = oscillatorIndexFromId(juce::String(descriptor.id));
        if (row < 0 || oscillator < 0) continue;
        if (auto& field = tableFields[static_cast<size_t>(oscillator)])
            field->setBounds(ui::rowBounds(moduleAreaFor(descriptor), descriptor, row)
                                 .reduced(ui::seatedControlGap, 0));
    }
}

// A wavetable oscillator's alone, and greyed with the rest of its module when
// that is switched off.
void Editor::applyTableFieldStates()
{
    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        const auto oscillator = oscillatorIndexFromId(juce::String(descriptor.id));
        if (descriptor.display != ui::Display::oscillator || oscillator < 0) continue;
        if (auto& field = tableFields[static_cast<size_t>(oscillator)])
        {
            field->setVisible(moduleShown(descriptor) && !oscillatorIsSpectral(oscillator));
            field->setEnabled(module.on());
        }
    }
}

// The list, read off the disk again first so a table put in the folder since
// the panel opened is in it.
void Editor::showTableMenu(int oscillator, ui::FxSelector& field)
{
    rescanTables();
    const auto& edit = processor.tableStore().edit(oscillator);
    auto menu = ui::tableMenu(tableEntries, tableEntryIndex(tableEntries, edit.title(), edit.isUntouched()));
    // Through the field's own choice, so the TABLE tab's takes its undo step
    // whichever way it was chosen.
    const auto safe = juce::Component::SafePointer<ui::FxSelector>(&field);
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(&field), [safe] (int choice)
    {
        if (safe == nullptr || choice <= 0 || safe->onChoose == nullptr) return;
        safe->onChoose(choice - 1);
    });
}

juce::Result Editor::chooseTable(int oscillator, int entry)
{
    if (entry < 0 || entry >= static_cast<int>(tableEntries.size()))
        return juce::Result::fail("that table is no longer in the list.");
    const auto file = tableEntries[static_cast<size_t>(entry)].file;
    if (file == juce::File {})
    {
        processor.tableStore().resetToBuiltIn(oscillator);
        return juce::Result::ok();
    }
    return processor.importTable(oscillator, file);
}

// The timer notices a replaced table by its revision and does all of this on
// the next tick anyway. Doing it now is what makes the click land on the frame
// it was made on.
void Editor::tableChanged()
{
    applyTableCounts();
    if (tablePanel != nullptr) tablePanel->refresh();
    refreshTableFields();
    repaint();
}

// Beside the preset name, where a failed load already says so, rather than in a
// dialog that has to be dismissed.
void Editor::showTableStatus(const juce::String& text, bool failed)
{
    presetName.setText(text, juce::dontSendNotification);
    presetName.setColour(juce::Label::textColourId, failed ? ui::signalViolet : ui::mutedText);
}

// A new table is a file first, named before anything is drawn, so SAVE always
// has somewhere to write and the table is in the list from the moment it exists.
// It goes in the folder of tables made here (see ownTableFolder).
void Editor::newTable(int oscillator)
{
    const auto folder = ownTableFolder(tableRoot());
    folder.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>(
        "Name the new table for OSC " + oscillatorLetter(oscillator),
        folder.getNonexistentChildFile("new-table", ".wav", false), "*.wav", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe, oscillator] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        auto file = chooser.getResult();
        if (file == juce::File {}) return;
        file = file.withFileExtension("wav");
        const auto result = safe->processor.newTable(oscillator, file);
        safe->rescanTables();
        if (safe->tablePanel != nullptr) safe->tablePanel->report(result);
        safe->tableChanged();
    });
}

// Straight to the table's own file when it has one in the folder of tables
// made here; anything else -- a factory table, the built-in ten, a preset's own
// -- is saved as a new file there instead, so SAVE never writes over a table
// that ships.
void Editor::saveTable(int oscillator)
{
    rescanTables();
    const auto& edit = processor.tableStore().edit(oscillator);
    const auto folder = ownTableFolder(tableRoot());
    const auto target = tableSaveTarget(tableEntries, edit.title(), edit.isUntouched(), folder);
    if (target != juce::File {})
    {
        writeTable(oscillator, target);
        return;
    }

    folder.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save OSC " + oscillatorLetter(oscillator) + "'s table",
        folder.getChildFile(juce::File::createLegalFileName(edit.title().toLowerCase()) + ".wav"),
        "*.wav", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe, oscillator] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        const auto file = chooser.getResult();
        if (file == juce::File {}) return;
        safe->writeTable(oscillator, file.withFileExtension("wav"));
    });
}

void Editor::writeTable(int oscillator, const juce::File& file)
{
    const auto result = processor.saveTable(oscillator, file);
    rescanTables();
    showTableStatus(result.wasOk() ? "TABLE SAVED // " + tableNameOf(file)
                                   : "COULD NOT SAVE // " + result.getErrorMessage(),
                    result.failed());
    tableChanged();
}
}
