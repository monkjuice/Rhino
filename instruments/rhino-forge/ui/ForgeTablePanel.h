#pragma once

#include "ForgeVisuals.h"
#include "../core/ForgeTableStore.h"
#include <functional>

// The wavetable editor: the selected frame drawn large enough to draw on, the
// table's frames in a strip beneath it, and the few operations that make a
// table rather than only change one.
//
// Everything here works on the WavetableEdit the store holds and asks the store
// to publish afterwards. Nothing in this file touches the audio thread, knows
// what a band-limited copy is, or holds a table of its own — so what is drawn
// and what is played cannot come apart.
//
// Deliberately absent, and named in PLAN.md rather than left to be discovered:
// the harmonic bars, the brush palette and the formula bar Serum also carries.
namespace rhino::forge::ui
{
// A flat button for something that happens rather than something that is on.
// ToggleChip next door draws state; this one only ever draws a press.
class PanelButton final : public juce::Button
{
public:
    explicit PanelButton(const juce::String& label) : juce::Button(label) {}

    juce::Colour accent = electricBlue;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto enabled = isEnabled();
        g.setColour(down ? accent.withAlpha(0.28f) : juce::Colour(0xff0b0e18));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour((highlighted || down ? accent : line).withAlpha(enabled ? 0.9f : 0.3f));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        g.setColour((down ? rhino::forge::ui::text : mutedText).withAlpha(enabled ? 1.0f : 0.35f));
        g.setFont(panelFont(Face::emphasis, 9.5f));
        g.drawText(getName(), area, juce::Justification::centred);
    }
};

// --- The canvas ---------------------------------------------------------------
//
// One frame, full width, zero in the middle. A drag is a stroke: the pen sets
// everything the pointer passes over, the line tool sets everything between
// where the drag began and where it ended. Both end up in WavetableEdit::draw,
// which is the only thing that ever writes a sample.
class WaveCanvas final : public juce::Component
{
public:
    WaveCanvas() { setMouseCursor(juce::MouseCursor::CrosshairCursor); }

    WavetableEdit* table = nullptr;
    int frame = 0;
    // Divisions across the phase axis; zero is no grid at all. The value axis
    // gets half as many, so a cell is roughly square on a canvas twice as wide
    // as it is tall.
    int grid = 16;
    bool snap = false;
    bool lineTool = false;
    juce::Colour accent = electricBlue;

    // Raised once per stroke, before the first sample of it is written, so an
    // undo step is a gesture rather than a mouse move.
    std::function<void()> onStrokeStart;
    // Raised after every change, so the table is rebuilt and the stroke is
    // audible while the hand is still moving.
    std::function<void()> onStroke;

    void paint(juce::Graphics& g) override
    {
        const auto box = plotArea();
        g.setColour(juce::Colour(0xff080b14));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), displayCorner);

        paintGrid(g, box);

        // The zero line, brighter than the grid: it is the line the fill is
        // measured against, not another division.
        g.setColour(line.withAlpha(0.85f));
        g.fillRect(box.getX(), box.getCentreY() - 0.5f, box.getWidth(), 1.0f);

        if (table != nullptr && table->frameCount() > 0)
        {
            const auto* samples = table->frame(frame);
            // One point per pixel: the frame holds far more than a canvas this
            // wide can show, so the curve is read at the resolution it is drawn
            // at rather than at the resolution it is stored at.
            const auto points = juce::jlimit(64, wavetableFrameSize, juce::roundToInt(box.getWidth()));
            const auto path = wavePath(box, [samples] (float phase)
            {
                return samples[juce::jlimit(0, wavetableFrameSize - 1,
                                            juce::roundToInt(phase * static_cast<float>(wavetableFrameSize)))];
            }, points);
            fillWaveArea(g, box, path, accent, 1.0f);
            g.setColour(accent.withAlpha(0.22f));
            g.strokePath(path, juce::PathStrokeType(5.0f));
            g.setColour(accent.interpolatedWith(juce::Colours::white, 0.35f));
            g.strokePath(path, juce::PathStrokeType(1.8f));
        }

        // The line tool's rubber band, so a straight edge can be placed before
        // it is committed rather than drawn and then corrected.
        if (dragging && lineTool)
        {
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawLine({pointFor(anchor), pointFor(cursor)}, 1.2f);
        }

        g.setColour(line.withAlpha(0.7f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), displayCorner, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (table == nullptr) return;
        anchor = cursor = positionOf(event);
        dragging = true;
        if (onStrokeStart != nullptr) onStrokeStart();
        // The pen marks where it landed. The line tool does not: until the drag
        // ends there is no line to draw, and a stray dot at the anchor would
        // have to be undrawn by the commit.
        if (!lineTool) commit(anchor, anchor);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging || table == nullptr) return;
        const auto next = positionOf(event);
        if (lineTool) cursor = next;
        else { commit(cursor, next); cursor = next; }
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (!dragging || table == nullptr) return;
        dragging = false;
        if (lineTool) commit(anchor, positionOf(event));
        repaint();
    }

