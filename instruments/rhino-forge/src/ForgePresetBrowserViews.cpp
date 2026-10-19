// What the browser draws, and how a hand moves through the two lists. What it
// lists and does is in ForgePresetBrowser.cpp.
#include "ForgePresetBrowserInternal.h"

namespace rhino::forge
{
using namespace browserLook;

// --- The browser's own face ---------------------------------------------------

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(ui::chassisWellFloor);
    ui::drawPlate(g, getLocalBounds().toFloat().reduced(2.0f), 1.0f, 9.0f);

    ui::drawWell(g, topBar.toFloat(), ui::wellFloor, 1.0f, 5.0f);
    auto bar = topBar.reduced(14, 0);
    g.setColour(ui::text);
    g.setFont(ui::panelFont(ui::Face::header, 14.5f));
    g.drawText("PRESETS", bar.removeFromLeft(120), juce::Justification::centredLeft);

    // The patch that is loaded, between the arrows that step through the list.
    const auto middle = previousButton.getBounds().getUnion(nextButton.getBounds()).reduced(34, 0);
    const auto current = processor.presetInfo();
    g.setFont(ui::panelFont(ui::Face::emphasis, 15.0f));
    g.setColour(ui::text);
    g.drawFittedText(current.name.toUpperCase(), middle, juce::Justification::centred, 1, 0.8f);

    g.setColour(ui::mutedText);
    g.setFont(ui::panelFont(ui::Face::label, 11.0f));
    g.drawText("FOLDERS", foldersTitle.withTrimmedLeft(2), juce::Justification::centredLeft);
    if (saving)
        g.drawText("CHOOSE A BANK FOLDER TO SAVE INTO", foldersTitle, juce::Justification::centredRight);

    if (saving)
    {
        ui::drawWell(g, infoArea.toFloat(), ui::wellFloor, 1.0f, 5.0f);
        auto form = infoArea.reduced(10, 8);
        g.setColour(ui::text);
        g.setFont(ui::panelFont(ui::Face::header, 13.0f));
        g.drawText("SAVE PRESET", form.removeFromTop(18), juce::Justification::centredLeft);
        const auto to = notesField.getBounds().withY(notesField.getBottom() + 4).withHeight(16);
        g.setColour(ui::mutedText);
        g.setFont(ui::panelFont(ui::Face::label, 11.0f));
        g.drawFittedText("INTO  " + folderCaption(saveFolder()).toUpperCase(), to,
                         juce::Justification::centredLeft, 1, 0.8f);
    }
}

// --- The info card ------------------------------------------------------------

PresetBrowser::InfoCard::InfoCard(PresetBrowser& owner) : browser(owner)
{
    dressField(notes, {}, true);
    notes.setReadOnly(true);
    notes.setCaretVisible(false);
    notes.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    notes.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    notes.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(notes);
}

void PresetBrowser::InfoCard::refresh()
{
    shownInfo = browser.processor.presetInfo();
    notes.setReadOnly(false);
    notes.clear();
    notes.setColour(juce::TextEditor::textColourId, ui::labelText);
    notes.insertTextAtCaret(shownInfo.notes);
    if (!shownInfo.warnings.isEmpty())
    {
        // What a conversion lost, in the colour every warning on the panel is
        // in, under the preset's own notes.
        notes.setColour(juce::TextEditor::textColourId, ui::signalViolet);
        auto lost = juce::String(shownInfo.notes.isEmpty() ? "" : "\n\n")
                  + "NOT CONVERTED FROM " + shownInfo.source.toUpperCase() + ":\n";
        for (const auto& warning : shownInfo.warnings) lost << "- " << warning << "\n";
        notes.insertTextAtCaret(lost.trimEnd());
    }
    notes.setReadOnly(true);
    notes.moveCaretToTop(false);
    repaint();
}

void PresetBrowser::InfoCard::resized()
{
    notes.setBounds(getLocalBounds().reduced(4, 6).withTrimmedTop(64));
}

