#include "../src/ForgeProcessor.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTooltips.h"
#include "../ui/ForgeVisuals.h"
#include "../ui/ForgeFxDisplay.h"
#include <algorithm>
#include <iostream>
#include <vector>

// One binary, three CTest cases selected by argv, matching Rhino's own test
// convention. Run with no argument to execute all three.
namespace
{
int failures = 0;

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void requireClose(float actual, float expected, float tolerance, const char* message)
{
    if (std::abs(actual - expected) <= tolerance) return;
    std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual << ")\n";
    ++failures;
}

void requireText(const juce::String& actual, const juce::String& expected, const char* message)
{
    if (actual == expected) return;
    std::cerr << "FAIL: " << message << " (expected \"" << expected << "\", got \"" << actual << "\")\n";
    ++failures;
}

void setValue(rhino::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return; }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float value(const rhino::forge::Processor& processor, const char* id)
{
    const auto* raw = processor.state.getRawParameterValue(id);
    if (raw == nullptr) { require(false, id); return 0.0f; }
    return raw->load();
}

juce::String textFor(const rhino::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return {}; }
    return parameter->getText(parameter->convertTo0to1(plainValue), 32);
}

// Render a held note and report the loudest sample. Used to prove a source is
// silent rather than merely quiet.
float peakForNote(rhino::forge::Processor& processor, int samples = 4096)
{
    processor.prepareToPlay(48000.0, samples);
    juce::AudioBuffer<float> buffer(2, samples);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    processor.processBlock(buffer, midi);
    return buffer.getMagnitude(0, samples);
}

// Render a held note into a caller-owned buffer, for checks that need to look
// at the waveform rather than only its peak.
// Source indices, taken from the enum rather than written out: they are what a
// slot's Source parameter stores, and they moved when LFO 2-6 were inserted
// into the middle of the list. Reading them from the one declaration is what
// stops these checks quietly testing the wrong source after the next such move.
constexpr int srcOff = static_cast<int>(rhino::forge::ModSource::off);
constexpr int srcEnv1 = static_cast<int>(rhino::forge::ModSource::env1);
constexpr int srcLfo1 = static_cast<int>(rhino::forge::ModSource::lfo1);
constexpr int srcVelocity = static_cast<int>(rhino::forge::ModSource::velocity);
constexpr int srcNote = static_cast<int>(rhino::forge::ModSource::note);

void renderNote(rhino::forge::Processor& processor, juce::AudioBuffer<float>& buffer, int note = 57)
{
    processor.prepareToPlay(48000.0, buffer.getNumSamples());
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    processor.processBlock(buffer, midi);
}

// Counted on the settled part of the render, past the envelope attack.
int zeroCrossings(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto crossings = 0;
    for (int i = from + 1; i < buffer.getNumSamples(); ++i)
        if ((buffer.getSample(channel, i - 1) < 0.0f) != (buffer.getSample(channel, i) < 0.0f))
            ++crossings;
    return crossings;
}

// How far the level swings over the course of a render, in dB, measured on the
// short-term RMS rather than on single samples. A detuned stack is supposed to
// move - that movement is the chorus - but it is supposed to move continuously.
// A stack whose members are evenly spaced instead swings in and out of phase
// all together on one slow period, and that reads as a throb rather than as
// chorus. The depth of the swing is what tells the two apart.
float envelopeDepthDb(const juce::AudioBuffer<float>& buffer, int channel, int from,
                      int window = 2048)
{
    std::vector<float> levels;
    for (auto start = from; start + window <= buffer.getNumSamples(); start += window)
    {
        auto sum = 0.0;
        for (auto i = start; i < start + window; ++i)
        {
            const auto value = static_cast<double>(buffer.getSample(channel, i));
            sum += value * value;
        }
        levels.push_back(static_cast<float>(std::sqrt(sum / window)));
    }
    if (levels.size() < 8) return 0.0f;
    std::sort(levels.begin(), levels.end());
    // Trimmed, so one stray window cannot stand for the whole render.
    const auto low = levels[levels.size() / 20];
    const auto high = levels[levels.size() - 1 - levels.size() / 20];
    return juce::Decibels::gainToDecibels(high / juce::jmax(low, 1.0e-9f));
}

float rms(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto sum = 0.0;
    const auto count = buffer.getNumSamples() - from;
    for (int i = from; i < buffer.getNumSamples(); ++i)
        sum += static_cast<double>(buffer.getSample(channel, i)) * buffer.getSample(channel, i);
    return count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
}

// High-frequency content relative to overall level. Amplitude-independent, so
// it measures how open a filter is without being fooled by a quieter note.
float brightness(const juce::AudioBuffer<float>& buffer, int channel, int from)
{
    auto edges = 0.0, total = 0.0;
    for (int i = from + 1; i < buffer.getNumSamples(); ++i)
    {
        const auto sample = static_cast<double>(buffer.getSample(channel, i));
        const auto step = sample - buffer.getSample(channel, i - 1);
        edges += step * step;
        total += sample * sample;
    }
    return total > 0.0 ? static_cast<float>(std::sqrt(edges / total)) : 0.0f;
}

bool allSamplesFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(buffer.getSample(channel, i))) return false;
    return true;
}

// ---------------------------------------------------------------- layout ---

void layoutSuite()
{
    rhino::forge::Processor processor;
    // The size the editor actually opens at, so the detailed checks below run
    // against the panel people see. The sweep further down covers the rest of
    // the allowed range.
    const auto bounds = juce::Rectangle<int>(0, 0, rhino::forge::ui::defaultPanelWidth,
                                             rhino::forge::ui::defaultPanelHeight);
    const auto content = rhino::forge::ui::contentBounds(bounds);
    const auto& modules = rhino::forge::ui::modules();
    require(!modules.empty(), "the panel declares at least one module");
    // The default has to be a size the window can actually be put at, or the
    // editor opens somewhere the resize limits would not let you return to.
    require(rhino::forge::ui::defaultPanelWidth >= rhino::forge::ui::minPanelWidth
                && rhino::forge::ui::defaultPanelWidth <= rhino::forge::ui::maxPanelWidth
                && rhino::forge::ui::defaultPanelHeight >= rhino::forge::ui::minPanelHeight
                && rhino::forge::ui::defaultPanelHeight <= rhino::forge::ui::maxPanelHeight,
            "the size the panel opens at is inside its own resize limits");

    // Every id the layout names must resolve. This is the guard that keeps a
    // declarative layout honest: a typo here would otherwise be a silent
    // missing knob rather than a build or test failure.
    for (const auto& module : modules)
    {
        if (module.enableId != nullptr)
            require(processor.state.getParameter(module.enableId) != nullptr,
                    "a module's enable id names a real parameter");
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
                require(processor.state.getParameter(control.id) != nullptr,
                        "a module's control id names a real parameter");
        if (module.display == rhino::forge::ui::Display::oscillator)
        {
            const auto* source = rhino::forge::ui::displaySourceId(module);
            require(source != nullptr && processor.state.getParameter(source) != nullptr,
                    "an oscillator display reads a real parameter");
        }
    }

    // A source is dropped on whatever is under the cursor, so a control is only
    // worth aiming at if it can show what landed on it afterwards. A knob wears
    // the ring and a numeric field wears the strip along its foot; a fader, a
    // switch and a mode field draw none of it, which is why the drop is offered
    // to the first two and the rest are still reached from the matrix table.
    //
    // The two tuning fields are the reason this is not just a rule about knobs:
    // they are the only destinations the panel draws as fields, so they are the
    // ones a narrowing of it would silently take the drop away from again.
    const auto destinationOf = [] (const char* id)
    {
        for (int i = 1; i < rhino::forge::destinationCount; ++i)
            if (juce::String(id) == rhino::forge::destinations()[static_cast<size_t>(i)].id)
                return i;
        return 0;
    };
    for (const auto* id : {"oscASemitone", "oscBSemitone"})
    {
        require(destinationOf(id) != 0, "an oscillator's tuning field is a destination");
        auto declared = 0;
        for (const auto& module : modules)
            for (const auto& row : module.rows)
                for (const auto& control : row.controls)
                    if (juce::String(control.id) == id
                        && control.style == rhino::forge::ui::Style::stepper)
                        ++declared;
        require(declared == 1, "an oscillator's tuning field is on the panel as a numeric field");
    }
    require(rhino::forge::ui::showsModulation(rhino::forge::ui::Style::stepper)
                && rhino::forge::ui::showsModulation(rhino::forge::ui::Style::knob)
                && !rhino::forge::ui::showsModulation(rhino::forge::ui::Style::fader)
                && !rhino::forge::ui::showsModulation(rhino::forge::ui::Style::chip),
            "the panel agrees with the look about which styles draw modulation");

    // A control that greys out under another must name a parameter that exists,
    // or the dependency silently never fires.
    for (const auto& module : modules)
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
            {
                if (control.disabledBy != nullptr)
                    require(processor.state.getParameter(control.disabledBy) != nullptr,
                            "a control's disabling parameter exists");
                if (control.enabledBy != nullptr)
                    require(processor.state.getParameter(control.enabledBy) != nullptr,
                            "a control's enabling parameter exists");
                // Both at once would be a control that is never live under one
                // setting and never live under the other.
                require(control.disabledBy == nullptr || control.enabledBy == nullptr,
                        "a control is gated one way or the other, not both");
            }

    // Two controls in one cell are two readings of one setting, and only one of
    // them may ever be on screen. That holds only if they are gated against each
    // other by the same parameter, one each way round â€” otherwise they would be
    // drawn on top of one another.
    for (const auto& module : modules)
    {
        const auto area = rhino::forge::ui::moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& controls = module.rows[static_cast<size_t>(r)].controls;
            for (int c = 0; c < static_cast<int>(controls.size()); ++c)
            {
                if (!controls[static_cast<size_t>(c)].sharesCell) continue;
                require(c > 0, "a shared cell has a control in front of it to share");
                if (c == 0) continue;
                const auto owner = rhino::forge::ui::cellOwner(module, r, c);
                require(rhino::forge::ui::bankOf(module, r, owner)
                            == rhino::forge::ui::bankOf(module, r, c),
                        "a shared cell is shared inside one bank");
                const auto& first = controls[static_cast<size_t>(owner)];
                const auto& second = controls[static_cast<size_t>(c)];
                require(first.disabledBy != nullptr && second.enabledBy != nullptr
                            && juce::String(first.disabledBy) == second.enabledBy,
                        "the two controls in a shared cell are gated by one parameter, one each way");
                require(rhino::forge::ui::cellBounds(area, module, r, c)
                            == rhino::forge::ui::cellBounds(area, module, r, owner),
                        "a shared control lands on the cell it shares");
                require(rhino::forge::ui::inSharedCell(module, r, c)
                            && rhino::forge::ui::inSharedCell(module, r, owner),
                        "both controls in a shared cell know they are sharing one");
            }
        }
    }

    // A module declared in banks shows one at a time, in the same cells, so
    // every bank has to declare the same controls in the same order — otherwise
    // which cell a control lands in would depend on which bank was showing.
    for (const auto& module : modules)
    {
        const auto banks = rhino::forge::ui::bankCount(module);
        for (const auto& row : module.rows)
        {
            require(juce::jmax(1, row.banks) == banks,
                    "every row of a module declares the same number of banks");
            const auto perBank = rhino::forge::ui::controlsPerBank(row);
            require(static_cast<int>(row.controls.size()) == perBank * banks,
                    "a banked row declares a whole number of identical banks");
            for (int bank = 1; bank < banks; ++bank)
                for (int i = 0; i < perBank; ++i)
                {
                    const auto& first = row.controls[static_cast<size_t>(i)];
                    const auto& other = row.controls[static_cast<size_t>(bank * perBank + i)];
                    require(first.style == other.style && first.weight == other.weight
                                && first.sharesCell == other.sharesCell
                                && juce::String(first.label) == other.label,
                            "every bank matches the first in style, width, label and cell sharing");
                    require(juce::String(first.id) != other.id,
                            "no two banks name the same parameter");
                }
        }
        // A banked module with no drag handle draws its own title, and its
        // cards start just past it — so that title has to be short enough to
        // fit the gutter they leave. Three characters at the header's font is
        // what bankTitleGutter covers.
        if (banks > 1 && module.handleSource == 0)
            require(juce::String(module.title).length() <= 3,
                    "a banked module that draws its own title keeps it short enough for its cards");

        // A module that is a source and shows several banks drags a different
        // source per bank, so all of them have to be real sources.
        if (module.handleSource != 0)
            require(module.handleSource + banks - 1 < rhino::forge::modSourceCount,
                    "every bank of a source module names a real modulation source");
    }

    // A control with no tooltip is a control nobody explained. Held to the same
    // standard as a control whose parameter does not exist, because a panel this
    // dense is unusable without them and a silent gap is easy to miss by eye.
    for (const auto& module : modules)
    {
        if (module.enableId != nullptr)
            require(rhino::forge::ui::tooltipFor(module.enableId).isNotEmpty(),
                    "a module's enable has a tooltip");
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
            {
                const auto tip = rhino::forge::ui::tooltipFor(control.id);
                require(tip.isNotEmpty(), "every control has a tooltip");
                if (tip.isEmpty()) std::cerr << "       no tooltip: " << control.id << '\n';
                // A leftover placeholder would pass the emptiness check while
                // saying nothing, so tooltips have to be sentences.
                require(tip.length() > 8, "a tooltip says something");
            }
    }

    // Polyphony means nothing in mono, and the panel has to say so.
    auto polyIsGated = false;
    for (const auto& module : modules)
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
                if (juce::String(control.id) == "polyphony")
                    polyIsGated = control.disabledBy != nullptr
                        && juce::String(control.disabledBy) == "mono";
    require(polyIsGated, "the polyphony knob greys out while mono is on");

    // Conversely, every parameter should be reachable from the panel. A
    // parameter nothing displays is either a bug or dead weight.
    for (const auto* raw : processor.getParameters())
    {
        const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(raw);
        if (withId == nullptr) continue;
        auto found = false;
        for (const auto& module : modules)
        {
            if (module.enableId != nullptr && withId->paramID == module.enableId) found = true;
            for (const auto& row : module.rows)
                for (const auto& control : row.controls)
                    if (withId->paramID == control.id) found = true;
        }
        require(found, "every parameter appears somewhere on the panel");
        if (!found) std::cerr << "       orphan parameter: " << withId->paramID << '\n';
    }

    for (size_t i = 0; i < modules.size(); ++i)
    {
        const auto area = rhino::forge::ui::moduleBounds(bounds, modules[i]);
        require(!area.isEmpty(), "a module occupies a non-empty rectangle");
        require(content.contains(area), "a module stays inside the content area");

        const auto controls = rhino::forge::ui::controlArea(area, modules[i]);
        require(controls.getHeight() > 30, "a module leaves usable height for its controls");
        require(area.withTrimmedTop(rhino::forge::ui::headerHeight).contains(controls),
                "controls stay clear of the module header, so labels cannot collide with the title");

        // A row that has reserved a strip for a display must not have put a
        // control on top of it. The strip and the cells are worked out from
        // one total, so this is what holds that arithmetic honest — and it is
        // checked at every size further down as well, because integer division
        // is exactly where the two would drift apart.
        for (int r = 0; r < static_cast<int>(modules[i].rows.size()); ++r)
        {
            const auto strip = rhino::forge::ui::rowDisplayBounds(area, modules[i], r);
            if (strip.isEmpty()) continue;
            require(rhino::forge::ui::rowBounds(area, modules[i], r).contains(strip),
                    "a row's display strip stays inside that row");
            const auto shared = rhino::forge::ui::uniformKnobDiameter(bounds);
            const auto& controls = modules[i].rows[static_cast<size_t>(r)].controls;
            for (int c = 0; c < static_cast<int>(controls.size()); ++c)
                require(!strip.intersects(
                            rhino::forge::ui::controlBlock(area, modules[i], r, c, shared)),
                        "no control sits on top of its row's display strip");
        }

        // Rows within a module must tile their area without overlapping either.
        for (int r = 0; r < static_cast<int>(modules[i].rows.size()); ++r)
        {
            const auto row = rhino::forge::ui::rowBounds(area, modules[i], r);
            require(controls.contains(row), "a control row stays inside its module");
            for (int s = r + 1; s < static_cast<int>(modules[i].rows.size()); ++s)
                require(!row.intersects(rhino::forge::ui::rowBounds(area, modules[i], s)),
                        "no two control rows in a module overlap");
        }

        // Two modules may share a rectangle as long as no tab shows them both:
        // that is exactly what the oscillators and the matrix do.
        for (size_t j = i + 1; j < modules.size(); ++j)
            if (rhino::forge::ui::sharePage(modules[i], modules[j]))
                require(!area.intersects(rhino::forge::ui::moduleBounds(bounds, modules[j])),
                        "no two modules shown together overlap");
    }

    // Every tab has to put something on screen, and the tabs themselves have to
    // stay clear of each other in the title bar.
    for (const auto page : rhino::forge::ui::tabPages)
    {
        // A module of its own: one this tab shows and one that is not simply
        // on every tab. A tab whose every module would have been on screen
        // anyway is a tab that does nothing.
        auto shown = 0;
        for (const auto& module : modules)
            if (rhino::forge::ui::onPage(module, page)
                && module.pages != rhino::forge::ui::everyPage) ++shown;
        require(shown > 0, "every tab shows at least one module of its own");
    }
    for (int i = 1; i < rhino::forge::ui::tabCount; ++i)
        require(!rhino::forge::ui::tabBounds(i - 1).intersects(rhino::forge::ui::tabBounds(i)),
                "no two tabs overlap");
    // The strip grows by a tab every time a page is added, and the preset name
    // and buttons are laid out from the right edge — so the two meet in the
    // middle at the narrowest window the panel allows, and nowhere else. The
    // fifth tab came within a hair of this, which is why the check exists.
    require(rhino::forge::ui::tabBounds(rhino::forge::ui::tabCount - 1).getRight()
                < rhino::forge::ui::minPanelWidth - rhino::forge::ui::windowMargin
                  - rhino::forge::ui::presetStripWidth,
            "the tab strip stays clear of the preset controls at the narrowest window");

    // A table names its columns once, above its rows, so every row has to have
    // the same controls in the same order as the first or the titles lie.
    for (const auto& module : modules)
    {
        if (module.columnHeaderHeight <= 0) continue;
        const auto area = rhino::forge::ui::moduleBounds(bounds, module);
        const auto titles = rhino::forge::ui::columnTitleBounds(area, module);
        require(!titles.isEmpty(), "a table reserves a strip for its column titles");
        require(!titles.intersects(rhino::forge::ui::controlArea(area, module)),
                "a table's column titles sit clear of its rows");
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            require(!rhino::forge::ui::rowGutterBounds(area, module, r)
                         .intersects(rhino::forge::ui::rowBounds(area, module, r)),
                    "a table's row numbers sit clear of its controls");
        const auto& first = module.rows.front().controls;
        for (const auto& row : module.rows)
        {
            require(row.controls.size() == first.size(), "every table row has the same columns");
            for (size_t c = 0; c < row.controls.size() && c < first.size(); ++c)
                require(row.controls[c].style == first[c].style && row.controls[c].weight == first[c].weight,
                        "a table column keeps its style and its width down every row");
        }
    }

    // Every knob on the panel is the same size, whichever module it sits in.
    const auto diameter = rhino::forge::ui::uniformKnobDiameter(bounds);
    require(diameter >= 48, "the shared knob diameter stays usable");
    for (const auto& module : modules)
    {
        const auto area = rhino::forge::ui::moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& row = module.rows[static_cast<size_t>(r)];
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                const auto block = rhino::forge::ui::controlBlock(area, module, r, i, diameter);
                require(!block.isEmpty(), "every control gets a non-empty rectangle");
                require(area.contains(block), "every control stays inside its module");
                if (row.controls[static_cast<size_t>(i)].style != rhino::forge::ui::Style::knob)
                    continue;
                if (module.compactKnobs)
                {
                    // A compact module opts out of the shared size on purpose,
                    // but its knobs still have to be smaller, not larger, and
                    // still have to be usable. Its block is wider than the
                    // circle in it — the readout needs the room — so the circle
                    // is read back from the block's height, not its width.
                    const auto circle = rhino::forge::ui::knobDiameterOf(block);
                    require(circle <= diameter, "a compact knob is no larger than the shared diameter");
                    require(circle >= 24, "a compact knob stays usable");
                    require(block.getWidth() >= circle,
                            "a compact knob's readout is at least as wide as its circle");
                }
                else
                {
                    require(block.getWidth() == diameter, "every ordinary knob is drawn at the shared diameter");
                }
            }
        }
    }

    // The proportions have to survive the whole resize range, not just the
    // default size.
    // Swept rather than sampled at the corners. Integer division means the
    // geometry can go wrong at one awkward size while both extremes are fine,
    // and a module clipping at some width nobody happened to try is exactly the
    // kind of thing an eye test misses.
    for (int width = rhino::forge::ui::minPanelWidth; width <= rhino::forge::ui::maxPanelWidth; width += 20)
        for (int height = rhino::forge::ui::minPanelHeight; height <= rhino::forge::ui::maxPanelHeight; height += 20)
        {
            const auto resized = juce::Rectangle<int>(0, 0, width, height);
            const auto area = rhino::forge::ui::contentBounds(resized);
            const auto diameter = rhino::forge::ui::uniformKnobDiameter(resized);
            if (diameter < 40)
            {
                require(false, "knobs stay usable at every allowed size");
                std::cerr << "       at " << width << "x" << height << '\n';
            }

            for (size_t i = 0; i < modules.size(); ++i)
            {
                const auto box = rhino::forge::ui::moduleBounds(resized, modules[i]);
                if (!area.contains(box))
                {
                    require(false, "a module stays inside the content area at every allowed size");
                    std::cerr << "       " << modules[i].id << " at " << width << "x" << height << '\n';
                }
                if (rhino::forge::ui::controlArea(box, modules[i]).getHeight() <= 24)
                {
                    require(false, "controls stay usable at every allowed size");
                    std::cerr << "       " << modules[i].id << " at " << width << "x" << height << '\n';
                }
                // Overlap has to hold at every size too, not only at the one the
                // panel was designed against.
                for (size_t j = i + 1; j < modules.size(); ++j)
                {
                    if (!rhino::forge::ui::sharePage(modules[i], modules[j])) continue;
                    if (box.intersects(rhino::forge::ui::moduleBounds(resized, modules[j])))
                    {
                        require(false, "no two modules shown together overlap at any allowed size");
                        std::cerr << "       " << modules[i].id << " and " << modules[j].id
                                  << " at " << width << "x" << height << '\n';
                    }
                }

                // And every control still lands inside the module that owns it.
                for (int r = 0; r < static_cast<int>(modules[i].rows.size()); ++r)
                    for (int c = 0; c < static_cast<int>(modules[i].rows[static_cast<size_t>(r)].controls.size()); ++c)
                    {
                        const auto block = rhino::forge::ui::controlBlock(box, modules[i], r, c, diameter);
                        if (block.isEmpty() || !box.contains(block))
                        {
                            require(false, "every control stays inside its module at any allowed size");
                            std::cerr << "       " << modules[i].id << " control " << c
                                      << " at " << width << "x" << height << '\n';
                        }
                    }
            }
        }
}

// ------------------------------------------------------------- fx displays ---

