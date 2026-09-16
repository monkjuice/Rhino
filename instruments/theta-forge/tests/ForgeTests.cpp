#include "../src/ForgeProcessor.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTooltips.h"
#include <algorithm>
#include <iostream>
#include <vector>

// One binary, three CTest cases selected by argv, matching Theta's own test
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

void setValue(theta::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return; }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

float value(const theta::forge::Processor& processor, const char* id)
{
    const auto* raw = processor.state.getRawParameterValue(id);
    if (raw == nullptr) { require(false, id); return 0.0f; }
    return raw->load();
}

juce::String textFor(const theta::forge::Processor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) { require(false, id); return {}; }
    return parameter->getText(parameter->convertTo0to1(plainValue), 32);
}

// Render a held note and report the loudest sample. Used to prove a source is
// silent rather than merely quiet.
float peakForNote(theta::forge::Processor& processor, int samples = 4096)
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
constexpr int srcOff = static_cast<int>(theta::forge::ModSource::off);
constexpr int srcEnv1 = static_cast<int>(theta::forge::ModSource::env1);
constexpr int srcLfo1 = static_cast<int>(theta::forge::ModSource::lfo1);
constexpr int srcVelocity = static_cast<int>(theta::forge::ModSource::velocity);
constexpr int srcNote = static_cast<int>(theta::forge::ModSource::note);

