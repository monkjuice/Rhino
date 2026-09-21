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

std::vector<juce::File> savedLfoShapes()
{
    juce::Array<juce::File> found;
    lfoFolder().findChildFiles(found, juce::File::findFiles, false, "*.forgelfo");
    std::vector<juce::File> sorted;
    sorted.reserve(static_cast<size_t>(found.size()));
    for (const auto& file : found) sorted.push_back(file);
    std::sort(sorted.begin(), sorted.end(), [] (const auto& a, const auto& b)
    { return a.getFileName().compareIgnoreCase(b.getFileName()) < 0; });
    return sorted;
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
    const auto custom = processor.lfoTableIsCustom(bank);
    const auto currentName = processor.lfoTableName(bank);
    juce::PopupMenu menu;
    menu.addSectionHeader("Default shapes");
    for (int i = 0; i < lfoShapeCount; ++i)
        menu.addItem(i + 1, lfoFullShapeName(i), true,
                     !custom
                         && juce::roundToInt(value(lfoParameterId(bank, "Shape"))) == i);
    menu.addSeparator();
    menu.addItem(100, "Load shape from file...");
    menu.addItem(101, "Save current shape...", custom);
    const auto saved = savedLfoShapes();
    if (!saved.empty())
    {
        juce::PopupMenu library;
        for (int i = 0; i < static_cast<int>(saved.size()); ++i)
        {
            const auto title = saved[static_cast<size_t>(i)].getFileNameWithoutExtension();
            library.addItem(200 + i, title, true, custom && currentName == title);
        }
        menu.addSubMenu("Saved shapes", library);
    }
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, bank, saved] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice >= 1 && choice <= lfoShapeCount)
            safe->selectLfoBasicShape(bank, choice - 1);
        else if (choice == 100) safe->chooseLfoTableFile(false);
        else if (choice == 101) safe->chooseLfoTableFile(true);
        else if (choice >= 200 && choice < 200 + static_cast<int>(saved.size()))
        {
            const auto result = safe->processor.loadLfoTable(bank, saved[static_cast<size_t>(choice - 200)]);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                        "LFO table", result.getErrorMessage());
        }
        safe->applyEnableStates();
        safe->repaint(safe->lfoDisplayBounds());
    });
}

void Editor::selectLfoBasicShape(int lfo, int shape)
{
    if (lfo < 0 || lfo >= lfoCount || shape < 0 || shape >= lfoShapeCount) return;
    auto table = processor.lfoTable(lfo);
    table.custom = false;
    table.count = 0;
    processor.setLfoTable(lfo, table);
    if (auto* parameter = processor.state.getParameter(lfoParameterId(lfo, "Shape")))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(shape)));
        parameter->endChangeGesture();
    }
    applyEnableStates();
    repaint(lfoDisplayBounds());
}

void Editor::stepLfoShape(int direction)
{
    const auto bank = shownLfo();
    const auto saved = savedLfoShapes();
    auto current = juce::jlimit(0, lfoShapeCount - 1,
        juce::roundToInt(value(lfoParameterId(bank, "Shape"))));
    if (processor.lfoTableIsCustom(bank))
    {
        const auto currentName = processor.lfoTableName(bank);
        for (int i = 0; i < static_cast<int>(saved.size()); ++i)
            if (currentName == saved[static_cast<size_t>(i)].getFileNameWithoutExtension())
            { current = lfoShapeCount + i; break; }
    }
    const auto total = lfoShapeCount + static_cast<int>(saved.size());
    const auto next = (current + direction + total) % total;
    if (next < lfoShapeCount) selectLfoBasicShape(bank, next);
    else
    {
        const auto result = processor.loadLfoTable(bank, saved[static_cast<size_t>(next - lfoShapeCount)]);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "LFO table", result.getErrorMessage());
        applyEnableStates();
        repaint(lfoDisplayBounds());
    }
}

void Editor::setLfoGridCount(int lfo, bool columns, int count)
{
    if (lfo < 0 || lfo >= lfoCount) return;
    auto table = processor.lfoTable(lfo);
    const auto bounded = juce::jlimit(2, 32, count);
    auto& target = columns ? table.columns : table.rows;
    if (target == bounded) return;
    target = bounded;
    processor.setLfoTable(lfo, table, processor.lfoTableName(lfo));
    repaint(lfoDisplayBounds());
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