// A slot's display claims to be drawn from the arithmetic the slot actually
// runs. These check that claim where it can be checked exactly: a curve is
// compared against the very function the engine calls, not against a picture of
// what the effect usually looks like.
//
// It is the same standard the filter module's display is held to below, and for
// the same reason — a display that is merely plausible is worse than none,
// because it is believed.
void fxDisplaySuite()
{
    namespace ui = rhino::forge::ui;
    using rhino::forge::FxType;

    // --- The equaliser ---------------------------------------------------------
    //
    // A band's magnitude is read off the coefficients setBand built, so a band
    // asked for no gain at all has to measure as no gain at all — at every
    // frequency, not only at its corner. This is what would catch a shelf built
    // from the wrong cookbook formula: it would still look like a shelf.
    {
        constexpr auto rate = 48000.0;
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(FxType::equaliser)];
        rhino::forge::FxSlot flat;
        flat.type = static_cast<float>(FxType::equaliser);
        flat.knobs = info.init;
        requireClose(rhino::forge::fxScaled(flat.knobs[2], -18.0f, 18.0f), 0.0f, 0.01f,
                     "the equaliser this checks really is asking for no gain");

        rhino::forge::Biquad low, high;
        rhino::forge::setBand(low, rhino::forge::BandShape::lowShelf,
                              rhino::forge::fxHertz(flat.knobs[0], 20.0f, 2000.0f),
                              rhino::forge::fxScaled(flat.knobs[1], 0.2f, 6.0f),
                              rhino::forge::fxScaled(flat.knobs[2], -18.0f, 18.0f), rate);
        rhino::forge::setBand(high, rhino::forge::BandShape::highShelf,
                              rhino::forge::fxHertz(flat.knobs[3], 500.0f, 18000.0f),
                              rhino::forge::fxScaled(flat.knobs[4], 0.2f, 6.0f),
                              rhino::forge::fxScaled(flat.knobs[5], -18.0f, 18.0f), rate);
        for (const auto hz : {30.0f, 120.0f, 440.0f, 2000.0f, 9000.0f, 17000.0f})
        {
            const auto gain = ui::biquadMagnitude(low, hz, rate) * ui::biquadMagnitude(high, hz, rate);
            requireClose(gain, 1.0f, 0.01f, "an equaliser opening flat measures flat at every frequency");
        }

        // A shelf asked for a boost has to measure as one below its corner and
        // as nothing well above it, or the display is drawing the wrong band.
        rhino::forge::Biquad boosted;
        rhino::forge::setBand(boosted, rhino::forge::BandShape::lowShelf, 200.0f, 0.7f, 12.0f, rate);
        require(ui::biquadMagnitude(boosted, 30.0f, rate) > 3.0f,
                "a low shelf asked for +12 dB lifts what is under it");
        requireClose(ui::biquadMagnitude(boosted, 12000.0f, rate), 1.0f, 0.05f,
                     "a low shelf leaves what is well above it alone");
    }

    // --- The distortion --------------------------------------------------------
    //
    // The transfer curve is fxShape called per pixel, so the two cannot disagree
    // by construction — what is worth checking is that the shapes behave the way
    // a curve drawn from them would be read: passing through the origin, odd
    // about it where they claim to be, and never leaving the box.
    {
        for (int shape = 0; shape < 8; ++shape)
        {
            if (shape == 7) continue;   // downsampling is a rate, not a curve
            for (const auto drive : {0.0f, 0.4f, 1.0f})
            {
                requireClose(rhino::forge::fxShape(shape, 0.0f, drive), 0.0f, 0.001f,
                             "a distortion shape leaves silence silent");
                for (const auto in : {-1.0f, -0.6f, -0.2f, 0.2f, 0.6f, 1.0f})
                {
                    const auto out = rhino::forge::fxShape(shape, in, drive);
                    if (!std::isfinite(out) || std::abs(out) > 1.001f)
                    {
                        require(false, "a distortion shape stays inside the box its curve is drawn in");
                        std::cerr << "       shape " << shape << " drive " << drive
                                  << " in " << in << " out " << out << '\n';
                    }
                }
            }
        }
        // Hard clipping at no drive is the one shape that is exactly the
        // diagonal the display draws behind every curve, which makes it the
        // check that the diagonal means what it claims.
        for (const auto in : {-0.9f, -0.3f, 0.3f, 0.9f})
            requireClose(rhino::forge::fxShape(2, in, 0.0f), in, 0.001f,
                         "hard clipping at no drive is the identity the faint diagonal stands for");
    }

    // --- The delay -------------------------------------------------------------
    //
    // The repeats are placed by the same fxDelaySeconds the line is read at, so
    // what is checked is that a synced delay lands on the beat it names.
    {
        rhino::forge::FxSlot synced;
        synced.type = static_cast<float>(FxType::delay);
        synced.modeB = 1.0f;   // BPM rather than milliseconds
        // The division a knob lands on, and the time that division is at 120.
        for (int step = 0; step < rhino::forge::fxDivisionCount; ++step)
        {
            const auto at = static_cast<float>(step) / (rhino::forge::fxDivisionCount - 1);
            synced.knobs[0] = at;
            const auto& division = rhino::forge::fxDivisionAt(at);
            const auto expected = juce::jlimit(0.001f, rhino::forge::fxMaxDelayTime,
                                               0.5f * division.beats);
            requireClose(rhino::forge::fxDelaySeconds(synced, 120.0), expected, 0.0005f,
                         "a synced delay lands on the division its readout names");
        }
    }

    // --- The reverb ------------------------------------------------------------
    //
    // The envelope is decay raised to the number of comb round trips. A longer
    // decay setting therefore has to give a longer tail, and a hall a longer one
    // than a plate at the same setting — which is the whole of what the two
    // types differ by in renderReverb.
    {
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(FxType::reverb)];
        const auto plate = rhino::forge::fxScaled(0.5f, 0.62f, 0.9f);
        const auto hall = rhino::forge::fxScaled(0.5f, 0.62f, 0.96f);
        require(hall > plate, "a hall holds its energy longer than a plate at the same setting");
        require(rhino::forge::fxScaled(1.0f, 0.62f, 0.9f) > rhino::forge::fxScaled(0.0f, 0.62f, 0.9f),
                "turning the decay up lengthens the tail");
        require(info.init[1] > 0.0f && info.init[1] < 1.0f,
                "a reverb opens somewhere a decay can be read from");
    }

    // --- Reading a parameter back while it is still being announced ------------
    //
    // The panel used to read parameter values from the cached atomic beside
    // them, and a mode field appeared to wait for an unrelated click before it
    // caught up. This is why: that atomic is kept up to date by one of the
    // parameter's own listeners, and JUCE calls listeners in the reverse of the
    // order they registered. A panel attachment registers after the state does,
    // so it is called first — and reads the value from before the change it is
    // being told about.
    //
    // What the panel reads now is the parameter itself, which stores its value
    // before it tells anybody. This pins that property rather than the panel
    // that depends on it, because the property is the whole of the fix.
    {
        rhino::forge::Processor processor;
        auto* parameter = processor.state.getParameter("fx1s1ModeA");
        require(parameter != nullptr, "the parameter this checks exists");
        if (parameter != nullptr)
        {
            struct Watcher final : juce::AudioProcessorParameter::Listener
            {
                Watcher(rhino::forge::Processor& p, juce::RangedAudioParameter& r)
                    : processor(p), ranged(r) { ranged.addListener(this); }
                ~Watcher() override { ranged.removeListener(this); }
                void parameterValueChanged(int, float) override
                {
                    ++calls;
                    fromParameter = ranged.convertFrom0to1(ranged.getValue());
                    const auto* atomic = processor.state.getRawParameterValue("fx1s1ModeA");
                    fromAtomic = atomic == nullptr ? -1.0f : atomic->load();
                }
                void parameterGestureChanged(int, bool) override {}
                rhino::forge::Processor& processor;
                juce::RangedAudioParameter& ranged;
                int calls = 0;
                float fromParameter = -1.0f, fromAtomic = -1.0f;
            };

            Watcher watcher(processor, *parameter);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(1.0f));
            require(watcher.calls > 0, "setting a parameter tells its listeners");
            requireClose(watcher.fromParameter, 1.0f, 0.001f,
                         "a parameter read inside its own announcement is already the new value");
            // The atomic is allowed to be either, and saying which it was makes
            // the reason for the fix visible when this is read later.
            if (std::abs(watcher.fromAtomic - 1.0f) > 0.001f)
                std::cerr << "       (the cached atomic was still "
                          << watcher.fromAtomic << " at that moment, which is the race)" << '\n';
        }
    }

    // --- A mode field, read back the way it is written -------------------------
    //
    // The parameter behind a mode is a plain 0..1, because what it steps
    // through changes with the type. The panel spreads a choice across that
    // range and fxModeOf reads it back; if the two ever disagreed, a field
    // would show one state and the engine would run another. So every choice of
    // every mode of every type is written and read here.
    for (int type = 0; type < rhino::forge::fxTypeCount; ++type)
    {
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(type)];
        for (const auto* mode : {&info.modeA, &info.modeB})
        {
            if (mode->count <= 1) continue;
            for (int choice = 0; choice < mode->count; ++choice)
            {
                const auto at = static_cast<float>(choice) / static_cast<float>(mode->count - 1);
                if (rhino::forge::fxModeOf(*mode, at) != choice)
                {
                    require(false, "a mode choice reads back as the one that was set");
                    std::cerr << "       " << info.name << " choice " << choice
                              << " of " << mode->count << '\n';
                }
            }
            // Every choice a field offers has to be named, or the selector
            // draws an empty segment.
            for (int choice = 0; choice < mode->count; ++choice)
                require(mode->choices[static_cast<size_t>(choice)] != nullptr
                            && juce::String(mode->choices[static_cast<size_t>(choice)]).isNotEmpty(),
                        "every choice a mode offers has a name to draw");
            require(mode->label != nullptr, "a mode field that has choices has a label");
        }
    }

    // --- A stack of choices, at the size the panel gives it ---------------------
    //
    // The choices are stacked, so the field has to divide into as many rows as
    // the most any mode offers and every one of them still be readable. Checked
    // as geometry rather than by eye: the three-choice case is the tight one,
    // and it is tight at the smallest window rather than at the default.
    {
        const auto tightest = juce::Rectangle<int>(0, 0, rhino::forge::ui::minPanelWidth,
                                                   rhino::forge::ui::minPanelHeight);
        const auto shared = rhino::forge::ui::uniformKnobDiameter(tightest);
        for (const auto& module : rhino::forge::ui::modules())
        {
            const auto area = rhino::forge::ui::moduleBounds(tightest, module);
            for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            {
                const auto& controls = module.rows[static_cast<size_t>(r)].controls;
                for (int c = 0; c < static_cast<int>(controls.size()); ++c)
                {
                    if (controls[static_cast<size_t>(c)].style != rhino::forge::ui::Style::selector)
                        continue;
                    const auto block = rhino::forge::ui::controlBlock(area, module, r, c, shared);
                    const auto field = block.withTrimmedTop(rhino::forge::ui::stepperLabelHeight);

                    rhino::forge::ui::FxSelector selector;
                    selector.setBounds(field);
                    // A field's geometry follows how many choices it is holding,
                    // so it is given some. Nothing here draws them.
                    const auto holding = [&selector] (int howMany)
                    {
                        selector.choices.assign(static_cast<size_t>(howMany), "X");
                    };
                    // Every mode field that stacks its choices, at its widest.
                    for (int count = 2; count <= rhino::forge::ui::FxSelector::inlineLimit; ++count)
                    {
                        holding(count);
                        auto covered = 0;
                        for (int i = 0; i < count; ++i)
                        {
                            const auto segment = selector.segmentBounds(i);
                            require(segment.getHeight() >= 14,
                                    "a stacked choice stays tall enough to read");
                            require(segment.getWidth() == field.getWidth(),
                                    "a stacked choice takes the whole width of its field");
                            covered += segment.getHeight();
                            for (int j = i + 1; j < count; ++j)
                                require(!segment.intersects(selector.segmentBounds(j)),
                                        "no two stacked choices overlap");
                        }
                        require(covered == field.getHeight(),
                                "the stack fills its field exactly, with no gap and no overhang");
                    }
                    // A field with more choices than fit draws one line instead,
                    // and it has to sit inside the same box.
                    holding(rhino::forge::warpModeCount);
                    require(field.withZeroOrigin().contains(selector.listBounds()),
                            "a field too long to stack draws a line inside the box it was given");
                }
            }
        }
    }

    // --- What a mode makes meaningless -----------------------------------------
    //
    // Three rules, each a fact about the effect rather than about the panel.
    // Checked both ways round, because a rule that greys a knob and never
    // ungreys it looks exactly like one that works.
    {
        rhino::forge::FxSlot distortion;
        distortion.type = static_cast<float>(FxType::distortion);
        const auto& dist = rhino::forge::fxTypes()[static_cast<size_t>(FxType::distortion)];
        distortion.modeB = 0.0f;   // FILTER OFF
        require(rhino::forge::fxKnobLive(distortion, 0), "DRIVE is live whatever the filter is doing");
        require(!rhino::forge::fxKnobLive(distortion, 1),
                "a distortion's FREQ is dead while its filter is switched off");
        require(!rhino::forge::fxKnobLive(distortion, 2),
                "a distortion's Q is dead while its filter is switched off");
        distortion.modeB = 1.0f / static_cast<float>(dist.modeB.count - 1);   // PRE
        require(rhino::forge::fxKnobLive(distortion, 1),
                "a distortion's FREQ comes back once the filter is in the path");

        rhino::forge::FxSlot eq;
        eq.type = static_cast<float>(FxType::equaliser);
        const auto& bands = rhino::forge::fxTypes()[static_cast<size_t>(FxType::equaliser)];
        eq.modeA = 0.0f;   // SHELF
        require(rhino::forge::fxKnobLive(eq, 2), "a shelf has a gain to set");
        eq.modeA = 1.0f;   // the last choice, HI PASS
        require(!rhino::forge::fxKnobLive(eq, 2), "a high pass has no gain to set");
        require(rhino::forge::fxKnobLive(eq, 0), "a high pass still has a frequency to set");
        eq.modeB = 1.0f;   // LO PASS
        require(!rhino::forge::fxKnobLive(eq, 5), "a low pass has no gain to set");
        juce::ignoreUnused(bands);

        // Every other type leaves every knob alone, so a rule added by accident
        // to one of them is caught rather than merely unnoticed.
        for (const auto type : {FxType::reverb, FxType::delay, FxType::chorus, FxType::filter})
        {
            rhino::forge::FxSlot other;
            other.type = static_cast<float>(type);
            for (const auto mode : {0.0f, 0.5f, 1.0f})
            {
                other.modeA = mode;
                other.modeB = mode;
                for (int knob = 0; knob < rhino::forge::fxKnobCount; ++knob)
                    require(rhino::forge::fxKnobLive(other, knob),
                            "a type with no such rule leaves all of its knobs live");
            }
        }
    }

    // --- The strip they are drawn in -------------------------------------------
    //
    // Every rack row reserves one, and nothing else on the panel does. A module
    // that grew a display strip without meaning to would be caught here.
    {
        const auto bounds = juce::Rectangle<int>(0, 0, rhino::forge::ui::defaultPanelWidth,
                                                 rhino::forge::ui::defaultPanelHeight);
        auto strips = 0;
        for (const auto& module : rhino::forge::ui::modules())
        {
            const auto area = rhino::forge::ui::moduleBounds(bounds, module);
            for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            {
                const auto strip = rhino::forge::ui::rowDisplayBounds(area, module, r);
                if (strip.isEmpty()) continue;
                ++strips;
                require(strip.getWidth() > 40 && strip.getHeight() > 20,
                        "a display strip is big enough to draw a curve in");
                require(juce::String(module.id) == "fx",
                        "only the rack reserves a strip of a row for a display");
            }
        }
        require(strips == rhino::forge::fxSlotCount,
                "every slot of the rack has a display strip and no row has two");
    }
}

// ---------------------------------------------------------- filter display ---

// The filter display claims to say which frequencies are being taken out, so
// what it draws has to be the response Core actually has rather than a picture
// of a filter in general. These check the curve through the geometry it is
// drawn from: gain read back at a frequency, and frequency read back off the
// axis it is plotted against.
void filterDisplaySuite()
{
    namespace ui = rhino::forge::ui;
    using rhino::forge::FilterType;
    const auto box = juce::Rectangle<float>(0.0f, 0.0f, 300.0f, 120.0f);

    // The axis is logarithmic, so a frequency put on it and read back off it
    // has to come back unchanged. Everything else here depends on that holding.
    for (const auto hz : {20.0f, 55.0f, 440.0f, 1000.0f, 7800.0f, 20000.0f})
        requireClose(ui::filterXToHz(box, ui::filterHzToX(box, hz)), hz, hz * 0.001f,
                     "a frequency read back off the axis is the one that was plotted");

    // Log, not linear: an octave takes the same width wherever it sits.
    const auto octaveLow = ui::filterHzToX(box, 200.0f) - ui::filterHzToX(box, 100.0f);
    const auto octaveHigh = ui::filterHzToX(box, 8000.0f) - ui::filterHzToX(box, 4000.0f);
    requireClose(octaveLow, octaveHigh, 0.01f, "every octave is the same width on the axis");

    // The panel and the engine have to agree about what the resonance knob
    // does, or the curve is of some other filter.
    requireClose(ui::filterDamping(0.0f), 1.0f, 0.0001f, "no resonance is full damping");
    requireClose(ui::filterDamping(1.0f), 1.0f / 16.0f, 0.0001f, "full resonance is Core's least damping");

    const auto cutoff = 1000.0f;
    const auto quiet = 0.0f;

    // Each tap passes its own end of the band and stops the other. Two decades
    // either side of the corner, which is well clear of the knee.
    require(ui::filterMagnitudeDb(FilterType::lowPass, cutoff, quiet, 10.0f) > -3.0f,
            "a low pass leaves the bottom of the band alone");
    require(ui::filterMagnitudeDb(FilterType::lowPass, cutoff, quiet, 100000.0f) < -40.0f,
            "a low pass takes the top of the band out");
    require(ui::filterMagnitudeDb(FilterType::highPass, cutoff, quiet, 100000.0f) > -3.0f,
            "a high pass leaves the top of the band alone");
    require(ui::filterMagnitudeDb(FilterType::highPass, cutoff, quiet, 10.0f) < -40.0f,
            "a high pass takes the bottom of the band out");
    require(ui::filterMagnitudeDb(FilterType::bandPass, cutoff, quiet, 10.0f) < -20.0f
                && ui::filterMagnitudeDb(FilterType::bandPass, cutoff, quiet, 100000.0f) < -20.0f,
            "a band pass takes both ends out");

    // A second-order response, so it falls twelve decibels an octave away from
    // the corner. Measured two octaves out, where the knee is long behind it.
    const auto atFour = ui::filterMagnitudeDb(FilterType::lowPass, cutoff, quiet, cutoff * 4.0f);
    const auto atEight = ui::filterMagnitudeDb(FilterType::lowPass, cutoff, quiet, cutoff * 8.0f);
    requireClose(atFour - atEight, 12.0f, 0.6f, "the skirt falls twelve decibels an octave");

    // Resonance is a peak at the corner, and it only ever adds.
    for (const auto type : {FilterType::lowPass, FilterType::highPass, FilterType::bandPass})
    {
        const auto flat = ui::filterMagnitudeDb(type, cutoff, 0.0f, cutoff);
        const auto peaked = ui::filterMagnitudeDb(type, cutoff, 0.9f, cutoff);
        require(peaked > flat + 6.0f, "resonance lifts the corner");
        require(peaked <= ui::filterTopDb, "a resonant peak stays inside the window it is drawn in");
    }
    // And the peak of a band pass is the corner itself, not somewhere else.
    const auto atCorner = ui::filterMagnitudeDb(FilterType::bandPass, cutoff, 0.5f, cutoff);
    require(atCorner > ui::filterMagnitudeDb(FilterType::bandPass, cutoff, 0.5f, cutoff * 1.5f)
                && atCorner > ui::filterMagnitudeDb(FilterType::bandPass, cutoff, 0.5f, cutoff / 1.5f),
            "a band pass peaks at the frequency the knob is holding");

    // Nothing the knobs can reach may draw outside the well, at any size the
    // panel allows. The curve is clipped to the display when it is painted, but
    // a curve that needed clipping to stay inside would be one the window is
    // the wrong shape for.
    for (const auto& module : ui::modules())
    {
        if (module.display != ui::Display::filter) continue;
        for (int width = ui::minPanelWidth; width <= ui::maxPanelWidth; width += 40)
            for (int height = ui::minPanelHeight; height <= ui::maxPanelHeight; height += 40)
            {
                const auto area = ui::moduleBounds({0, 0, width, height}, module);
                const auto display = ui::displayBounds(area, module);
                const auto plot = display.toFloat().reduced(0.0f, 6.0f);
                if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
                {
                    require(false, "the filter display has room to draw in at every allowed size");
                    std::cerr << "       at " << width << "x" << height << '\n';
                    continue;
                }
                for (const auto type : {FilterType::lowPass, FilterType::highPass, FilterType::bandPass})
                    for (const auto corner : {30.0f, 1000.0f, 18000.0f})
                        for (const auto resonance : {0.0f, 0.5f, 1.0f})
                        {
                            const auto bounds = ui::filterResponsePath(plot, type, corner, resonance)
                                                    .getBounds();
                            if (!plot.expanded(0.5f).contains(bounds))
                            {
                                require(false, "the response stays inside the well it is drawn in");
                                std::cerr << "       " << corner << " Hz res " << resonance
                                          << " at " << width << "x" << height << '\n';
                            }
                        }
            }
    }

    // The reading beside the corner marker is the frequency the knob holds, in
    // the units it is read in.
    require(ui::filterHzText(440.0f) == "440 Hz", "a corner under a kilohertz is named in hertz");
    require(ui::filterHzText(7800.0f) == "7.80 kHz", "a corner over a kilohertz is named in kilohertz");
    require(ui::filterHzText(18000.0f) == "18.0 kHz", "a corner over ten kilohertz drops a decimal");
}

// ------------------------------------------------------- envelope display ---