void renderNote(theta::forge::Processor& processor, juce::AudioBuffer<float>& buffer, int note = 57)
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
    theta::forge::Processor processor;
    // The size the editor actually opens at, so the detailed checks below run
    // against the panel people see. The sweep further down covers the rest of
    // the allowed range.
    const auto bounds = juce::Rectangle<int>(0, 0, theta::forge::ui::defaultPanelWidth,
                                             theta::forge::ui::defaultPanelHeight);
    const auto content = theta::forge::ui::contentBounds(bounds);
    const auto& modules = theta::forge::ui::modules();
    require(!modules.empty(), "the panel declares at least one module");
    // The default has to be a size the window can actually be put at, or the
    // editor opens somewhere the resize limits would not let you return to.
    require(theta::forge::ui::defaultPanelWidth >= theta::forge::ui::minPanelWidth
                && theta::forge::ui::defaultPanelWidth <= theta::forge::ui::maxPanelWidth
                && theta::forge::ui::defaultPanelHeight >= theta::forge::ui::minPanelHeight
                && theta::forge::ui::defaultPanelHeight <= theta::forge::ui::maxPanelHeight,
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
        if (module.display == theta::forge::ui::Display::oscillator)
        {
            const auto* source = theta::forge::ui::displaySourceId(module);
            require(source != nullptr && processor.state.getParameter(source) != nullptr,
                    "an oscillator display reads a real parameter");
        }
    }

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
        const auto area = theta::forge::ui::moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& controls = module.rows[static_cast<size_t>(r)].controls;
            for (int c = 0; c < static_cast<int>(controls.size()); ++c)
            {
                if (!controls[static_cast<size_t>(c)].sharesCell) continue;
                require(c > 0, "a shared cell has a control in front of it to share");
                if (c == 0) continue;
                const auto owner = theta::forge::ui::cellOwner(module, r, c);
                require(theta::forge::ui::bankOf(module, r, owner)
                            == theta::forge::ui::bankOf(module, r, c),
                        "a shared cell is shared inside one bank");
                const auto& first = controls[static_cast<size_t>(owner)];
                const auto& second = controls[static_cast<size_t>(c)];
                require(first.disabledBy != nullptr && second.enabledBy != nullptr
                            && juce::String(first.disabledBy) == second.enabledBy,
                        "the two controls in a shared cell are gated by one parameter, one each way");
                require(theta::forge::ui::cellBounds(area, module, r, c)
                            == theta::forge::ui::cellBounds(area, module, r, owner),
                        "a shared control lands on the cell it shares");
                require(theta::forge::ui::inSharedCell(module, r, c)
                            && theta::forge::ui::inSharedCell(module, r, owner),
                        "both controls in a shared cell know they are sharing one");
            }
        }
    }

    // A module declared in banks shows one at a time, in the same cells, so
    // every bank has to declare the same controls in the same order — otherwise
    // which cell a control lands in would depend on which bank was showing.
    for (const auto& module : modules)
    {
        const auto banks = theta::forge::ui::bankCount(module);
        for (const auto& row : module.rows)
        {
            require(juce::jmax(1, row.banks) == banks,
                    "every row of a module declares the same number of banks");
            const auto perBank = theta::forge::ui::controlsPerBank(row);
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
        // A module that is a source and shows several banks drags a different
        // source per bank, so all of them have to be real sources.
        if (module.handleSource != 0)
            require(module.handleSource + banks - 1 < theta::forge::modSourceCount,
                    "every bank of a source module names a real modulation source");
    }

    // A control with no tooltip is a control nobody explained. Held to the same
    // standard as a control whose parameter does not exist, because a panel this
    // dense is unusable without them and a silent gap is easy to miss by eye.
    for (const auto& module : modules)
    {
        if (module.enableId != nullptr)
            require(theta::forge::ui::tooltipFor(module.enableId).isNotEmpty(),
                    "a module's enable has a tooltip");
        for (const auto& row : module.rows)
            for (const auto& control : row.controls)
            {
                const auto tip = theta::forge::ui::tooltipFor(control.id);
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
        const auto area = theta::forge::ui::moduleBounds(bounds, modules[i]);
        require(!area.isEmpty(), "a module occupies a non-empty rectangle");
        require(content.contains(area), "a module stays inside the content area");

        const auto controls = theta::forge::ui::controlArea(area, modules[i]);
        require(controls.getHeight() > 30, "a module leaves usable height for its controls");
        require(area.withTrimmedTop(theta::forge::ui::headerHeight).contains(controls),
                "controls stay clear of the module header, so labels cannot collide with the title");

        // Rows within a module must tile their area without overlapping either.
        for (int r = 0; r < static_cast<int>(modules[i].rows.size()); ++r)
        {
            const auto row = theta::forge::ui::rowBounds(area, modules[i], r);
            require(controls.contains(row), "a control row stays inside its module");
            for (int s = r + 1; s < static_cast<int>(modules[i].rows.size()); ++s)
                require(!row.intersects(theta::forge::ui::rowBounds(area, modules[i], s)),
                        "no two control rows in a module overlap");
        }

        // Two modules may share a rectangle as long as no tab shows them both:
        // that is exactly what the oscillators and the matrix do.
        for (size_t j = i + 1; j < modules.size(); ++j)
            if (theta::forge::ui::sharePage(modules[i], modules[j]))
                require(!area.intersects(theta::forge::ui::moduleBounds(bounds, modules[j])),
                        "no two modules shown together overlap");
    }

    // Every tab has to put something on screen, and the tabs themselves have to
    // stay clear of each other in the title bar.
    for (const auto page : theta::forge::ui::tabPages)
    {
        auto shown = 0;
        for (const auto& module : modules)
            if (module.page == page) ++shown;
        require(shown > 0, "every tab shows at least one module of its own");
    }
    for (int i = 1; i < theta::forge::ui::tabCount; ++i)
        require(!theta::forge::ui::tabBounds(i - 1).intersects(theta::forge::ui::tabBounds(i)),
                "no two tabs overlap");

    // A table names its columns once, above its rows, so every row has to have
    // the same controls in the same order as the first or the titles lie.
    for (const auto& module : modules)
    {
        if (module.columnHeaderHeight <= 0) continue;
        const auto area = theta::forge::ui::moduleBounds(bounds, module);
        const auto titles = theta::forge::ui::columnTitleBounds(area, module);
        require(!titles.isEmpty(), "a table reserves a strip for its column titles");
        require(!titles.intersects(theta::forge::ui::controlArea(area, module)),
                "a table's column titles sit clear of its rows");
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
            require(!theta::forge::ui::rowGutterBounds(area, module, r)
                         .intersects(theta::forge::ui::rowBounds(area, module, r)),
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
    const auto diameter = theta::forge::ui::uniformKnobDiameter(bounds);
    require(diameter >= 48, "the shared knob diameter stays usable");
    for (const auto& module : modules)
    {
        const auto area = theta::forge::ui::moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& row = module.rows[static_cast<size_t>(r)];
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                const auto block = theta::forge::ui::controlBlock(area, module, r, i, diameter);
                require(!block.isEmpty(), "every control gets a non-empty rectangle");
                require(area.contains(block), "every control stays inside its module");
                if (row.controls[static_cast<size_t>(i)].style != theta::forge::ui::Style::knob)
                    continue;
                if (module.compactKnobs)
                {
                    // A compact module opts out of the shared size on purpose,
                    // but its knobs still have to be smaller, not larger, and
                    // still have to be usable.
                    require(block.getWidth() <= diameter, "a compact knob is no larger than the shared diameter");
                    require(block.getWidth() >= 24, "a compact knob stays usable");
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
    for (int width = theta::forge::ui::minPanelWidth; width <= theta::forge::ui::maxPanelWidth; width += 20)
        for (int height = theta::forge::ui::minPanelHeight; height <= theta::forge::ui::maxPanelHeight; height += 20)
        {
            const auto resized = juce::Rectangle<int>(0, 0, width, height);
            const auto area = theta::forge::ui::contentBounds(resized);
            const auto diameter = theta::forge::ui::uniformKnobDiameter(resized);
            if (diameter < 40)
            {
                require(false, "knobs stay usable at every allowed size");
                std::cerr << "       at " << width << "x" << height << '\n';
            }

            for (size_t i = 0; i < modules.size(); ++i)
            {
                const auto box = theta::forge::ui::moduleBounds(resized, modules[i]);
                if (!area.contains(box))
                {
                    require(false, "a module stays inside the content area at every allowed size");
                    std::cerr << "       " << modules[i].id << " at " << width << "x" << height << '\n';
                }
                if (theta::forge::ui::controlArea(box, modules[i]).getHeight() <= 24)
                {
                    require(false, "controls stay usable at every allowed size");
                    std::cerr << "       " << modules[i].id << " at " << width << "x" << height << '\n';
                }
                // Overlap has to hold at every size too, not only at the one the
                // panel was designed against.
                for (size_t j = i + 1; j < modules.size(); ++j)
                {
                    if (!theta::forge::ui::sharePage(modules[i], modules[j])) continue;
                    if (box.intersects(theta::forge::ui::moduleBounds(resized, modules[j])))
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
                        const auto block = theta::forge::ui::controlBlock(box, modules[i], r, c, diameter);
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

// --------------------------------------------------------------- presets ---

// A state written before LFO 2-6 existed. Its LFO parameters were named for the
// only LFO there was, and its matrix sources were indices into a list that five
// LFOs have since been inserted into the middle of. A dropped parameter loads at
// its default and no harm is done; a source index that quietly means something
// else is a slot silently pointed somewhere nobody asked for, so it is remapped
// rather than left.
void legacyStateSuite()
{
    theta::forge::Processor processor;

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
    // As the sources were numbered then: LFO 1 at 2, VELOCITY straight after it
    // at 3, NOTE at 4, and the macros from 5.
    add("mod1Source", 2.0f);
    add("mod2Source", 3.0f);
    add("mod3Source", 4.0f);
    add("mod4Source", 5.0f);

    juce::MemoryBlock block;
    const auto xml = saved.createXml();
    require(xml != nullptr, "the legacy state serialises");
    if (xml == nullptr) return;
    juce::AudioProcessor::copyXmlToBinary(*xml, block);
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

    const auto value = [&processor] (const juce::String& id)
    {
        const auto* raw = processor.state.getRawParameterValue(id);
        return raw == nullptr ? std::numeric_limits<float>::quiet_NaN() : raw->load();
    };

    requireClose(value("lfo1Shape"), 2.0f, 0.001f, "the one LFO's shape becomes LFO 1's");
    requireClose(value("lfo1Mode"), 1.0f, 0.001f, "the one LFO's mode becomes LFO 1's");
    requireClose(value("lfo1Rate"), 3.0f, 0.001f, "the one LFO's rate becomes LFO 1's");
    requireClose(value("lfo1Division"), 4.0f, 0.001f, "the one LFO's division becomes LFO 1's");
    requireClose(value("cutoff"), 900.0f, 0.5f, "everything else is left alone");

    requireClose(value("mod1Source"), static_cast<float>(srcLfo1), 0.001f,
                 "a slot on LFO 1 stays on LFO 1");
    requireClose(value("mod2Source"), static_cast<float>(srcVelocity), 0.001f,
                 "a slot on velocity is still on velocity");
    requireClose(value("mod3Source"), static_cast<float>(srcNote), 0.001f,
                 "a slot on note is still on note");
    requireClose(value("mod4Source"), static_cast<float>(static_cast<int>(theta::forge::ModSource::macro1)),
                 0.001f, "a slot on the first macro is still on it");

    // Running it again must change nothing: state already migrated no longer
    // carries the old ids, which is what the migration keys off.
    juce::MemoryBlock again;
    processor.getStateInformation(again);
    processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
    requireClose(value("mod2Source"), static_cast<float>(srcVelocity), 0.001f,
                 "saving and reopening migrated state does not move the sources again");
    requireClose(value("lfo1Rate"), 3.0f, 0.001f,
                 "saving and reopening migrated state keeps LFO 1's rate");
}

void presetSuite()
{
    theta::forge::Processor processor;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("theta-forge-preset-test", {}, true);
    require(directory.createDirectory(), "temporary preset directory can be created");
    const auto preset = directory.getChildFile("Round Trip.forgepreset");

    setValue(processor, "cutoff", 1320.0f);
    setValue(processor, "release", 2.5f);
    setValue(processor, "noiseEnable", 1.0f);
    setValue(processor, "oscBEnable", 0.0f);
    require(processor.savePreset(preset, "Round Trip").wasOk(), "preset saves");

    setValue(processor, "cutoff", 9000.0f);
    setValue(processor, "release", 0.1f);
    setValue(processor, "noiseEnable", 0.0f);
    setValue(processor, "oscBEnable", 1.0f);
    require(processor.loadPreset(preset).wasOk(), "preset loads");
    requireClose(value(processor, "cutoff"), 1320.0f, 1.0f, "preset restores cutoff");
    requireClose(value(processor, "release"), 2.5f, 0.001f, "preset restores release");
    requireClose(value(processor, "noiseEnable"), 1.0f, 0.001f, "preset restores an enabled module");
    requireClose(value(processor, "oscBEnable"), 0.0f, 0.001f, "preset restores a disabled module");

    // A table is data rather than a parameter, so it travels beside the
    // parameter state. A preset that carries one has to give back the frames
    // that were drawn, and a preset that carries none has to put the oscillator
    // back on the built-in ten rather than leave the previous patch's table
    // behind â€” the same rule an omitted parameter follows.
    {
        theta::forge::Processor drawn;
        drawn.tableStore().edit(0).draw(0, 0.0f, -1.0f, 1.0f, 1.0f);
        drawn.tableStore().edit(0).insertFrame(0, true);
        drawn.tableStore().publish(0);
        const auto frames = drawn.tableStore().edit(0).frameCount();
        std::vector<float> authored(drawn.tableStore().edit(0).samples());

        const auto withTable = directory.getChildFile("Drawn.forgepreset");
        require(drawn.savePreset(withTable, "Drawn").wasOk(), "a preset holding a table saves");

        theta::forge::Processor reopened;
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
        theta::forge::Processor hosted;
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
    require(version1.replaceWithText("<ThetaForgePreset formatVersion=\"1\"><ThetaForgeState/></ThetaForgePreset>"),
            "format 1 fixture writes");
    require(processor.loadPreset(version1).failed(), "a format 1 preset is refused");

    const auto future = directory.getChildFile("Future.forgepreset");
    require(future.replaceWithText("<ThetaForgePreset formatVersion=\"99\"><ThetaForgeState/></ThetaForgePreset>"),
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
                "<ThetaForgePreset formatVersion=\"2\"><ThetaForgeState>"
                "<PARAM id=\"cutoff\" value=\"5000.0\"/>"
                "<PARAM id=\"retiredKnob\" value=\"0.5\"/>"
                "</ThetaForgeState></ThetaForgePreset>"),
            "partial fixture writes");
    require(processor.loadPreset(partial).wasOk(), "a preset missing parameters still loads");
    requireClose(value(processor, "cutoff"), 5000.0f, 1.0f, "a partial preset restores what it names");
    requireClose(value(processor, "noiseEnable"), 0.0f, 0.001f,
                 "a parameter the preset omits returns to its default, not the previous patch's value");
    requireClose(value(processor, "release"), 0.35f, 0.001f,
                 "an omitted float parameter returns to its default");

    const auto resaved = directory.getChildFile("Resaved.forgepreset");
    require(processor.savePreset(resaved, "Resaved").wasOk(), "a reconciled preset saves");
    const auto text = resaved.loadFileAsString();
    require(text.contains("formatVersion=\"2\""), "a saved preset declares format 2");
    require(!text.contains("retiredKnob"), "an unknown entry is dropped, not carried as ballast");
    require(text.contains("oscAEnable"), "the module enables are saved");
    require(text.contains("release"), "an omitted parameter is written back out at its default");

    const auto invalid = directory.getChildFile("Invalid.forgepreset");
    require(invalid.replaceWithText("<NotForge />"), "invalid fixture writes");
    const auto before = value(processor, "cutoff");
    require(processor.loadPreset(invalid).failed(), "a foreign preset is rejected");
    require(value(processor, "cutoff") == before, "a rejected preset leaves state untouched");

    // The readout bug: a SliderAttachment overwrites any formatter the editor
    // installs, so the formatting has to belong to the parameter itself.
    requireText(textFor(processor, "cutoff", 7800.0f), "7.80 kHz", "cutoff reads as kHz");
    requireText(textFor(processor, "cutoff", 440.0f), "440 Hz", "a low cutoff reads as Hz");
    requireText(textFor(processor, "sustain", 0.75f), "75 %", "sustain reads as a percentage");
    requireText(textFor(processor, "attack", 0.01f), "10 ms", "a short attack reads in milliseconds");
    requireText(textFor(processor, "release", 2.5f), "2.50 s", "a long release reads in seconds");
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
void soloSineOnA(theta::forge::Processor& processor)
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
    setValue(processor, "attack", 0.001f);
}

void oscillatorSuite()
{
    // Tuning is one multiplier built from three controls. Check the arithmetic
    // directly before checking that the voice honours it.
    theta::forge::Oscillator osc;
    requireClose(theta::forge::tuningRatio(osc), 1.0f, 0.0001f, "an untuned oscillator plays at pitch");
    osc.octave = 1.0f;
    requireClose(theta::forge::tuningRatio(osc), 2.0f, 0.0001f, "one octave doubles the frequency");
    osc.octave = 0.0f; osc.semitone = 12.0f;
    requireClose(theta::forge::tuningRatio(osc), 2.0f, 0.0001f, "twelve semitones doubles the frequency");
    osc.semitone = 0.0f; osc.fine = 100.0f;
    requireClose(theta::forge::tuningRatio(osc), std::pow(2.0f, 1.0f / 12.0f), 0.0001f,
                 "one hundred cents is one semitone");
    osc.octave = -1.0f; osc.semitone = 12.0f; osc.fine = 0.0f;
    requireClose(theta::forge::tuningRatio(osc), 1.0f, 0.0001f, "octave and semitone cancel");

    // And the voice honours it: an octave up doubles the zero-crossing rate.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    theta::forge::Processor atPitch;
    soloSineOnA(atPitch);
    renderNote(atPitch, buffer);
    const auto baseCrossings = zeroCrossings(buffer, 0, settled);
    require(baseCrossings > 0, "a solo sine crosses zero");

    theta::forge::Processor anOctaveUp;
    soloSineOnA(anOctaveUp);
    setValue(anOctaveUp, "oscAOctave", 1.0f);
    renderNote(anOctaveUp, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 static_cast<float>(baseCrossings * 2), static_cast<float>(baseCrossings) * 0.05f,
                 "an octave up doubles the oscillator's frequency");

    theta::forge::Processor sevenSemis;
    soloSineOnA(sevenSemis);
    setValue(sevenSemis, "oscASemitone", 7.0f);
    renderNote(sevenSemis, buffer);
    requireClose(static_cast<float>(zeroCrossings(buffer, 0, settled)),
                 baseCrossings * std::pow(2.0f, 7.0f / 12.0f), static_cast<float>(baseCrossings) * 0.05f,
                 "seven semitones is a fifth");

    // Pan law: hard left puts nothing in the right channel.
    theta::forge::Processor panned;
    soloSineOnA(panned);
    setValue(panned, "oscAPan", -1.0f);
    renderNote(panned, buffer);
    const auto left = rms(buffer, 0, settled);
    const auto right = rms(buffer, 1, settled);
    require(left > 0.0f, "a hard-left oscillator still feeds the left channel");
    require(right < left * 0.01f, "a hard-left oscillator is absent from the right channel");

    // Centred, an equal-power pan puts the same energy in both channels.
    theta::forge::Processor centred;
    soloSineOnA(centred);
    renderNote(centred, buffer);
    requireClose(rms(buffer, 0, settled), rms(buffer, 1, settled), 0.0001f,
                 "a centred oscillator is equal in both channels");

    // Level scales the oscillator directly, now that it owns one.
    const auto fullLevel = rms(buffer, 0, settled);
    theta::forge::Processor halfLevel;
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

    theta::forge::Processor single;
    soloSineOnA(single);
    setValue(single, "oscADetune", 0.3f);
    setValue(single, "oscAUnison", 1.0f);
    renderNote(single, longBuffer);
    const auto oneVoice = rms(longBuffer, 0, longSettled);
    require(oneVoice > 0.0f, "a single voice makes sound");

    // Every stack size, not just one, so the widest the parameter allows cannot
    // quietly be the loudest.
    for (int count = 2; count <= theta::forge::unisonMax; ++count)
    {
        theta::forge::Processor stacked;
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
                juce::String(theta::forge::unisonMax),
                "unison stops at the width the voice can render");

    // A full stack has to chorus, not throb. Evenly spaced members give every
    // neighbouring pair the same beat rate, which makes the whole stack swing
    // together on one slow period; unisonOffset exists to prevent exactly that,
    // and this is the check that it still does.
    theta::forge::Processor wide;
    soloSineOnA(wide);
    setValue(wide, "oscAPosition", 6.0f / 9.0f);
    setValue(wide, "oscADetune", 0.5f);
    setValue(wide, "oscAUnison", static_cast<float>(theta::forge::unisonMax));
    renderNote(wide, longBuffer);
    const auto depth = envelopeDepthDb(longBuffer, 0, longSettled);
    require(depth < 12.0f, "a full unison stack choruses rather than throbs");
    if (depth >= 12.0f)
        std::cerr << "       full stack envelope depth: " << depth << " dB\n";

    // The members must be unevenly spaced for that to hold. Checked directly so
    // a failure says which of the two things broke.
    auto smallest = 1.0e9f, largest = 0.0f;
    for (int i = 1; i < theta::forge::unisonMax; ++i)
    {
        const auto gap = theta::forge::unisonOffset(i, theta::forge::unisonMax)
                       - theta::forge::unisonOffset(i - 1, theta::forge::unisonMax);
        require(gap > 0.0f, "the stack stays in order");
        smallest = juce::jmin(smallest, gap);
        largest = juce::jmax(largest, gap);
    }
    require(largest > smallest * 2.0f, "the stack is not evenly spaced");
    requireClose(theta::forge::unisonOffset(theta::forge::unisonMax - 1, theta::forge::unisonMax)
                 - theta::forge::unisonOffset(0, theta::forge::unisonMax),
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
        theta::forge::Processor processor;
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
    theta::forge::Processor split;
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
    theta::forge::Processor driven;
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
    theta::forge::Processor typed;
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
    theta::forge::Processor off;
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

void envelopeSuite()
{
    // ENV 1 is hardwired to amplitude and is the one envelope Forge has, so its
    // timing is the timing of every note.
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    enum Stage { idle, attack, decay, sustain, release };

    theta::forge::Processor processor;
    soloSineOnA(processor);
    setValue(processor, "attack", 0.1f);
    setValue(processor, "decay", 0.1f);
    setValue(processor, "sustain", 0.5f);
    setValue(processor, "release", 0.2f);
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
    theta::forge::Processor early;
    soloSineOnA(early);
    setValue(early, "attack", 1.0f);
    setValue(early, "decay", 0.1f);
    setValue(early, "sustain", 0.9f);
    setValue(early, "release", 0.2f);
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

// Destination indices, matching theta::forge::destinations().
enum Destination { destOff = 0, destAPitch = 5, destSub = 11, destCutoff = 13 };

void setSlot(theta::forge::Processor& processor, int slot, float source, float destination, float depth)
{
    const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
    setValue(processor, id("Source").toRawUTF8(), source);
    setValue(processor, id("Dest").toRawUTF8(), destination);
    setValue(processor, id("Depth").toRawUTF8(), depth);
}

// A patch with a filter that is closed enough for cutoff modulation to be
// plainly audible, and no other slot interfering.
void closedFilterOnA(theta::forge::Processor& processor)
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
    for (int slot = 1; slot <= theta::forge::modSlotCount; ++slot)
        setSlot(processor, slot, srcOff, destOff, 0.0f);
}

void modulationSuite()
{
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> plain(2, samples), modulated(2, samples);

    // A slot at zero depth must cost nothing at all, not merely almost nothing:
    // an idle matrix may not colour the sound.
    theta::forge::Processor bare;
    closedFilterOnA(bare);
    renderNote(bare, plain);

    theta::forge::Processor wired;
    closedFilterOnA(wired);
    setSlot(wired, 1, srcLfo1, destCutoff, 0.0f);
    renderNote(wired, modulated);
    require(identical(plain, modulated), "a slot at zero depth renders bit-identically to no slot");

    // Pointed at nothing, a slot with depth is equally inert.
    theta::forge::Processor unpointed;
    closedFilterOnA(unpointed);
    setSlot(unpointed, 1, srcLfo1, destOff, 1.0f);
    renderNote(unpointed, modulated);
    require(identical(plain, modulated), "a slot with no destination renders bit-identically");

    // With depth, the envelope opens the filter and more gets through.
    theta::forge::Processor swept;
    closedFilterOnA(swept);
    setSlot(swept, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(swept, modulated);
    const auto closed = rms(plain, 0, settled);
    const auto opened = rms(modulated, 0, settled);
    require(opened > closed * 1.2f, "an envelope pointed at the cutoff opens the filter");

    // Two slots on one destination sum, rather than one winning.
    theta::forge::Processor halves;
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
    theta::forge::Processor watched;
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
    theta::forge::Processor idle;
    closedFilterOnA(idle);
    setSlot(idle, 1, static_cast<float>(theta::forge::ModSource::macro1), destCutoff, 1.0f);
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

    // Depth clamps at the destination's own limits instead of running past them.
    theta::forge::Processor slammed;
    closedFilterOnA(slammed);
    setValue(slammed, "cutoff", 18000.0f);
    setSlot(slammed, 1, srcEnv1, destCutoff, 1.0f);
    renderNote(slammed, modulated);
    require(allSamplesFinite(modulated), "modulation past a parameter's top stays finite");
    theta::forge::Processor atTop;
    closedFilterOnA(atTop);
    setValue(atTop, "cutoff", 18000.0f);
    renderNote(atTop, plain);
    require(identical(plain, modulated),
            "modulating a parameter already at its maximum changes nothing");

    // Velocity is a source like any other, and a softer note modulates less.
    const auto atVelocity = [&] (float velocity)
    {
        theta::forge::Processor processor;
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
    theta::forge::Processor bent;
    soloSineOnA(bent);
    for (int slot = 1; slot <= theta::forge::modSlotCount; ++slot)
        setSlot(bent, slot, srcOff, destOff, 0.0f);
    renderNote(bent, plain);
    const auto atPitch = zeroCrossings(plain, 0, settled);
    setSlot(bent, 1, srcEnv1, destAPitch, 1.0f);
    renderNote(bent, modulated);
    require(zeroCrossings(modulated, 0, settled) > atPitch,
            "an envelope pointed at pitch raises the note");

    // Macros are sources like any other, and reach their target only through
    // the matrix.
    const auto firstMacro = static_cast<float>(theta::forge::ModSource::macro1);
    require(theta::forge::modSourceCount == static_cast<int>(theta::forge::ModSource::macro1)
                + theta::forge::macroCount,
            "every macro is offered as a source");

    theta::forge::Processor byMacro;
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
    theta::forge::Processor byNote;
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

void lfoSuite()
{
    using theta::forge::LfoShape;
    constexpr int samples = 8192;

    // Every shape has to stay inside the bounds a source promises, and has to
    // come back to where it started after exactly one cycle. A source that ran
    // past one would push a destination further than its depth allows.
    for (int shape = 0; shape < theta::forge::lfoShapeCount; ++shape)
    {
        const auto which = static_cast<LfoShape>(shape);
        auto extreme = 0.0f;
        for (int i = 0; i <= 512; ++i)
        {
            const auto phase = static_cast<float>(i) / 512.0f;
            const auto at = theta::forge::lfoWave(which, phase, 1.0f);
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
        requireClose(theta::forge::lfoWave(continuous, 0.0f, 0.0f),
                     theta::forge::lfoWave(continuous, 1.0f, 0.0f), 0.0001f,
                     "a continuous LFO shape joins up across the cycle");
    for (const auto stepped : {LfoShape::saw, LfoShape::square})
        require(std::abs(theta::forge::lfoWave(stepped, 0.0f, 0.0f)
                         - theta::forge::lfoWave(stepped, 0.999f, 0.0f)) > 1.5f,
                "a saw and a square jump a full swing at the cycle boundary");

    // Sample and hold is its held step and nothing else: it does not move
    // within a cycle, which is what makes it a step rather than a ramp.
    for (const auto phase : {0.0f, 0.2f, 0.75f, 0.99f})
        requireClose(theta::forge::lfoWave(LfoShape::sampleHold, phase, -0.4f), -0.4f, 0.0001f,
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
                if (std::abs(theta::forge::lfoWave(static_cast<LfoShape>(a), phase, 0.0f)
                             - theta::forge::lfoWave(static_cast<LfoShape>(b), phase, 0.0f)) > 0.01f)
                    differs = true;
            }
            require(differs, "no two LFO shapes are the same curve");
        }

    // --- The three modes ------------------------------------------------------
    //
    // Driven through the Core rather than the Processor: the question is what
    // the phase does across a note, which is exactly what the Core owns and
    // what a block of audio would only let us infer.
    const auto runCore = [] (theta::forge::Core& core, const theta::forge::Patch& patch, int count)
    {
        float left = 0.0f, right = 0.0f;
        for (int i = 0; i < count; ++i) core.renderSample(patch, left, right);
    };
    // One cycle a second at 48 kHz, so a count of samples is a share of a cycle
    // and every check below reads as a fraction of one.
    constexpr int cycle = 48000;
    const auto patchFor = [] (theta::forge::LfoMode mode)
    {
        theta::forge::Patch patch;
        auto& first = patch.lfos.front();
        first.rate = 1.0f;
        first.shape = static_cast<float>(theta::forge::LfoShape::saw);
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
    const auto phaseAfterNote = [&] (theta::forge::Core& core, const theta::forge::Patch& patch,
                                     int note)
    {
        core.noteOn(note, 1.0f, patch);
        runCore(core, patch, 1);
        return core.lfoPosition(0);
    };

    for (const auto keyed : {theta::forge::LfoMode::trigger, theta::forge::LfoMode::envelope})
    {
        theta::forge::Core core;
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
        theta::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(theta::forge::LfoMode::trigger);
        patch.attack = 0.001f;
        patch.decay = 0.001f;
        patch.sustain = 1.0f;
        patch.release = 0.005f;
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
        theta::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(theta::forge::LfoMode::free);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        require(before > 0.2f, "a free-running LFO runs before a note arrives");
        requireClose(phaseAfterNote(core, patch, 57), before, 0.001f,
                     "a note does not restart a free-running LFO");
    }

    // TRIG loops for as long as the note is held: past the end of the cycle it
    // comes round again rather than stopping.
    {
        theta::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(theta::forge::LfoMode::trigger);
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
        theta::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(theta::forge::LfoMode::envelope);
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
        theta::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(theta::forge::LfoMode::trigger);
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
        theta::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(theta::forge::LfoMode::trigger);
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
        theta::forge::Core core;
        core.initialise(48000.0);
        theta::forge::Patch patch;
        for (int i = 0; i < theta::forge::lfoCount; ++i)
        {
            patch.lfos[static_cast<size_t>(i)].rate = 1.0f + static_cast<float>(i);
            patch.lfos[static_cast<size_t>(i)].mode = static_cast<float>(theta::forge::LfoMode::free);
        }
        runCore(core, patch, cycle / 8);
        for (int i = 0; i < theta::forge::lfoCount; ++i)
            for (int j = i + 1; j < theta::forge::lfoCount; ++j)
                require(std::abs(core.lfoPosition(i) - core.lfoPosition(j)) > 0.01f,
                        "no two LFOs are the same cycle");
    }

    // And each is a source in its own right, reachable from the matrix. Square
    // and free-running, so the source holds a steady +1 across the render
    // rather than sweeping through it.
    for (int lfo = 0; lfo < theta::forge::lfoCount; ++lfo)
    {
        const auto level = [lfo] (float depth)
        {
            theta::forge::Processor processor;
            soloSineOnA(processor);
            setValue(processor, "subEnable", 1.0f);
            setValue(processor, "subLevel", 0.0f);
            setValue(processor, theta::forge::lfoParameterId(lfo, "Shape").toRawUTF8(),
                     static_cast<float>(theta::forge::LfoShape::square));
            setValue(processor, theta::forge::lfoParameterId(lfo, "Mode").toRawUTF8(),
                     static_cast<float>(theta::forge::LfoMode::free));
            setValue(processor, theta::forge::lfoParameterId(lfo, "RateUnit").toRawUTF8(), 0.0f);
            setValue(processor, theta::forge::lfoParameterId(lfo, "Rate").toRawUTF8(), 0.05f);
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
    for (int a = 0; a < theta::forge::lfoModeCount; ++a)
    {
        require(juce::String(theta::forge::lfoModeName(a)).isNotEmpty(), "every LFO mode is named");
        for (int b = a + 1; b < theta::forge::lfoModeCount; ++b)
            require(juce::String(theta::forge::lfoModeName(a)) != theta::forge::lfoModeName(b),
                    "no two LFO modes share a name");
    }

    // Each LFO's rate is resolved from its own parameters, not LFO 1's.
    {
        theta::forge::Processor six;
        for (int lfo = 0; lfo < theta::forge::lfoCount; ++lfo)
        {
            setValue(six, theta::forge::lfoParameterId(lfo, "RateUnit").toRawUTF8(), 0.0f);
            setValue(six, theta::forge::lfoParameterId(lfo, "Rate").toRawUTF8(), 1.0f + static_cast<float>(lfo));
        }
        for (int lfo = 0; lfo < theta::forge::lfoCount; ++lfo)
            requireClose(six.lfoRateHz(lfo), 1.0f + static_cast<float>(lfo), 0.001f,
                         "each LFO reports its own rate");
    }

    // Free-running, the rate is the knob.
    theta::forge::Processor free;
    setValue(free, "lfo1RateUnit", 0.0f);
    setValue(free, "lfo1Rate", 3.0f);
    requireClose(free.lfoRateHz(0), 3.0f, 0.001f, "an unsynced LFO runs at its rate knob");

    // Synced, the rate is a division of the host's tempo and the knob stops
    // mattering. 1/4 at 120 BPM is two beats a second, so two cycles a second.
    theta::forge::Processor synced;
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
    theta::forge::Processor running;
    setValue(running, "lfo1RateUnit", 0.0f);
    setValue(running, "lfo1Rate", 1.0f);
    setValue(running, "lfo1Mode", static_cast<float>(theta::forge::LfoMode::free));
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
    theta::forge::Processor idle;
    setValue(idle, "lfo1RateUnit", 0.0f);
    setValue(idle, "lfo1Rate", 1.0f);
    setValue(idle, "lfo1Mode", static_cast<float>(theta::forge::LfoMode::trigger));
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
std::vector<float> readFrame(const theta::forge::Wavetable& table, int level, int frame, int points)
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
    using theta::forge::wavetableFrameSize;
    const auto& table = theta::forge::builtInWavetable();

    require(table.frameCount() == theta::forge::waveShapeCount,
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
    for (int shape = 0; shape < theta::forge::waveShapeCount; ++shape)
        for (int i = 0; i < wavetableFrameSize; i += 37)
        {
            const auto phase = static_cast<float>(i) / static_cast<float>(wavetableFrameSize);
            requireClose(table.frameSample(0, shape, phase), theta::forge::waveShape(shape, phase), 0.0005f,
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
    using theta::forge::waveShape;
    using theta::forge::waveShapeCount;

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
            std::cerr << "       " << theta::forge::waveShapeName(shape)
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
                std::cerr << "       " << theta::forge::waveShapeName(a) << " and "
                          << theta::forge::waveShapeName(b) << '\n';
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
            requireClose(theta::forge::waveAt(position, phase), waveShape(shape, phase), 0.0005f,
                         "a position on a frame reads that frame exactly");
        }
        requireText(theta::forge::waveLabel(position), theta::forge::waveShapeName(shape),
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
    requireText(theta::forge::waveLabel(0.5f / 9.0f), "SINE>TRI",
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
            const auto step = std::abs(theta::forge::waveAt(after, phase)
                                       - theta::forge::waveAt(before, phase));
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
        theta::forge::Processor processor;
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
        setValue(processor, "release", 4.0f);
        setValue(processor, "sustain", 1.0f);
        setValue(processor, "attack", 0.01f);

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
        theta::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "polyphony", polyphony);
        setValue(processor, "sustain", 1.0f);
        setValue(processor, "release", 0.01f);
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
    theta::forge::Processor processor;
    // The setting that leaves the most in the filter to be cut off: a cutoff
    // near the bottom of its range with the sub at full level, a sine an octave
    // down being exactly what a low cutoff passes.
    setValue(processor, "cutoff", 54.0f);
    setValue(processor, "subEnable", 1.0f);
    setValue(processor, "subLevel", 1.0f);
    setValue(processor, "release", 0.35f);

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
        theta::forge::Processor processor;
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
    return juce::File(THETA_FORGE_SOURCE_DIR).getChildFile("tables");
}

// --- The table editor ---------------------------------------------------------
//
// Everything M9b-2 and M9b-3 added: the frames as a person changes them, the
// hand-over to the audio thread, and reading a table out of a file.
void tableEditSuite()
{
    using theta::forge::WavetableEdit;
    using theta::forge::WavetableStore;
    using theta::forge::wavetableFrameSize;
    using theta::forge::maxEditableFrames;

    WavetableStore store;

    // A fresh store is the built-in ten, and what the editor draws is exactly
    // what the oscillator has been playing â€” the M9a guarantee, now that the
    // display reads the editable frames rather than the formulas.
    require(store.edit(0).frameCount() == theta::forge::waveShapeCount,
            "a fresh table holds the built-in frames");
    require(store.edit(0).isUntouched(), "a fresh table counts as untouched");
    require(store.isBuiltIn(0) && store.frameCount(0) == theta::forge::waveShapeCount,
            "what POSITION reads is published with the table");
    for (const auto position : {0.0f, 0.23f, 0.5f, 0.77f, 1.0f})
        for (const auto phase : {0.0f, 0.1f, 0.37f, 0.62f, 0.99f})
            requireClose(store.edit(0).sample(position, phase), theta::forge::waveAt(position, phase),
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
    requireText(theta::forge::positionLabel(theta::forge::waveShapeCount, true, 6.0f / 9.0f), "SAW",
                "POSITION names a built-in shape");
    requireText(theta::forge::positionLabel(16, false, 0.0f), "1 / 16",
                "POSITION counts frames on a table nobody named");
    requireText(theta::forge::positionLabel(16, false, 1.0f), "16 / 16",
                "POSITION counts to the last frame");
}

// Reading a wavetable out of a .wav, against the files Forge actually ships.
void tableFileSuite()
{
    theta::forge::Processor processor;
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
            for (int i = 0; i < theta::forge::wavetableFrameSize; ++i)
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
        .getNonexistentChildFile("theta-forge-table-test", {}, true);
    require(directory.createDirectory(), "temporary table directory can be created");
    const auto notAudio = directory.getChildFile("not-a-table.wav");
    notAudio.replaceWithText("this is not a wav file");
    require(!processor.importTable(0, notAudio).wasOk(), "a file that is not audio is refused");
    directory.deleteRecursively();

    // And the table reaches the voice. With everything else switched off and
    // POSITION parked on the first frame, flattening that frame has to silence
    // the oscillator â€” which it can only do if the voice is reading the frames
    // the editor changed.
    theta::forge::Processor voice;
    for (const auto* id : {"oscBEnable", "subEnable", "noiseEnable"}) setValue(voice, id, 0.0f);
    setValue(voice, "oscAPosition", 0.0f);
    require(peakForNote(voice) > 0.01f, "the oscillator sounds before its frame is changed");
    voice.tableStore().edit(0).draw(0, 0.0f, 0.0f, 1.0f, 0.0f);
    voice.tableStore().publishFrame(0, 0);
    require(peakForNote(voice) < 0.001f, "flattening the frame POSITION is on silences the oscillator");
}

void tuningSuite();

void engineSuite()
{
    theta::forge::Processor processor;

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
    envelopeSuite();
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
    tuningSuite();

    // Output stays finite and bounded across an extreme patch.
    theta::forge::Processor extreme;
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

    theta::forge::Patch patch;
    patch.a.enable = 1.0f;
    patch.a.position = 6.0f / 9.0f;   // the SAW frame, landed on exactly
    patch.a.unison = 1.0f;
    patch.a.level = 0.75f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.attack = 0.001f;
    patch.sustain = 1.0f;

    theta::forge::Core core;
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

    if (suite.isEmpty() || suite == "--layout") layoutSuite();
    if (suite.isEmpty() || suite == "--presets") { presetSuite(); legacyStateSuite(); }
    if (suite.isEmpty() || suite == "--engine") engineSuite();

    if (failures > 0)
    {
        std::cerr << failures << " Forge check(s) failed\n";
        return 1;
    }
    std::cout << "Theta Forge checks passed" << (suite.isEmpty() ? "" : " (" + suite + ")") << '\n';
    return 0;
}
