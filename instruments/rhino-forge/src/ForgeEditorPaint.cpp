// What the panel draws that is not a component: the cached metal chassis, the
// module boxes over it, the displays inside them, and the drag line and
// value bubble over the top. The cache keys are here too, because what the
// chassis is redrawn for is the same question as what it looks like.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
juce::String Editor::chassisKey(float scale) const
{
    juce::String key;
    key << getWidth() << 'x' << getHeight() << '@' << juce::String(scale, 3);
    return key;
}

juce::String Editor::chromeKey(float scale) const
{
    auto key = chassisKey(scale);
    key << '|' << static_cast<int>(page) << (fxExpanded ? 'E' : 'e') << (fxListOpen ? 'L' : 'l');
    // A module that has been switched off is drawn dimmer, and one the tab is
    // hiding is not drawn at all, so both belong in the key.
    for (const auto& module : moduleUis)
    {
        key << (!moduleShown(*module.descriptor) ? '-' : module.on() ? '1' : '0');
        // A plate is drawn in its module's colour, so a colour that has been
        // changed has to throw the cached layer away like anything else.
        if (module.descriptor->display == ui::Display::oscillator)
            key << processor.panelColour(module.descriptor->id);
    }
    return key;
}

void Editor::paintPlates(juce::Graphics& g)
{
    // The shared plates go down first, because the modules that sit on them
    // draw their own panels on top. Each group is drawn once however many
    // members it has, which is what the set is for.
    juce::StringArray drawn;
    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (descriptor.group == nullptr || !moduleShown(descriptor)) continue;
        if (drawn.contains(descriptor.group)) continue;
        drawn.add(descriptor.group);
        ui::drawGroupPlate(g, ui::groupBounds(getLocalBounds(), descriptor.group, pageOf(descriptor)),
                           descriptor.group, ui::plateCode(descriptor, pageOf(descriptor)));
    }

    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (!moduleShown(descriptor)) continue;
        ui::drawModuleShell(g, moduleAreaFor(descriptor), descriptor, module.on(),
                            accentOf(descriptor), ui::plateCode(descriptor, pageOf(descriptor)));
    }
}