// The envelope display used to stretch whatever A/D/S/R it was given across the
// full width of its well, so a 51 ms envelope and a 5.1 s one drew the same
// picture and the only display in the panel whose job is to show duration
// showed none. These check the geometry rather than the pixels: what the shape
// claims about time, read back through the axis it is drawn against.
void envelopeDisplaySuite()
{
    namespace ui = rhino::forge::ui;
    const auto box = juce::Rectangle<float>(0.0f, 0.0f, 400.0f, 100.0f);

    // Read a corner back as the time it stands for. The whole point of the
    // display is that this round-trip holds.
    const auto secondsAt = [&box] (const ui::EnvelopeShape& shape, float x)
    {
        return (x - box.getX()) / box.getWidth() * shape.axis.seconds;
    };

    {
        // The window the display opens on, which is a fixed three seconds and
        // not something derived from the patch.
        requireClose(ui::envelopeAxis(ui::envelopeDefaultZoom).seconds, 3.0f, 0.0001f,
                     "the display opens on a three second window");
        requireClose(ui::envelopeAxis(ui::envelopeDefaultZoom).mark, 1.0f, 0.0001f,
                     "a three second window is marked off in seconds");
        const auto quiet = ui::envelopeShape(box, 0.05f, 0.1f, 0.5f, 0.2f);
        const auto busy = ui::envelopeShape(box, 2.0f, 0.5f, 0.5f, 0.4f);
        requireClose(quiet.axis.seconds, busy.axis.seconds, 0.0001f,
                     "the window does not move when the envelope does");
    }

    {
        // The patch from the bug report: a short percussive envelope with
        // sustain wide open. It has to read as short.
        const auto shape = ui::envelopeShape(box, 0.030f, 0.155f, 1.0f, 0.021f);
        requireClose(secondsAt(shape, shape.attackX), 0.030f, 0.0005f,
                     "the peak lands at the attack time");
        requireClose(secondsAt(shape, shape.decayX), 0.185f, 0.0005f,
                     "the sustain corner lands at the attack plus the decay");
        requireClose(secondsAt(shape, shape.endX), 0.206f, 0.0005f,
                     "a 206 ms envelope ends 206 ms along the axis");
        require(shape.endX < box.getX() + box.getWidth() * 0.1f,
                "a short envelope is a sliver against the left edge, not a shape filling the well");
        requireClose(shape.sustainY, shape.peakY, 0.001f,
                     "full sustain sits at the top of the well");
    }

    {
        // The same envelope ten times over. The old display drew these two
        // identically; they must now differ.
        const auto brief = ui::envelopeShape(box, 0.030f, 0.155f, 1.0f, 0.021f);
        const auto long_ = ui::envelopeShape(box, 0.300f, 1.550f, 1.0f, 0.210f);
        require(long_.endX > brief.endX * 9.0f,
                "an envelope ten times longer is drawn about ten times wider");
        requireClose(secondsAt(long_, long_.endX), 2.060f, 0.002f,
                     "a 2.06 s envelope ends 2.06 s along the axis");
    }

    {
        // Stages keep their proportions to each other: decay twice the attack
        // is drawn twice as wide.
        const auto shape = ui::envelopeShape(box, 0.05f, 0.10f, 0.5f, 0.05f);
        const auto attackWidth = shape.attackX - box.getX();
        requireClose(shape.decayX - shape.attackX, attackWidth * 2.0f, 0.01f,
                     "a decay twice the attack is drawn twice as wide");
        requireClose(shape.endX - shape.decayX, attackWidth, 0.01f,
                     "a release equal to the attack is drawn the same width");
        requireClose(shape.sustainY, (shape.floorY + shape.peakY) * 0.5f, 0.01f,
                     "half sustain sits halfway up the well");
    }

    {
        // The two ends of the sustain range, which used to collapse a segment to
        // nothing and leave the knob behind it apparently dead. Sustain sets a
        // level, so it must change the height of the sustain corner and nothing
        // whatever about where the corners sit along the axis.
        const auto full = ui::envelopeShape(box, 0.2f, 0.4f, 1.0f, 0.6f);
        const auto none = ui::envelopeShape(box, 0.2f, 0.4f, 0.0f, 0.6f);
        const auto half = ui::envelopeShape(box, 0.2f, 0.4f, 0.5f, 0.6f);

        for (const auto& shape : {full, none, half})
        {
            requireClose(secondsAt(shape, shape.attackX), 0.2f, 0.001f,
                         "the peak is at the attack time whatever sustain is");
            requireClose(secondsAt(shape, shape.decayX), 0.6f, 0.001f,
                         "the decay is drawn its full width whatever sustain is");
            requireClose(secondsAt(shape, shape.endX), 1.2f, 0.001f,
                         "the release is drawn its full width whatever sustain is");
        }
        requireClose(full.sustainY, full.peakY, 0.001f,
                     "full sustain sits at the top of the well");
        requireClose(none.sustainY, none.floorY, 0.001f,
                     "no sustain sits on the floor of the well");
        requireClose(half.sustainY, (half.floorY + half.peakY) * 0.5f, 0.01f,
                     "half sustain sits halfway up the well");

        // And the height has to arrive there continuously: turning sustain up to
        // the stop used to snap the decay from its whole width to none.
        const auto nearlyFull = ui::envelopeShape(box, 0.2f, 0.4f, 0.999f, 0.6f);
        const auto nearlyNone = ui::envelopeShape(box, 0.2f, 0.4f, 0.001f, 0.6f);
        requireClose(nearlyFull.decayX, full.decayX, 0.001f,
                     "the last thousandth of sustain does not move the decay corner");
        requireClose(nearlyNone.endX, none.endX, 0.001f,
                     "the first thousandth of sustain does not move the end of the release");
    }

    {
        // Zooming changes the window and nothing else. The same envelope has to
        // come back at the same times through whichever axis it was drawn
        // against, and a shorter window has to draw it wider.
        auto previous = 0.0f;
        for (int zoom = 0; zoom < ui::envelopeZoomCount; ++zoom)
        {
            const auto axis = ui::envelopeAxis(zoom);
            if (axis.seconds <= previous)
                require(false, "the zoom ladder runs from the shortest window to the longest");
            previous = axis.seconds;
            if (ui::envelopeAxis(zoom).mark > axis.seconds)
                require(false, "a window is never shorter than the marks dividing it");

            const auto shape = ui::envelopeShape(box, 0.1f, 0.2f, 0.5f, 0.3f, zoom);
            requireClose(secondsAt(shape, shape.endX), 0.6f, 0.001f,
                         "an envelope reads back as its own length at every zoom");
        }

        // Out of range on either side is held at the end of the ladder rather
        // than wrapping or dividing by a window of nothing.
        requireClose(ui::envelopeAxis(-5).seconds, ui::envelopeZooms[0].seconds, 0.0001f,
                     "zooming past the shortest window stops there");
        requireClose(ui::envelopeAxis(99).seconds,
                     ui::envelopeZooms[ui::envelopeZoomCount - 1].seconds, 0.0001f,
                     "zooming past the longest window stops there");

        const auto tight = ui::envelopeShape(box, 0.1f, 0.2f, 0.5f, 0.3f, ui::envelopeDefaultZoom - 1);
        const auto wide = ui::envelopeShape(box, 0.1f, 0.2f, 0.5f, 0.3f, ui::envelopeDefaultZoom);
        require(tight.endX > wide.endX,
                "a shorter window draws the same envelope wider");
    }

    // Across the whole range the knobs allow, and at every zoom, the corners
    // stay in order and the sustain level stays between the floor and the peak.
    // Corners are free to land past the right-hand edge now: an envelope longer
    // than the window is cut by the frame, and that is the reading.
    for (float attack = 0.001f; attack <= 4.0f; attack += 0.37f)
        for (float decay = 0.001f; decay <= 4.0f; decay += 0.53f)
            for (float release = 0.001f; release <= 8.0f; release += 0.91f)
                for (float sustain = 0.0f; sustain <= 1.0f; sustain += 0.25f)
                    for (int zoom = 0; zoom < ui::envelopeZoomCount; ++zoom)
                    {
                        const auto shape = ui::envelopeShape(box, attack, decay, sustain, release, zoom);
                        if (!(box.getX() <= shape.attackX && shape.attackX <= shape.decayX
                              && shape.decayX <= shape.endX))
                        {
                            require(false, "the corners stay in order");
                            std::cerr << "       a " << attack << " d " << decay << " s " << sustain
                                      << " r " << release << " zoom " << zoom << '\n';
                        }
                        if (!(shape.peakY <= shape.sustainY && shape.sustainY <= shape.floorY))
                        {
                            require(false, "the sustain level stays between the floor and the peak");
                            std::cerr << "       s " << sustain << '\n';
                        }
                    }

    // The zoom strip is painted rather than built from components, so the only
    // thing holding the box that is drawn and the box that is clicked together
    // is that both ask these functions. Checked at every size the panel allows,
    // because the display it divides is sized as a share of the module.
    for (const auto& module : rhino::forge::ui::modules())
    {
        if (module.display != ui::Display::envelope) continue;
        for (int width = ui::minPanelWidth; width <= ui::maxPanelWidth; width += 40)
            for (int height = ui::minPanelHeight; height <= ui::maxPanelHeight; height += 40)
            {
                const auto area = ui::moduleBounds({0, 0, width, height}, module);
                const auto display = ui::displayBounds(area, module);
                const auto plot = ui::envelopePlotBounds(display);
                const auto strip = ui::envelopeZoomStrip(display);
                const auto in = ui::envelopeZoomIn(display);
                const auto out = ui::envelopeZoomOut(display);

                const auto fault = [&] (const char* what)
                {
                    require(false, what);
                    std::cerr << "       at " << width << "x" << height << '\n';
                };
                if (!display.contains(strip) || !display.contains(plot))
                    fault("the plot and the zoom strip both stay inside the display");
                if (plot.intersects(strip))
                    fault("the zoom strip is off the plot, so no curve can run under it");
                if (plot.getWidth() < display.getWidth() / 2)
                    fault("the strip takes a sliver of the display, not half of it");
                if (!strip.contains(in) || !strip.contains(out))
                    fault("both zoom buttons sit inside the strip");
                if (in.intersects(out)) fault("the zoom buttons do not overlap");
                if (in.getBottom() > out.getY()) fault("plus sits above minus");
                if (in.getWidth() < 16 || in.getHeight() < 16)
                    fault("a zoom button is big enough to hit");
            }
    }
}

// --------------------------------------------------------------- presets ---

// States written before a modulator arrived as a bank. Each time one did, the
// one that already existed was renamed for its place in the bank and the rest
// were inserted into the middle of the source list. A dropped parameter loads at
// its default and no harm is done; a source index that quietly means something
// else is a slot silently pointed somewhere nobody asked for, so it is remapped
// rather than left.
//
// Two eras are checked, because a state can predate either both changes or only
// the later one, and the shifts have to compose in the first case without being
// applied twice in the second.
void legacyStateSuite()
{
    // Whatever `saved` holds, put through the processor and read back out.
    const auto reopened = [] (rhino::forge::Processor& processor, const juce::ValueTree& saved)
    {
        juce::MemoryBlock block;
        const auto xml = saved.createXml();
        require(xml != nullptr, "the legacy state serialises");
        if (xml == nullptr) return;
        juce::AudioProcessor::copyXmlToBinary(*xml, block);
        processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    };
    const auto value = [] (rhino::forge::Processor& processor, const juce::String& id)
    {
        const auto* raw = processor.state.getRawParameterValue(id);
        return raw == nullptr ? std::numeric_limits<float>::quiet_NaN() : raw->load();
    };

    // Before LFO 2-6, and so before ENV 2-4 as well: both shifts apply, one
    // after the other, and a slot has to land where the second one leaves it.
    {
        rhino::forge::Processor processor;
        juce::ValueTree saved(processor.state.state.getType());
        const auto add = [&saved] (const char* id, float value)
        {
            juce::ValueTree entry("PARAM");
            entry.setProperty("id", id, nullptr);
            entry.setProperty("value", value, nullptr);
            saved.addChild(entry, -1, nullptr);
        };
        add("lfoShape", 2.0f);                      // SAW
        add("lfoMode", 1.0f);                       // ENV
        add("lfoRate", 3.0f);
        add("lfoRateUnit", 0.0f);
        add("lfoDivision", 4.0f);
        add("cutoff", 900.0f);
        // As the sources were numbered then: ENV 1 at 1, LFO 1 at 2, VELOCITY
        // straight after it at 3, NOTE at 4, and the macros from 5.
        add("mod1Source", 2.0f);
        add("mod2Source", 3.0f);
        add("mod3Source", 4.0f);
        add("mod4Source", 5.0f);
        add("mod5Source", 1.0f);
        reopened(processor, saved);

        const auto read = [&] (const juce::String& id) { return value(processor, id); };
        requireClose(read("lfo1Shape"), 2.0f, 0.001f, "the one LFO's shape becomes LFO 1's");
        requireClose(read("lfo1Mode"), 1.0f, 0.001f, "the one LFO's mode becomes LFO 1's");
        requireClose(read("lfo1Rate"), 3.0f, 0.001f, "the one LFO's rate becomes LFO 1's");
        requireClose(read("lfo1Division"), 4.0f, 0.001f, "the one LFO's division becomes LFO 1's");
        requireClose(read("cutoff"), 900.0f, 0.5f, "everything else is left alone");

        requireClose(read("mod1Source"), static_cast<float>(srcLfo1), 0.001f,
                     "a slot on LFO 1 stays on LFO 1 across both shifts");
        requireClose(read("mod2Source"), static_cast<float>(srcVelocity), 0.001f,
                     "a slot on velocity is still on velocity");
        requireClose(read("mod3Source"), static_cast<float>(srcNote), 0.001f,
                     "a slot on note is still on note");
        requireClose(read("mod4Source"), static_cast<float>(static_cast<int>(rhino::forge::ModSource::macro1)),
                     0.001f, "a slot on the first macro is still on it");
        requireClose(read("mod5Source"), static_cast<float>(srcEnv1), 0.001f,
                     "ENV 1 has never moved, so a slot on it stays put");

        // Running it again must change nothing: state already migrated no longer
        // carries the old ids, which is what the migration keys off.
        juce::MemoryBlock again;
        processor.getStateInformation(again);
        processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        requireClose(read("mod2Source"), static_cast<float>(srcVelocity), 0.001f,
                     "saving and reopening migrated state does not move the sources again");
        requireClose(read("lfo1Rate"), 3.0f, 0.001f,
                     "saving and reopening migrated state keeps LFO 1's rate");
    }

    // After the LFOs came in banks but before the envelopes did: the LFO names
    // are already current, so only the envelope shift may fire.
    {
        rhino::forge::Processor processor;
        juce::ValueTree saved(processor.state.state.getType());
        const auto add = [&saved] (const char* id, float value)
        {
            juce::ValueTree entry("PARAM");
            entry.setProperty("id", id, nullptr);
            entry.setProperty("value", value, nullptr);
            saved.addChild(entry, -1, nullptr);
        };
        add("attack", 0.5f);
        add("decay", 0.4f);
        add("sustain", 0.3f);
        add("release", 1.5f);
        add("lfo3Rate", 2.0f);
        // As the sources were numbered then: ENV 1 at 1, the six LFOs from 2,
        // VELOCITY at 8, NOTE at 9, and the macros from 10.
        add("mod1Source", 1.0f);
        add("mod2Source", 2.0f);
        add("mod3Source", 8.0f);
        add("mod4Source", 10.0f);
        reopened(processor, saved);

        const auto read = [&] (const juce::String& id) { return value(processor, id); };
        requireClose(read("env1Attack"), 0.5f, 0.001f, "the one envelope's attack becomes ENV 1's");
        requireClose(read("env1Decay"), 0.4f, 0.001f, "the one envelope's decay becomes ENV 1's");
        requireClose(read("env1Sustain"), 0.3f, 0.001f, "the one envelope's sustain becomes ENV 1's");
        requireClose(read("env1Release"), 1.5f, 0.001f, "the one envelope's release becomes ENV 1's");
        requireClose(read("env2Attack"), 0.01f, 0.001f,
                     "an envelope the state predates opens at its default");
        requireClose(read("lfo3Rate"), 2.0f, 0.001f, "an LFO already in a bank is left alone");

        requireClose(read("mod1Source"), static_cast<float>(srcEnv1), 0.001f,
                     "a slot on ENV 1 stays on ENV 1");
        requireClose(read("mod2Source"), static_cast<float>(srcLfo1), 0.001f,
                     "a slot on LFO 1 follows the three envelopes inserted ahead of it");
        requireClose(read("mod3Source"), static_cast<float>(srcVelocity), 0.001f,
                     "a slot on velocity is still on velocity");
        requireClose(read("mod4Source"), static_cast<float>(static_cast<int>(rhino::forge::ModSource::macro1)),
                     0.001f, "a slot on the first macro is still on it");

        juce::MemoryBlock again;
        processor.getStateInformation(again);
        processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        requireClose(read("mod2Source"), static_cast<float>(srcLfo1), 0.001f,
                     "saving and reopening does not move the sources again");
        requireClose(read("env1Release"), 1.5f, 0.001f,
                     "saving and reopening keeps ENV 1's release");
    }

    // Before the mixer. SUB and NOISE were summed into both channels at full
    // amplitude and are now panned at equal power, which costs them 3 dB at
    // centre, so their saved levels are raised by exactly that on the way in.
    // Everything else about a state of this era is already current.
    {
        rhino::forge::Processor processor;
        juce::ValueTree saved(processor.state.state.getType());
        const auto add = [&saved] (const char* id, float value)
        {
            juce::ValueTree entry("PARAM");
            entry.setProperty("id", id, nullptr);
            entry.setProperty("value", value, nullptr);
            saved.addChild(entry, -1, nullptr);
        };
        add("subLevel", 0.12f);
        add("noiseLevel", 0.25f);
        add("oscALevel", 0.75f);
        add("cutoff", 900.0f);
        reopened(processor, saved);

        const auto read = [&] (const juce::String& id) { return value(processor, id); };
        requireClose(read("subLevel"), 0.12f * juce::MathConstants<float>::sqrt2, 0.001f,
                     "a sub level written before the mixer is raised by the 3 dB its pan law costs");
        requireClose(read("noiseLevel"), 0.25f * juce::MathConstants<float>::sqrt2, 0.001f,
                     "a noise level written before the mixer is raised the same way");
        requireClose(read("oscALevel"), 0.75f, 0.001f,
                     "an oscillator was already panned at equal power, so its level is left alone");
        requireClose(read("cutoff"), 900.0f, 0.5f, "everything else is left alone");
        requireClose(read("subPan"), 0.0f, 0.001f, "a pan the state predates opens centred");
        requireClose(read("filterMix"), 1.0f, 0.001f, "the filter channel opens all wet");

        // The raise must not compound. Saving and reopening carries subPan,
        // which is the mark that says the state has already been through this.
        juce::MemoryBlock again;
        processor.getStateInformation(again);
        processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        requireClose(read("subLevel"), 0.12f * juce::MathConstants<float>::sqrt2, 0.001f,
                     "reopening migrated state does not raise the sub a second time");

        // A level already at the top of its range has nowhere to be raised to,
        // and must stay in range rather than leaving it.
        rhino::forge::Processor loud;
        juce::ValueTree maxed(loud.state.state.getType());
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", "subLevel", nullptr);
        entry.setProperty("value", 1.0f, nullptr);
        maxed.addChild(entry, -1, nullptr);
        reopened(loud, maxed);
        requireClose(value(loud, "subLevel"), 1.0f, 0.001f,
                     "a sub already at maximum stays at maximum rather than leaving its range");
    }
}

void presetSuite()
{
    rhino::forge::Processor processor;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-preset-test", {}, true);
    require(directory.createDirectory(), "temporary preset directory can be created");
    const auto preset = directory.getChildFile("Round Trip.forgepreset");

    setValue(processor, "cutoff", 1320.0f);
    setValue(processor, "env1Release", 2.5f);
    setValue(processor, "noiseEnable", 1.0f);
    setValue(processor, "oscBEnable", 0.0f);
    require(processor.savePreset(preset, "Round Trip").wasOk(), "preset saves");

    setValue(processor, "cutoff", 9000.0f);
    setValue(processor, "env1Release", 0.1f);
    setValue(processor, "noiseEnable", 0.0f);
    setValue(processor, "oscBEnable", 1.0f);
    require(processor.loadPreset(preset).wasOk(), "preset loads");
    requireClose(value(processor, "cutoff"), 1320.0f, 1.0f, "preset restores cutoff");
    requireClose(value(processor, "env1Release"), 2.5f, 0.001f, "preset restores release");
    requireClose(value(processor, "noiseEnable"), 1.0f, 0.001f, "preset restores an enabled module");
    requireClose(value(processor, "oscBEnable"), 0.0f, 0.001f, "preset restores a disabled module");

    // A table is data rather than a parameter, so it travels beside the
    // parameter state. A preset that carries one has to give back the frames
    // that were drawn, and a preset that carries none has to put the oscillator
    // back on the built-in ten rather than leave the previous patch's table
    // behind â€” the same rule an omitted parameter follows.
    {
        rhino::forge::Processor drawn;
        drawn.tableStore().edit(0).draw(0, 0.0f, -1.0f, 1.0f, 1.0f);
        drawn.tableStore().edit(0).insertFrame(0, true);
        drawn.tableStore().publish(0);
        const auto frames = drawn.tableStore().edit(0).frameCount();
        std::vector<float> authored(drawn.tableStore().edit(0).samples());

        const auto withTable = directory.getChildFile("Drawn.forgepreset");
        require(drawn.savePreset(withTable, "Drawn").wasOk(), "a preset holding a table saves");

        rhino::forge::Processor reopened;
        require(reopened.loadPreset(withTable).wasOk(), "a preset holding a table loads");
        require(reopened.tableStore().edit(0).frameCount() == frames,
                "a preset gives back the frames it was saved with");
        require(reopened.tableStore().edit(0).samples() == authored,
                "a preset gives back the samples it was saved with, exactly");
        require(!reopened.tableStore().edit(0).isUntouched(),
                "a table that came out of a preset is not the built-in one");
        require(reopened.tableStore().frameCount(0) == frames,
                "and POSITION is told how many frames it now has");
        require(reopened.tableStore().edit(1).isUntouched(),
                "an oscillator the preset said nothing about keeps the built-in table");

        require(reopened.loadPreset(preset).wasOk(), "a preset with no table loads over one with a table");
        require(reopened.tableStore().edit(0).isUntouched(),
                "a preset that carries no table puts the built-in one back");

        // Host state is the same payload by the same path, so a project reopens
        // on the table it was saved with.
        juce::MemoryBlock block;
        drawn.getStateInformation(block);
        rhino::forge::Processor hosted;
        hosted.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        require(hosted.tableStore().edit(0).samples() == authored,
                "host state carries the table too");
        // The table travels beside the parameters, never inside them.
        for (const auto child : hosted.state.copyState())
            require(!child.hasType("TABLE"), "the live parameter state holds no table node");
    }

    // Format 1 described a synth that no longer exists and is deliberately not
    // accepted.
    const auto version1 = directory.getChildFile("Old.forgepreset");
    require(version1.replaceWithText("<RhinoForgePreset formatVersion=\"1\"><RhinoForgeState/></RhinoForgePreset>"),
            "format 1 fixture writes");
    require(processor.loadPreset(version1).failed(), "a format 1 preset is refused");

    const auto future = directory.getChildFile("Future.forgepreset");
    require(future.replaceWithText("<RhinoForgePreset formatVersion=\"99\"><RhinoForgeState/></RhinoForgePreset>"),
            "future-version fixture writes");
    require(processor.loadPreset(future).failed(), "a newer preset version is refused");

    // Within format 2, a preset is reconciled against the parameters that exist
    // when it opens. This is what keeps presets working while Forge's controls
    // are still being built out milestone by milestone: entries Forge no longer
    // has are dropped, and controls the preset predates return to their
    // defaults instead of inheriting the previous patch.
    setValue(processor, "cutoff", 2000.0f);
    setValue(processor, "noiseEnable", 1.0f);
    const auto partial = directory.getChildFile("Partial.forgepreset");
    require(partial.replaceWithText(
                "<RhinoForgePreset formatVersion=\"2\"><RhinoForgeState>"
                "<PARAM id=\"cutoff\" value=\"5000.0\"/>"
                "<PARAM id=\"retiredKnob\" value=\"0.5\"/>"
                "</RhinoForgeState></RhinoForgePreset>"),
            "partial fixture writes");
    require(processor.loadPreset(partial).wasOk(), "a preset missing parameters still loads");
    requireClose(value(processor, "cutoff"), 5000.0f, 1.0f, "a partial preset restores what it names");
    requireClose(value(processor, "noiseEnable"), 0.0f, 0.001f,
                 "a parameter the preset omits returns to its default, not the previous patch's value");
    requireClose(value(processor, "env1Release"), 0.35f, 0.001f,
                 "an omitted float parameter returns to its default");

    const auto resaved = directory.getChildFile("Resaved.forgepreset");
    require(processor.savePreset(resaved, "Resaved").wasOk(), "a reconciled preset saves");
    const auto text = resaved.loadFileAsString();
    require(text.contains("formatVersion=\"2\""), "a saved preset declares format 2");
    require(!text.contains("retiredKnob"), "an unknown entry is dropped, not carried as ballast");
    require(text.contains("oscAEnable"), "the module enables are saved");
    require(text.contains("env1Release"), "an omitted parameter is written back out at its default");

    const auto invalid = directory.getChildFile("Invalid.forgepreset");
    require(invalid.replaceWithText("<NotForge />"), "invalid fixture writes");
    const auto before = value(processor, "cutoff");
    require(processor.loadPreset(invalid).failed(), "a foreign preset is rejected");
    require(value(processor, "cutoff") == before, "a rejected preset leaves state untouched");

    // The readout bug: a SliderAttachment overwrites any formatter the editor
    // installs, so the formatting has to belong to the parameter itself.
    requireText(textFor(processor, "cutoff", 7800.0f), "7.80 kHz", "cutoff reads as kHz");
    requireText(textFor(processor, "cutoff", 440.0f), "440 Hz", "a low cutoff reads as Hz");
    requireText(textFor(processor, "env1Sustain", 0.75f), "75 %", "sustain reads as a percentage");
    requireText(textFor(processor, "env1Attack", 0.01f), "10 ms", "a short attack reads in milliseconds");
    requireText(textFor(processor, "env1Release", 2.5f), "2.50 s", "a long release reads in seconds");
    requireText(textFor(processor, "oscBSemitone", 7.0f), "+7 st", "tune reads as signed semitones");
    requireText(textFor(processor, "oscAUnison", 4.0f), "4", "unison reads as a plain count");
    requireText(textFor(processor, "mod1Depth", -0.5f), "-50 %", "a bipolar depth keeps its sign");
    require(!textFor(processor, "cutoff", 7800.0f).contains("7800.0004"),
            "no knob falls back to a raw float readout");

    directory.deleteRecursively();
}

// ---------------------------------------------------------------- engine ---

