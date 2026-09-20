// The panel: what is declared, where it lands, and that the layout and the
// parameter list agree in both directions.
#include "ForgeTestSupport.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTooltips.h"
#include "../ui/ForgeVisuals.h"

namespace rhino::forge::tests
{
namespace
{
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
    // other by the same parameter, one each way round — otherwise they would be
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
            if (juce::String(modules[i].id) == "fx")
                require(row.getHeight() == rhino::forge::ui::fxSlotHeight,
                        "every FX control row has the fixed rack height");
            else
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
    for (const auto width : {rhino::forge::ui::minPanelWidth, rhino::forge::ui::defaultPanelWidth,
                             rhino::forge::ui::maxPanelWidth})
    {
        using namespace rhino::forge::ui;
        const juce::Rectangle<int> headerBounds(0, 0, width, defaultPanelHeight);
        const auto identity = identityPlateBounds(headerBounds);
        const auto preset = presetLabelBounds(headerBounds);
        require(tabBounds(tabCount - 1, width).getRight() < identity.getX(),
                "tabs stay inside the left header plate at every window width");
        require(identity.contains(preset) && preset.getWidth() >= 60,
                "the maker plate leaves readable space for the preset name");
        require(!preset.intersects(presetButtonBounds(headerBounds, false)),
                "the preset name stays clear of the load button");
        require(identity.contains(presetButtonBounds(headerBounds, true)),
                "the save button stays inside its plate");
    }

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

    // A wave grid is painted cell by cell and hit-tested cell by cell, against
    // the same arithmetic — so what that arithmetic has to guarantee is that
    // the cells tile the box: no gap a click could fall into, no overlap where
    // two shapes would answer for one point, and nothing outside the component
    // the pointer is being measured against. Checked over the range of sizes
    // the picker is laid out at rather than at one, because tiling by dividing
    // a box is exactly where integer division goes wrong.
    {
        rhino::forge::ui::WaveGrid grid;
        grid.choices = rhino::forge::subShapeCount;
        for (int width = 30; width <= rhino::forge::ui::maxWaveGridWidth; width += 3)
            for (int height = 30; height <= rhino::forge::ui::maxWaveGridHeight; height += 3)
            {
                grid.setBounds(0, 0, width, height);
                auto covered = 0;
                for (int i = 0; i < grid.choices; ++i)
                {
                    const auto cell = grid.cellBounds(i);
                    require(!cell.isEmpty() && grid.getLocalBounds().contains(cell),
                            "a wave grid's cell stays inside the picker");
                    covered += cell.getWidth() * cell.getHeight();
                    for (int j = i + 1; j < grid.choices; ++j)
                        require(!cell.intersects(grid.cellBounds(j)),
                                "no two wave grid cells overlap");
                }
                require(covered == width * height, "a wave grid's cells tile it exactly");
            }
    }

    // The picker, built by the editor the panel actually opens with rather than
    // by this test. Everything above holds the declaration to its parameters;
    // this holds the component to its declaration — that the sub's six shapes
    // reached it, that choosing one writes the parameter a host reads, and that
    // the cell lit afterwards is the cell chosen. Nothing else on the panel
    // drives a parameter from a picture, so nothing else was covering this.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        require(editor != nullptr, "the panel opens");
        if (editor != nullptr)
        {
            editor->setSize(rhino::forge::ui::defaultPanelWidth, rhino::forge::ui::defaultPanelHeight);
            rhino::forge::ui::WaveGrid* grid = nullptr;
            for (auto* child : editor->getChildren())
                if (auto* found = dynamic_cast<rhino::forge::ui::WaveGrid*>(child)) grid = found;
            require(grid != nullptr, "the panel builds a picker for the wave grid it declares");
            if (grid != nullptr)
            {
                require(grid->choices == rhino::forge::subShapeCount && grid->shapeAt != nullptr,
                        "the picker offers every sub shape, drawn from the shapes themselves");
                require(!grid->getBounds().isEmpty(), "the picker is given a rectangle to draw in");
                for (int shape = rhino::forge::subShapeCount; --shape >= 0;)
                {
                    grid->onChoose(shape);
                    requireClose(value(processor, "subWave"), static_cast<float>(shape), 0.001f,
                                 "choosing a shape writes the parameter the engine reads");
                    require(grid->chosen == shape, "the picker lights the shape that was chosen");
                }
                // And back, so the panel this test opened leaves the processor
                // on the shape it found it on.
                grid->onChoose(0);
            }
        }
    }

