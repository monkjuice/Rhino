#include "ForgeEditorInternal.h"

namespace rhino::forge
{
namespace
{
juce::File lfoFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Rhino Forge").getChildFile("LFO Tables");
}

float pointValue(juce::Rectangle<int> plot, int y, int rows, bool snap)
{
    const auto raw = 1.0f - 2.0f * (y - plot.getY()) / static_cast<float>(plot.getHeight());
    return std::clamp(snap ? std::round(raw * rows / 2.0f) * 2.0f / rows : raw, -1.0f, 1.0f);
}
}

juce::Rectangle<int> Editor::lfoDisplayBounds() const
{
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::lfo && moduleShown(*module.descriptor))
            return ui::displayBounds(moduleAreaFor(*module.descriptor), *module.descriptor);
    return {};
}

LfoShape Editor::shownLfoShape() const
{
    return static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
        juce::roundToInt(value(lfoParameterId(shownLfo(), "Shape")))));
}

LfoTable Editor::editableLfoTable(int lfo) const
{
    auto table = processor.lfoTable(lfo);
    if (table.custom && table.valid()) return table;
    table.count = 9;
    table.custom = true;
    const auto shape = static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
        juce::roundToInt(value(lfoParameterId(lfo, "Shape")))));
    for (int i = 0; i < table.count; ++i)
    {
        const auto x = i / 8.0f;
        table.points[static_cast<size_t>(i)] = {x, lfoWave(shape, x, processor.lfoValue(lfo))};
    }
    return table;
}

int Editor::lfoPointAt(juce::Point<int> at) const
{
    const auto display = lfoDisplayBounds();
    const auto plot = ui::lfoPlotBounds(display);
    if (!plot.expanded(8).contains(at)) return -1;
    const auto table = editableLfoTable(shownLfo());
    for (int i = 0; i < table.count; ++i)
    {
        const auto& point = table.points[static_cast<size_t>(i)];
        const auto x = plot.getX() + point.x * plot.getWidth();
        const auto y = plot.getCentreY() - point.y * plot.getHeight() * 0.48f;
        if (std::hypot(at.x - x, at.y - y) <= 9.0f) return i;
    }
    return -1;
}

void Editor::editLfoPoint(int lfo, int point, juce::Point<int> at, bool snap)
{
    auto table = editableLfoTable(lfo);
    if (point < 0 || point >= table.count) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (plot.isEmpty()) return;
    const auto rawX = std::clamp((at.x - plot.getX()) / static_cast<float>(plot.getWidth()), 0.0f, 1.0f);
    auto x = snap ? std::round(rawX * table.columns) / table.columns : rawX;
    if (point == 0) x = 0.0f;
    else if (point == table.count - 1) x = 1.0f;
    else
    {
        const auto left = table.points[static_cast<size_t>(point - 1)].x;
        const auto right = table.points[static_cast<size_t>(point + 1)].x;
        const auto gap = std::min(0.001f, (right - left) / 3.0f);
        x = std::clamp(x, left + gap, right - gap);
    }
    table.points[static_cast<size_t>(point)] = {x, pointValue(plot, at.y, table.rows, snap)};
    processor.setLfoTable(lfo, table);
    repaint(lfoDisplayBounds());
}

void Editor::addLfoPoint(int lfo, juce::Point<int> at)
{
    auto table = editableLfoTable(lfo);
    if (table.count >= maxLfoPoints) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (!plot.contains(at)) return;
    const auto raw = std::clamp((at.x - plot.getX()) / static_cast<float>(plot.getWidth()), 0.0f, 1.0f);
    const auto x = raw;
    int insert = 1;
    while (insert < table.count && table.points[static_cast<size_t>(insert)].x < x) ++insert;
    if (x <= table.points[static_cast<size_t>(insert - 1)].x + 0.001f
        || x >= table.points[static_cast<size_t>(insert)].x - 0.001f) return;
    std::move_backward(table.points.begin() + insert, table.points.begin() + table.count,
                       table.points.begin() + table.count + 1);
    table.points[static_cast<size_t>(insert)] = {x, pointValue(plot, at.y, table.rows, false)};
    ++table.count;
    processor.setLfoTable(lfo, table);
    repaint(lfoDisplayBounds());
}