void PresetBrowser::InfoCard::paint(juce::Graphics& g)
{
    ui::drawWell(g, getLocalBounds().toFloat(), ui::wellFloor, 1.0f, 5.0f);
    auto area = getLocalBounds().reduced(10, 8);

    auto title = area.removeFromTop(22);
    const auto file = browser.processor.presetFile();
    if (file != juce::File {})
    {
        const auto mark = title.removeFromRight(18).toFloat().reduced(1.0f);
        const auto favourite = browser.isFavourite(file);
        g.setColour(favourite ? ui::electricBlue : ui::mutedText.withAlpha(0.6f));
        if (favourite) g.fillPath(star(mark));
        else g.strokePath(star(mark), juce::PathStrokeType(1.0f));
    }
    g.setColour(ui::text);
    g.setFont(ui::panelFont(ui::Face::header, 15.5f));
    g.drawFittedText(shownInfo.name.toUpperCase(), title, juce::Justification::centredLeft, 1, 0.75f);

    juce::StringArray byline;
    if (shownInfo.author.isNotEmpty()) byline.add("BY " + shownInfo.author.toUpperCase());
    if (shownInfo.style.isNotEmpty()) byline.add(shownInfo.style.toUpperCase());
    g.setColour(ui::labelText);
    g.setFont(ui::panelFont(ui::Face::label, 12.0f));
    g.drawFittedText(byline.isEmpty() ? juce::String("NO AUTHOR") : byline.joinIntoString("  /  "),
                     area.removeFromTop(16), juce::Justification::centredLeft, 1, 0.8f);

    // Where it came from: a folder of the bank, a library, or a conversion.
    juce::String origin;
    if (shownInfo.source.isNotEmpty()) origin = "CONVERTED FROM " + shownInfo.source.toUpperCase();
    else if (file != juce::File {}) origin = browser.folderCaption(file.getParentDirectory()).toUpperCase();
    else origin = "NOT FILED";
    if (!shownInfo.warnings.isEmpty())
        origin << "  /  " << juce::String(shownInfo.warnings.size())
               << (shownInfo.warnings.size() == 1 ? " WARNING" : " WARNINGS");
    g.setColour(shownInfo.warnings.isEmpty() ? ui::mutedText : ui::signalViolet);
    g.setFont(ui::panelFont(ui::Face::label, 11.0f));
    g.drawFittedText(origin, area.removeFromTop(16), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour(ui::line.withAlpha(0.7f));
    g.drawHorizontalLine(area.getY() + 3, static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
}

// --- The folder list ----------------------------------------------------------

void PresetBrowser::FolderList::rebuildDrawn()
{
    drawn.clear();
    // A row is folded away when any row above it that is its ancestor is
    // collapsed. The rows come depth first, so the folded ancestors so far are
    // a stack.
    std::vector<juce::File> folded;
    for (int i = 0; i < static_cast<int>(browser.folderRows.size()); ++i)
    {
        const auto& row = browser.folderRows[static_cast<size_t>(i)];
        if (row.kind != FolderRow::Kind::folder) folded.clear();
        while (!folded.empty() && !row.folder.isAChildOf(folded.back())) folded.pop_back();
        if (row.kind == FolderRow::Kind::folder && !folded.empty()) continue;
        drawn.push_back(i);
        if (row.hasChildren && browser.collapsed.count(row.folder.getFullPathName()) > 0)
            folded.push_back(row.folder);
    }
}

int PresetBrowser::FolderList::visibleRows() const
{
    return juce::jmax(1, (getHeight() - 8) / rowHeight);
}

void PresetBrowser::FolderList::clampScroll()
{
    first = juce::jlimit(0, juce::jmax(0, static_cast<int>(drawn.size()) - visibleRows()), first);
}

void PresetBrowser::FolderList::reveal(int row)
{
    const auto at = std::find(drawn.begin(), drawn.end(), row);
    if (at == drawn.end()) return;
    const auto index = static_cast<int>(at - drawn.begin());
    if (index < first) first = index;
    else if (index >= first + visibleRows()) first = index - visibleRows() + 1;
    clampScroll();
    repaint();
}

juce::Rectangle<int> PresetBrowser::FolderList::rowBounds(int row) const
{
    const auto at = std::find(drawn.begin(), drawn.end(), row);
    if (at == drawn.end()) return {};
    const auto index = static_cast<int>(at - drawn.begin()) - first;
    return {4, 4 + index * rowHeight, getWidth() - 8 - scrollRail - 2, rowHeight};
}

int PresetBrowser::FolderList::rowAt(juce::Point<int> at) const
{
    if (at.y < 4) return -1;
    const auto index = first + (at.y - 4) / rowHeight;
    return index >= 0 && index < static_cast<int>(drawn.size()) ? drawn[static_cast<size_t>(index)] : -1;
}

void PresetBrowser::FolderList::paint(juce::Graphics& g)
{
    ui::drawWell(g, getLocalBounds().toFloat(), ui::wellFloor, 1.0f, 5.0f);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(getLocalBounds().reduced(2));
    const auto shownRows = juce::jmin(visibleRows() + 1, static_cast<int>(drawn.size()) - first);
    for (int i = 0; i < shownRows; ++i)
    {
        const auto rowIndex = drawn[static_cast<size_t>(first + i)];
        const auto& row = browser.folderRows[static_cast<size_t>(rowIndex)];
        auto box = rowBounds(rowIndex);
        const auto chosen = rowIndex == browser.chosenFolder;
        const auto dropping = rowIndex == browser.dropRow;
        const auto heading = row.kind != FolderRow::Kind::folder;

        // A library starts a section of its own, set off by a rule.
        if (row.kind == FolderRow::Kind::library && i > 0)
        {
            g.setColour(ui::line.withAlpha(0.6f));
            g.drawHorizontalLine(box.getY(), static_cast<float>(box.getX() + 4), static_cast<float>(box.getRight() - 4));
        }
        if (chosen || dropping || rowIndex == hover)
        {
            const auto fill = box.toFloat().reduced(0.5f, 1.5f);
            g.setColour(chosen ? ui::electricBlue.withAlpha(0.20f) : ui::line.withAlpha(0.25f));
            g.fillRoundedRectangle(fill, 3.0f);
            if (chosen || dropping)
            {
                g.setColour((dropping ? ui::signalViolet : ui::electricBlue).withAlpha(dropping ? 1.0f : 0.7f));
                g.drawRoundedRectangle(fill, 3.0f, dropping ? 1.5f : 1.0f);
            }
        }

        box.removeFromLeft(6 + row.depth * indent);
        auto arrow = box.removeFromLeft(12).toFloat();
        if (row.hasChildren)
        {
            const auto open = browser.collapsed.count(row.folder.getFullPathName()) == 0;
            juce::Path triangle;
            const auto c = arrow.getCentre();
            if (open) triangle.addTriangle(c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
            else triangle.addTriangle(c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
            g.setColour(ui::mutedText);
            g.fillPath(triangle);
        }
        if (row.kind == FolderRow::Kind::favourites)
        {
            g.setColour(ui::electricBlue.withAlpha(0.85f));
            g.fillPath(star(box.removeFromLeft(14).toFloat().withSizeKeepingCentre(11.0f, 11.0f)));
            box.removeFromLeft(5);
        }
        g.setColour(chosen ? ui::text : (heading ? ui::labelText : ui::mutedText.brighter(0.2f)));
        g.setFont(heading ? ui::panelFont(ui::Face::header, 12.0f) : ui::panelFont(ui::Face::label, 12.5f));
        g.drawFittedText(row.caption, box.withTrimmedRight(4), juce::Justification::centredLeft, 1, 0.8f);
    }
    drawScrollRail(g, getLocalBounds().reduced(4).removeFromRight(scrollRail - 2), first, visibleRows(),
                   static_cast<int>(drawn.size()));
}

void PresetBrowser::FolderList::mouseDown(const juce::MouseEvent& event)
{
    const auto row = rowAt(event.getPosition());
    if (row < 0) return;
    if (event.mods.isPopupMenu())
    {
        browser.chooseFolder(row);
        browser.showFolderMenu(row);
        return;
    }
    const auto& target = browser.folderRows[static_cast<size_t>(row)];
    const auto box = rowBounds(row);
    const auto arrowRight = box.getX() + 6 + target.depth * indent + 12;
    if (target.hasChildren && event.x < arrowRight + 2)
    {
        browser.toggleCollapsed(row);
        return;
    }
    browser.chooseFolder(row);
    if (event.getNumberOfClicks() == 2 && target.hasChildren) browser.toggleCollapsed(row);
}

void PresetBrowser::FolderList::mouseMove(const juce::MouseEvent& event)
{
    const auto row = rowAt(event.getPosition());
    if (row != hover) { hover = row; repaint(); }
}

void PresetBrowser::FolderList::mouseExit(const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void PresetBrowser::FolderList::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheelDetails)
{
    wheel -= wheelDetails.deltaY * 6.0f;
    const auto rows = static_cast<int>(wheel);
    if (rows == 0) return;
    wheel -= static_cast<float>(rows);
    first += rows;
    clampScroll();
    repaint();
}

// --- The table ----------------------------------------------------------------

int PresetBrowser::PresetTable::visibleRows() const
{
    return juce::jmax(1, (getHeight() - headerHeight - 8) / rowHeight);
}

void PresetBrowser::PresetTable::clampScroll()
{
    first = juce::jlimit(0, juce::jmax(0, static_cast<int>(browser.shown.size()) - visibleRows()), first);
}

void PresetBrowser::PresetTable::reveal(int index)
{
    if (index < first) first = index;
    else if (index >= first + visibleRows()) first = index - visibleRows() + 1;
    clampScroll();
    repaint();
}

juce::Rectangle<int> PresetBrowser::PresetTable::rowBounds(int index) const
{
    return {4, 4 + headerHeight + (index - first) * rowHeight, getWidth() - 8 - scrollRail - 4, rowHeight};
}

int PresetBrowser::PresetTable::rowAt(juce::Point<int> at) const
{
    if (at.y < 4 + headerHeight) return -1;
    const auto index = first + (at.y - 4 - headerHeight) / rowHeight;
    return index >= 0 && index < static_cast<int>(browser.shown.size()) && index < first + visibleRows() + 1
        ? index : -1;
}

// The columns, as shares of the row: a star, the name taking most of it, and
// three readings beside it. Shared by the header and every row, so a title
// cannot sit over a column it does not name.
juce::Rectangle<int> PresetBrowser::PresetTable::columnBounds(Column column, juce::Rectangle<int> row) const
{
    auto rest = row;
    const auto starCell = rest.removeFromLeft(30);
    const auto width = rest.getWidth();
    const auto dateCell = rest.removeFromRight(juce::jmax(90, width * 15 / 100));
    const auto authorCell = rest.removeFromRight(width * 22 / 100);
    const auto styleCell = rest.removeFromRight(width * 14 / 100);
    switch (column)
    {
        case Column::favourite: return starCell;
        case Column::style:     return styleCell.withTrimmedLeft(6);
        case Column::author:    return authorCell.withTrimmedLeft(6);
        case Column::date:      return dateCell.withTrimmedRight(8);
        case Column::name:      break;
    }
    return rest.withTrimmedRight(6);
}

void PresetBrowser::PresetTable::paint(juce::Graphics& g)
{
    ui::drawWell(g, getLocalBounds().toFloat(), ui::wellFloor, 1.0f, 5.0f);

    // The titles, the one being sorted by lit and carrying its direction.
    const auto header = juce::Rectangle<int>(4, 4, getWidth() - 8 - scrollRail - 4, headerHeight);
    const std::array<std::pair<Column, const char*>, 5> titles {{
        {Column::favourite, ""}, {Column::name, "NAME"}, {Column::style, "STYLE"},
        {Column::author, "AUTHOR"}, {Column::date, "DATE"}}};
    for (const auto& [column, title] : titles)
    {
        const auto cell = columnBounds(column, header);
        const auto sorted = browser.sortColumn == column;
        if (column == Column::favourite)
        {
            g.setColour(sorted ? ui::electricBlue : ui::mutedText.withAlpha(0.7f));
            g.fillPath(star(cell.toFloat().withSizeKeepingCentre(11.0f, 11.0f)));
            continue;
        }
        g.setColour(sorted ? ui::text : ui::mutedText);
        g.setFont(ui::panelFont(ui::Face::header, 11.5f));
        const auto justification = column == Column::date ? juce::Justification::centredRight
                                                          : juce::Justification::centredLeft;
        g.drawText(title, cell, justification);
        if (sorted)
        {
            const auto width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), title));
            const auto x = column == Column::date ? static_cast<float>(cell.getRight() - width - 12)
                                                  : static_cast<float>(cell.getX() + width + 6);
            const auto y = static_cast<float>(cell.getCentreY());
            juce::Path arrow;
            if (browser.sortAscending) arrow.addTriangle(x, y + 2.5f, x + 7.0f, y + 2.5f, x + 3.5f, y - 2.5f);
            else arrow.addTriangle(x, y - 2.5f, x + 7.0f, y - 2.5f, x + 3.5f, y + 2.5f);
            g.setColour(ui::electricBlue);
            g.fillPath(arrow);
        }
    }
    g.setColour(ui::line.withAlpha(0.7f));
    g.drawHorizontalLine(header.getBottom() - 1, static_cast<float>(header.getX()), static_cast<float>(header.getRight()));

    if (browser.shown.empty())
    {
        g.setColour(ui::mutedText);
        g.setFont(ui::panelFont(ui::Face::label, 13.0f));
        g.drawText(browser.listed.empty() ? "NO PRESETS IN THIS FOLDER" : "NOTHING HERE MATCHES",
                   getLocalBounds().withTrimmedTop(headerHeight), juce::Justification::centred);
        return;
    }

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(getLocalBounds().reduced(2).withTrimmedTop(headerHeight + 2));
    const auto rows = juce::jmin(visibleRows() + 1, static_cast<int>(browser.shown.size()) - first);
    for (int i = 0; i < rows; ++i)
    {
        const auto index = first + i;
        const auto& entry = browser.listed[static_cast<size_t>(browser.shown[static_cast<size_t>(index)])];
        const auto row = rowBounds(index);
        const auto loaded = entry.file == browser.highlighted;
        if (index % 2 == 1)
        {
            g.setColour(ui::chipFloor.withAlpha(0.55f));
            g.fillRect(row.reduced(0, 1));
        }
        if (loaded || index == hover)
        {
            const auto fill = row.toFloat().reduced(0.5f, 1.0f);
            g.setColour(loaded ? ui::electricBlue.withAlpha(0.22f) : ui::line.withAlpha(0.25f));
            g.fillRoundedRectangle(fill, 3.0f);
            if (loaded)
            {
                g.setColour(ui::electricBlue.withAlpha(0.75f));
                g.drawRoundedRectangle(fill, 3.0f, 1.0f);
            }
        }

        const auto mark = columnBounds(Column::favourite, row).toFloat().withSizeKeepingCentre(12.0f, 12.0f);
        if (browser.isFavourite(entry.file))
        {
            g.setColour(ui::electricBlue);
            g.fillPath(star(mark));
        }
        else
        {
            g.setColour(ui::mutedText.withAlpha(0.35f));
            g.strokePath(star(mark), juce::PathStrokeType(1.0f));
        }

        auto name = columnBounds(Column::name, row);
        // A preset of another synth wears a tag saying so: opening it means a
        // conversion, and what comes back may not be all of it.
        if (entry.format != PresetFormat::forge)
        {
            const auto tag = name.removeFromRight(52).reduced(2, 5).toFloat();
            g.setColour(ui::signalViolet.withAlpha(0.18f));
            g.fillRoundedRectangle(tag, 2.5f);
            g.setColour(ui::signalViolet);
            g.setFont(ui::panelFont(ui::Face::emphasis, 9.5f));
            g.drawText(entry.format == PresetFormat::serum ? "SERUM" : "VITAL", tag, juce::Justification::centred);
        }
        g.setColour(loaded ? ui::text : ui::labelText);
        g.setFont(ui::panelFont(ui::Face::label, 13.5f));
        g.drawFittedText(entry.name, name, juce::Justification::centredLeft, 1, 0.85f);

        g.setColour(ui::mutedText);
        g.setFont(ui::panelFont(ui::Face::label, 12.5f));
        g.drawFittedText(entry.style, columnBounds(Column::style, row), juce::Justification::centredLeft, 1, 0.8f);
        g.drawFittedText(entry.author, columnBounds(Column::author, row), juce::Justification::centredLeft, 1, 0.8f);
        g.setFont(ui::panelFont(ui::Face::reading, 10.5f));
        g.drawText(entry.modified.formatted("%d %b %Y"), columnBounds(Column::date, row),
                   juce::Justification::centredRight);
    }
    drawScrollRail(g, getLocalBounds().reduced(4).withTrimmedTop(headerHeight).removeFromRight(scrollRail - 2),
                   first, visibleRows(), static_cast<int>(browser.shown.size()));
}

void PresetBrowser::PresetTable::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    dragStarted = false;
    pressed = {};
    // A click on a title sorts by it.
    if (event.y < 4 + headerHeight)
    {
        const auto header = juce::Rectangle<int>(4, 4, getWidth() - 8 - scrollRail - 4, headerHeight);
        for (const auto column : {Column::favourite, Column::name, Column::style, Column::author, Column::date})
            if (columnBounds(column, header).contains(event.getPosition())) browser.sortBy(column);
        return;
    }
    const auto index = rowAt(event.getPosition());
    if (index < 0) return;
    const auto file = browser.listed[static_cast<size_t>(browser.shown[static_cast<size_t>(index)])].file;
    if (event.mods.isPopupMenu())
    {
        browser.showPresetMenu(file);
        return;
    }
    if (columnBounds(Column::favourite, rowBounds(index)).contains(event.getPosition()))
    {
        browser.toggleFavourite(file);
        return;
    }
    pressed = file;
    if (file != browser.highlighted) browser.loadPreset(file);
}