// Reduce a processor to one clean sine from oscillator A, so the waveform can
// be measured directly.
void soloSineOnA(rhino::forge::Processor& processor)
{
    for (const auto* id : {"oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(processor, id, 0.0f);
    setValue(processor, "oscAEnable", 1.0f);
    setValue(processor, "oscAPosition", 0.0f);
    setValue(processor, "oscAUnison", 1.0f);
    setValue(processor, "oscADetune", 0.0f);
    setValue(processor, "oscAPan", 0.0f);
    setValue(processor, "oscALevel", 1.0f);
    setValue(processor, "oscAOctave", 0.0f);
    setValue(processor, "oscASemitone", 0.0f);
    setValue(processor, "oscAFine", 0.0f);
    setValue(processor, "drive", 0.0f);
    setValue(processor, "env1Attack", 0.001f);
}

void oscillatorSuite()
{
    // Tuning is one multiplier built from three controls. Check the arithmetic
    // directly before checking that the voice honours it.
    rhino::forge::Oscillator osc;
    requireClose(rhino::forge::tuningRatio(osc), 1.0f, 0.0001f, "an untuned oscillator plays at pitch");
    osc.octave = 1.0f;
    requireClose(rhino::forge::tuningRatio(osc), 2.0f, 0.0001f, "one octave doubles the frequency");
    osc.octave = 0.0f; osc.semitone = 12.0f;
    requireClose(rhino::forge::tuningRatio(osc), 2.0f, 0.0001f, "twelve semitones doubles the frequency");
    osc.semitone = 0.0f; osc.fine = 100.0f;
    requireClose(rhino::forge::tuningRatio(osc), std::pow(2.0f, 1.0f / 12.0f), 0.0001f,
                 "one hundred cents is one semitone");
    osc.octave = -1.0f; osc.semitone = 12.0f; osc.fine = 0.0f;
    requireClose(rhino::forge::tuningRatio(osc), 1.0f, 0.0001f, "octave and semitone cancel");

    // And the voice honours it: an octave up doubles the zero-crossing rate.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    rhino::forge::Processor atPitch;
    soloSineOnA(atPitch);
    renderNote(atPitch, buffer);
    const auto baseCrossings = zeroCrossings(buffer, 0, settled);
    require(baseCrossings > 0, "a solo sine crosses zero");

    rhino::forge::Processor anOctaveUp;
    soloSineOnA(anOctaveUp);
    setValue(anOctaveUp, "oscAOctave", 1.0f);
    renderNote(anOctaveUp, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 static_cast<float>(baseCrossings * 2), static_cast<float>(baseCrossings) * 0.05f,
                 "an octave up doubles the oscillator's frequency");

    rhino::forge::Processor sevenSemis;
    soloSineOnA(sevenSemis);
    setValue(sevenSemis, "oscASemitone", 7.0f);
    renderNote(sevenSemis, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 baseCrossings * std::pow(2.0f, 7.0f / 12.0f), static_cast<float>(baseCrossings) * 0.05f,
                 "seven semitones is a fifth");

    // Pan law: hard left puts nothing in the right channel.
    rhino::forge::Processor panned;
    soloSineOnA(panned);
    setValue(panned, "oscAPan", -1.0f);
    renderNote(panned, buffer);
    const auto left = rms(buffer, 0, settled);
    const auto right = rms(buffer, 1, settled);
    require(left > 0.0f, "a hard-left oscillator still feeds the left channel");
    require(right < left * 0.01f, "a hard-left oscillator is absent from the right channel");

    // Centred, an equal-power pan puts the same energy in both channels.
    rhino::forge::Processor centred;
    soloSineOnA(centred);
    renderNote(centred, buffer);
    requireClose(rms(buffer, 0, settled), rms(buffer, 1, settled), 0.0001f,
                 "a centred oscillator is equal in both channels");

    // Level scales the oscillator directly, now that it owns one.
    const auto fullLevel = rms(buffer, 0, settled);
    rhino::forge::Processor halfLevel;
    soloSineOnA(halfLevel);
    setValue(halfLevel, "oscALevel", 0.5f);
    renderNote(halfLevel, buffer);
    requireClose(rms(buffer, 0, settled), fullLevel * 0.5f, fullLevel * 0.02f,
                 "halving an oscillator's level halves its output");

    // Widening the stack changes the sound without changing the level. This has
    // to be measured over a window longer than the slowest beat in the stack -
    // at A3 with twelve voices that is nearly five seconds - because a shorter
    // one reports wherever the stack happens to sit rather than its level.
    constexpr int longSamples = 48000 * 13 / 2;
    constexpr int longSettled = 24000;
    juce::AudioBuffer<float> longBuffer(2, longSamples);

    rhino::forge::Processor single;
    soloSineOnA(single);
    setValue(single, "oscADetune", 0.3f);
    setValue(single, "oscAUnison", 1.0f);
    renderNote(single, longBuffer);
    const auto oneVoice = rms(longBuffer, 0, longSettled);
    require(oneVoice > 0.0f, "a single voice makes sound");

    // Every stack size, not just one, so the widest the parameter allows cannot
    // quietly be the loudest.
    for (int count = 2; count <= rhino::forge::unisonMax; ++count)
    {
        rhino::forge::Processor stacked;
        soloSineOnA(stacked);
        setValue(stacked, "oscADetune", 0.3f);
        setValue(stacked, "oscAUnison", static_cast<float>(count));
        renderNote(stacked, longBuffer);
        const auto stackedVoices = rms(longBuffer, 0, longSettled);
        require(stackedVoices > 0.0f, "every stack size makes sound");
        const auto decibels = juce::Decibels::gainToDecibels(stackedVoices / oneVoice);
        require(std::abs(decibels) < 4.0f, "stacking voices does not change the oscillator's level");
        if (std::abs(decibels) >= 4.0f)
            std::cerr << "       unison " << count << " level shift: " << decibels
                      << " dB\n";
    }

    // The parameter stops exactly where the phase arrays do, so the widest
    // stack the panel can ask for is one the voice can actually render.
    requireText(textFor(single, "oscAUnison", 99.0f),
                juce::String(rhino::forge::unisonMax),
                "unison stops at the width the voice can render");

    // A full stack has to chorus, not throb. Evenly spaced members give every
    // neighbouring pair the same beat rate, which makes the whole stack swing
    // together on one slow period; unisonOffset exists to prevent exactly that,
    // and this is the check that it still does.
    rhino::forge::Processor wide;
    soloSineOnA(wide);
    setValue(wide, "oscAPosition", 6.0f / 9.0f);
    setValue(wide, "oscADetune", 0.5f);
    setValue(wide, "oscAUnison", static_cast<float>(rhino::forge::unisonMax));
    renderNote(wide, longBuffer);
    const auto depth = envelopeDepthDb(longBuffer, 0, longSettled);
    require(depth < 12.0f, "a full unison stack choruses rather than throbs");
    if (depth >= 12.0f)
        std::cerr << "       full stack envelope depth: " << depth << " dB\n";

    // The members must be unevenly spaced for that to hold. Checked directly so
    // a failure says which of the two things broke.
    auto smallest = 1.0e9f, largest = 0.0f;
    for (int i = 1; i < rhino::forge::unisonMax; ++i)
    {
        const auto gap = rhino::forge::unisonOffset(i, rhino::forge::unisonMax)
                       - rhino::forge::unisonOffset(i - 1, rhino::forge::unisonMax);
        require(gap > 0.0f, "the stack stays in order");
        smallest = juce::jmin(smallest, gap);
        largest = juce::jmax(largest, gap);
    }
    require(largest > smallest * 2.0f, "the stack is not evenly spaced");
    requireClose(rhino::forge::unisonOffset(rhino::forge::unisonMax - 1, rhino::forge::unisonMax)
                 - rhino::forge::unisonOffset(0, rhino::forge::unisonMax),
                 1.0f, 0.0001f, "the stack still spans exactly what detune asks for");
}

void filterRoutingSuite()
{
    // A source that is routed into the filter is affected by the cutoff; one
    // that is not, is not. That is the whole point of the routing chips.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto peakWithCutoff = [&buffer] (bool routed, float cutoff)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "filterEnable", 1.0f);
        setValue(processor, "filterType", 0.0f);
        setValue(processor, "routeA", routed ? 1.0f : 0.0f);
        setValue(processor, "resonance", 0.0f);
        setValue(processor, "cutoff", cutoff);
        renderNote(processor, buffer);
        return rms(buffer, 0, settled);
    };

    // A 220 Hz note against a 60 Hz low pass: routed, it is heavily attenuated.
    const auto routedOpen = peakWithCutoff(true, 18000.0f);
    const auto routedClosed = peakWithCutoff(true, 60.0f);
    require(routedOpen > 0.0f, "a routed source is audible with the filter open");
    require(routedClosed < routedOpen * 0.25f, "closing the cutoff attenuates a routed source");

    const auto bypassedOpen = peakWithCutoff(false, 18000.0f);
    const auto bypassedClosed = peakWithCutoff(false, 60.0f);
    require(bypassedOpen > 0.0f, "an unrouted source is still audible");
    requireClose(bypassedClosed, bypassedOpen, bypassedOpen * 0.001f,
                 "the cutoff does not touch an unrouted source");

    // Routing is per source, so closing the filter on one leaves the other.
    rhino::forge::Processor split;
    soloSineOnA(split);
    setValue(split, "subEnable", 1.0f);
    setValue(split, "subLevel", 0.6f);
    setValue(split, "filterEnable", 1.0f);
    setValue(split, "cutoff", 60.0f);
    setValue(split, "routeA", 1.0f);
    setValue(split, "routeSub", 0.0f);
    renderNote(split, buffer);
    const auto subSurvives = rms(buffer, 0, settled);
    setValue(split, "routeSub", 1.0f);
    renderNote(split, buffer);
    require(rms(buffer, 0, settled) < subSurvives * 0.7f,
            "routing the sub into a closed filter removes it, while oscillator A's routing is unchanged");

    // Drive belongs to the filter, so it only touches what is routed there.
    rhino::forge::Processor driven;
    soloSineOnA(driven);
    setValue(driven, "filterEnable", 1.0f);
    setValue(driven, "cutoff", 18000.0f);
    setValue(driven, "routeA", 0.0f);
    renderNote(driven, buffer);
    const auto undriven = rms(buffer, 0, settled);
    setValue(driven, "drive", 1.0f);
    renderNote(driven, buffer);
    requireClose(rms(buffer, 0, settled), undriven, undriven * 0.001f,
                 "drive does not touch a source that bypasses the filter");

    // Each filter type keeps the output finite and does something different.
    rhino::forge::Processor typed;
    soloSineOnA(typed);
    setValue(typed, "filterEnable", 1.0f);
    setValue(typed, "routeA", 1.0f);
    setValue(typed, "cutoff", 1000.0f);
    std::vector<float> levels;
    for (auto type = 0.0f; type <= 2.0f; type += 1.0f)
    {
        setValue(typed, "filterType", type);
        renderNote(typed, buffer);
        require(allSamplesFinite(buffer), "every filter type renders finite audio");
        levels.push_back(rms(buffer, 0, settled));
    }
    require(levels[0] > levels[1], "a 220 Hz note passes the low pass more than the high pass");
    require(levels[0] > 0.0f, "the low pass passes something");

    // Switching the filter module off bypasses the drive with it: drive is a
    // control of that module, not a master saturator that survives it.
    rhino::forge::Processor off;
    soloSineOnA(off);
    setValue(off, "routeA", 1.0f);
    setValue(off, "filterEnable", 0.0f);
    renderNote(off, buffer);
    const auto moduleOff = rms(buffer, 0, settled);
    setValue(off, "drive", 1.0f);
    renderNote(off, buffer);
    requireClose(rms(buffer, 0, settled), moduleOff, moduleOff * 0.001f,
                 "drive does nothing while the filter module is off");
}

// ----------------------------------------------------------------- mixer ---

// The mixer claims to route, to place and to balance. Every one of those is
// checked by measuring what comes out rather than by reading the patch back:
// a send summed into the wrong accumulator, or a pan law applied twice, reads
// perfectly correct in the parameters and wrong in the audio.
void mixerSuite()
{
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    // --- The busses -----------------------------------------------------------

    // A send is parallel: it adds the source to a bus without taking it away
    // from wherever it was already going.
    rhino::forge::Processor sent;
    soloSineOnA(sent);
    renderNote(sent, buffer);
    const auto direct = rms(buffer, 0, settled);
    require(direct > 0.0f, "a source with no sends is audible on its own");

    setValue(sent, "bus1Level", 1.0f);
    setValue(sent, "oscASend1", 1.0f);
    renderNote(sent, buffer);
    require(allSamplesFinite(buffer), "a send renders finite audio");
    require(rms(buffer, 0, settled) > direct * 1.5f,
            "sending a source to a bus adds it to the output rather than moving it there");

    // The bus's own fader scales what arrives at it, and its enable removes the
    // bus entirely, leaving what the source was already doing untouched.
    setValue(sent, "bus1Level", 0.0f);
    renderNote(sent, buffer);
    requireClose(rms(buffer, 0, settled), direct, direct * 0.01f,
                 "a bus at silence contributes nothing");
    setValue(sent, "bus1Level", 1.0f);
    setValue(sent, "bus1Enable", 0.0f);
    renderNote(sent, buffer);
    requireClose(rms(buffer, 0, settled), direct, direct * 0.01f,
                 "a bus switched off is heard nowhere, and takes nothing else with it");

    // A source whose destination is silenced is still heard on the bus it is
    // sent to: a send is taken from the channel, not from the output.
    rhino::forge::Processor onlyBus;
    soloSineOnA(onlyBus);
    setValue(onlyBus, "oscAEnable", 0.0f);
    setValue(onlyBus, "subEnable", 1.0f);
    setValue(onlyBus, "subLevel", 0.8f);
    setValue(onlyBus, "routeSub", 0.0f);
    setValue(onlyBus, "subSend1", 1.0f);
    setValue(onlyBus, "bus1Level", 1.0f);
    renderNote(onlyBus, buffer);
    require(rms(buffer, 0, settled) > 0.0f, "a source reaches the output through a bus");

    // Bus 1 into bus 2 is one chain: silencing the bus at the end of it
    // silences everything upstream.
    rhino::forge::Processor chained;
    soloSineOnA(chained);
    setValue(chained, "oscASend1", 1.0f);
    setValue(chained, "bus1Level", 1.0f);
    setValue(chained, "bus2Level", 1.0f);
    setValue(chained, "bus1Dest", 1.0f);
    renderNote(chained, buffer);
    require(rms(buffer, 0, settled) > direct * 1.5f,
            "a bus routed into the other still reaches the output");
    setValue(chained, "bus2Level", 0.0f);
    renderNote(chained, buffer);
    requireClose(rms(buffer, 0, settled), direct, direct * 0.01f,
                 "silencing the bus at the end of a chain silences the whole chain");

    // Two busses pointed at each other is a loop with no answer. It has to be
    // finite and audible rather than either silent or runaway: the second of
    // the pair goes to the output instead.
    rhino::forge::Processor looped;
    soloSineOnA(looped);
    setValue(looped, "oscASend1", 1.0f);
    setValue(looped, "bus1Level", 1.0f);
    setValue(looped, "bus2Level", 1.0f);
    setValue(looped, "bus1Dest", 1.0f);
    setValue(looped, "bus2Dest", 1.0f);
    renderNote(looped, buffer);
    require(allSamplesFinite(buffer), "two busses pointed at each other render finite audio");
    require(rms(buffer, 0, settled) > direct * 1.5f,
            "a mutually crossed pair still reaches the output");

    // --- Placing a source -----------------------------------------------------

    // The sub and the noise have a pan of their own now. Panned hard, they are
    // on one side and not the other.
    rhino::forge::Processor placed;
    soloSineOnA(placed);
    setValue(placed, "oscAEnable", 0.0f);
    setValue(placed, "subEnable", 1.0f);
    setValue(placed, "subLevel", 0.8f);
    setValue(placed, "subPan", -1.0f);
    renderNote(placed, buffer);
    require(rms(buffer, 0, settled) > 0.0f, "a hard left sub is audible on the left");
    require(rms(buffer, 1, settled) < rms(buffer, 0, settled) * 0.01f,
            "a hard left sub is silent on the right");

    // Centred, it is equally on both. And the level it is centred at is the one
    // it had before the mixer gave it a pan law at all: 0.12 under the old law
    // and 0.17 under the new are the same sound, which is what the migration in
    // Processor::migrated exists to preserve. Measured rather than assumed.
    setValue(placed, "subPan", 0.0f);
    setValue(placed, "subLevel", 0.17f);
    renderNote(placed, buffer);
    const auto centred = rms(buffer, 0, settled);
    requireClose(rms(buffer, 1, settled), centred, centred * 0.001f,
                 "a centred sub is equally on both channels");

    rhino::forge::Processor asBefore;
    soloSineOnA(asBefore);
    setValue(asBefore, "oscAEnable", 0.0f);
    setValue(asBefore, "subEnable", 1.0f);
    setValue(asBefore, "subLevel", 0.12f * juce::MathConstants<float>::sqrt2);
    renderNote(asBefore, buffer);
    requireClose(rms(buffer, 0, settled), centred, centred * 0.01f,
                 "the sub's new default is the level its old default sounded at");

    // --- The filter's channel -------------------------------------------------

    // MIX blends what came out of the filter against what went in, so at
    // nothing the filter is inaudible however closed it is.
    rhino::forge::Processor blended;
    soloSineOnA(blended);
    setValue(blended, "filterEnable", 1.0f);
    setValue(blended, "routeA", 1.0f);
    setValue(blended, "resonance", 0.0f);
    setValue(blended, "cutoff", 18000.0f);
    renderNote(blended, buffer);
    const auto open = rms(buffer, 0, settled);
    setValue(blended, "cutoff", 60.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, settled) < open * 0.25f, "a closed filter attenuates its channel");
    setValue(blended, "filterMix", 0.0f);
    renderNote(blended, buffer);
    requireClose(rms(buffer, 0, settled), open, open * 0.02f,
                 "MIX at nothing passes what went into the filter, whatever the cutoff is doing");

    // The channel's fader and its pan sit after that blend.
    setValue(blended, "filterMix", 1.0f);
    setValue(blended, "cutoff", 18000.0f);
    setValue(blended, "filterLevel", 0.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, settled) < open * 0.001f, "the filter channel's fader silences it");
    setValue(blended, "filterLevel", 1.0f);
    setValue(blended, "filterPan", 1.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, settled) < open * 0.01f, "a hard right filter is silent on the left");
    require(rms(buffer, 1, settled) > 0.0f, "a hard right filter is audible on the right");

    // A channel pan is unity at its centre, because it moves a sum that is
    // already balanced rather than placing a source among others. A source pan
    // is not, which is exactly why the two laws are separate.
    setValue(blended, "filterPan", 0.0f);
    renderNote(blended, buffer);
    requireClose(rms(buffer, 0, settled), open, open * 0.001f,
                 "a filter panned to the centre is as loud as one with no pan at all");

    // --- Nothing moved that was not asked to ----------------------------------
    //
    // The mixer's defaults have to leave the synth where they found it, or
    // every patch written before it quietly changed.
    require(rhino::forge::Patch {}.filterMix == 1.0f, "the filter is all wet by default");
    require(rhino::forge::Patch {}.filterLevel == 1.0f, "the filter channel opens at unity");
    require(rhino::forge::Patch {}.filterPan == 0.0f, "the filter channel opens centred");
    for (int bus = 0; bus < rhino::forge::busCount; ++bus)
    {
        const auto& settings = rhino::forge::Patch {}.buses[static_cast<size_t>(bus)];
        require(settings.dest == 0.0f, "a bus opens pointed at the main output");
    }
    rhino::forge::Processor fresh;
    require(peakForNote(fresh) > 0.0f, "the patch Forge opens on still makes a sound");
    for (const auto* id : {"oscASend1", "oscASend2", "subSend1", "filterSend1"})
        require(value(fresh, id) == 0.0f, "nothing is sent anywhere until it is asked for");
}

// ------------------------------------------------------------------- rack ---

// The rack is measured rather than read back, for the same reason the mixer is:
// a slot wired to the wrong accumulator, a delay line read at the wrong offset
// or a type that silently does nothing all look perfectly correct in the
// parameters.
//
// These lean on what each type *provably* does rather than on how it sounds — a
// delay puts energy where there was none, a filter takes brightness away, a
// distortion adds harmonics — because those are the claims that would be wrong
// if the wiring were wrong.
// Defined with the modulation suite further down, which is where the rest of
// the matrix checks live; declared here because the rack is reached the same
// way any other destination is and this is where that is proven.
void setSlot(rhino::forge::Processor& processor, int slot, float source, float destination, float depth);