void Editor::removeLfoPoint(int lfo, int point)
{
    auto table = editableLfoTable(lfo);
    if (point <= 0 || point >= table.count - 1) return;
    std::move(table.points.begin() + point + 1, table.points.begin() + table.count,
              table.points.begin() + point);
    --table.count;
    processor.setLfoTable(lfo, table);
    repaint(lfoDisplayBounds());
}

void Editor::showLfoMenu()
{
    const auto bank = shownLfo();
    juce::PopupMenu menu, basics;
    for (int i = 0; i < lfoShapeCount; ++i)
        basics.addItem(i + 1, lfoShapeName(i), true,
                       !processor.lfoTable(bank).custom
                           && juce::roundToInt(value(lfoParameterId(bank, "Shape"))) == i);
    menu.addSubMenu("Default", basics);
    menu.addItem(20, "Custom", false, processor.lfoTable(bank).custom);
    menu.addSeparator();
    menu.addItem(100, "Load Table...");
    menu.addItem(101, "Save Table...", processor.lfoTable(bank).custom);
    juce::Array<juce::File> saved;
    lfoFolder().findChildFiles(saved, juce::File::findFiles, false, "*.forgelfo");
    if (!saved.isEmpty())
    {
        juce::PopupMenu library;
        for (int i = 0; i < saved.size(); ++i)
            library.addItem(200 + i, saved[static_cast<size_t>(i)].getFileNameWithoutExtension());
        menu.addSubMenu("Saved Tables", library);
    }
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, bank, saved] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice >= 1 && choice <= lfoShapeCount)
        {
            safe->processor.setLfoTable(bank, {});
            if (auto* parameter = safe->processor.state.getParameter(lfoParameterId(bank, "Shape")))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(choice - 1)));
                parameter->endChangeGesture();
            }
        }
        else if (choice == 100) safe->chooseLfoTableFile(false);
        else if (choice == 101) safe->chooseLfoTableFile(true);
        else if (choice >= 200 && choice < 200 + saved.size())
        {
            const auto result = safe->processor.loadLfoTable(bank, saved[static_cast<size_t>(choice - 200)]);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                        "LFO table", result.getErrorMessage());
        }
        safe->repaint(safe->lfoDisplayBounds());
    });
}

void Editor::showLfoGridMenu(bool columns)
{
    const auto bank = shownLfo();
    const auto table = processor.lfoTable(bank);
    juce::PopupMenu menu;
    menu.addSectionHeader(columns ? "Columns" : "Rows");
    for (int count = 2; count <= 32; ++count)
        menu.addItem(count, juce::String(count), true, (columns ? table.columns : table.rows) == count);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, bank, columns] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto next = safe->processor.lfoTable(bank);
        if (columns) next.columns = choice;
        else next.rows = choice;
        safe->processor.setLfoTable(bank, next);
        safe->repaint(safe->lfoDisplayBounds());
    });
}

void Editor::chooseLfoTableFile(bool save)
{
    const auto bank = shownLfo();
    const auto folder = lfoFolder();
    folder.createDirectory();
    fileChooser = std::make_unique<juce::FileChooser>(save ? "Save LFO table" : "Load LFO table",
        save ? folder.getChildFile("Custom.forgelfo") : folder, "*.forgelfo", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    const auto browserFlags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                             : juce::FileBrowserComponent::openMode)
                       | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(browserFlags, [safe, bank, save] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        auto file = chooser.getResult();
        if (file == juce::File {}) return;
        if (save) file = file.withFileExtension("forgelfo");
        const auto result = save ? safe->processor.saveLfoTable(bank, file)
                                 : safe->processor.loadLfoTable(bank, file);
        if (save && result.wasOk())
            safe->processor.setLfoTable(bank, safe->processor.lfoTable(bank),
                                        file.getFileNameWithoutExtension());
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "LFO table", result.getErrorMessage());
        safe->repaint(safe->lfoDisplayBounds());
    });
}
}