private:
    struct Point { float phase = 0.0f, value = 0.0f; };

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced(2.0f); }

    int valueDivisions() const { return juce::jmax(2, grid / 2); }

    void paintGrid(juce::Graphics& g, juce::Rectangle<float> box) const
    {
        if (grid <= 0) return;
        g.setColour(line.withAlpha(0.32f));
        for (int i = 1; i < grid; ++i)
        {
            const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(grid);
            g.fillRect(x, box.getY(), 1.0f, box.getHeight());
        }
        const auto rows = valueDivisions();
        for (int i = 1; i < rows; ++i)
        {
            const auto y = box.getY() + box.getHeight() * static_cast<float>(i) / static_cast<float>(rows);
            g.fillRect(box.getX(), y, box.getWidth(), 1.0f);
        }
    }

    Point positionOf(const juce::MouseEvent& event) const
    {
        const auto box = plotArea();
        Point result;
        result.phase = juce::jlimit(0.0f, 1.0f, (static_cast<float>(event.position.x) - box.getX())
                                                    / juce::jmax(1.0f, box.getWidth()));
        result.value = juce::jlimit(-1.0f, 1.0f, (box.getCentreY() - static_cast<float>(event.position.y))
                                                     / juce::jmax(1.0f, box.getHeight() * 0.5f));
        if (!snap || grid <= 0) return result;
        const auto steps = static_cast<float>(grid);
        result.phase = juce::jlimit(0.0f, 1.0f, std::round(result.phase * steps) / steps);
        const auto rows = static_cast<float>(valueDivisions());
        result.value = juce::jlimit(-1.0f, 1.0f, std::round(result.value * rows * 0.5f) / (rows * 0.5f));
        return result;
    }

    juce::Point<float> pointFor(Point at) const
    {
        const auto box = plotArea();
        return {box.getX() + at.phase * box.getWidth(),
                box.getCentreY() - at.value * box.getHeight() * 0.5f};
    }

    void commit(Point from, Point to)
    {
        table->draw(frame, from.phase, from.value, to.phase, to.value);
        if (onStroke != nullptr) onStroke();
    }

    Point anchor, cursor;
    bool dragging = false;
};

// --- The strip ----------------------------------------------------------------
//
// Every frame in the table, in order, each drawn the way the canvas draws the
// one that is selected. It is the only place the shape of the whole table can
// be seen at once, which is what POSITION is actually sweeping through.
class FrameStrip final : public juce::Component
{
public:
    WavetableEdit* table = nullptr;
    int selected = 0;
    juce::Colour accent = electricBlue;
    std::function<void(int)> onSelect;

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff080b14));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), displayCorner);
        if (table == nullptr) return;

        for (int i = 0; i < table->frameCount(); ++i)
        {
            auto cell = cellFor(i);
            if (cell.getWidth() < 3) continue;
            const auto on = i == selected;
            g.setColour(on ? accent.withAlpha(0.14f) : juce::Colour(0xff0d1120));
            g.fillRoundedRectangle(cell.toFloat().reduced(1.0f), 2.0f);

            const auto* samples = table->frame(i);
            const auto box = cell.toFloat().reduced(3.0f);
            if (box.getWidth() > 4.0f)
            {
                const auto path = wavePath(box, [samples] (float phase)
                {
                    return samples[juce::jlimit(0, wavetableFrameSize - 1,
                                                juce::roundToInt(phase * static_cast<float>(wavetableFrameSize)))];
                }, juce::jlimit(24, 256, juce::roundToInt(box.getWidth())));
                fillWaveArea(g, box, path, accent, on ? 1.0f : 0.55f);
                g.setColour(accent.withAlpha(on ? 1.0f : 0.5f));
                g.strokePath(path, juce::PathStrokeType(1.2f));
            }

            g.setColour((on ? accent : line).withAlpha(on ? 1.0f : 0.7f));
            g.drawRoundedRectangle(cell.toFloat().reduced(1.0f), 2.0f, on ? 1.4f : 1.0f);
            // Numbered only where a number still fits: past about thirty frames
            // the cells are narrower than the digits and the strip reads better
            // as shapes alone.
            if (cell.getWidth() >= 22)
            {
                g.setColour((on ? rhino::forge::ui::text : mutedText).withAlpha(0.85f));
                g.setFont(panelFont(Face::reading, 8.5f));
                g.drawText(juce::String(i + 1), cell.removeFromBottom(11), juce::Justification::centred);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (table == nullptr || onSelect == nullptr) return;
        for (int i = 0; i < table->frameCount(); ++i)
            if (cellFor(i).contains(event.getPosition())) { onSelect(i); return; }
    }

private:
    // The frames share the width evenly. A table is capped at maxEditableFrames
    // so the narrowest cell a full one can produce is still wide enough to tell
    // one shape from the next.
    juce::Rectangle<int> cellFor(int index) const
    {
        const auto count = juce::jmax(1, table == nullptr ? 1 : table->frameCount());
        const auto area = getLocalBounds().reduced(2);
        const auto x = area.getX() + area.getWidth() * index / count;
        const auto next = area.getX() + area.getWidth() * (index + 1) / count;
        return {x, area.getY(), next - x, area.getHeight()};
    }
};