void PresetBrowser::PresetTable::mouseDrag(const juce::MouseEvent& event)
{
    if (pressed == juce::File {}) return;
    if (!dragStarted && event.getDistanceFromDragStart() < 6) return;
    if (!dragStarted)
    {
        dragStarted = true;
        browser.dragging = pressed;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    browser.dragTo(browser.getLocalPoint(this, event.getPosition()));
}

void PresetBrowser::PresetTable::mouseUp(const juce::MouseEvent& event)
{
    if (dragStarted)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        browser.dropAt(browser.getLocalPoint(this, event.getPosition()));
    }
    dragStarted = false;
    pressed = {};
}

void PresetBrowser::PresetTable::mouseDoubleClick(const juce::MouseEvent& event)
{
    // The first click loaded it; the second says this is the one.
    if (rowAt(event.getPosition()) >= 0 && !event.mods.isPopupMenu() && browser.onClose) browser.onClose();
}

void PresetBrowser::PresetTable::mouseMove(const juce::MouseEvent& event)
{
    const auto index = rowAt(event.getPosition());
    if (index != hover) { hover = index; repaint(); }
}

void PresetBrowser::PresetTable::mouseExit(const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void PresetBrowser::PresetTable::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheelDetails)
{
    wheel -= wheelDetails.deltaY * 8.0f;
    const auto rows = static_cast<int>(wheel);
    if (rows == 0) return;
    wheel -= static_cast<float>(rows);
    first += rows;
    clampScroll();
    repaint();
}
}
