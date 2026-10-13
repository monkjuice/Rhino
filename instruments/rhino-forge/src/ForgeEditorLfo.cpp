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

float pointAcross(juce::Rectangle<int> plot, int x, int columns, bool snap)
{
    const auto raw = std::clamp((x - plot.getX()) / static_cast<float>(plot.getWidth()), 0.0f, 1.0f);
    return snap ? std::round(raw * columns) / columns : raw;
}

// One spelling of where a table coordinate lands in the well. The painter draws
// from the same numbers, so what is grabbed is what is seen.
juce::Point<float> plotPosition(juce::Rectangle<int> plot, LfoPoint point)
{
    return ui::lfoPlotPosition(plot, point.x, point.y);
}

// Where a pointer lands on a path's pad, snapped to the grid when asked. The
// grid is the LFO's table's, because the grid is a property of the well
// rather than of the picture standing in it.
LfoPathPoint padPoint(juce::Rectangle<int> plot, juce::Point<int> at, const LfoTable& grid, bool snap)
{
    return {pointAcross(plot, at.x, grid.columns, snap),
            (pointValue(plot, at.y, grid.rows, snap) + 1.0f) * 0.5f};
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

LfoType Editor::typeOfLfo(int lfo) const
{
    return static_cast<LfoType>(juce::jlimit(0, lfoTypeCount - 1,
        juce::roundToInt(value(lfoParameterId(lfo, "Type")))));
}

LfoType Editor::shownLfoType() const { return typeOfLfo(shownLfo()); }

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
    return table;
}

// The same rule for a path: the default diamond's own points, picked up as
// they stand, the first time anyone takes hold of one.
LfoPath Editor::editableLfoPath(int lfo) const
{
    auto path = processor.lfoPath(lfo);
    if (path.custom && path.valid()) return path;
    path = lfoDefaultPathTable;
    path.custom = true;
    return path;
}