void fxSuite()
{
    using rhino::forge::FxType;
    constexpr int samples = 16384;
    juce::AudioBuffer<float> buffer(2, samples);

    // One slot of one rack, set up in a line.
    const auto place = [] (rhino::forge::Processor& processor, int rack, int slot, FxType type)
    {
        setValue(processor, rhino::forge::fxParameterId(rack, slot, "Type").toRawUTF8(),
                 static_cast<float>(type));
    };
    const auto knob = [] (rhino::forge::Processor& processor, int rack, int slot, int index, float value)
    {
        setValue(processor, (rhino::forge::fxParameterId(rack, slot, "Knob")
                             + juce::String(index + 1)).toRawUTF8(), value);
    };

    // Everything below plays one short note and then listens to what is left
    // after it, which is where a delay and a reverb live and where a dry synth
    // is silent.
    const auto tailAfterNote = [&buffer] (rhino::forge::Processor& processor)
    {
        processor.prepareToPlay(48000.0, buffer.getNumSamples());
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 57), 2000);
        processor.processBlock(buffer, midi);
        // Well past the note and its release, so anything here arrived by way
        // of a delay line rather than from the voice.
        return rms(buffer, 0, 9000);
    };

    // --- A rack does something, and only where it is put ----------------------

    rhino::forge::Processor dry;
    soloSineOnA(dry);
    setValue(dry, "env1Release", 0.02f);
    const auto silence = tailAfterNote(dry);

    rhino::forge::Processor delayed;
    soloSineOnA(delayed);
    setValue(delayed, "env1Release", 0.02f);
    place(delayed, 0, 0, FxType::delay);
    knob(delayed, 0, 0, 0, 0.35f);   // a time long enough to outlast the note
    knob(delayed, 0, 0, 2, 0.8f);    // feedback, so there is a tail to find
    const auto withDelay = tailAfterNote(delayed);
    require(allSamplesFinite(buffer), "a delay renders finite audio");
    require(withDelay > silence * 4.0f + 0.0001f,
            "a delay on the main rack leaves sound behind after the note has gone");

    // The same delay on a bus nothing is sent to must change nothing at all.
    rhino::forge::Processor unsent;
    soloSineOnA(unsent);
    setValue(unsent, "env1Release", 0.02f);
    place(unsent, 1, 0, FxType::delay);
    knob(unsent, 1, 0, 0, 0.35f);
    knob(unsent, 1, 0, 2, 0.8f);
    requireClose(tailAfterNote(unsent), silence, silence + 0.0001f,
                 "a rack on a bus with nothing sent to it is heard nowhere");

    // Sent to that bus, it arrives. This is the whole point of the busses.
    rhino::forge::Processor sentToBus;
    soloSineOnA(sentToBus);
    setValue(sentToBus, "env1Release", 0.02f);
    setValue(sentToBus, "oscASend1", 1.0f);
    setValue(sentToBus, "bus1Level", 1.0f);
    place(sentToBus, 1, 0, FxType::delay);
    knob(sentToBus, 1, 0, 0, 0.35f);
    knob(sentToBus, 1, 0, 2, 0.8f);
    require(tailAfterNote(sentToBus) > silence * 4.0f + 0.0001f,
            "a source sent to a bus is heard through that bus's rack");

    // A reverb is the type most easily broken into silence — a comb read past
    // the end of its own line answers nothing and the bank stays quiet — so it
    // is held to the same claim the delay is: sound after the note is gone.
    rhino::forge::Processor reverbed;
    soloSineOnA(reverbed);
    setValue(reverbed, "env1Release", 0.02f);
    place(reverbed, 0, 0, FxType::reverb);
    knob(reverbed, 0, 0, 0, 0.8f);   // size
    knob(reverbed, 0, 0, 1, 0.9f);   // decay
    knob(reverbed, 0, 0, 2, 0.1f);   // very little damping, so the tail carries
    require(tailAfterNote(reverbed) > silence * 4.0f + 0.0001f,
            "a reverb leaves a tail behind the note");
    require(allSamplesFinite(buffer), "a reverb renders finite audio");

    // --- Bypass, at both levels ------------------------------------------------

    rhino::forge::Processor bypassed;
    soloSineOnA(bypassed);
    setValue(bypassed, "env1Release", 0.02f);
    place(bypassed, 0, 0, FxType::delay);
    knob(bypassed, 0, 0, 0, 0.35f);
    knob(bypassed, 0, 0, 2, 0.8f);
    setValue(bypassed, rhino::forge::fxParameterId(0, 0, "Bypass").toRawUTF8(), 1.0f);
    requireClose(tailAfterNote(bypassed), silence, silence + 0.0001f,
                 "a bypassed slot is out of the signal");
    setValue(bypassed, rhino::forge::fxParameterId(0, 0, "Bypass").toRawUTF8(), 0.0f);
    setValue(bypassed, rhino::forge::fxRackParameterId(0, "Bypass").toRawUTF8(), 1.0f);
    requireClose(tailAfterNote(bypassed), silence, silence + 0.0001f,
                 "bypassing the whole rack takes every slot in it out at once");

    // A slot left at OFF is not a slot that does nothing quietly — it must be
    // exactly the same signal as no slot at all.
    rhino::forge::Processor emptySlot;
    soloSineOnA(emptySlot);
    renderNote(emptySlot, buffer);
    const auto plain = rms(buffer, 0, 1024);
    place(emptySlot, 0, 2, FxType::off);
    renderNote(emptySlot, buffer);
    requireClose(rms(buffer, 0, 1024), plain, plain * 0.0001f, "a slot set to OFF changes nothing");

    // --- MIX and LEVEL mean one thing across every type ------------------------

    rhino::forge::Processor blended;
    soloSineOnA(blended);
    place(blended, 0, 0, FxType::filter);
    knob(blended, 0, 0, 0, 0.05f);   // a low cutoff, so the effect is obvious
    knob(blended, 0, 0, 1, 0.0f);
    renderNote(blended, buffer);
    const auto filtered = rms(buffer, 0, 1024);
    require(filtered < plain * 0.7f, "a filter in the rack takes the level down");
    setValue(blended, rhino::forge::fxParameterId(0, 0, "Mix").toRawUTF8(), 0.0f);
    renderNote(blended, buffer);
    requireClose(rms(buffer, 0, 1024), plain, plain * 0.02f,
                 "MIX at nothing passes what went into the slot, whatever the slot is doing");
    setValue(blended, rhino::forge::fxParameterId(0, 0, "Mix").toRawUTF8(), 1.0f);
    setValue(blended, rhino::forge::fxParameterId(0, 0, "Level").toRawUTF8(), 0.0f);
    renderNote(blended, buffer);
    require(rms(buffer, 0, 1024) < plain * 0.001f, "a slot's LEVEL at nothing silences it");

    // --- The slots run in order ------------------------------------------------
    //
    // A filter opened wide after a filter closed down is still dark; the other
    // way round it is still dark too, but a rack that ran its slots in the
    // wrong order would let the second one undo the first.
    rhino::forge::Processor ordered;
    soloSineOnA(ordered);
    setValue(ordered, "oscAPosition", 6.0f / 9.0f);   // a saw, so there is something to take away
    renderNote(ordered, buffer);
    const auto open = brightness(buffer, 0, 1024);
    place(ordered, 0, 0, FxType::filter);
    knob(ordered, 0, 0, 0, 0.1f);
    knob(ordered, 0, 0, 1, 0.0f);
    renderNote(ordered, buffer);
    const auto afterFirst = brightness(buffer, 0, 1024);
    require(afterFirst < open * 0.8f, "a low pass in the rack takes the top off");
    place(ordered, 0, 1, FxType::filter);
    knob(ordered, 0, 1, 0, 1.0f);
    knob(ordered, 0, 1, 1, 0.0f);
    renderNote(ordered, buffer);
    require(brightness(buffer, 0, 1024) < open * 0.8f,
            "a filter wide open after a closed one cannot put back what the first took out");

    // --- Every type renders, and none of them is silent or infinite ------------
    //
    // The cheapest check there is, and the one that would have caught every
    // mistake made writing these: a type whose state is read before it is
    // prepared, or whose feedback path runs away.
    for (int type = 1; type < rhino::forge::fxTypeCount; ++type)
    {
        rhino::forge::Processor each;
        soloSineOnA(each);
        place(each, 0, 0, static_cast<FxType>(type));
        // Driven hard on purpose: a feedback path that is going to run away
        // does it here rather than in somebody's project.
        for (int index = 0; index < rhino::forge::fxKnobCount; ++index)
            knob(each, 0, 0, index, 0.95f);
        renderNote(each, buffer);
        if (!allSamplesFinite(buffer))
        {
            require(false, "every effect type renders finite audio at its extremes");
            std::cerr << "       type: " << rhino::forge::fxTypeName(type) << '\n';
        }
        if (buffer.getMagnitude(0, buffer.getNumSamples()) > 1.0f)
        {
            require(false, "no effect type leaves full scale");
            std::cerr << "       type: " << rhino::forge::fxTypeName(type) << '\n';
        }
        // And again with everything at nothing, which is the other end a
        // divide-by-zero hides at.
        for (int index = 0; index < rhino::forge::fxKnobCount; ++index)
            knob(each, 0, 0, index, 0.0f);
        renderNote(each, buffer);
        if (!allSamplesFinite(buffer))
        {
            require(false, "every effect type renders finite audio at the bottom of its range");
            std::cerr << "       type: " << rhino::forge::fxTypeName(type) << '\n';
        }
    }

    // --- A knob's reading is the one the DSP uses ------------------------------
    //
    // The whole point of the type table: the panel, the readout and the render
    // all take their arithmetic from the same helpers. If a delay's TIME said
    // 250 ms while the line was read at some other offset, this is what would
    // notice.
    rhino::forge::Processor reading;
    place(reading, 0, 0, FxType::delay);
    const auto quarter = rhino::forge::fxScaled(0.5f, 0.01f, rhino::forge::fxMaxDelayTime, 2.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 0.5f),
                quarter < 1.0f ? juce::String(juce::roundToInt(quarter * 1000.0f)) + " ms"
                               : juce::String(quarter, 2) + " s",
                "a delay's TIME reads the time the engine will use");
    // Switched to beats, the same knob reads a division instead.
    setValue(reading, rhino::forge::fxParameterId(0, 0, "ModeB").toRawUTF8(), 1.0f);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob1").toRawUTF8(), 1.0f),
                "2/1", "a synced delay's TIME reads a division of the beat");
    // A knob the type does not use says so rather than showing a number that
    // means nothing.
    place(reading, 0, 0, FxType::filter);
    requireText(textFor(reading, rhino::forge::fxParameterId(0, 0, "Knob5").toRawUTF8(), 0.5f),
                "-", "a knob the type does not use reads as nothing");

    // --- What a type opens on --------------------------------------------------
    //
    // Every type is put into a slot by the panel, which then sets it up from
    // the table beside the type. These hold that table to the two things that
    // would actually be wrong: an equaliser that colours a signal the moment it
    // is dropped in, and a reverb that drowns the main output.
    for (int type = 1; type < rhino::forge::fxTypeCount; ++type)
    {
        const auto& info = rhino::forge::fxTypes()[static_cast<size_t>(type)];
        require(info.initMix > 0.0f && info.initMix <= 1.0f,
                "a type opens at a wet amount you can hear and cannot exceed");
        for (int index = 0; index < rhino::forge::fxKnobCount; ++index)
            require(info.init[static_cast<size_t>(index)] >= 0.0f
                        && info.init[static_cast<size_t>(index)] <= 1.0f,
                    "a type opens on knob values inside the range the knobs have");
    }
    // An equaliser has to open flat, or dropping one in colours the sound
    // before it is asked to. Gain sits at the centre of a signed range, so
    // "flat" is a number this can check rather than a claim.
    {
        const auto& eq = rhino::forge::fxTypes()[static_cast<size_t>(FxType::equaliser)];
        requireClose(rhino::forge::fxScaled(eq.init[2], -18.0f, 18.0f), 0.0f, 0.01f,
                     "an equaliser's low band opens at no gain at all");
        requireClose(rhino::forge::fxScaled(eq.init[5], -18.0f, 18.0f), 0.0f, 0.01f,
                     "an equaliser's high band opens at no gain at all");
    }
    // A reverb and a delay are the two that usually sit on the main output, so
    // they are the two that must not arrive fully wet.
    for (const auto type : {FxType::reverb, FxType::delay})
        require(rhino::forge::fxTypes()[static_cast<size_t>(type)].initMix < 0.5f,
                "a reverb and a delay open mostly dry, because they are usually placed on MAIN");

    // --- The matrix reaches the rack ------------------------------------------
    //
    // FX run on the summed voices, so a per-voice source has to resolve to one
    // voice's value rather than to none. What is checked here is that it
    // arrives at all: a macro is a steady source, so the same patch at two
    // macro settings has to render differently.
    rhino::forge::Processor modulated;
    soloSineOnA(modulated);
    place(modulated, 0, 0, FxType::filter);
    knob(modulated, 0, 0, 0, 0.05f);
    knob(modulated, 0, 0, 1, 0.0f);
    const auto cutoffSlot = rhino::forge::fxDestinationOf(0, 0, 0);
    require(cutoffSlot > 0 && cutoffSlot < rhino::forge::destinationCount,
            "a rack knob has a destination index inside the list");
    requireText(juce::String(rhino::forge::destinations()[static_cast<size_t>(cutoffSlot)].id),
                rhino::forge::fxParameterId(0, 0, "Knob1"),
                "the destination list names the parameter it claims to");
    // Slots are numbered from one here, as the parameter ids are.
    setSlot(modulated, 1, static_cast<float>(rhino::forge::ModSource::macro1),
            static_cast<float>(cutoffSlot), 1.0f);
    setValue(modulated, "macro1", 0.0f);
    renderNote(modulated, buffer);
    const auto closed = brightness(buffer, 0, 1024);
    setValue(modulated, "macro1", 1.0f);
    renderNote(modulated, buffer);
    require(brightness(buffer, 0, 1024) > closed * 1.2f,
            "a macro pointed at a rack knob opens it");
}

void envelopeSuite()
{
    // ENV 1 is hardwired to amplitude and is the one envelope Forge has, so its
    // timing is the timing of every note.
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    enum Stage { idle, attack, decay, sustain, release };

    rhino::forge::Processor processor;
    soloSineOnA(processor);
    setValue(processor, "env1Attack", 0.1f);
    setValue(processor, "env1Decay", 0.1f);
    setValue(processor, "env1Sustain", 0.5f);
    setValue(processor, "env1Release", 0.2f);
    processor.prepareToPlay(rate, block);

    juce::AudioBuffer<float> buffer(2, block);
    const auto advance = [&] (int samples, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < samples; rendered += block)
        {
            processor.processBlock(buffer, events);
            events.clear();
        }
    };

    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);

    // A tenth of the way into a 100 ms attack, the envelope is a tenth up.
    advance(static_cast<int>(rate * 0.01), noteOn);
    require(processor.envelopeStage() == attack, "a new note starts in attack");
    requireClose(processor.envelopeLevel(), 0.1f, 0.03f, "the attack climbs linearly");

    // Past the attack, into decay, and on to the sustain level.
    // 110 ms in: the 100 ms attack has finished and 10 ms of a 100 ms decay has
    // run, so the level has fallen a tenth of the way from 1.0 towards 0.5.
    advance(static_cast<int>(rate * 0.1));
    require(processor.envelopeStage() == decay, "the envelope reaches decay after the attack time");
    requireClose(processor.envelopeLevel(), 0.95f, 0.02f, "decay falls from the peak towards sustain");

    advance(static_cast<int>(rate * 0.12));
    require(processor.envelopeStage() == sustain, "the envelope settles into sustain after the decay time");
    requireClose(processor.envelopeLevel(), 0.5f, 0.02f, "sustain holds at the sustain level");

    advance(static_cast<int>(rate * 0.2));
    require(processor.envelopeStage() == sustain, "sustain holds for as long as the note is held");
    requireClose(processor.envelopeLevel(), 0.5f, 0.02f, "sustain does not drift");

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
    advance(static_cast<int>(rate * 0.1), noteOff);
    require(processor.envelopeStage() == release, "releasing the note enters release");
    requireClose(processor.envelopeLevel(), 0.25f, 0.03f, "release falls from the held level");

    advance(static_cast<int>(rate * 0.15));
    require(processor.envelopeStage() == idle, "the envelope reaches idle after the release time");
    requireClose(processor.envelopeLevel(), 0.0f, 0.001f, "an idle envelope is silent");

    // Released mid-attack, the fall starts from the level actually reached,
    // not from the sustain level the note never got to.
    rhino::forge::Processor early;
    soloSineOnA(early);
    setValue(early, "env1Attack", 1.0f);
    setValue(early, "env1Decay", 0.1f);
    setValue(early, "env1Sustain", 0.9f);
    setValue(early, "env1Release", 0.2f);
    early.prepareToPlay(rate, block);

    const auto advanceEarly = [&] (int samples, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < samples; rendered += block)
        {
            early.processBlock(buffer, events);
            events.clear();
        }
    };

    advanceEarly(static_cast<int>(rate * 0.2), noteOn);
    const auto reached = early.envelopeLevel();
    require(early.envelopeStage() == attack, "a long attack is still climbing after 200 ms");
    requireClose(reached, 0.2f, 0.03f, "a fifth of the way up a one-second attack");

    advanceEarly(static_cast<int>(rate * 0.1), noteOff);
    require(early.envelopeStage() == release, "a note released mid-attack enters release");
    require(early.envelopeLevel() < reached,
            "a note released mid-attack falls rather than continuing to climb");
    requireClose(early.envelopeLevel(), reached * 0.5f, 0.03f,
                 "the fall starts from the level actually reached, not from sustain");

    advanceEarly(static_cast<int>(rate * 0.15));
    require(early.envelopeStage() == idle,
            "a release from a partial attack still takes the full release time");
}

bool identical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(channel, i) != b.getSample(channel, i)) return false;
    return true;
}

// Destination indices, matching rhino::forge::destinations().
enum Destination { destOff = 0, destAPitch = 5, destSub = 11, destCutoff = 13 };

void setSlot(rhino::forge::Processor& processor, int slot, float source, float destination, float depth)
{
    const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
    setValue(processor, id("Source").toRawUTF8(), source);
    setValue(processor, id("Dest").toRawUTF8(), destination);
    setValue(processor, id("Depth").toRawUTF8(), depth);
}

// A patch with a filter that is closed enough for cutoff modulation to be
// plainly audible, and no other slot interfering.
void closedFilterOnA(rhino::forge::Processor& processor)
{
    soloSineOnA(processor);
    // A saw, for the harmonics the filter is there to remove. Named through the
    // table rather than written as a bare number: the position a shape sits at
    // moves whenever the table gains a frame, and a test that hard-codes one is
    // silently measuring a different sound afterwards.
    setValue(processor, "oscAPosition", 6.0f / 9.0f);
    setValue(processor, "filterEnable", 1.0f);
    setValue(processor, "routeA", 1.0f);
    setValue(processor, "cutoff", 300.0f);
    setValue(processor, "resonance", 0.0f);
    for (int slot = 1; slot <= rhino::forge::modSlotCount; ++slot)
        setSlot(processor, slot, srcOff, destOff, 0.0f);
}

void modulationSuite()
{
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> plain(2, samples), modulated(2, samples);

    // A slot at zero depth must cost nothing at all, not merely almost nothing:
    // an idle matrix may not colour the sound.
    rhino::forge::Processor bare;
    closedFilterOnA(bare);
    renderNote(bare, plain);

    rhino::forge::Processor wired;
    closedFilterOnA(wired);
    setSlot(wired, 1, srcLfo1, destCutoff, 0.0f);
    renderNote(wired, modulated);
    require(identical(plain, modulated), "a slot at zero depth renders bit-identically to no slot");

    // Pointed at nothing, a slot with depth is equally inert.
    rhino::forge::Processor unpointed;
    closedFilterOnA(unpointed);
    setSlot(unpointed, 1, srcLfo1, destOff, 1.0f);
    renderNote(unpointed, modulated);
    require(identical(plain, modulated), "a slot with no destination renders bit-identically");

    // With depth, the envelope opens the filter and more gets through.
    rhino::forge::Processor swept;
    closedFilterOnA(swept);
    setSlot(swept, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(swept, modulated);
    const auto closed = rms(plain, 0, settled);
    const auto opened = rms(modulated, 0, settled);
    require(opened > closed * 1.2f, "an envelope pointed at the cutoff opens the filter");

    // Two slots on one destination sum, rather than one winning.
    rhino::forge::Processor halves;
    closedFilterOnA(halves);
    setSlot(halves, 1, srcEnv1, destCutoff, 0.5f);
    setSlot(halves, 2, srcEnv1, destCutoff, 0.5f);
    juce::AudioBuffer<float> summed(2, samples);
    renderNote(halves, summed);
    require(identical(modulated, summed), "two half-depth slots sum to one full-depth slot");

    // Switching a slot's source off restores the unmodulated render exactly.
    setSlot(swept, 1, srcOff, destCutoff, 1.0f);
    renderNote(swept, modulated);
    require(identical(plain, modulated), "switching a slot's source off restores the plain render");

    // What the knobs draw is published from the same reading the voice renders
    // with, so a ring that moves is proof the engine moved the value, not a
    // second guess at it from the UI.
    rhino::forge::Processor watched;
    closedFilterOnA(watched);
    require(watched.modulationOffset(destCutoff) == 0.0f,
            "an idle matrix publishes no offset for a knob to draw");

    setSlot(watched, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(watched, modulated);
    const auto published = watched.modulationOffset(destCutoff);
    require(published > 0.0f, "an envelope pointed at the cutoff publishes an offset to draw");
    require(published <= 1.0f, "a unipolar source at full depth cannot publish past full travel");
    // At full depth from ENV 1 the offset is the envelope itself, so the ring on
    // the cutoff knob and the curve on ENV 1's display cannot disagree.
    require(published == watched.envelopeLevel(),
            "the offset drawn on a knob is the same reading ENV 1's own display draws");
    require(watched.modulationOffset(destSub) == 0.0f,
            "a destination nothing points at publishes nothing");

    // Pointing the same slot somewhere else has to release the knob it left.
    setSlot(watched, 1, srcEnv1, destSub, 1.0f);
    renderNote(watched, modulated);
    require(watched.modulationOffset(destCutoff) == 0.0f,
            "a destination a slot has left goes back to publishing nothing");

    // A source only reaches a destination through a voice, so with nothing
    // sounding there is no modulated value and the knobs have nothing to
    // animate. This holds for a macro too, which is the case that looks most
    // like it ought to be an exception: the hand is on the macro, but until a
    // note is played the macro is moving nothing.
    rhino::forge::Processor idle;
    closedFilterOnA(idle);
    setSlot(idle, 1, static_cast<float>(rhino::forge::ModSource::macro1), destCutoff, 1.0f);
    setValue(idle, "macro1", 0.5f);
    idle.prepareToPlay(48000.0, samples);
    juce::AudioBuffer<float> silence(2, samples);
    juce::MidiBuffer noNotes;
    silence.clear();
    idle.processBlock(silence, noNotes);
    require(idle.modulationOffset(destCutoff) == 0.0f,
            "nothing sounding publishes no offset, so the rings stay still");

    // And the same patch under a note does publish, so the check above is
    // measuring silence rather than a routing that was never live.
    renderNote(idle, modulated);
    requireClose(idle.modulationOffset(destCutoff), 0.5f, 0.001f,
                 "the same macro publishes its offset once a note is sounding");


    // --- Polarity -------------------------------------------------------------
    //
    // BI centres a source that only rises, so the destination's own setting
    // becomes the middle of the reach rather than the bottom of it. A macro at
    // rest then pulls the destination as far negative as the depth goes, where
    // UNI leaves it alone.
    //
    // This is checked on the published offset rather than on a render, because
    // the claim is about the arithmetic and not about what a filter does with
    // it. A Serum patch copied knob for knob came out an octave wrong on its
    // pitch destination for exactly the want of this switch, which is why the
    // whole travel is pinned here rather than only that it does something.
    {
        const auto offsetFor = [samples] (float source, float macro, bool bipolar)
        {
            rhino::forge::Processor probe;
            closedFilterOnA(probe);
            setSlot(probe, 1, source, static_cast<float>(destCutoff), 1.0f);
            setValue(probe, "mod1Bipolar", bipolar ? 1.0f : 0.0f);
            setValue(probe, "macro1", macro);
            juce::AudioBuffer<float> rendered(2, samples);
            renderNote(probe, rendered);
            return probe.modulationOffset(destCutoff);
        };
        constexpr auto macro1 = static_cast<float>(rhino::forge::ModSource::macro1);

        requireClose(offsetFor(macro1, 0.0f, false), 0.0f, 0.001f,
                     "UNI leaves a source at rest moving nothing");
        requireClose(offsetFor(macro1, 0.5f, false), 0.5f, 0.001f,
                     "UNI reaches half the depth from half the source");
        requireClose(offsetFor(macro1, 1.0f, false), 1.0f, 0.001f,
                     "UNI reaches the whole depth from the top of the source");

        requireClose(offsetFor(macro1, 0.0f, true), -0.5f, 0.001f,
                     "BI at rest pulls as far negative as the depth reaches");
        requireClose(offsetFor(macro1, 0.5f, true), 0.0f, 0.001f,
                     "BI at half leaves the destination where it was set");
        requireClose(offsetFor(macro1, 1.0f, true), 0.5f, 0.001f,
                     "BI at the top reaches as far positive, the reach recentred not resized");
    }

    // An LFO already swings both ways, so the switch has nothing to centre and
    // must not quietly double its reach instead.
    {
        rhino::forge::Processor uni, bi;
        closedFilterOnA(uni);
        closedFilterOnA(bi);
        setSlot(uni, 1, srcLfo1, destCutoff, 1.0f);
        setSlot(bi, 1, srcLfo1, destCutoff, 1.0f);
        setValue(bi, "mod1Bipolar", 1.0f);
        juce::AudioBuffer<float> unipolar(2, samples), bipolar(2, samples);
        renderNote(uni, unipolar);
        renderNote(bi, bipolar);
        require(identical(unipolar, bipolar),
                "BI leaves an LFO alone, which already swings both ways");
    }

    // Off to begin with, so every preset written before the switch existed
    // still modulates exactly as it did.
    require(value(bare, "mod1Bipolar") == 0.0f, "a slot starts unipolar");

    // Depth clamps at the destination's own limits instead of running past them.
    rhino::forge::Processor slammed;
    closedFilterOnA(slammed);
    setValue(slammed, "cutoff", 18000.0f);
    setSlot(slammed, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(slammed, modulated);
    require(allSamplesFinite(modulated), "modulation past a parameter's top stays finite");
    rhino::forge::Processor atTop;
    closedFilterOnA(atTop);
    setValue(atTop, "cutoff", 18000.0f);
    renderNote(atTop, plain);
    require(identical(plain, modulated),
            "modulating a parameter already at its maximum changes nothing");

    // Velocity is a source like any other, and a softer note modulates less.
    const auto atVelocity = [&] (float velocity)
    {
        rhino::forge::Processor processor;
        closedFilterOnA(processor);
        setSlot(processor, 1, srcVelocity, destCutoff, 1.0f);
        processor.prepareToPlay(48000.0, samples);
        juce::AudioBuffer<float> buffer(2, samples);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, velocity), 0);
        processor.processBlock(buffer, midi);
        // Measured as brightness rather than level, so this cannot be
        // satisfied merely by a hard note being louder than a soft one.
        return brightness(buffer, 0, settled);
    };
    require(atVelocity(1.0f) > atVelocity(0.25f) * 1.1f,
            "a harder note opens a velocity-driven filter further");

    // Pitch is reachable now that Semitone is continuous, which is what
    // replaced the old hardwired LFO-to-pitch knob.
    rhino::forge::Processor bent;
    soloSineOnA(bent);
    for (int slot = 1; slot <= rhino::forge::modSlotCount; ++slot)
        setSlot(bent, slot, srcOff, destOff, 0.0f);
    renderNote(bent, plain);
    const auto atPitch = zeroCrossings(plain, 0, settled);
    setSlot(bent, 1, srcEnv1, destAPitch, 1.0f);
    renderNote(bent, modulated);
    require(zeroCrossings(modulated, 0, settled) > atPitch,
            "an envelope pointed at pitch raises the note");

    // Macros are sources like any other, and reach their target only through
    // the matrix.
    const auto firstMacro = static_cast<float>(rhino::forge::ModSource::macro1);
    require(rhino::forge::modSourceCount == static_cast<int>(rhino::forge::ModSource::macro1)
                + rhino::forge::macroCount,
            "every macro is offered as a source");

    rhino::forge::Processor byMacro;
    closedFilterOnA(byMacro);
    setValue(byMacro, "macro1", 1.0f);
    renderNote(byMacro, plain);
    require(rms(plain, 0, settled) > 0.0f, "a macro alone changes nothing until it is routed");

    setSlot(byMacro, 1, firstMacro, destCutoff, 1.0f);
    renderNote(byMacro, modulated);
    require(brightness(modulated, 0, settled) > brightness(plain, 0, settled) * 1.1f,
            "a macro turned up opens the filter it is pointed at");

    setValue(byMacro, "macro1", 0.0f);
    renderNote(byMacro, modulated);
    require(identical(plain, modulated), "a macro at zero leaves its target exactly where it was");

    // And a source that never moves still behaves: NOTE is constant per voice.
    rhino::forge::Processor byNote;
    closedFilterOnA(byNote);
    setValue(byNote, "subEnable", 1.0f);
    setValue(byNote, "subLevel", 0.0f);
    renderNote(byNote, plain, 36);
    setSlot(byNote, 1, srcNote, destSub, 1.0f);
    renderNote(byNote, modulated, 36);
    require(!identical(plain, modulated), "the note source reaches its destination");
    require(rms(modulated, 0, settled) > rms(plain, 0, settled),
            "note-driven modulation adds the sub it was pointed at");
}

// A host that reports one fixed tempo and nothing else, so a synced LFO can be
// checked against a BPM the test controls.
class FixedTempo final : public juce::AudioPlayHead
{
public:
    explicit FixedTempo(double beatsPerMinute) : bpm(beatsPerMinute) {}

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(bpm);
        return info;
    }

    double bpm = 120.0;
};