// --- The panel ----------------------------------------------------------------

class TablePanel final : public juce::Component,
                         public juce::FileDragAndDropTarget
{
public:
    // Reading a wavetable file is filesystem work, so it belongs to the
    // Processor rather than to a component. The panel only says which file and
    // which oscillator, and reports back what it was told.
    std::function<juce::Result(int, const juce::File&)> importer;
    // Raised whenever the table changes, so the oscillator displays and the
    // POSITION readouts on the other tab follow it.
    std::function<void()> onTableChanged;

    explicit TablePanel(WavetableStore& tableStore) : store(tableStore)
    {
        for (int i = 0; i < oscillatorCount; ++i)
        {
            auto chip = std::make_unique<ToggleChip>(i == 0 ? "A" : "B");
            chip->accent = electricBlue;
            chip->setTooltip(i == 0 ? "Edit oscillator A's table" : "Edit oscillator B's table");
            chip->setToggleState(i == oscillator, juce::dontSendNotification);
            chip->onClick = [this, i] { chooseOscillator(i); };
            addAndMakeVisible(*chip);
            oscChips.push_back(std::move(chip));
        }

        const char* toolNames[] {"PEN", "LINE"};
        const char* toolTips[] {"Draw freehand: the frame follows the pointer",
                                "Draw a straight line between where the drag starts and ends"};
        for (int i = 0; i < 2; ++i)
        {
            auto chip = std::make_unique<ToggleChip>(toolNames[i]);
            chip->accent = signalViolet;
            chip->setTooltip(toolTips[i]);
            chip->setToggleState(i == 0, juce::dontSendNotification);
            chip->onClick = [this, i] { chooseTool(i == 1); };
            addAndMakeVisible(*chip);
            toolChips.push_back(std::move(chip));
        }

        gridButton.accent = signalViolet;
        gridButton.setTooltip("Cycle the drawing grid: off, 8, 16 or 32 divisions");
        gridButton.setToggleState(true, juce::dontSendNotification);
        gridButton.onClick = [this] { cycleGrid(); };
        addAndMakeVisible(gridButton);

        snapChip.accent = signalViolet;
        snapChip.setTooltip("Land every point of a stroke on the grid");
        snapChip.onClick = [this] { canvas.snap = snapChip.getToggleState(); };
        addAndMakeVisible(snapChip);

        // Every one of these is a whole-table or whole-frame change, so each
        // takes an undo snapshot before it runs.
        const auto action = [this] (PanelButton& button, const char* tip, std::function<void()> run)
        {
            button.accent = electricBlue;
            button.setTooltip(tip);
            button.onClick = [this, run = std::move(run)] { remember(); run(); };
            addAndMakeVisible(button);
        };
        action(loadButton, "Load a wavetable from a .wav file of single-cycle frames", [this] { chooseFile(); });
        action(resetButton, "Throw the table away and go back to the ten built-in shapes",
               [this] { store.resetToBuiltIn(oscillator); selected = 0; changed(); });
        action(initButton, "Replace this frame with a plain sine",
               [this] { edit().initFrame(selected); publishFrame(); });
        action(normaliseButton, "Scale this frame so its peak reaches full scale",
               [this] { edit().normaliseFrame(selected); publishFrame(); });
        action(addButton, "Add a new frame after this one",
               [this] { selected = edit().insertFrame(selected, false); changed(); });
        action(duplicateButton, "Add a copy of this frame after it",
               [this] { selected = edit().insertFrame(selected, true); changed(); });
        action(removeButton, "Remove this frame from the table",
               [this] { selected = edit().removeFrame(selected); changed(); });

        canvas.onStrokeStart = [this] { remember(); };
        canvas.onStroke = [this] { publishFrame(); };
        addAndMakeVisible(canvas);

        strip.onSelect = [this] (int frame) { selected = frame; refresh(); };
        addAndMakeVisible(strip);

        refresh();
    }

    int editingOscillator() const noexcept { return oscillator; }

    // What the module header says: which table this is and how big it is.
    juce::String headerDetail() const
    {
        return edit().title().toUpperCase() + " // " + juce::String(edit().frameCount()) + " FRAMES";
    }

    // Pulled back into step with the store — after a preset load, or after the
    // host has replaced the whole state behind the panel's back.
    void refresh()
    {
        selected = juce::jlimit(0, juce::jmax(0, edit().frameCount() - 1), selected);
        canvas.table = &edit();
        canvas.frame = selected;
        strip.table = &edit();
        strip.selected = selected;
        removeButton.setEnabled(edit().frameCount() > 1);
        addButton.setEnabled(edit().frameCount() < maxEditableFrames);
        duplicateButton.setEnabled(edit().frameCount() < maxEditableFrames);
        canvas.repaint();
        strip.repaint();
    }

    bool undo()
    {
        if (history.empty()) return false;
        const auto step = std::move(history.back());
        history.pop_back();
        oscillator = step.oscillator;
        for (int i = 0; i < oscillatorCount; ++i)
            oscChips[static_cast<size_t>(i)]->setToggleState(i == oscillator, juce::dontSendNotification);
        edit().setFrames(step.samples.data(), step.frames, step.title, {}, step.untouched);
        selected = step.selected;
        store.publish(oscillator);
        refresh();
        if (onTableChanged != nullptr) onTableChanged();
        return true;
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto toolbar = area.removeFromTop(22);
        const auto chip = [&toolbar] (juce::Component& c, int width)
        {
            c.setBounds(toolbar.removeFromLeft(width).reduced(1, 0));
            toolbar.removeFromLeft(4);
        };
        for (auto& c : oscChips) chip(*c, 30);
        toolbar.removeFromLeft(10);
        for (auto& c : toolChips) chip(*c, 46);
        toolbar.removeFromLeft(10);
        chip(gridButton, 62);
        chip(snapChip, 46);
        // The file controls sit at the far end, away from the tools, because
        // they change the whole table rather than a frame of it.
        loadButton.setBounds(toolbar.removeFromRight(70).reduced(1, 0));
        toolbar.removeFromRight(4);
        resetButton.setBounds(toolbar.removeFromRight(70).reduced(1, 0));

        area.removeFromTop(5);
        auto actions = area.removeFromBottom(22);
        for (auto* button : {&initButton, &normaliseButton, &addButton, &duplicateButton, &removeButton})
        {
            button->setBounds(actions.removeFromLeft(84).reduced(1, 0));
            actions.removeFromLeft(4);
        }
        area.removeFromBottom(5);
        strip.setBounds(area.removeFromBottom(juce::jlimit(30, 56, area.getHeight() / 4)));
        area.removeFromBottom(5);
        canvas.setBounds(area);
    }

    // A wavetable dropped on the panel goes to whichever oscillator is being
    // edited, which is the one the canvas is already showing.
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (const auto& file : files)
            if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".wt")) return true;
        return false;
    }

    void filesDropped(const juce::StringArray& files, int, int) override
    {
        if (files.isEmpty() || importer == nullptr) return;
        remember();
        report(importer(oscillator, juce::File(files[0])));
    }

    // What the last load said, if it failed. Reported beside the preset name
    // rather than in a dialog that has to be dismissed before the table that
    // did load can be tried.
    juce::String status;

