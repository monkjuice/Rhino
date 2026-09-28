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

// One spelling of where a table coordinate lands in the well. The painter draws
// from the same numbers, so what is grabbed is what is seen.
juce::Point<float> plotPosition(juce::Rectangle<int> plot, LfoPoint point)
{
    return {plot.getX() + point.x * plot.getWidth(),
            plot.getCentreY() - point.y * plot.getHeight() * 0.48f};
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
    const auto shape = static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
        juce::roundToInt(value(lfoParameterId(lfo, "Shape")))));
    // The Default shape's own nodes rather than a row of dots sampled off it, so
    // what is picked up is exactly what was on screen. The grid stays as it was
    // set: it is a property of the well, not of the shape standing in it.
    const auto& starter = lfoShapeTables[static_cast<size_t>(shape)];
    table.points = starter.points;
    table.count = starter.count;
    table.custom = true;
    if (shape == LfoShape::sampleHold)
        table.points[0].y = table.points[1].y
            = std::clamp(processor.lfoValue(lfo), -1.0f, 1.0f);
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
        const auto centre = plotPosition(plot, table.points[static_cast<size_t>(i)]);
        if (std::hypot(at.x - centre.x, at.y - centre.y) <= 9.0f) return i;
    }
    return -1;
}

// The other kind of node: one per gap, carrying the bend of the segment it sits
// on and nothing else. A position node under the same pointer wins, so a handle
// dragged up against a point never takes a grab meant for the point.
int Editor::lfoCurveHandleAt(juce::Point<int> at) const
{
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (!plot.expanded(8).contains(at)) return -1;
    if (lfoPointAt(at) >= 0) return -1;
    const auto table = editableLfoTable(shownLfo());
    for (int i = 0; i + 1 < table.count; ++i)
    {
        const auto centre = plotPosition(plot, table.curveHandle(i));
        if (std::hypot(at.x - centre.x, at.y - centre.y) <= 8.0f) return i;
    }
    return -1;
}

// Dragging a curve handle reads both axes of the drop. Its height is how far the
// segment stands off its straight line; its position along the segment is where
// that stand-off is measured, so the same height nearer one end is a harder
// bend leaning that way.
void Editor::editLfoCurve(int lfo, int segment, juce::Point<int> at, bool snap)
{
    auto table = editableLfoTable(lfo);
    if (segment < 0 || segment + 1 >= table.count) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (plot.isEmpty()) return;
    auto& left = table.points[static_cast<size_t>(segment)];
    const auto right = table.points[static_cast<size_t>(segment + 1)];
    const auto width = right.x - left.x;
    if (width <= 0.0f) return;
    const auto rawX = std::clamp((at.x - plot.getX()) / static_cast<float>(plot.getWidth()), 0.0f, 1.0f);
    const auto x = snap ? std::round(rawX * table.columns) / table.columns : rawX;
    const auto along = std::clamp((x - left.x) / width, minLfoCurveAt, maxLfoCurveAt);
    left.curve = LfoTable::curveThrough(left, right, along,
                                        pointValue(plot, at.y, table.rows, snap));
    left.curveAt = along;
    processor.setLfoTable(lfo, table);
    repaint(lfoDisplayBounds());
}

// Straightening a segment is the curve handle's equivalent of removing a point,
// and it puts the handle back in the middle where it is found again.
void Editor::resetLfoCurve(int lfo, int segment)
{
    auto table = editableLfoTable(lfo);
    if (segment < 0 || segment + 1 >= table.count) return;
    table.points[static_cast<size_t>(segment)].curve = 0.0f;
    table.points[static_cast<size_t>(segment)].curveAt = 0.5f;
    processor.setLfoTable(lfo, table);
    repaint(lfoDisplayBounds());
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
    // Only the position moves: the bend either side is held in coordinates
    // normalised to its own segment, so it follows the point rather than being
    // flattened by it.
    auto& moved = table.points[static_cast<size_t>(point)];
    moved.x = x;
    moved.y = pointValue(plot, at.y, table.rows, snap);
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
    // A parabola does not cut into two parabolas, so the halves of a bent
    // segment come back straight rather than approximately wrong.
    table.points[static_cast<size_t>(insert - 1)].curve = 0.0f;
    table.points[static_cast<size_t>(insert - 1)].curveAt = 0.5f;
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
    // The two segments either side become one, and neither bend describes it.
    table.points[static_cast<size_t>(point - 1)].curve = 0.0f;
    table.points[static_cast<size_t>(point - 1)].curveAt = 0.5f;
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