// ENV 2-4: the same shape as ENV 1, wired to nothing. What has to be true of an
// auxiliary envelope is that it is silent until something points at it, that it
// then reaches whatever that is, and that it keeps times of its own rather than
// following the amplitude's.
void auxEnvelopeSuite()
{
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    enum Stage { idle, attack, decay, sustain, release };

    // Nothing routed: an auxiliary envelope may not colour the sound at all,
    // whatever its knobs are set to. Bit-identical rather than nearly so — a
    // second envelope leaking into the signal path is exactly the hardwired
    // filter envelope this design set out to remove.
    juce::AudioBuffer<float> plain(2, samples), moved(2, samples);
    rhino::forge::Processor quiet;
    closedFilterOnA(quiet);
    renderNote(quiet, plain);
    for (int env = 1; env < rhino::forge::envCount; ++env)
    {
        setValue(quiet, rhino::forge::envParameterId(env, "Attack").toRawUTF8(), 2.0f);
        setValue(quiet, rhino::forge::envParameterId(env, "Decay").toRawUTF8(), 0.01f);
        setValue(quiet, rhino::forge::envParameterId(env, "Sustain").toRawUTF8(), 0.0f);
        setValue(quiet, rhino::forge::envParameterId(env, "Release").toRawUTF8(), 4.0f);
    }
    renderNote(quiet, moved);
    require(identical(plain, moved),
            "an envelope nothing points at changes nothing, however it is set");

    // Each one reaches the matrix as a source of its own, checked the way the
    // six LFOs were: point it at a closed filter and hear the filter open.
    const auto closed = rms(plain, 0, settled);
    for (int env = 0; env < rhino::forge::envCount; ++env)
    {
        rhino::forge::Processor swept;
        closedFilterOnA(swept);
        setSlot(swept, 1, static_cast<float>(srcEnv1 + env), destCutoff, 1.0f);
        renderNote(swept, moved);
        require(rms(moved, 0, settled) > closed * 1.2f,
                "every envelope opens a filter it is pointed at");
        if (!(rms(moved, 0, settled) > closed * 1.2f))
            std::cerr << "       envelope " << env + 1 << " reaches nothing\n";
    }

    // Four envelopes under one note, each running its own shape. ENV 1 is long
    // since settled while ENV 2 is still climbing and ENV 3 has already fallen
    // to nothing, which no single shared shape could do.
    rhino::forge::Processor apart;
    soloSineOnA(apart);
    setValue(apart, "env1Attack", 0.01f);
    setValue(apart, "env1Decay", 0.01f);
    setValue(apart, "env1Sustain", 0.75f);
    setValue(apart, "env2Attack", 1.0f);
    setValue(apart, "env3Attack", 0.001f);
    setValue(apart, "env3Decay", 0.02f);
    setValue(apart, "env3Sustain", 0.0f);
    apart.prepareToPlay(rate, block);

    juce::AudioBuffer<float> buffer(2, block);
    const auto advance = [&] (int count, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < count; rendered += block)
        {
            apart.processBlock(buffer, events);
            events.clear();
        }
    };

    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    advance(static_cast<int>(rate * 0.2), noteOn);

    require(apart.envelopeStage(0) == sustain, "ENV 1 has settled 200 ms into the note");
    requireClose(apart.envelopeLevel(0), 0.75f, 0.02f, "ENV 1 holds its own sustain");
    require(apart.envelopeStage(1) == attack, "ENV 2 is still climbing an attack of its own");
    requireClose(apart.envelopeLevel(1), 0.2f, 0.03f,
                 "a fifth of the way up ENV 2's one-second attack");
    require(apart.envelopeStage(2) == sustain, "ENV 3 has reached its own sustain");
    requireClose(apart.envelopeLevel(2), 0.0f, 0.001f, "ENV 3 sustains at nothing");
    // ENV 4 is untouched, so it is still running the defaults every envelope
    // opens on: a 10 ms attack long past, and 190 ms of a 240 ms decay from the
    // peak towards a sustain of 0.75.
    require(apart.envelopeStage(3) == decay, "an envelope left alone runs the default shape");
    requireClose(apart.envelopeLevel(3), 0.80f, 0.02f,
                 "and is most of the way down that decay 200 ms in");

    // The key that started them releases them all. An auxiliary envelope that
    // only fell when the amplitude did would be a shape with no release of its
    // own, which is half a control.
    const auto reached = apart.envelopeLevel(1);
    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
    advance(static_cast<int>(rate * 0.05), noteOff);
    require(apart.envelopeStage(1) == release, "lifting the key releases ENV 2 as well as ENV 1");
    require(apart.envelopeLevel(1) < reached,
            "ENV 2 falls from the level it had reached rather than climbing on");

    // And once the voice has gone there is no reading at all, for the same
    // reason a knob's ring stops moving: a source reaches anything only through
    // a voice, so with no voice there is nothing to report.
    advance(static_cast<int>(rate * 1.0));
    for (int env = 0; env < rhino::forge::envCount; ++env)
    {
        require(apart.envelopeStage(env) == idle, "every envelope is idle once the voice has gone");
        requireClose(apart.envelopeLevel(env), 0.0f, 0.001f, "an idle envelope reads nothing");
    }

    // The ring on a knob and the curve on the display are one reading, for an
    // auxiliary envelope exactly as for ENV 1.
    rhino::forge::Processor watched;
    closedFilterOnA(watched);
    setSlot(watched, 1, static_cast<float>(srcEnv1 + 1), destCutoff, 1.0f);
    renderNote(watched, moved);
    require(watched.modulationOffset(destCutoff) == watched.envelopeLevel(1),
            "the offset drawn on a knob is the same reading ENV 2's own display draws");
}

void lfoSuite()
{
    using rhino::forge::LfoShape;
    constexpr int samples = 8192;

    // Every shape has to stay inside the bounds a source promises, and has to
    // come back to where it started after exactly one cycle. A source that ran
    // past one would push a destination further than its depth allows.
    for (int shape = 0; shape < rhino::forge::lfoShapeCount; ++shape)
    {
        const auto which = static_cast<LfoShape>(shape);
        auto extreme = 0.0f;
        for (int i = 0; i <= 512; ++i)
        {
            const auto phase = static_cast<float>(i) / 512.0f;
            const auto at = rhino::forge::lfoWave(which, phase, 1.0f);
            require(std::isfinite(at), "an LFO shape is finite everywhere");
            extreme = std::max(extreme, std::abs(at));
        }
        require(extreme <= 1.0f, "an LFO shape stays inside plus or minus one");
        require(extreme > 0.9f, "an LFO shape uses the range it is given");
    }

    // Sine and triangle join up at the cycle boundary. A saw and a square jump
    // a full swing there instead, and that jump is the shape rather than a
    // fault â€” checked so neither can be quietly smoothed away.
    for (const auto continuous : {LfoShape::sine, LfoShape::triangle})
        requireClose(rhino::forge::lfoWave(continuous, 0.0f, 0.0f),
                     rhino::forge::lfoWave(continuous, 1.0f, 0.0f), 0.0001f,
                     "a continuous LFO shape joins up across the cycle");
    for (const auto stepped : {LfoShape::saw, LfoShape::square})
        require(std::abs(rhino::forge::lfoWave(stepped, 0.0f, 0.0f)
                         - rhino::forge::lfoWave(stepped, 0.999f, 0.0f)) > 1.5f,
                "a saw and a square jump a full swing at the cycle boundary");

    // Sample and hold is its held step and nothing else: it does not move
    // within a cycle, which is what makes it a step rather than a ramp.
    for (const auto phase : {0.0f, 0.2f, 0.75f, 0.99f})
        requireClose(rhino::forge::lfoWave(LfoShape::sampleHold, phase, -0.4f), -0.4f, 0.0001f,
                     "sample and hold holds its step for the whole cycle");

    // Shape is a real choice, not a relabelled sine: the four continuous shapes
    // have to differ from each other somewhere.
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
        {
            auto differs = false;
            for (int i = 0; i < 64; ++i)
            {
                const auto phase = static_cast<float>(i) / 64.0f;
                if (std::abs(rhino::forge::lfoWave(static_cast<LfoShape>(a), phase, 0.0f)
                             - rhino::forge::lfoWave(static_cast<LfoShape>(b), phase, 0.0f)) > 0.01f)
                    differs = true;
            }
            require(differs, "no two LFO shapes are the same curve");
        }

    // --- The three modes ------------------------------------------------------
    //
    // Driven through the Core rather than the Processor: the question is what
    // the phase does across a note, which is exactly what the Core owns and
    // what a block of audio would only let us infer.
    const auto runCore = [] (rhino::forge::Core& core, const rhino::forge::Patch& patch, int count)
    {
        float left = 0.0f, right = 0.0f;
        for (int i = 0; i < count; ++i) core.renderSample(patch, left, right);
    };
    // One cycle a second at 48 kHz, so a count of samples is a share of a cycle
    // and every check below reads as a fraction of one.
    constexpr int cycle = 48000;
    const auto patchFor = [] (rhino::forge::LfoMode mode)
    {
        rhino::forge::Patch patch;
        auto& first = patch.lfos.front();
        first.rate = 1.0f;
        first.shape = static_cast<float>(rhino::forge::LfoShape::saw);
        first.mode = static_cast<float>(mode);
        // One voice, so the phase the Core publishes is that voice's own. An
        // LFO that answers the keyboard lives inside the voice now, and what
        // reaches the panel is the loudest voice's copy of it — which is the
        // point of the per-voice check further down, but here it would only get
        // in the way of asking what one note does.
        patch.polyphony = 1.0f;
        return patch;
    };
    // The phase is published while rendering, so a note has to be given a
    // sample before what it did to its LFO can be read back.
    const auto phaseAfterNote = [&] (rhino::forge::Core& core, const rhino::forge::Patch& patch,
                                     int note)
    {
        core.noteOn(note, 1.0f, patch);
        runCore(core, patch, 1);
        return core.lfoPosition(0);
    };

    for (const auto keyed : {rhino::forge::LfoMode::trigger, rhino::forge::LfoMode::envelope})
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(keyed);
        // Nothing is sounding, so the shape waits at the start rather than
        // running on where nobody can hear it. The indicator is then sitting
        // exactly where the next key press will start it from.
        runCore(core, patch, cycle / 4);
        requireClose(core.lfoPosition(0), 0.0f, 0.0001f,
                     "a key-synced LFO waits at the start until a note arrives");
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > 0.2f, "a key-synced LFO runs once a note is playing");
        requireClose(phaseAfterNote(core, patch, 60), 0.0f, 0.0001f,
                     "a new note restarts a key-synced LFO");
    }

    // And it comes back to the start when the last voice has gone, having run
    // on through the release tail rather than being cut dead at note-off.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(rhino::forge::LfoMode::trigger);
        patch.envs[rhino::forge::ampEnv].attack = 0.001f;
        patch.envs[rhino::forge::ampEnv].decay = 0.001f;
        patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
        patch.envs[rhino::forge::ampEnv].release = 0.005f;
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > 0.2f, "TRIG runs while the note is held");

        core.noteOff(57);
        const auto atRelease = core.lfoPosition(0);
        runCore(core, patch, 100);
        require(core.lfoPosition(0) > atRelease, "TRIG keeps running through the release tail");
        runCore(core, patch, cycle / 2);
        requireClose(core.lfoPosition(0), 0.0f, 0.0001f,
                     "TRIG returns to the start once nothing is sounding");
    }

    // OFF is the mode that keeps its place, which is the whole reason to have
    // it: a free-running LFO does not jump every time a key goes down.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(rhino::forge::LfoMode::free);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        require(before > 0.2f, "a free-running LFO runs before a note arrives");
        requireClose(phaseAfterNote(core, patch, 57), before, 0.001f,
                     "a note does not restart a free-running LFO");
    }

    // TRIG loops for as long as the note is held: past the end of the cycle it
    // comes round again rather than stopping.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(rhino::forge::LfoMode::trigger);
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle + cycle / 4);
        const auto wrapped = core.lfoPosition(0);
        require(wrapped > 0.1f && wrapped < 0.5f, "TRIG comes round again at the end of the cycle");
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > wrapped, "TRIG keeps running after it has wrapped");
    }

    // ENV is the same restart followed by a full stop on the last point of the
    // shape. A saw ends at the top, so the held value is the one thing a
    // one-shot envelope is for: it stays where the shape left it.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(rhino::forge::LfoMode::envelope);
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle + cycle / 4);
        requireClose(core.lfoPosition(0), 1.0f, 0.0001f, "ENV stops at the end of its shape");
        requireClose(core.lfoOutput(0), 1.0f, 0.001f, "ENV holds the value the shape ended on");
        runCore(core, patch, 4 * cycle);
        requireClose(core.lfoPosition(0), 1.0f, 0.0001f, "ENV stays stopped however long it is left");

        // And the next note starts it over, or it would be a one-shot that only
        // ever fired once.
        requireClose(phaseAfterNote(core, patch, 60), 0.0f, 0.0001f,
                     "a new note restarts a stopped ENV");
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > 0.2f, "a restarted ENV runs again");
    }

    // A legato note in mono did not lift a key, so it does not restart the
    // shape â€” the same rule the amp envelope already follows.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(rhino::forge::LfoMode::trigger);
        patch.mono = 1.0f;
        patch.legato = 1.0f;
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        requireClose(phaseAfterNote(core, patch, 60), before, 0.001f,
                     "a legato note does not restart the LFO");

        // Without legato it is a new note again, and it does.
        patch.legato = 0.0f;
        requireClose(phaseAfterNote(core, patch, 62), 0.0f, 0.0001f,
                     "a mono note without legato restarts the LFO");
    }

    // --- Six of them, and each one per voice -----------------------------------
    //
    // An LFO that answers the keyboard lives inside the voice, so a new note
    // restarts its own copy and leaves a note already sounding alone. One LFO
    // shared by every voice meant a second key jerked whatever the first was
    // driving, part-way through a note.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(rhino::forge::LfoMode::trigger);
        patch.polyphony = 8.0f;
        core.noteOn(45, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        require(before > 0.2f, "the first note's LFO is running");

        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, 64);
        // What reaches the panel is the loudest voice's copy, and that is still
        // the note that has been sounding — so its own cycle carried straight on
        // across the new one.
        const auto after = core.lfoPosition(0);
        require(after > before, "a second note leaves the first note's LFO running");
        requireClose(after, before, 0.01f, "a second note does not jump the first note's LFO");
    }

    // The six are independent: each runs at its own rate rather than six views
    // of one cycle.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        rhino::forge::Patch patch;
        for (int i = 0; i < rhino::forge::lfoCount; ++i)
        {
            patch.lfos[static_cast<size_t>(i)].rate = 1.0f + static_cast<float>(i);
            patch.lfos[static_cast<size_t>(i)].mode = static_cast<float>(rhino::forge::LfoMode::free);
        }
        runCore(core, patch, cycle / 8);
        for (int i = 0; i < rhino::forge::lfoCount; ++i)
            for (int j = i + 1; j < rhino::forge::lfoCount; ++j)
                require(std::abs(core.lfoPosition(i) - core.lfoPosition(j)) > 0.01f,
                        "no two LFOs are the same cycle");
    }

    // And each is a source in its own right, reachable from the matrix. Square
    // and free-running, so the source holds a steady +1 across the render
    // rather than sweeping through it.
    for (int lfo = 0; lfo < rhino::forge::lfoCount; ++lfo)
    {
        const auto level = [lfo] (float depth)
        {
            rhino::forge::Processor processor;
            soloSineOnA(processor);
            setValue(processor, "subEnable", 1.0f);
            setValue(processor, "subLevel", 0.0f);
            setValue(processor, rhino::forge::lfoParameterId(lfo, "Shape").toRawUTF8(),
                     static_cast<float>(rhino::forge::LfoShape::square));
            setValue(processor, rhino::forge::lfoParameterId(lfo, "Mode").toRawUTF8(),
                     static_cast<float>(rhino::forge::LfoMode::free));
            setValue(processor, rhino::forge::lfoParameterId(lfo, "RateUnit").toRawUTF8(), 0.0f);
            setValue(processor, rhino::forge::lfoParameterId(lfo, "Rate").toRawUTF8(), 0.05f);
            setSlot(processor, 1, static_cast<float>(srcLfo1 + lfo), destSub, depth);
            juce::AudioBuffer<float> rendered(2, samples);
            rendered.clear();
            renderNote(processor, rendered);
            return rms(rendered, 0, 1024);
        };
        require(level(1.0f) > level(0.0f) * 1.2f,
                "every LFO reaches the matrix as a source of its own");
    }

    // Every mode has a name of its own, or the stepper would show two the same.
    for (int a = 0; a < rhino::forge::lfoModeCount; ++a)
    {
        require(juce::String(rhino::forge::lfoModeName(a)).isNotEmpty(), "every LFO mode is named");
        for (int b = a + 1; b < rhino::forge::lfoModeCount; ++b)
            require(juce::String(rhino::forge::lfoModeName(a)) != rhino::forge::lfoModeName(b),
                    "no two LFO modes share a name");
    }

    // Each LFO's rate is resolved from its own parameters, not LFO 1's.
    {
        rhino::forge::Processor six;
        for (int lfo = 0; lfo < rhino::forge::lfoCount; ++lfo)
        {
            setValue(six, rhino::forge::lfoParameterId(lfo, "RateUnit").toRawUTF8(), 0.0f);
            setValue(six, rhino::forge::lfoParameterId(lfo, "Rate").toRawUTF8(), 1.0f + static_cast<float>(lfo));
        }
        for (int lfo = 0; lfo < rhino::forge::lfoCount; ++lfo)
            requireClose(six.lfoRateHz(lfo), 1.0f + static_cast<float>(lfo), 0.001f,
                         "each LFO reports its own rate");
    }

    // Free-running, the rate is the knob.
    rhino::forge::Processor free;
    setValue(free, "lfo1RateUnit", 0.0f);
    setValue(free, "lfo1Rate", 3.0f);
    requireClose(free.lfoRateHz(0), 3.0f, 0.001f, "an unsynced LFO runs at its rate knob");

    // Synced, the rate is a division of the host's tempo and the knob stops
    // mattering. 1/4 at 120 BPM is two beats a second, so two cycles a second.
    rhino::forge::Processor synced;
    setValue(synced, "lfo1RateUnit", 1.0f);
    setValue(synced, "lfo1Rate", 3.0f);
    setValue(synced, "lfo1Division", 2.0f);
    FixedTempo tempo(120.0);
    synced.setPlayHead(&tempo);
    juce::AudioBuffer<float> buffer(2, samples);
    juce::MidiBuffer midi;
    synced.prepareToPlay(48000.0, samples);
    synced.processBlock(buffer, midi);
    requireClose(synced.lfoRateHz(0), 2.0f, 0.001f, "a synced LFO divides the host tempo");

    // And it follows the tempo rather than latching the first one it saw.
    tempo.bpm = 60.0;
    synced.processBlock(buffer, midi);
    requireClose(synced.lfoRateHz(0), 1.0f, 0.001f, "a synced LFO tracks a tempo change");

    // A longer division is a slower cycle, in proportion.
    setValue(synced, "lfo1Division", 0.0f);   // 1/1, a bar of four beats
    requireClose(synced.lfoRateHz(0), 0.25f, 0.001f, "a whole-bar division is four beats long");

    // The phase the display draws has to be the one the voice is reading, and
    // it has to move.
    // In OFF, because that is the mode that runs with nothing playing — which
    // is the case this check is about: the phase reaching the panel is the one
    // the voice is reading, and it moves.
    rhino::forge::Processor running;
    setValue(running, "lfo1RateUnit", 0.0f);
    setValue(running, "lfo1Rate", 1.0f);
    setValue(running, "lfo1Mode", static_cast<float>(rhino::forge::LfoMode::free));
    running.prepareToPlay(48000.0, samples);
    juce::MidiBuffer none;
    buffer.clear();
    running.processBlock(buffer, none);
    const auto first = running.lfoPhase(0);
    running.processBlock(buffer, none);
    const auto second = running.lfoPhase(0);
    require(first >= 0.0f && first < 1.0f, "the published LFO phase stays inside one cycle");
    require(second != first, "the published LFO phase advances with the blocks");

    // The same panel reading, in TRIG with nothing playing: parked at the start
    // rather than sweeping a display for a shape that is not running.
    rhino::forge::Processor idle;
    setValue(idle, "lfo1RateUnit", 0.0f);
    setValue(idle, "lfo1Rate", 1.0f);
    setValue(idle, "lfo1Mode", static_cast<float>(rhino::forge::LfoMode::trigger));
    idle.prepareToPlay(48000.0, samples);
    buffer.clear();
    idle.processBlock(buffer, none);
    idle.processBlock(buffer, none);
    requireClose(idle.lfoPhase(0), 0.0f, 0.0001f,
                 "the panel shows a key-synced LFO parked at the start with nothing playing");

    synced.setPlayHead(nullptr);
}

// How much energy a signal carries at a given harmonic of its fundamental. A
// plain correlation rather than a transform: the checks below ask about a
// handful of named harmonics, not a whole spectrum.
float harmonicEnergy(const std::vector<float>& cycle, int harmonic)
{
    auto real = 0.0, imaginary = 0.0;
    const auto points = static_cast<double>(cycle.size());
    for (size_t i = 0; i < cycle.size(); ++i)
    {
        const auto angle = 2.0 * juce::MathConstants<double>::pi * harmonic * static_cast<double>(i) / points;
        real += cycle[i] * std::cos(angle);
        imaginary += cycle[i] * std::sin(angle);
    }
    return static_cast<float>(2.0 * std::sqrt(real * real + imaginary * imaginary) / points);
}

// One cycle of a table's frame at a level, read the way the voice reads it.
std::vector<float> readFrame(const rhino::forge::Wavetable& table, int level, int frame, int points)
{
    std::vector<float> cycle(static_cast<size_t>(points));
    for (int i = 0; i < points; ++i)
        cycle[static_cast<size_t>(i)] = table.frameSample(level, frame,
                                                          static_cast<float>(i) / static_cast<float>(points));
    return cycle;
}