    // The part number stamped on a plate reads across the panel, not down the
    // declaration: the numbers a tab shows have to run 01, 02, 03 left to
    // right with no gaps, whichever modules that tab is hiding.
    for (const auto page : rhino::forge::ui::tabPages)
    {
        std::vector<std::pair<int, juce::String>> shown;
        for (const auto& module : modules)
        {
            if (!rhino::forge::ui::onPage(module, page) || module.row != 0) continue;
            shown.emplace_back(module.column, rhino::forge::ui::plateCode(module, page));
        }
        std::sort(shown.begin(), shown.end(),
                  [] (const auto& a, const auto& b) { return a.first < b.first; });
        for (int i = 0; i < static_cast<int>(shown.size()); ++i)
            require(shown[static_cast<size_t>(i)].second
                        == "A-" + juce::String(i + 1).paddedLeft('0', 2),
                    "a tab's part numbers run left to right without gaps");
    }

    // An oscillator's colour is chosen by right-clicking its LED, and a
    // juce::Button triggers on *any* mouse button — so without the gesture
    // being taken out of both halves of the click, reaching for the menu would
    // switch the oscillator off on the way. That is the whole reason EnableLed
    // overrides mouseDown and mouseUp, and it is what this holds.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        require(editor != nullptr, "the panel opens");
        if (editor != nullptr)
        {
            editor->setSize(rhino::forge::ui::defaultPanelWidth, rhino::forge::ui::defaultPanelHeight);
            std::vector<rhino::forge::ui::EnableLed*> leds;
            for (auto* child : editor->getChildren())
                if (auto* led = dynamic_cast<rhino::forge::ui::EnableLed*>(child)) leds.push_back(led);
            require(!leds.empty(), "the panel builds an enable LED for the modules that switch off");

            auto withMenu = 0;
            for (auto* led : leds) if (led->onColourMenu != nullptr) ++withMenu;
            auto oscillators = 0;
            for (const auto& module : modules)
                if (module.display == rhino::forge::ui::Display::oscillator
                    && module.enableId != nullptr) ++oscillators;
            require(withMenu == oscillators,
                    "a colour menu is attached to every oscillator's LED and to nothing else");

            // What the menu actually offers. Every colour on it once, the one
            // in use ticked, and a swatch on each row — the swatch being the
            // part you choose by, so a row without one is a broken menu even
            // though it still reads correctly.
            {
                const auto current = processor.panelColour("oscA");
                auto menu = rhino::forge::ui::panelColourMenu(current, "OSC A colour");
                auto rows = 0, ticked = 0, swatches = 0;
                juce::PopupMenu::MenuItemIterator walk(menu);
                while (walk.next())
                {
                    const auto& item = walk.getItem();
                    if (item.itemID == 0) continue;   // the section header
                    ++rows;
                    if (item.isTicked) ++ticked;
                    if (!item.colour.isTransparent()) ++swatches;
                    require(item.itemID == rows, "the menu's colours are offered in order");
                    require(item.text == rhino::forge::ui::panelColourName(
                                rhino::forge::ui::panelColourFrom(rows - 1)),
                            "each row of the menu is named for the colour it sets");
                }
                require(rows == rhino::forge::ui::panelColourCount,
                        "the menu offers every colour and no others");
                require(ticked == 1, "the menu ticks exactly the colour in use");
                require(swatches == rows, "every row of the menu carries its swatch");
            }

            auto* led = leds.front();
            for (auto* candidate : leds) if (candidate->onColourMenu != nullptr) led = candidate;
            if (led->onColourMenu != nullptr)
            {
                auto opened = 0;
                const auto restore = led->onColourMenu;
                led->onColourMenu = [&opened] { ++opened; };
                const auto before = led->getToggleState();

                auto mouse = juce::Desktop::getInstance().getMainMouseSource();
                const juce::MouseEvent click(mouse, {}, juce::ModifierKeys::rightButtonModifier,
                                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, led, led,
                                             juce::Time::getCurrentTime(), {},
                                             juce::Time::getCurrentTime(), 1, false);
                led->mouseDown(click);
                led->mouseUp(click);
                require(opened == 1, "right-clicking an oscillator's LED opens the colour menu");
                require(led->getToggleState() == before,
                        "right-clicking the LED leaves the module switched as it was");
                led->onColourMenu = restore;
            }
        }
    }

    // The chassis and the module plates are cached between frames, so the panel
    // now has a way of being wrong it did not have before: a change that moves
    // a plate but does not reach the cache's key would leave the old picture on
    // screen. The strip along the foot of the top row carries the plates'
    // stamped legends and nothing else — no control is ever laid out in it — so
    // a difference there is a difference in the cached layer, and changing tab
    // has to produce one.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        require(editor != nullptr, "the panel opens");
        if (editor != nullptr)
        {
            editor->setSize(rhino::forge::ui::defaultPanelWidth, rhino::forge::ui::defaultPanelHeight);
            const auto shot = [&editor]
            {
                juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                juce::Graphics g(image);
                editor->paintEntireComponent(g, false);
                return image;
            };
            const auto band = rhino::forge::ui::plateFooterBounds(
                rhino::forge::ui::moduleBounds(editor->getLocalBounds(), modules.front()));

            const auto before = shot();
            rhino::forge::ui::PageTab* mix = nullptr;
            for (auto* child : editor->getChildren())
                if (auto* tab = dynamic_cast<rhino::forge::ui::PageTab*>(child))
                    if (tab->getButtonText() == "MIX") mix = tab;
            require(mix != nullptr && mix->onClick != nullptr,
                    "the panel builds a tab that can be clicked for every page");
            if (mix != nullptr && mix->onClick != nullptr)
            {
                mix->onClick();
                const auto after = shot();
                auto redrawn = false;
                for (int y = band.getY(); y < band.getBottom() && !redrawn; ++y)
                    for (int x = band.getX(); x < band.getRight() && !redrawn; ++x)
                        redrawn = before.getPixelAt(x, y) != after.getPixelAt(x, y);
                require(redrawn, "changing tab redraws the cached plate layer");
            }
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
                if (juce::String(module.id) != "fx")
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

                // And every ordinary control still lands inside the module
                // that owns it. FX rows intentionally continue below their
                // fixed-height viewport and are tested as a scrolled rack.
                if (juce::String(modules[i].id) == "fx") continue;
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

    // The FX rack has a second geometry when it grows through both module
    // rows, and two widths for its left-hand list. Exercise all four view
    // combinations at every supported size: the sidebar must never steal a
    // control's cell or escape the module it is summarising.
    const rhino::forge::ui::Module* fxModule = nullptr;
    for (const auto& module : modules)
        if (juce::String(module.id) == "fx") fxModule = &module;
    require(fxModule != nullptr, "the panel declares its effects rack");
    if (fxModule != nullptr)
        for (int width = rhino::forge::ui::minPanelWidth; width <= rhino::forge::ui::maxPanelWidth; width += 40)
            for (int height = rhino::forge::ui::minPanelHeight; height <= rhino::forge::ui::maxPanelHeight; height += 40)
                for (const auto expanded : {false, true})
                    for (const auto listOpen : {false, true})
                    {
                        const auto panel = juce::Rectangle<int>(0, 0, width, height);
                        const auto area = rhino::forge::ui::fxModuleBounds(panel, *fxModule, expanded);
                        const auto list = rhino::forge::ui::fxListBounds(area, listOpen);
                        const auto rack = rhino::forge::ui::fxRackBounds(area, listOpen);
                        const auto add = rhino::forge::ui::fxAddButtonBounds(area, listOpen);
                        const auto slots = rhino::forge::ui::fxSlotViewportBounds(area);
                        require(rhino::forge::ui::contentBounds(panel).contains(area),
                                "either FX height stays inside the module field");
                        require(area.contains(list) && area.contains(rack) && !list.intersects(rack),
                                "the FX list and editor divide the rack without overlap");
                        require(list.contains(add) && area.contains(slots) && !add.intersects(slots),
                                "the fixed FX add action stays above the scrolling slots");
                        require(area.contains(rhino::forge::ui::fxExpandButtonBounds(area))
                                    && area.contains(rhino::forge::ui::fxListButtonBounds(area)),
                                "both FX view buttons stay in the rack header");
                        const auto visible = rhino::forge::ui::fxVisibleSlotCount(area);
                        const auto lastFirst = rhino::forge::ui::fxMaxFirstSlot(area);
                        require(visible >= 1 && visible <= rhino::forge::fxSlotCount,
                                "the FX viewport always exposes at least one whole slot");
                        for (const auto first : {0, lastFirst})
                        {
                            const auto scrolledRack = rhino::forge::ui::fxScrolledRackBounds(
                                area, listOpen, first);
                            for (int slot = first; slot < first + visible; ++slot)
                            {
                                const auto item = rhino::forge::ui::fxListItemBounds(
                                    area, *fxModule, slot, listOpen, first);
                                require(list.contains(item), "every visible FX list item stays in the list");
                                require(slots.contains(item), "every visible FX item stays below the add action");
                                require(item.getHeight() == rhino::forge::ui::fxSlotHeight,
                                        "an FX slot keeps its fixed height in every view");
                                const auto& row = fxModule->rows[static_cast<size_t>(slot)];
                                for (int control = 0; control < static_cast<int>(row.controls.size()); ++control)
                                {
                                    const auto block = rhino::forge::ui::controlBlock(
                                        scrolledRack, *fxModule, slot, control,
                                        rhino::forge::ui::uniformKnobDiameter(panel));
                                    require(rack.contains(block), "every visible FX control stays beside the list");
                                }
                            }
                        }
                    }

    // A macro is the one control laid out by geometry of its own rather than by
    // controlBlock: a knob, the plate beside it carrying the number you drag
    // and the count of what that number reaches, and the macro's name under
    // both — three rectangles carved out of a cell that is 60 pixels wide at
    // the smallest window the panel allows. Integer division over a cell that
    // small is exactly where three rectangles quietly begin to touch, so this
    // is swept rather than sampled at the corners.
    const rhino::forge::ui::Module* macroModule = nullptr;
    for (const auto& module : modules)
        if (juce::String(module.id) == "macros") macroModule = &module;
    require(macroModule != nullptr, "the panel declares its macros");
    if (macroModule != nullptr)
        for (int width = rhino::forge::ui::minPanelWidth; width <= rhino::forge::ui::maxPanelWidth; width += 20)
            for (int height = rhino::forge::ui::minPanelHeight; height <= rhino::forge::ui::maxPanelHeight; height += 20)
            {
                using namespace rhino::forge::ui;
                const auto panel = juce::Rectangle<int>(0, 0, width, height);
                const auto area = moduleBounds(panel, *macroModule);
                const auto shared = uniformKnobDiameter(panel);
                for (int r = 0; r < static_cast<int>(macroModule->rows.size()); ++r)
                {
                    const auto& row = macroModule->rows[static_cast<size_t>(r)];
                    for (int c = 0; c < static_cast<int>(row.controls.size()); ++c)
                    {
                        const auto cell = cellBounds(area, *macroModule, r, c);
                        const auto knob = macroKnobBounds(cell, shared);
                        const auto plate = macroPlateBounds(cell, shared);
                        const auto name = macroNameBounds(cell, shared);
                        juce::String fault;
                        if (!cell.contains(knob) || !cell.contains(plate) || !cell.contains(name))
                            fault = "a macro's knob, plate and name all stay inside its cell";
                        else if (knob.intersects(plate) || knob.intersects(name)
                                 || plate.intersects(name))
                            fault = "nothing in a macro's cell sits on top of anything else in it";
                        else if (knob.getWidth() < 24)
                            fault = "a macro's knob stays usable at every allowed size";
                        else if (knob.getWidth() > shared)
                            fault = "a macro never outranks the knobs it drives";
                        else if (plate.getWidth() < minMacroPlateWidth)
                            fault = "a macro's plate stays big enough to take hold of";
                        if (fault.isEmpty()) continue;
                        require(false, fault.toRawUTF8());
                        std::cerr << "       macro " << (r * 2 + c + 1) << " at "
                                  << width << "x" << height << '\n';
                    }
                }
            }
}
}

void layoutTests()
{
    layoutSuite();
}
}