void Editor::paint(juce::Graphics& g)
{
    // Rendered at the display's own pixel scale rather than at the panel's
    // logical size, so the cached layer is as sharp on a scaled monitor as it
    // would be drawn straight onto the window, including during a resize.
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto key = chromeKey(scale);
    // The tab that was showing a moment ago is still in hand: going back to it
    // is a swap, and the panel it left takes the place this one had.
    if (key != chromeState && key == previousChromeState && !previousChrome.isNull())
    {
        std::swap(chrome, previousChrome);
        std::swap(chromeState, previousChromeState);
    }
    if (chrome.isNull() || key != chromeState)
    {
        const auto pixelWidth = juce::jmax(1, static_cast<int>(std::ceil(getWidth() * scale)));
        const auto pixelHeight = juce::jmax(1, static_cast<int>(std::ceil(getHeight() * scale)));
        // Whatever is being replaced becomes the one held, so the way back is
        // the cheap direction whichever way the tabs are walked. A layer at a
        // different size cannot be reused and is discarded below.
        if (!chrome.isNull() && chromeState.isNotEmpty())
        {
            previousChrome = chrome;
            previousChromeState = chromeState;
        }
        if (previousChrome.getWidth() != pixelWidth || previousChrome.getHeight() != pixelHeight)
        {
            previousChrome = {};
            previousChromeState = {};
        }
        chrome = juce::Image(juce::Image::ARGB, pixelWidth, pixelHeight, true);

        // The chassis first, and only when its own key has moved: a tab switch,
        // a module switched off and an oscillator recoloured all leave the metal
        // exactly as it was, and it is the more expensive half of the layer.
        const auto backdrop = chassisKey(scale);
        if (chassisLayer.isNull() || backdrop != chassisState)
        {
            chassisLayer = juce::Image(juce::Image::ARGB, pixelWidth, pixelHeight, true);
            juce::Graphics into(chassisLayer);
            into.addTransform(juce::AffineTransform::scale(scale));
            ui::drawBackdrop(into, getLocalBounds());
            chassisState = backdrop;
        }

        // And the page's own plates onto the metal.
        {
            juce::Graphics into(chrome);
            into.drawImageAt(chassisLayer, 0, 0);
            into.addTransform(juce::AffineTransform::scale(scale));
            paintPlates(into);
        }
        chromeState = key;
    }
    // Undo only the raster scale. Fitting the rounded image dimensions back
    // into the logical bounds would resample it at fractional display scales.
    g.drawImageTransformed(chrome, juce::AffineTransform::scale(1.0f / scale));

    // Only the plate's state, over the cached chassis that already carries its
    // metal: the lamp follows arpEnable and the face follows whether the panes
    // are showing, and neither of those is something the metal knows about.
    ui::drawArpPlateState(g, getLocalBounds(), value("arpEnable") >= 0.5f, arpOpen);

    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (!moduleShown(descriptor)) continue;
        const auto area = moduleAreaFor(descriptor);
        const auto on = module.on();
        const auto alpha = on ? 1.0f : 0.35f;
        // A module's header carries what it is doing rather than what it is:
        // the envelope names the stage it is in, and the LFO names the rate it
        // is actually running at, which in sync is a tempo division and so
        // cannot be read off the greyed-out rate knob.
        const auto tableModule = juce::String(descriptor.id) == "table";
        const auto rackModule = isFxModule(descriptor);
        ui::drawModuleDetail(g, area, descriptor, on,
                             descriptor.display == ui::Display::envelope ? envHeaderDetail()
                             : descriptor.display == ui::Display::lfo ? lfoHeaderDetail()
                             : rackModule ? fxHeaderDetail()
                             : tableModule && tablePanel != nullptr ? tablePanel->headerDetail()
                                 : juce::String(),
                             rackModule ? (ui::fxViewButtonSize * 2 + ui::fxViewButtonGap + 12) : 0);

        if (descriptor.columnHeaderHeight > 0) paintTable(g, area, descriptor);
        if (rackModule)
        {
            paintFxShelves(g, area, descriptor);
            paintFxList(g, area, descriptor);
        }

        const auto display = ui::displayBounds(area, descriptor);
        if (display.isEmpty()) continue;
        const auto accent = accentOf(descriptor);
        // An oscillator shows its wave on a picture tube; the other displays
        // stay flat wells, which is what keeps the tubes reading as screens.
        if (descriptor.display == ui::Display::oscillator)
            ui::drawCrtScreen(g, display, accent, alpha);
        else if (descriptor.display == ui::Display::envelope)
            // Short of the full strip: the zoom control has its own well beside
            // this one, and draws it with the curve.
            ui::drawDisplayWell(g, ui::envelopePlotBounds(display));
        else
            ui::drawDisplayWell(g, display);
        switch (descriptor.display)
        {
            case ui::Display::oscillator:
                // Each oscillator draws the table it is actually reading, taken
                // from the frames as authored rather than from a band-limited
                // copy — so the tube shows the table and not a formula, and not
                // whichever copy the note being held happens to want.
                if (const auto* source = ui::displaySourceId(descriptor))
                {
                    // Warped as the voice warps it, which is what the manual
                    // means when it says the 2D view shows what the mode is
                    // doing. The modes it cannot honestly draw take themselves
                    // off the picture -- see drawWaveform.
                    const auto second = juce::String(descriptor.id) == "oscB";
                    ui::drawWaveform(g, display, processor.tableStore().edit(second ? 1 : 0),
                                     value(source), warpStagesOf(second ? "oscB" : "oscA"),
                                     accent, alpha);
                }
                break;
            case ui::Display::envelope:
            {
                // Whichever envelope the module is showing, drawn from its own
                // knobs, its own window and its own live reading.
                const auto env = shownEnv();
                const auto stage = static_cast<ui::Stage>(
                    juce::jlimit(0, 4, processor.envelopeStage(env)));
                ui::drawEnvelope(g, display, value(envParameterId(env, "Attack")),
                                 value(envParameterId(env, "Decay")),
                                 value(envParameterId(env, "Sustain")),
                                 value(envParameterId(env, "Release")), accent, alpha,
                                 stage, processor.envelopeLevel(env),
                                 envelopeZoom[static_cast<size_t>(env)]);
                break;
            }
            case ui::Display::lfo:
            {
                const auto lfo = shownLfo();
                ui::drawLfo(g, display,
                            static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
                                                               juce::roundToInt(value(lfoParameterId(lfo, "Shape"))))),
                            processor.lfoPhase(lfo), processor.lfoValue(lfo), accent, alpha);
                break;
            }
            case ui::Display::filter:
                ui::drawFilterResponse(g, display, filterTypeOf(value("filterType")),
                                       value("cutoff"), value("resonance"), accent, alpha);
                break;
            case ui::Display::none:
                break;
        }
    }

    // The line follows a source handle to the cursor, and the knob under it
    // lights up, so a drop lands where it looks like it will.
    if (draggingHandle != nullptr)
    {
        const auto from = draggingHandle->getBounds().toFloat().getCentre();
        const auto to = dragPosition.toFloat();
        g.setColour(ui::signalViolet.withAlpha(0.55f));
        g.drawLine({from, to}, 2.0f);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(to));

        if (const auto* target = const_cast<Editor*>(this)->controlAt(dragPosition))
        {
            const auto reachable = destinationFor(target->id) != 0;
            g.setColour((reachable ? ui::signalViolet : ui::mutedText).withAlpha(0.9f));
            g.drawRoundedRectangle(target->slider.getBounds().toFloat().reduced(2.0f), 4.0f, 1.6f);
        }
    }
}