// Band-limiting is the whole reason a frame is stored more than once. These
// check the extra copies really are the same wave with harmonics taken off the
// top, rather than something the transform has scaled, shifted or mangled.
void bandLimitSuite()
{
    using rhino::forge::wavetableFrameSize;
    const auto& table = rhino::forge::builtInWavetable();

    require(table.frameCount() == rhino::forge::waveShapeCount,
            "the built-in table holds every declared frame");
    require(table.levelCount() > 1, "a table carries band-limited copies of its frames");

    // The one that catches a scaling mistake in the transform. A sine is a
    // single harmonic, so every level that keeps any harmonic at all has to
    // hand back that same sine at that same amplitude: not half of it, not
    // 2048 times it, not inverted.
    for (int level = 0; level < table.levelCount(); ++level)
        for (int i = 0; i < 64; ++i)
        {
            const auto phase = static_cast<float>(i) / 64.0f;
            requireClose(table.frameSample(level, 0, phase),
                         std::sin(phase * juce::MathConstants<float>::twoPi), 0.02f,
                         "a band-limited sine is the same sine at every level");
        }

    // A saw carries every harmonic, so it is what shows whether the levels are
    // cut where they say they are: each must keep what is below its limit and
    // have thrown away what is above it.
    constexpr auto saw = 6;
    for (int level = 1; level < table.levelCount(); ++level)
    {
        const auto limit = table.harmonicsAt(level);
        if (limit < 8 || limit >= wavetableFrameSize / 2) continue;
        const auto cycle = readFrame(table, level, saw, 8192);
        const auto kept = limit / 2;
        require(harmonicEnergy(cycle, kept) > 0.3f / static_cast<float>(kept),
                "a band-limited frame keeps the harmonics below its limit");
        // Everything above the cut, swept up to the stored Nyquist rather than
        // sampled at one harmonic. Past the stored Nyquist a probe measures the
        // interpolator's images and not the band-limiting, so it stops there —
        // and a single probe at twice the limit, which is what this used to be,
        // sat on the stored frame's DC image and so read near zero whatever the
        // transform had done.
        const auto top = table.sizeAt(level) / 2;
        const auto stride = std::max(1, (top - limit) / 32);
        auto leaked = 0.0f;
        for (int harmonic = limit + 1; harmonic <= top; harmonic += stride)
            leaked = std::max(leaked, harmonicEnergy(cycle, harmonic));
        require(leaked < 0.02f / static_cast<float>(limit),
                "a band-limited frame has thrown away the harmonics above its limit");
    }

    // Level 0 is the frame exactly as authored, because it is what the panel
    // draws. If the transform touched it, the display and the table would
    // disagree and M9a's guarantee would be gone.
    for (int shape = 0; shape < rhino::forge::waveShapeCount; ++shape)
        for (int i = 0; i < wavetableFrameSize; i += 37)
        {
            const auto phase = static_cast<float>(i) / static_cast<float>(wavetableFrameSize);
            requireClose(table.frameSample(0, shape, phase), rhino::forge::waveShape(shape, phase), 0.0005f,
                         "level 0 is the frame exactly as it was authored");
        }

    // The level a note is given has to be one whose harmonics all fit under
    // Nyquist, at every note Forge can be asked to play.
    constexpr double sampleRate = 48000.0;
    for (int note = 0; note <= 127; ++note)
    {
        const auto hz = static_cast<float>(440.0 * std::pow(2.0, (note - 69) / 12.0));
        const auto level = table.levelFor(hz, sampleRate);
        const auto harmonics = table.harmonicsAt(level);
        require(level == 0 || static_cast<double>(harmonics) * hz <= sampleRate * 0.5 + 1.0,
                "the level a note reads keeps its harmonics under Nyquist");
        // And the point of the finer spacing: it keeps most of what it could
        // have had, rather than as little as half of it. Only asked where a
        // note is entitled to enough harmonics for the spacing to be the thing
        // deciding; at the very top of the keyboard the counts are so small
        // that rounding them to whole harmonics is.
        const auto allowed = sampleRate * 0.5 / hz;
        require(level == 0 || allowed < 8.0 || static_cast<double>(harmonics) >= allowed * 0.7,
                "the level a note reads keeps most of the harmonics it was entitled to");
    }

    // And the point of the whole exercise: a high note aliases far less than
    // the raw frame does. Read straight from the table rather than through the
    // synth, so nothing but the band-limiting is being measured. Aliasing lands
    // between the note's harmonics, never on them, so the bins halfway between
    // are where a clean saw has nothing and a folded one does not.
    const auto hz = 4186.0f;
    constexpr int steps = 8192;
    std::vector<float> raw(steps), limited(steps);
    const auto level = table.levelFor(hz, sampleRate);
    for (int i = 0; i < steps; ++i)
    {
        const auto phase = static_cast<float>(std::fmod(static_cast<double>(i) * hz / sampleRate, 1.0));
        raw[static_cast<size_t>(i)] = table.frameSample(0, saw, phase);
        limited[static_cast<size_t>(i)] = table.frameSample(level, saw, phase);
    }
    auto rawFold = 0.0f, limitedFold = 0.0f;
    for (int bin = 40; bin < 3900; ++bin)
    {
        const auto binHz = static_cast<float>(bin) * static_cast<float>(sampleRate) / static_cast<float>(steps);
        const auto ofFundamental = binHz / hz;
        if (std::abs(ofFundamental - std::round(ofFundamental)) < 0.35f) continue;
        rawFold = std::max(rawFold, harmonicEnergy(raw, bin));
        limitedFold = std::max(limitedFold, harmonicEnergy(limited, bin));
    }
    if (limitedFold >= rawFold * 0.25f)
    {
        require(false, "band-limiting takes the aliasing off a high note");
        std::cerr << "       raw folded " << rawFold << ", band-limited folded " << limitedFold << '\n';
    }
}

void waveTableSuite()
{
    using rhino::forge::waveShape;
    using rhino::forge::waveShapeCount;

    // Every frame has to be finite, stay inside plus or minus one, and reach
    // full scale. The last of those is what keeps morphing level: if one frame
    // were quiet, sweeping POSITION across it would dip the oscillator.
    for (int shape = 0; shape < waveShapeCount; ++shape)
    {
        auto extreme = 0.0f;
        for (int i = 0; i < 2048; ++i)
        {
            const auto at = waveShape(shape, static_cast<float>(i) / 2048.0f);
            require(std::isfinite(at), "a wavetable frame is finite everywhere");
            extreme = std::max(extreme, std::abs(at));
        }
        if (extreme > 1.0f || extreme < 0.9f)
        {
            require(false, "a wavetable frame fills the range without leaving it");
            std::cerr << "       " << rhino::forge::waveShapeName(shape)
                      << " peaks at " << extreme << '\n';
        }
    }

    // Ten shapes, not one shape ten times. Checked against every other frame
    // rather than only its neighbour, so a duplicate anywhere in the table is
    // caught.
    for (int a = 0; a < waveShapeCount; ++a)
        for (int b = a + 1; b < waveShapeCount; ++b)
        {
            auto apart = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                const auto phase = static_cast<float>(i) / 512.0f;
                apart = std::max(apart, std::abs(waveShape(a, phase) - waveShape(b, phase)));
            }
            if (apart <= 0.05f)
            {
                require(false, "no two wavetable frames are the same shape");
                std::cerr << "       " << rhino::forge::waveShapeName(a) << " and "
                          << rhino::forge::waveShapeName(b) << '\n';
            }
        }

    // Landing on a frame's own position has to give that frame exactly, or the
    // names the knob reads out would be pointing at the wrong thing.
    for (int shape = 0; shape < waveShapeCount; ++shape)
    {
        const auto position = static_cast<float>(shape) / static_cast<float>(waveShapeCount - 1);
        for (int i = 0; i < 128; ++i)
        {
            const auto phase = static_cast<float>(i) / 128.0f;
            requireClose(rhino::forge::waveAt(position, phase), waveShape(shape, phase), 0.0005f,
                         "a position on a frame reads that frame exactly");
        }
        requireText(rhino::forge::waveLabel(position), rhino::forge::waveShapeName(shape),
                    "a position on a frame is named after it");
    }

    // The saw's jump belongs in the middle of its frame, not at the edge of it
    // where nothing can see it. Both halves climb, it crosses zero where the
    // frame begins and ends, and the whole swing happens in one step across the
    // centre. That is the difference between a display that reads as a saw and
    // one that reads as a single diagonal.
    {
        constexpr auto saw = 6;
        constexpr auto step = 1.0f / 2048.0f;
        requireClose(waveShape(saw, 0.0f), 0.0f, 0.001f, "the saw starts its frame at zero");
        requireClose(waveShape(saw, 0.5f - step), 1.0f, 0.005f, "the saw climbs to the top by the centre");
        requireClose(waveShape(saw, 0.5f), -1.0f, 0.001f, "the saw drops to the bottom at the centre");
        requireClose(waveShape(saw, 1.0f - step), 0.0f, 0.005f, "the saw returns to zero by the end");
        require(std::abs(waveShape(saw, 1.0f - step) - waveShape(saw, 0.0f)) < 0.01f,
                "the saw joins up across the cycle boundary");
    }

    // And between two frames it names both, so a blend never masquerades as a
    // shape you could have chosen.
    requireText(rhino::forge::waveLabel(0.5f / 9.0f), "SINE>TRI",
                "a position between two frames names both");

    // Morphing has to be continuous in position: a small move must not jump the
    // output, or modulating POSITION would click.
    for (int i = 1; i < 900; ++i)
    {
        const auto before = static_cast<float>(i) / 900.0f;
        const auto after = before + 0.001f;
        for (int k = 0; k < 32; ++k)
        {
            const auto phase = static_cast<float>(k) / 32.0f;
            const auto step = std::abs(rhino::forge::waveAt(after, phase)
                                       - rhino::forge::waveAt(before, phase));
            if (step > 0.05f)
            {
                require(false, "morphing POSITION moves the wave smoothly");
                std::cerr << "       " << before << " -> " << after
                          << " at phase " << phase << " jumped " << step << '\n';
            }
        }
    }
}

// The largest step from one sample to the next. A click is exactly that: a
// discontinuity the ear hears as a tick over the top of the note. Read on a
// signal made only of sines, so every legitimate step is bounded by the
// frequency and anything larger came from the engine cutting something off.
float largestStep(const juce::AudioBuffer<float>& buffer)
{
    auto worst = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        for (int i = 1; i < buffer.getNumSamples(); ++i)
            worst = std::max(worst, std::abs(data[i] - data[i - 1]));
    }
    return worst;
}

// Taking a voice away from a note that is still sounding must not be audible as
// anything but the new note arriving. A run of notes longer than the polyphony
// is the ordinary way to reach that, and it used to leave a tick on every note
// past the fourth.
void voiceStealSuite()
{
    constexpr int samples = 32768;
    constexpr int spacing = 3000;
    const int notes[] = {45, 52, 57, 61, 64, 68, 71, 76, 78, 81};

    const auto worstStep = [&] (float polyphony)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        // Both oscillators, which is where the user hears it: two of them make
        // the step twice the size.
        setValue(processor, "oscBEnable", 1.0f);
        setValue(processor, "oscBPosition", 0.0f);
        setValue(processor, "oscBUnison", 1.0f);
        setValue(processor, "oscBDetune", 0.0f);
        setValue(processor, "oscBSemitone", 0.0f);
        setValue(processor, "oscBLevel", 1.0f);
        setValue(processor, "polyphony", polyphony);
        // Long tails, so every voice is still sounding when the next note wants
        // one and the run really does have to take them.
        setValue(processor, "env1Release", 4.0f);
        setValue(processor, "env1Sustain", 1.0f);
        setValue(processor, "env1Attack", 0.01f);

        juce::AudioBuffer<float> buffer(2, samples);
        buffer.clear();
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (int i = 0; i < static_cast<int>(std::size(notes)); ++i)
            midi.addEvent(juce::MidiMessage::noteOn(1, notes[i], 1.0f), i * spacing);
        processor.processBlock(buffer, midi);
        return largestStep(buffer);
    };

    // Four voices for ten notes: six of them have to take a voice that is still
    // sounding. Sixteen voices for the same ten notes never steals at all, so
    // it is the same music with the steals taken out — which makes it the
    // reference for how large a step this material legitimately contains.
    const auto stealing = worstStep(4.0f);
    const auto roomy = worstStep(16.0f);
    require(roomy > 0.0f, "the reference run makes sound");
    require(stealing < roomy * 1.5f,
            "taking a voice from a sounding note is no louder a step than the notes themselves");
    if (stealing >= roomy * 1.5f)
        std::cerr << "       worst step " << stealing << " stealing, " << roomy << " with room" << std::endl;

    // And a voice is only ever taken when one genuinely has to be. A note that
    // has finished leaves a voice free, and the next note has to take that one
    // rather than whichever slot a rotation had reached — strict rotation would
    // silence a note still under the player's finger while a dead voice sat
    // beside it. The invariant that says so: while no more notes sound at once
    // than the patch has voices, raising the polyphony cannot change a sample.
    const auto sequence = [] (float polyphony, juce::AudioBuffer<float>& buffer)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "polyphony", polyphony);
        setValue(processor, "env1Sustain", 1.0f);
        setValue(processor, "env1Release", 0.01f);
        buffer.clear();
        processor.prepareToPlay(48000.0, buffer.getNumSamples());
        juce::MidiBuffer midi;
        // One note held throughout, a second struck and let go, and a third
        // arriving long after the second has died away. Never more than two at
        // once, so two voices are enough and nothing need ever be taken.
        midi.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 1000);
        midi.addEvent(juce::MidiMessage::noteOff(1, 57), 2000);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 12000);
        processor.processBlock(buffer, midi);
    };

    juce::AudioBuffer<float> tight(2, samples), spare(2, samples);
    sequence(2.0f, tight);
    sequence(8.0f, spare);
    require(rms(tight, 0, samples / 2) > 0.0f, "the two-voice run makes sound");
    require(identical(tight, spare),
            "a free voice is taken before a sounding one, so more polyphony than the music needs changes nothing");
}

// A voice is let go of, not cut off. Everything a voice makes passes through
// its filter, and a filter holds energy: at a low cutoff it is still ringing
// after the envelope that fed it has reached zero. Dropping the voice at that
// point truncates the ring, and the truncation is a click at the end of a note.
void voiceTailSuite()
{
    rhino::forge::Processor processor;
    // The setting that leaves the most in the filter to be cut off: a cutoff
    // near the bottom of its range with the sub at full level, a sine an octave
    // down being exactly what a low cutoff passes.
    setValue(processor, "cutoff", 54.0f);
    setValue(processor, "subEnable", 1.0f);
    setValue(processor, "subLevel", 1.0f);
    setValue(processor, "env1Release", 0.35f);

    constexpr int total = 96000;
    juce::AudioBuffer<float> buffer(2, total);
    buffer.clear();
    processor.prepareToPlay(48000.0, total);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 45), total / 4);
    processor.processBlock(buffer, midi);

    const auto* data = buffer.getReadPointer(0);
    auto peak = 0.0f;
    for (int i = 0; i < total; ++i) peak = std::max(peak, std::abs(data[i]));
    auto last = total - 1;
    while (last > 0 && data[last] == 0.0f) --last;

    require(peak > 0.01f, "the note sounds");
    require(last < total - 1, "the note has finished well inside the render");
    // Cutting the voice at the envelope's zero left this 39 dB below the peak
    // of the note, which against the silence after a note is plainly audible.
    require(std::abs(data[last]) < peak * 0.0005f,
            "a finished voice is faded out rather than cut off, so the filter's ring is not truncated");
    if (std::abs(data[last]) >= peak * 0.0005f)
        std::cerr << "       left " << std::abs(data[last]) << " against a peak of " << peak << std::endl;
}

void voicingSuite()
{
    // Mono collapses to a single voice, so polyphony stops meaning anything.
    // The panel greys the POLY knob out to say so; this checks the engine
    // agrees rather than the two drifting apart.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto chordLevel = [&buffer] (bool mono, float polyphony)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "mono", mono ? 1.0f : 0.0f);
        setValue(processor, "polyphony", polyphony);
        setValue(processor, "glide", 0.0f);
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (const auto note : {52, 57, 61})
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        processor.processBlock(buffer, midi);
        return rms(buffer, 0, settled);
    };

    const auto polyThree = chordLevel(false, 8.0f);
    const auto monoThree = chordLevel(true, 8.0f);
    require(polyThree > 0.0f && monoThree > 0.0f, "both voicings make sound");
    require(monoThree < polyThree * 0.8f, "mono plays one note where poly plays three");

    // And polyphony genuinely does nothing while mono is on.
    requireClose(chordLevel(true, 1.0f), monoThree, monoThree * 0.001f,
                 "polyphony does not affect a mono patch");
    requireClose(chordLevel(true, 16.0f), monoThree, monoThree * 0.001f,
                 "raising polyphony does not affect a mono patch either");

    // While in poly it very much does.
    require(chordLevel(false, 1.0f) < polyThree * 0.8f, "polyphony limits a polyphonic patch");
}


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
    // what the oscillator has been playing â€” the M9a guarantee, now that the
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
    // it while the message thread publishes over the top â€” which is the case
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
    // the oscillator â€” which it can only do if the voice is reading the frames
    // the editor changed.
    rhino::forge::Processor voice;
    for (const auto* id : {"oscBEnable", "subEnable", "noiseEnable"}) setValue(voice, id, 0.0f);
    setValue(voice, "oscAPosition", 0.0f);
    require(peakForNote(voice) > 0.01f, "the oscillator sounds before its frame is changed");
    voice.tableStore().edit(0).draw(0, 0.0f, 0.0f, 1.0f, 0.0f);
    voice.tableStore().publishFrame(0, 0);
    require(peakForNote(voice) < 0.001f, "flattening the frame POSITION is on silences the oscillator");
}

// ------------------------------------------------------------------ warp ---
//
// Warp is a family of twenty-six modes and the measurements below are mostly
// about the family rather than about one of them: that every mode is stable,
// that every mode leaves the note where it was, and that the knob beside a mode
// means nothing when it is at nothing. The handful of per-mode checks are the
// ones where the claim in the tooltip is worth holding the code to.

// A patch with one oscillator on a saw, nothing else sounding and no filter, so
// what is measured is the oscillator and not the voice around it.
rhino::forge::Patch warpTestPatch()
{
    rhino::forge::Patch patch;
    patch.a.enable = 1.0f;
    patch.a.position = 6.0f / 9.0f;   // the SAW frame, landed on exactly
    patch.a.unison = 1.0f;
    patch.a.level = 0.75f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.envs[rhino::forge::ampEnv].attack = 0.001f;
    patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
    return patch;
}

// One held note rendered through a Core, past the attack, as a mono sum.
std::vector<float> warpRender(const rhino::forge::Patch& patch, int samples, int note = 57,
                              double sampleRate = 48000.0)
{
    rhino::forge::Core core;
    core.initialise(sampleRate);
    core.noteOn(note, 1.0f, patch);
    for (int i = 0; i < 4096; ++i) { auto l = 0.0f, r = 0.0f; core.renderSample(patch, l, r); }

    std::vector<float> out(static_cast<size_t>(samples), 0.0f);
    for (int i = 0; i < samples; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
        out[static_cast<size_t>(i)] = 0.5f * (l + r);
    }
    return out;
}

float warpMean(const std::vector<float>& signal)
{
    auto sum = 0.0;
    for (const auto sample : signal) sum += sample;
    return signal.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(signal.size()));
}

float warpPeak(const std::vector<float>& signal)
{
    auto peak = 0.0f;
    for (const auto sample : signal) peak = juce::jmax(peak, std::abs(sample));
    return peak;
}

// How much of the signal sits in the steps between one sample and the next,
// against how much sits in the signal itself. The same amplitude-independent
// reading brightness() takes off a buffer, on a plain vector.
float warpBrightness(const std::vector<float>& signal)
{
    auto edges = 0.0, total = 0.0;
    for (size_t i = 1; i < signal.size(); ++i)
    {
        const auto step = static_cast<double>(signal[i]) - signal[i - 1];
        edges += step * step;
        total += static_cast<double>(signal[i]) * signal[i];
    }
    return total > 0.0 ? static_cast<float>(std::sqrt(edges / total)) : 0.0f;
}

// Whether a render repeats over a given lag, from -1 for the exact opposite to
// 1 for the same thing again. The lag is in samples and need not be a whole
// number of them, because a note's period almost never is.
//
// This is what the pitch of a warped oscillator is measured with, in place of
// the tallest-partial method tuningSuite uses. That method is right for a plain
// saw and wrong here: SYNC deliberately makes its own harmonic the loudest
// thing in the signal, so the tallest bin would report the mode working as the
// note moving. What a warp does leave alone is the period -- it bends the read
// inside the cycle, and the cycle still comes round at the note -- so the
// period is what is asked about. Measured off the rendered samples, with no
// reference to the phase accumulator that produced them.
double warpRepeat(const std::vector<float>& signal, double lag)
{
    const auto window = static_cast<int>(signal.size()) - static_cast<int>(std::ceil(lag)) - 1;
    if (window <= 0) return 0.0;
    auto together = 0.0, here = 0.0, there = 0.0;
    for (int i = 0; i < window; ++i)
    {
        const auto at = static_cast<double>(i) + lag;
        const auto index = static_cast<size_t>(at);
        const auto fraction = at - std::floor(at);
        const auto shifted = signal[index] * (1.0 - fraction) + signal[index + 1] * fraction;
        const auto sample = static_cast<double>(signal[static_cast<size_t>(i)]);
        together += sample * shifted;
        here += sample * sample;
        there += shifted * shifted;
    }
    return here > 0.0 && there > 0.0 ? together / std::sqrt(here * there) : 0.0;
}