int Editor::lfoPointAt(juce::Point<int> at) const
{
    const auto display = lfoDisplayBounds();
    const auto plot = ui::lfoPlotBounds(display);
    if (!plot.expanded(8).contains(at)) return -1;
    const auto type = shownLfoType();
    // A sample and hold's steps are not points: there is nothing to take hold of.
    if (type == LfoType::sampleHold) return -1;
    if (type == LfoType::path)
    {
        // The last match rather than the first, because two points laid on
        // top of each other — a pause — are drawn in order and the later one
        // is on top.
        const auto path = editableLfoPath(shownLfo());
        auto found = -1;
        for (int i = 0; i < path.count; ++i)
        {
            const auto centre = ui::lfoPathPosition(plot, path.points[static_cast<size_t>(i)]);
            if (std::hypot(at.x - centre.x, at.y - centre.y) <= 9.0f) found = i;
        }
        return found;
    }
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
// dragged up against a point never takes a grab meant for the point. Only a
// NORMAL shape has them: a path's segments are straight.
int Editor::lfoCurveHandleAt(juce::Point<int> at) const
{
    if (shownLfoType() != LfoType::normal) return -1;
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
    const auto x = pointAcross(plot, at.x, table.columns, snap);
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
    if (typeOfLfo(lfo) == LfoType::path) { editLfoPathPoint(lfo, point, at, snap); return; }
    auto table = editableLfoTable(lfo);
    if (point < 0 || point >= table.count) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (plot.isEmpty()) return;
    auto x = pointAcross(plot, at.x, table.columns, snap);
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
    const auto type = typeOfLfo(lfo);
    if (type == LfoType::sampleHold) return;
    if (type == LfoType::path) { addLfoPathPoint(lfo, at); return; }
    auto table = editableLfoTable(lfo);
    if (table.count >= maxLfoPoints) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (!plot.contains(at)) return;
    const auto x = pointAcross(plot, at.x, table.columns, false);
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
    if (typeOfLfo(lfo) == LfoType::path) { removeLfoPathPoint(lfo, point); return; }
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

// A path's point goes wherever it is dropped: both axes are free, and its
// neighbours put no limit on it, because the route is read in the order the
// points are in rather than left to right.
void Editor::editLfoPathPoint(int lfo, int point, juce::Point<int> at, bool snap)
{
    auto path = editableLfoPath(lfo);
    if (point < 0 || point >= path.count) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (plot.isEmpty()) return;
    path.points[static_cast<size_t>(point)] = padPoint(plot, at, processor.lfoTable(lfo), snap);
    processor.setLfoPath(lfo, path);
    repaint(lfoDisplayBounds());
}

// A new point joins the route at the segment it is nearest, so a point dropped
// beside the line becomes part of the line there rather than being hung off
// the end and dragging the route across the pad to reach it.
void Editor::addLfoPathPoint(int lfo, juce::Point<int> at)
{
    auto path = editableLfoPath(lfo);
    if (path.count >= maxLfoPoints) return;
    const auto plot = ui::lfoPlotBounds(lfoDisplayBounds());
    if (!plot.contains(at)) return;
    const auto here = at.toFloat();
    auto insert = path.count;
    auto nearest = std::numeric_limits<float>::max();
    for (int i = 0; i + 1 < path.count; ++i)
    {
        juce::Point<float> onLine;
        const auto distance = juce::Line<float>(
            ui::lfoPathPosition(plot, path.points[static_cast<size_t>(i)]),
            ui::lfoPathPosition(plot, path.points[static_cast<size_t>(i + 1)])).getDistanceFromPoint(here, onLine);
        if (distance < nearest)
        {
            nearest = distance;
            insert = i + 1;
        }
    }
    std::move_backward(path.points.begin() + insert, path.points.begin() + path.count,
                       path.points.begin() + path.count + 1);
    path.points[static_cast<size_t>(insert)] = padPoint(plot, at, processor.lfoTable(lfo), false);
    ++path.count;
    processor.setLfoPath(lfo, path);
    repaint(lfoDisplayBounds());
}

// Any point may leave a path, its ends included, as long as a route is left:
// two points is the shortest one there is.
void Editor::removeLfoPathPoint(int lfo, int point)
{
    auto path = editableLfoPath(lfo);
    if (point < 0 || point >= path.count || path.count <= 2) return;
    std::move(path.points.begin() + point + 1, path.points.begin() + path.count,
              path.points.begin() + point);
    --path.count;
    processor.setLfoPath(lfo, path);
    repaint(lfoDisplayBounds());
}

void Editor::resetLfoPath(int lfo)
{
    if (lfo < 0 || lfo >= lfoCount) return;
    processor.setLfoPath(lfo, LfoPath {});
    repaint(lfoDisplayBounds());
}

void Editor::showLfoMenu()
{
    const auto bank = shownLfo();
    const auto safe = juce::Component::SafePointer<Editor>(this);
    if (typeOfLfo(bank) == LfoType::path)
    {
        // A path has no library yet: the one thing to choose is to start again
        // from the diamond.
        juce::PopupMenu menu;
        menu.addItem(1, "Default path", true, !processor.lfoPathIsCustom(bank));
        menu.showMenuAsync(juce::PopupMenu::Options(), [safe, bank] (int choice)
        {
            if (safe != nullptr && choice == 1) safe->resetLfoPath(bank);
        });
        return;
    }
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

// The three types, as the footer's first field offers them. The choice is a
// parameter, so a preset or a host can make it too.
void Editor::showLfoTypeMenu()
{
    const auto bank = shownLfo();
    const auto current = static_cast<int>(typeOfLfo(bank));
    juce::PopupMenu menu;
    for (int i = 0; i < lfoTypeCount; ++i) menu.addItem(i + 1, lfoTypeName(i), true, i == current);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, bank] (int choice)
    {
        if (safe != nullptr && choice >= 1) safe->selectLfoChoice(bank, "Type", choice - 1);
    });
}

void Editor::showLfoDirectionMenu()
{
    const auto bank = shownLfo();
    const auto current = juce::jlimit(0, lfoDirectionCount - 1,
                                      juce::roundToInt(value(lfoParameterId(bank, "Direction"))));
    juce::PopupMenu menu;
    for (int i = 0; i < lfoDirectionCount; ++i)
        menu.addItem(i + 1, lfoDirectionName(i), true, i == current);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, bank] (int choice)
    {
        if (safe != nullptr && choice >= 1) safe->selectLfoChoice(bank, "Direction", choice - 1);
    });
}

void Editor::selectLfoChoice(int lfo, const char* suffix, int choice)
{
    if (lfo < 0 || lfo >= lfoCount) return;
    if (auto* parameter = processor.state.getParameter(lfoParameterId(lfo, suffix)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(choice)));
        parameter->endChangeGesture();
    }
    // What the footer offers and what the header hangs both follow the type,
    // so the whole module is laid out again rather than only the well.
    applyEnableStates();
    resized();
    repaint();
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
    if (typeOfLfo(bank) != LfoType::normal) return;
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