// The furniture that makes eight rows of fields read as a table: the column
// titles, once, above the rows; a rule under them; and every row numbered in
// the gutter, against a band on alternate rows.
void Editor::paintTable(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& descriptor)
{
    const auto accent = accentOf(descriptor);
    const auto titles = ui::columnTitleBounds(area, descriptor);

    ui::drawColumnTitle(g, titles.withWidth(descriptor.rowGutter), "#");
    const auto& first = descriptor.rows.front().controls;
    for (int i = 0; i < static_cast<int>(first.size()); ++i)
    {
        const auto cell = ui::cellBounds(area, descriptor, 0, i);
        ui::drawColumnTitle(g, titles.withX(cell.getX()).withWidth(cell.getWidth()),
                            first[static_cast<size_t>(i)].label);
    }
    ui::drawColumnTitleRule(g, titles);

    for (int row = 0; row < static_cast<int>(descriptor.rows.size()); ++row)
        ui::drawTableRow(g, ui::rowBounds(area, descriptor, row),
                         ui::rowGutterBounds(area, descriptor, row),
                         row + 1, slotIsLive(row), accent);
}

// A wash across the keys the letters can reach, with a brighter line under
// them. Faint enough to read as a shadow on the piano rather than as another
// lit thing competing with the notes actually being held.
void Editor::paintOverChildren(juce::Graphics& g)
{
    const auto first = computerKeyOctave * 12;
    const auto last = first + computerKeySpan - 1;
    if (first < keyboard.getRangeStart() || last > keyboard.getRangeEnd()) return;

    const auto reach = (keyboard.getRectangleForKey(first)
                            .getUnion(keyboard.getRectangleForKey(last)))
                           .translated(static_cast<float>(keyboard.getX()),
                                       static_cast<float>(keyboard.getY()));

    g.setColour(ui::electricBlue.withAlpha(0.07f));
    g.fillRect(reach);
    g.setColour(ui::electricBlue.withAlpha(0.3f));
    g.fillRect(reach.withTop(reach.getBottom() - 2.0f));
}
}