private:
    struct Step
    {
        int oscillator = 0, frames = 1, selected = 0;
        bool untouched = false;
        juce::String title;
        std::vector<float> samples;
    };

    WavetableEdit& edit() { return store.edit(oscillator); }
    const WavetableEdit& edit() const { return store.edit(oscillator); }

    void chooseOscillator(int which)
    {
        oscillator = juce::jlimit(0, oscillatorCount - 1, which);
        for (int i = 0; i < oscillatorCount; ++i)
            oscChips[static_cast<size_t>(i)]->setToggleState(i == oscillator, juce::dontSendNotification);
        selected = 0;
        refresh();
        if (onTableChanged != nullptr) onTableChanged();
    }

    void chooseTool(bool line)
    {
        canvas.lineTool = line;
        toolChips[0]->setToggleState(!line, juce::dontSendNotification);
        toolChips[1]->setToggleState(line, juce::dontSendNotification);
    }

    void cycleGrid()
    {
        canvas.grid = canvas.grid == 0 ? 8 : canvas.grid == 8 ? 16 : canvas.grid == 16 ? 32 : 0;
        // A chip lights itself on click, and four states do not fit two, so the
        // light is set from the grid instead of left to the toggle.
        gridButton.setName(canvas.grid == 0 ? "GRID OFF" : "GRID " + juce::String(canvas.grid));
        gridButton.setToggleState(canvas.grid != 0, juce::dontSendNotification);
        gridButton.repaint();
        canvas.repaint();
    }

    // One frame changed: rebuild that frame only, which is cheap enough to do
    // on every mouse move.
    void publishFrame()
    {
        store.publishFrame(oscillator, selected);
        refresh();
        if (onTableChanged != nullptr) onTableChanged();
    }

    // The table itself changed — a frame added or removed, a file loaded — so
    // every frame has to be rebuilt.
    void changed()
    {
        store.publish(oscillator);
        refresh();
        if (onTableChanged != nullptr) onTableChanged();
    }

    // An undo step is the whole table, not a diff. A frame is 8 KB and the
    // history is short, so the simple thing costs little and cannot get a
    // partial edit wrong.
    void remember()
    {
        Step step;
        step.oscillator = oscillator;
        step.frames = edit().frameCount();
        step.selected = selected;
        step.untouched = edit().isUntouched();
        step.title = edit().title();
        step.samples = edit().samples();
        history.push_back(std::move(step));
        // Bounded by weight rather than by count: a step on a ten-frame table is
        // 80 KB and one on a full sixty-four-frame table is half a megabyte, so
        // counting steps would let the history run to tens of megabytes.
        auto held = size_t {0};
        for (const auto& kept : history) held += kept.samples.size() * sizeof(float);
        while (history.size() > 1 && (history.size() > 48 || held > 8u * 1024u * 1024u))
        {
            held -= history.front().samples.size() * sizeof(float);
            history.erase(history.begin());
        }
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser>("Load a wavetable", lastFolder(), "*.wav;*.wt", true);
        const auto safe = juce::Component::SafePointer<TablePanel>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& c)
            {
                if (safe == nullptr) return;
                const auto file = c.getResult();
                if (file == juce::File {} || safe->importer == nullptr) return;
                safe->folder = file.getParentDirectory();
                safe->report(safe->importer(safe->oscillator, file));
            });
    }

    // Forge's own tables, when they can be found: the standalone and the plugin
    // both sit several directories below the source tree in a development
    // build, so the folder is looked for upward from the binary rather than
    // assumed. Falling back to the last folder used, and then to home, means a
    // packaged build simply opens where the person last was.
    juce::File lastFolder() const
    {
        if (folder != juce::File {} && folder.isDirectory()) return folder;
        auto here = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        for (int step = 0; step < 8 && here != juce::File {}; ++step)
        {
            const auto candidate = here.getChildFile("tables");
            if (candidate.isDirectory()) return candidate;
            here = here.getParentDirectory();
        }
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    }

    void report(const juce::Result& result)
    {
        status = result.wasOk() ? juce::String() : "COULD NOT LOAD // " + result.getErrorMessage();
        if (!result.wasOk() && !history.empty()) history.pop_back();
        selected = 0;
        refresh();
        if (onTableChanged != nullptr) onTableChanged();
    }

    WavetableStore& store;
    int oscillator = 0;
    int selected = 0;
    WaveCanvas canvas;
    FrameStrip strip;
    std::vector<std::unique_ptr<ToggleChip>> oscChips, toolChips;
    ToggleChip gridButton {"GRID 16"}, snapChip {"SNAP"};
    PanelButton loadButton {"LOAD"}, resetButton {"RESET"};
    PanelButton initButton {"INIT"}, normaliseButton {"NORMALISE"};
    PanelButton addButton {"ADD"}, duplicateButton {"DUPLICATE"}, removeButton {"REMOVE"};
    std::vector<Step> history;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File folder;
};
}