void warpSuite()
{
    using rhino::forge::WarpMode;
    using rhino::forge::WarpStage;
    using rhino::forge::WarpState;
    constexpr auto modeCount = rhino::forge::warpModeCount;

    // --- The list itself ------------------------------------------------------
    require(static_cast<int>(rhino::forge::warpModes().size()) == modeCount,
            "the warp table holds exactly as many modes as it says it does");
    for (int mode = 0; mode < modeCount; ++mode)
    {
        const auto name = juce::String(rhino::forge::warpModeName(mode));
        require(name.isNotEmpty(), "every warp mode has a name");
        for (int other = mode + 1; other < modeCount; ++other)
            require(name != rhino::forge::warpModeName(other), "no two warp modes share a name");
    }
    // Every category the menu offers has something in it, or the menu draws an
    // empty submenu.
    for (int category = 0; category < rhino::forge::warpCategoryCount; ++category)
    {
        auto held = 0;
        for (int mode = 0; mode < modeCount; ++mode)
            if (rhino::forge::warpCategoryOf(static_cast<WarpMode>(mode))
                == static_cast<rhino::forge::WarpCategory>(category)) ++held;
        require(held > 0, "every warp category has at least one mode in it");
    }

    // --- Neutrality -----------------------------------------------------------
    //
    // A mode is chosen first and opened up afterwards, so a mode sitting at zero
    // depth has to be inaudible. MIRROR is the one deliberate exception: it
    // folds the cycle whatever the knob says, which is exactly what the manual
    // says of it.
    const auto ramp = [] (float phase) { return phase * 2.0f - 1.0f; };
    for (int mode = 0; mode < modeCount; ++mode)
    {
        if (static_cast<WarpMode>(mode) == WarpMode::mirror) continue;
        // Where the mode itself says it does nothing: at the bottom of the knob
        // for most of them, in the middle for the four that go both ways.
        const auto neutral = rhino::forge::warpNeutralDepth(static_cast<WarpMode>(mode));
        // QUANTIZE holds the read at steps rather than sweeping it, so at
        // nothing it is a staircase finer than the table itself rather than a
        // straight line. Measured against that resolution instead of against
        // nothing at all.
        const auto stepped = static_cast<WarpMode>(mode) == WarpMode::quantize;
        auto stage = rhino::forge::warpStageFor(static_cast<float>(mode), neutral, 220.0, 48000.0);
        WarpState state;
        auto worst = 0.0f;
        for (int i = 0; i <= 64; ++i)
        {
            const auto phase = static_cast<float>(i) / 64.0f * 0.999f;
            worst = juce::jmax(worst, std::abs(rhino::forge::warpRead(stage, phase, state, ramp)
                                               - ramp(phase)));
        }
        if (worst > (stepped ? 0.01f : 1.0e-5f))
        {
            require(false, "a warp mode leaves the wave as it was at the depth it calls neutral");
            std::cerr << "       " << rhino::forge::warpModeName(mode) << " moved it by " << worst << '\n';
        }
    }

    // MIRROR really does mirror: the second half of the cycle is the first half
    // backwards, whatever the depth.
    {
        auto stage = rhino::forge::warpStageFor(static_cast<float>(WarpMode::mirror), 0.5f, 220.0f, 48000.0);
        WarpState state;
        for (int i = 1; i < 32; ++i)
        {
            const auto phase = static_cast<float>(i) / 64.0f;
            requireClose(rhino::forge::warpRead(stage, phase, state, ramp),
                         rhino::forge::warpRead(stage, 1.0f - phase, state, ramp), 1.0e-5f,
                         "MIRROR reads the second half of the cycle as the first half backwards");
        }
    }

    // A bend keeps both ends of the cycle where they were, or the wave would no
    // longer join up with itself.
    for (const float k : {-0.9f, -0.4f, 0.0f, 0.4f, 0.9f})
    {
        requireClose(rhino::forge::warpBend(0.0f, k), 0.0f, 1.0e-6f, "a bend starts where the cycle does");
        requireClose(rhino::forge::warpBend(1.0f, k), 1.0f, 1.0e-6f, "a bend ends where the cycle does");
        auto previous = -1.0f;
        for (int i = 0; i <= 32; ++i)
        {
            const auto bent = rhino::forge::warpBend(static_cast<float>(i) / 32.0f, k);
            require(bent > previous, "a bend never doubles back on itself");
            previous = bent;
        }
    }

    // --- Every mode, rendered -------------------------------------------------
    //
    // Both stages set to the same mode at full depth, on top of a stack wide
    // enough that every member of it is carrying its own state. Nothing here is
    // allowed to produce a value that is not a number, to fall silent, or to
    // leave full scale.
    for (int mode = 1; mode < modeCount; ++mode)
    {
        auto patch = warpTestPatch();
        patch.a.unison = 4.0f;
        patch.a.detune = 0.3f;
        // The modes that read another source need it switched on, or the stage
        // takes itself out and the render below proves nothing about it.
        patch.b.enable = 1.0f;
        patch.b.level = 0.5f;
        patch.subEnable = 1.0f;
        patch.subLevel = 0.0f;
        for (int slot = 0; slot < rhino::forge::warpSlots; ++slot)
        {
            patch.a.warpMode[static_cast<size_t>(slot)] = static_cast<float>(mode);
            patch.a.warpAmount[static_cast<size_t>(slot)] = 1.0f;
        }
        for (const int note : {21, 57, 96})
        {
            const auto rendered = warpRender(patch, 4096, note);
            auto finite = true;
            for (const auto sample : rendered) finite = finite && std::isfinite(sample);
            if (!finite || warpPeak(rendered) <= 0.0f || warpPeak(rendered) > 1.0f)
            {
                require(false, "every warp mode renders finite, audible signal inside full scale");
                std::cerr << "       " << rhino::forge::warpModeName(mode) << " at note " << note
                          << " peaked at " << warpPeak(rendered) << '\n';
            }
        }
    }

    // --- The note survives the warp -------------------------------------------
    //
    // A warp bends where inside the cycle the table is read, and the cycle
    // still comes round at the note, so none of these may move the pitch. This
    // is the check that would catch a warp applied to the phase increment
    // rather than to the phase -- which is the easy way to write one and the
    // wrong way, because it would make the depth knob a tuning control.
    //
    // ODD/EVEN is deliberately absent: at the top of its travel it really does
    // leave only the even harmonics, and a wave with no fundamental in it is an
    // octave up. The manual says as much.
    {
        const auto note = 57;
        const auto expected = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const auto period = 48000.0 / expected;
        // `cycles` is how many of the note's own cycles the wave takes to come
        // round. One for everything that warps a cycle in place; two for FM
        // SUB, because the sub is an octave below the note and a modulator at
        // half the carrier's rate puts the sidebands half a note apart. That is
        // FM working, not the pitch slipping, and it is worth saying out loud
        // rather than leaving the mode out of the check.
        //
        // `frame` is which shape the table is read at. Every mode is checked on
        // the saw except FM SELF, which is checked on the sine: a loop reading
        // its own output back into its own phase is a discontinuous map when
        // the wave it reads has an edge in it, and a discontinuous map has no
        // period to measure however shallow it is set. On a sine -- which is
        // the shape feedback is classically reached for, and the one it turns
        // into a saw -- it is perfectly periodic, and that is the claim worth
        // holding: what the loop does to the pitch, not what a saw does to the
        // loop.
        struct Checked { WarpMode mode; float depth; double cycles; float frame; };
        constexpr auto saw = 6.0f / 9.0f, sine = 0.0f;
        const Checked checks[] = {{WarpMode::sync, 0.7f, 1.0, saw}, {WarpMode::bendUp, 0.7f, 1.0, saw},
                                  {WarpMode::bendDown, 0.7f, 1.0, saw}, {WarpMode::pwm, 0.7f, 1.0, saw},
                                  {WarpMode::asym, 0.8f, 1.0, saw}, {WarpMode::flip, 0.6f, 1.0, saw},
                                  {WarpMode::mirror, 0.7f, 1.0, saw}, {WarpMode::quantize, 0.7f, 1.0, saw},
                                  {WarpMode::hardClip, 0.7f, 1.0, saw}, {WarpMode::linearFold, 0.7f, 1.0, saw},
                                  {WarpMode::lowPass, 0.7f, 1.0, saw}, {WarpMode::pdSub, 0.5f, 2.0, saw},
                                  {WarpMode::pdSelf, 0.7f, 1.0, sine}};
        for (const auto& checked : checks)
        {
            auto patch = warpTestPatch();
            patch.subEnable = 1.0f;   // FM SUB reads it, and the rest never hear it
            patch.subLevel = 0.0f;
            patch.a.position = checked.frame;
            patch.a.warpMode[0] = static_cast<float>(checked.mode);
            patch.a.warpAmount[0] = checked.depth;
            const auto rendered = warpRender(patch, 16384, note);

            const auto atNote = warpRepeat(rendered, period * checked.cycles);
            if (atNote < 0.95)
            {
                require(false, "a warped oscillator still repeats at the note's own period");
                std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(checked.mode))
                          << " repeated at only " << atNote << '\n';
            }
            // And nothing shorter repeats, or the warp has put the note up
            // rather than left it alone. Stopped short of the period itself,
            // because a lag a hair under it correlates nearly as well by
            // arithmetic rather than by the wave saying anything.
            auto shortest = 0.0;
            auto worst = 0.0;
            for (auto lag = 24.0; lag < period * checked.cycles * 0.96; lag += 0.5)
                if (warpRepeat(rendered, lag) > worst) { worst = warpRepeat(rendered, lag); shortest = lag; }
            if (worst > 0.95)
            {
                require(false, "nothing shorter than the note's period repeats in a warped oscillator");
                std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(checked.mode))
                          << " repeated at " << shortest << " samples, " << worst << '\n';
            }
        }
    }

    // --- What the modes are for ------------------------------------------------
    auto plain = warpTestPatch();
    const auto plainRender = warpRender(plain, 8192);
    const auto plainBrightness = warpBrightness(plainRender);

    const auto warped = [] (WarpMode mode, float depth)
    {
        auto patch = warpTestPatch();
        patch.a.warpMode[0] = static_cast<float>(mode);
        patch.a.warpAmount[0] = depth;
        return warpRender(patch, 8192);
    };

    // LPF takes the top off and HPF takes the bottom off, which is a fall and a
    // rise in the same reading.
    require(warpBrightness(warped(WarpMode::lowPass, 1.0f)) < plainBrightness * 0.6f,
            "the LPF warp darkens the waveform");
    require(warpBrightness(warped(WarpMode::highPass, 1.0f)) > plainBrightness * 1.2f,
            "the HPF warp brightens the waveform");

    // A wavefolder adds harmonics the table never held.
    require(warpBrightness(warped(WarpMode::linearFold, 1.0f)) > plainBrightness * 1.2f,
            "folding the wave adds harmonics to it");

    // RECTIFY is asymmetric by construction and would leave a constant offset
    // behind if nothing took it off. This is the check on the blocker: the
    // offset is what a note would thump with, and what the filter would
    // otherwise have to carry.
    const auto rectified = warped(WarpMode::rectify, 1.0f);
    require(warpPeak(rectified) > 0.05f, "RECTIFY leaves something to measure");
    require(std::abs(warpMean(rectified)) < warpPeak(rectified) * 0.02f,
            "a rectified oscillator is left with no constant offset");

    // Both stages run, in the order they are declared. Measured on two modes
    // that shape the sample rather than move the read, because those are the
    // ones the order can be seen in: a mode that bends the phase and a mode
    // that shapes the sample commute by construction here, since a stage hands
    // the one in front of it a way to read rather than something already read.
    {
        auto first = warpTestPatch();
        first.a.warpMode[0] = static_cast<float>(WarpMode::hardClip);
        first.a.warpAmount[0] = 0.8f;
        first.a.warpMode[1] = static_cast<float>(WarpMode::rectify);
        first.a.warpAmount[1] = 0.8f;
        auto second = warpTestPatch();
        second.a.warpMode[0] = static_cast<float>(WarpMode::rectify);
        second.a.warpAmount[0] = 0.8f;
        second.a.warpMode[1] = static_cast<float>(WarpMode::hardClip);
        second.a.warpAmount[1] = 0.8f;

        const auto one = warpRender(first, 2048);
        const auto two = warpRender(second, 2048);
        auto difference = 0.0f;
        for (size_t i = 0; i < one.size(); ++i)
            difference = juce::jmax(difference, std::abs(one[i] - two[i]));
        require(difference > 0.01f, "the two warp stages run in the order they are declared");
    }

    // Every mode driven by the other oscillator needs it switched on, exactly as
    // the manual says of all four families. With it off there is nothing to
    // modulate with and the stage takes itself out rather than going quietly
    // wrong; with it on, its own level makes no difference to the modulation.
    for (const auto mode : {WarpMode::pdOsc, WarpMode::fmOsc, WarpMode::fmExpOsc,
                            WarpMode::amOsc, WarpMode::rmOsc})
    {
        auto alone = warpTestPatch();
        alone.a.warpMode[0] = static_cast<float>(mode);
        alone.a.warpAmount[0] = 1.0f;
        const auto withoutB = warpRender(alone, 2048);
        auto difference = 0.0f;
        for (size_t i = 0; i < withoutB.size(); ++i)
            difference = juce::jmax(difference, std::abs(withoutB[i] - plainRender[i]));
        if (difference > 1.0e-5f)
        {
            require(false, "a stage driven by the other oscillator does nothing while it is off");
            std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(mode)) << '\n';
        }

        alone.b.enable = 1.0f;
        alone.b.level = 0.0f;   // heard only as a modulator, as the manual suggests
        const auto withB = warpRender(alone, 2048);
        difference = 0.0f;
        for (size_t i = 0; i < withB.size(); ++i)
            difference = juce::jmax(difference, std::abs(withB[i] - plainRender[i]));
        if (difference < 0.01f)
        {
            require(false, "a stage driven by the other oscillator works with its level down");
            std::cerr << "       " << rhino::forge::warpModeName(static_cast<int>(mode)) << '\n';
        }
    }

    // --- FM against PD --------------------------------------------------------
    //
    // The manual separates them and so does this. PD moves where in the cycle
    // the table is read and never touches the rate the cycle runs at; FM moves
    // that rate and nothing else. The multiplier is the whole of the
    // difference, so it is checked directly rather than inferred from a render.
    {
        const auto factor = [] (WarpMode mode, float depth, float modulator)
        {
            return rhino::forge::warpPitchFactor(mode, depth, modulator);
        };
        for (const auto mode : {WarpMode::pdOsc, WarpMode::pdSelf, WarpMode::bendUp,
                                WarpMode::amOsc, WarpMode::rmOsc, WarpMode::sync})
            requireClose(factor(mode, 1.0f, 1.0f), 1.0f, 0.0001f,
                         "only the FM modes reach the rate the cycle runs at");
        for (const auto mode : {WarpMode::fmOsc, WarpMode::fmExpOsc})
        {
            requireClose(factor(mode, 0.0f, 1.0f), 1.0f, 0.0001f,
                         "FM at no depth leaves the note where it was");
            requireClose(factor(mode, 1.0f, 0.0f), 1.0f, 0.0001f,
                         "FM with nothing arriving leaves the note where it was");
        }
        // Linear is proportional and clamps at zero rather than running the
        // frequency backwards, which is the traditional FM the manual describes.
        requireClose(factor(WarpMode::fmOsc, 1.0f, 1.0f), 1.0f + rhino::forge::warpFmDepth, 0.0001f,
                     "linear FM is proportional to the modulator");
        requireClose(factor(WarpMode::fmOsc, 1.0f, -1.0f), 0.0f, 0.0001f,
                     "linear FM clamps at zero rather than running backwards");
        // The clamp bites at a quarter of the way down at full depth, which is
        // what "traditional FM" means: a good part of the modulator's trough is
        // spent at a standstill. Short of that it is still proportional.
        requireClose(factor(WarpMode::fmOsc, 1.0f, -0.2f),
                     1.0f - 0.2f * rhino::forge::warpFmDepth, 0.0001f,
                     "linear FM short of the clamp is still proportional");
        // Exponential is symmetric in octaves, which is why it sweeps so much
        // further for the same depth and why it does not hold the note.
        requireClose(factor(WarpMode::fmExpOsc, 1.0f, 1.0f),
                     std::pow(2.0f, rhino::forge::warpFmOctaves), 0.01f,
                     "exponential FM sweeps in octaves");
        requireClose(factor(WarpMode::fmExpOsc, 1.0f, -1.0f),
                     1.0f / std::pow(2.0f, rhino::forge::warpFmOctaves), 0.001f,
                     "exponential FM sweeps the same distance downwards");

        // And the two are audibly different things from the same source at the
        // same depth, which is the claim the separation is worth making for.
        auto pd = warpTestPatch();
        pd.b.enable = 1.0f;
        pd.b.level = 0.0f;
        pd.a.warpMode[0] = static_cast<float>(WarpMode::pdOsc);
        pd.a.warpAmount[0] = 0.6f;
        auto fm = pd;
        fm.a.warpMode[0] = static_cast<float>(WarpMode::fmOsc);
        const auto pdRender = warpRender(pd, 4096);
        const auto fmRender = warpRender(fm, 4096);
        auto difference = 0.0f;
        for (size_t i = 0; i < pdRender.size(); ++i)
            difference = juce::jmax(difference, std::abs(pdRender[i] - fmRender[i]));
        require(difference > 0.05f, "FM and PD from one source at one depth are not one sound");
    }

    // --- AM against RM --------------------------------------------------------
    //
    // AM rides the carrier and leaves it in the sound; RM replaces it, so the
    // carrier's own pitch goes and the two sidebands are what is left. Driven
    // by the sub, which runs an octave below the note, that difference is
    // exact rather than approximate: a cycle of the note later, the sub has
    // turned over, so a ring-modulated wave comes back inverted while an
    // amplitude-modulated one does not.
    {
        const auto note = 57;
        const auto period = 48000.0 / (440.0 * std::pow(2.0, (note - 69) / 12.0));
        const auto driven = [note] (WarpMode mode)
        {
            auto patch = warpTestPatch();
            patch.subEnable = 1.0f;
            patch.subLevel = 0.0f;
            patch.a.warpMode[0] = static_cast<float>(mode);
            patch.a.warpAmount[0] = 1.0f;
            return warpRender(patch, 16384, note);
        };
        require(warpRepeat(driven(WarpMode::amSub), period) > 0.2,
                "an amplitude-modulated oscillator still has its own note in it");
        require(warpRepeat(driven(WarpMode::rmSub), period) < -0.5,
                "a ring-modulated oscillator comes back inverted, its own note gone");
    }

    // --- The matrix reaches the depths ----------------------------------------
    //
    // The four warp depths were appended past the racks rather than put beside
    // the oscillator controls they belong with, because a destination is stored
    // as an index and moving one would move it inside every preset already
    // saved. These are the two halves of that: the new entries land where they
    // are expected, and nothing that was already there has shifted.
    {
        rhino::forge::Patch patch;
        const auto base = rhino::forge::warpDestinationBase;
        require(rhino::forge::destinationField(patch, base) == &patch.a.warpAmount[0]
                    && rhino::forge::destinationField(patch, base + 1) == &patch.a.warpAmount[1]
                    && rhino::forge::destinationField(patch, base + 2) == &patch.b.warpAmount[0]
                    && rhino::forge::destinationField(patch, base + 3) == &patch.b.warpAmount[1],
                "each warp depth is the destination its index names");
        require(rhino::forge::destinationField(patch, rhino::forge::destinationCount) == nullptr,
                "an index past the end of the list points at nothing");
        require(rhino::forge::fxDestinationOf(0, 0, 0) == rhino::forge::fxDestinationBase
                    && rhino::forge::destinationField(patch, rhino::forge::fxDestinationBase)
                           == &patch.racks[0].slots[0].knobs[0],
                "appending the warp depths left the racks where they were");
        for (int i = 0; i < rhino::forge::warpDestinationCount; ++i)
            require(juce::String(rhino::forge::destinations()[static_cast<size_t>(base + i)].id)
                        == rhino::forge::warpDestinations()[static_cast<size_t>(i)].id,
                    "the built list carries the warp depths at the end");
    }

    // --- The parameters and the panel -----------------------------------------
    rhino::forge::Processor processor;
    for (const auto* prefix : {"oscA", "oscB"})
        for (int slot = 1; slot <= rhino::forge::warpSlots; ++slot)
        {
            const auto id = juce::String(prefix) + "Warp" + juce::String(slot);
            require(processor.state.getParameter(id + "Mode") != nullptr
                        && processor.state.getParameter(id) != nullptr,
                    "both halves of every warp stage are real parameters");
            // A fresh patch has no warp on it, so every preset written before
            // warp existed still sounds as it did.
            requireClose(value(processor, (id + "Mode").toRawUTF8()), 0.0f, 0.0001f,
                         "a warp stage opens on OFF");
            requireClose(value(processor, id.toRawUTF8()), 0.0f, 0.0001f,
                         "a warp depth opens at nothing");
            // A host's automation lane names the very mode the engine will run.
            for (int mode = 0; mode < modeCount; ++mode)
                requireText(textFor(processor, (id + "Mode").toRawUTF8(), static_cast<float>(mode)),
                            rhino::forge::warpModeName(mode),
                            "a warp mode reads out as the mode the engine runs");
        }

    // The four bipolar modes are the ones whose neutral is in the middle, and
    // nothing else claims to be. Written out rather than read back from the
    // same function, so this is a second opinion and not a tautology.
    for (int mode = 0; mode < modeCount; ++mode)
    {
        const auto bipolar = static_cast<WarpMode>(mode) == WarpMode::bendBoth
                          || static_cast<WarpMode>(mode) == WarpMode::asym
                          || static_cast<WarpMode>(mode) == WarpMode::mirror
                          || static_cast<WarpMode>(mode) == WarpMode::oddEven;
        requireClose(rhino::forge::warpNeutralDepth(static_cast<WarpMode>(mode)),
                     bipolar ? 0.5f : 0.0f, 0.0001f,
                     "a warp mode is neutral where the panel returns its knob to");
    }

    // Every mode has something to say for itself, which is what the field's
    // tooltip shows once a mode is chosen.
    for (int mode = 0; mode < modeCount; ++mode)
        require(rhino::forge::ui::warpTooltipFor(mode).isNotEmpty(),
                "every warp mode explains itself");

    // The depth knob greys out under OFF and comes back under anything else.
    // Declared as the panel's own rule rather than checked through the editor,
    // which needs a window.
    for (const auto& module : rhino::forge::ui::modules())
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
            {
                const auto id = juce::String(control.id);
                if (!id.startsWith("osc") || !id.contains("Warp") || id.endsWith("Mode")) continue;
                require(control.style == rhino::forge::ui::Style::knob,
                        "a warp depth is drawn as a knob");
                require(control.enabledBy != nullptr && juce::String(control.enabledBy) == id + "Mode",
                        "a warp depth is greyed out by its own mode field");
            }
}

void tuningSuite();

void engineSuite()
{
    rhino::forge::Processor processor;

    // A silent patch really is silent: with every source switched off, nothing
    // reaches the output however the level knobs are set.
    for (const auto* id : {"oscAEnable", "oscBEnable", "subEnable", "noiseEnable"})
        setValue(processor, id, 0.0f);
    setValue(processor, "subLevel", 1.0f);
    setValue(processor, "noiseLevel", 1.0f);
    require(peakForNote(processor) == 0.0f, "every source disabled renders silence");

    // Each source is independently audible.
    setValue(processor, "subEnable", 1.0f);
    const auto subOnly = peakForNote(processor);
    require(subOnly > 0.0f, "the sub module alone makes sound");
    setValue(processor, "subEnable", 0.0f);

    setValue(processor, "noiseEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "the noise module alone makes sound");
    setValue(processor, "noiseEnable", 0.0f);

    setValue(processor, "oscAEnable", 1.0f);
    const auto oscAOnly = peakForNote(processor);
    require(oscAOnly > 0.0f, "oscillator A alone makes sound");

    // Each oscillator owns its level outright. Nothing about B may reach A.
    setValue(processor, "oscBLevel", 0.9f);
    setValue(processor, "oscBDetune", 1.0f);
    setValue(processor, "oscBUnison", 8.0f);
    requireClose(peakForNote(processor), oscAOnly, 0.0001f,
                 "oscillator B's controls do not affect A while B is switched off");

    setValue(processor, "oscBEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "both oscillators together make sound");

    // Bypassing the filter must actually bypass it: a cutoff low enough to
    // remove nearly everything should stop mattering.
    setValue(processor, "cutoff", 60.0f);
    setValue(processor, "filterEnable", 1.0f);
    const auto filtered = peakForNote(processor);
    setValue(processor, "filterEnable", 0.0f);
    const auto bypassed = peakForNote(processor);
    require(bypassed > filtered * 2.0f, "switching the filter off bypasses it");

    filterRoutingSuite();
    mixerSuite();
    fxSuite();
    envelopeSuite();
    auxEnvelopeSuite();
    lfoSuite();
    waveTableSuite();
    bandLimitSuite();
    tableEditSuite();
    tableFileSuite();
    voicingSuite();
    voiceStealSuite();
    voiceTailSuite();
    modulationSuite();

    oscillatorSuite();
    warpSuite();
    tuningSuite();

    // Output stays finite and bounded across an extreme patch.
    rhino::forge::Processor extreme;
    for (const auto* id : {"oscAEnable", "oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(extreme, id, 1.0f);
    setValue(extreme, "oscAUnison", 8.0f);
    setValue(extreme, "oscBUnison", 8.0f);
    setValue(extreme, "oscADetune", 1.0f);
    setValue(extreme, "oscBDetune", 1.0f);
    setValue(extreme, "resonance", 1.0f);
    setValue(extreme, "drive", 1.0f);
    setValue(extreme, "output", 1.25f);
    setValue(extreme, "subLevel", 1.0f);
    setValue(extreme, "noiseLevel", 1.0f);
    setValue(extreme, "lfo1Rate", 20.0f);
    // Drive the matrix hard too: LFO 1 into the cutoff and into oscillator A's
    // pitch, both at full depth.
    setValue(extreme, "mod1Source", 2.0f);
    setValue(extreme, "mod1Dest", 13.0f);
    setValue(extreme, "mod1Depth", 1.0f);
    setValue(extreme, "mod2Source", 2.0f);
    setValue(extreme, "mod2Dest", 5.0f);
    setValue(extreme, "mod2Depth", 1.0f);

    constexpr int blockSize = 512;
    extreme.prepareToPlay(48000.0, blockSize);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (int note = 48; note < 58; ++note)
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    auto peak = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        extreme.processBlock(buffer, midi);
        midi.clear();
        require(allSamplesFinite(buffer), "every rendered sample is finite");
        peak = juce::jmax(peak, buffer.getMagnitude(0, blockSize));
    }
    require(peak > 0.0f, "an extreme patch still produces signal");
    require(peak <= 1.0f, "an extreme patch stays within full scale");
}

// --- Tuning -------------------------------------------------------------------
//
// The first question a player asks and the one the panel cannot answer for
// itself: does a note come out at the pitch it names? Measured off the rendered
// signal rather than off the phase accumulator, so nothing here can agree with
// the oscillator by construction — a transposed table, a mis-scaled frame or a
// phase increment that is off by a ratio all show up as cents.
double renderedFundamental(int note, double sampleRate)
{
    constexpr int order = 15, size = 1 << order;

    rhino::forge::Patch patch;
    patch.a.enable = 1.0f;
    patch.a.position = 6.0f / 9.0f;   // the SAW frame, landed on exactly
    patch.a.unison = 1.0f;
    patch.a.level = 0.75f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.envs[rhino::forge::ampEnv].attack = 0.001f;
    patch.envs[rhino::forge::ampEnv].sustain = 1.0f;

    rhino::forge::Core core;
    core.initialise(sampleRate);
    core.noteOn(note, 1.0f, patch);

    // Past the attack before anything is measured, so the window holds steady
    // state and not the envelope's edge.
    for (int i = 0; i < 4096; ++i) { auto l = 0.0f, r = 0.0f; core.renderSample(patch, l, r); }

    std::vector<float> data(2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
        const auto w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                            * static_cast<double>(i) / static_cast<double>(size));
        data[static_cast<size_t>(i)] = static_cast<float>(0.5 * (l + r) * w);
    }

    juce::dsp::FFT fft(order);
    fft.performRealOnlyForwardTransform(data.data());
    const auto magnitude = [&data] (int bin)
    {
        const auto re = static_cast<double>(data[static_cast<size_t>(2 * bin)]);
        const auto im = static_cast<double>(data[static_cast<size_t>(2 * bin + 1)]);
        return std::sqrt(re * re + im * im);
    };

    // A saw's fundamental is its loudest partial, so the tallest bin names it.
    auto best = 2;
    for (int bin = 3; bin < size / 2 - 1; ++bin)
        if (magnitude(bin) > magnitude(best)) best = bin;

    // Where a Hann window actually puts the peak between two bins.
    const auto a = std::log(std::max(1.0e-20, magnitude(best - 1)));
    const auto b = std::log(std::max(1.0e-20, magnitude(best)));
    const auto c = std::log(std::max(1.0e-20, magnitude(best + 1)));
    const auto denominator = a - 2.0 * b + c;
    const auto shift = std::abs(denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;
    return (static_cast<double>(best) + shift) * sampleRate / static_cast<double>(size);
}

void tuningSuite()
{
    for (const double sampleRate : {44100.0, 48000.0})
        for (const int note : {27, 33, 45, 52, 54, 56, 57, 69, 81})
        {
            const auto expected = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const auto measured = renderedFundamental(note, sampleRate);
            const auto cents = 1200.0 * std::log2(measured / expected);
            // Two cents is under what anyone hears and well over the peak
            // interpolation's own error, which measures at about half of one.
            require(std::abs(cents) < 2.0, "a note sounds at the pitch it names");
        }
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const juce::String suite = argc > 1 ? argv[1] : "";

    if (suite.isEmpty() || suite == "--layout")
    {
        layoutSuite();
        envelopeDisplaySuite();
        filterDisplaySuite();
        fxDisplaySuite();
    }
    if (suite.isEmpty() || suite == "--presets") { presetSuite(); legacyStateSuite(); }
    if (suite.isEmpty() || suite == "--engine") engineSuite();

    if (failures > 0)
    {
        std::cerr << failures << " Forge check(s) failed\n";
        return 1;
    }
    std::cout << "Rhino Forge checks passed" << (suite.isEmpty() ? "" : " (" + suite + ")") << '\n';
    return 0;
}
