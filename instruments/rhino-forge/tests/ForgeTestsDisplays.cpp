// What the modules draw of themselves: the filter response, the envelope, and
// the cached metal behind them. Each one is computed from the same arithmetic
// the engine runs, so a display cannot claim one thing while the voice does
// another - these checks are what holds that.
#include "ForgeTestSupport.h"
#include "../ui/ForgePlacement.h"
#include "../ui/ForgeControls.h"
#include "../ui/ForgeEnvelopeVisuals.h"
#include "../ui/ForgeFilterVisuals.h"

namespace rhino::forge::tests
{
namespace
{
// ------------------------------------------------------------- fx displays ---

// A slot's display claims to be drawn from the arithmetic the slot actually
// runs. These check that claim where it can be checked exactly: a curve is
// compared against the very function the engine calls, not against a picture of
// what the effect usually looks like.
//
// It is the same standard the filter module's display is held to below, and for
// the same reason — a display that is merely plausible is worse than none,
// because it is believed.
// The panel's chassis and module plates are cached as an image, and the layer
// the previous tab was showing is held whole so that going back to it is a swap
// rather than a redraw. Everything the layer is drawn from therefore has to be
// in Editor::chromeKey. One that has forgotten an input does not fail anywhere
// else: it puts the previous tab's metal under the current tab's controls, and
// only a pair of eyes on the running plugin would catch it.
//
// So: render a page on an editor that has been walked around the panel and had
// its layer swapped back, and require it to match the same page on an editor
// seeing it for the first time.
void chromeCacheSuite()
{
    const auto render = [] (std::initializer_list<const char*> pages)
    {
        auto processor = std::make_unique<rhino::forge::Processor>();
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        juce::Image canvas(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        for (const auto* page : pages)
        {
            for (auto* child : editor->getChildren())
                if (auto* tab = dynamic_cast<rhino::forge::ui::PageTab*>(child))
                    if (tab->getButtonText() == juce::String(page) && tab->onClick) tab->onClick();
            juce::Graphics g(canvas);
            editor->paint(g);
        }
        return canvas;
    };

    const auto same = [] (const juce::Image& a, const juce::Image& b)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
        const juce::Image::BitmapData left(a, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData right(b, juce::Image::BitmapData::readOnly);
        int differentPixels = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (left.getPixelColour(x, y) != right.getPixelColour(x, y))
                {
                    if (++differentPixels > 1) return false;
                }
        // A single antialiased edge pixel can quantise differently when JUCE
        // composites a cached plate. A page-wide stale layer cannot.
        return true;
    };

    // The last step returns to a page whose layer is the one being held, which
    // is the swap this is here to check.
    require(same(render({"MATRIX"}), render({"OSC", "MIX", "MATRIX", "OSC", "MATRIX"})),
            "a tab returned to draws what it drew the first time");
    // And a page reached the long way round, whose layer has been thrown away
    // and drawn again rather than swapped back.
    require(same(render({"FX"}), render({"OSC", "TABLE", "MATRIX", "MIX", "FX"})),
            "a tab reached after every other one draws what it draws on its own");
}

// Compare consecutive resize frames with opening the editor at each size.
// The parent's cached headings and chassis must retain their resolution even
// when changes arrive faster than the old resize-quality timeout.
void resizeSharpnessSuite()
{
    const auto paint = [] (juce::AudioProcessorEditor& editor, float scale)
    {
        juce::Image image(juce::Image::ARGB,
                          static_cast<int>(std::ceil(editor.getWidth() * scale)),
                          static_cast<int>(std::ceil(editor.getHeight() * scale)), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        // Live oscillator traces have subpixel raster differences independent
        // of the static layer being tested. Keep all headings and frame edges.
        for (const auto& module : ui::modules())
            if (module.display == ui::Display::oscillator)
                g.excludeClipRegion(ui::displayBounds(ui::moduleBounds(editor.getLocalBounds(), module), module));
        editor.paint(g);
        return image;
    };
    const auto same = [] (const juce::Image& a, const juce::Image& b)
    {
        if (a.getBounds() != b.getBounds()) return false;
        const juce::Image::BitmapData left(a, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData right(b, juce::Image::BitmapData::readOnly);
        int differentPixels = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto l = left.getPixelColour(x, y), r = right.getPixelColour(x, y);
                // Native raster/compositing can differ by one quantization
                // step at an edge. Larger changes, including blur, fail.
                if (std::abs(int(l.getRed()) - int(r.getRed())) > 1
                    || std::abs(int(l.getGreen()) - int(r.getGreen())) > 1
                    || std::abs(int(l.getBlue()) - int(r.getBlue())) > 1
                    || std::abs(int(l.getAlpha()) - int(r.getAlpha())) > 1)
                {
                    if (++differentPixels > 1) return false;
                }
            }
        return true;
    };
    const juce::Point<int> sizes[] {{1181, 821}, {1243, 853}, {1220, 847}, {1181, 821}};
    auto processor = std::make_unique<Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    for (const auto scale : {1.0f, 1.25f, 2.0f})
    {
        std::vector<juce::Image> expected;
        for (const auto size : sizes)
        {
            auto freshProcessor = std::make_unique<Processor>();
            std::unique_ptr<juce::AudioProcessorEditor> fresh(freshProcessor->createEditor());
            fresh->setSize(size.x, size.y);
            expected.push_back(paint(*fresh, scale));
        }
        editor->setSize(ui::minPanelWidth, ui::minPanelHeight);
        paint(*editor, scale);
        std::vector<juce::Image> frames;
        for (const auto size : sizes)
        {
            editor->setSize(size.x, size.y);
            frames.push_back(paint(*editor, scale));
        }
        for (size_t i = 0; i < frames.size(); ++i)
            require(same(frames[i], expected[i]),
                    "a resize frame keeps the same sharp chassis and headings as a fresh editor");
    }
}

// ---------------------------------------------------------- filter display ---

// The filter display claims to say which frequencies are being taken out, so
// what it draws has to be the response the engine actually has rather than a
// picture of a filter in general. These check the curve through the geometry it
// is drawn from: gain read back at a frequency, and frequency read back off the
// axis it is plotted against.
//
// The arithmetic is shared with the engine rather than copied, so what is being
// held here is the claim that the shared function says the right things. What
// binds it to the audio is ForgeTestsFilter.cpp, which measures the render
// against this very curve.
void filterDisplaySuite()
{
    namespace ui = rhino::forge::ui;
    using rhino::forge::FilterType;
    using rhino::forge::FilterShape;
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
    // does, or the curve is of some other filter. One function now, so this is
    // holding the value rather than the agreement.
    requireClose(rhino::forge::filterDamping(0.0f), 1.0f, 0.0001f, "no resonance is full damping");
    requireClose(rhino::forge::filterDamping(1.0f), 1.0f / 16.0f, 0.0001f,
                 "full resonance is the engine's least damping");

    const auto cutoff = 1000.0f;
    const auto quiet = 0.0f;
    const auto shapeAt = [] (FilterType type, float corner, float resonance)
    {
        return FilterShape {type, corner, resonance, 0.0f, 48000.0};
    };
    const auto shape = [cutoff] (FilterType type, float resonance, float second)
    {
        return FilterShape {type, cutoff, resonance, second, 48000.0};
    };
    const auto db = [&shape] (FilterType type, float resonance, float second, float hz)
    {
        return ui::filterMagnitudeDb(shape(type, resonance, second), hz);
    };

    // Each tap passes its own end of the band and stops the other. Two decades
    // either side of the corner, which is well clear of the knee.
    require(db(FilterType::lowPass, quiet, 0.0f, 10.0f) > -3.0f,
            "a low pass leaves the bottom of the band alone");
    require(db(FilterType::lowPass, quiet, 0.0f, 100000.0f) < -40.0f,
            "a low pass takes the top of the band out");
    require(db(FilterType::highPass, quiet, 0.0f, 100000.0f) > -3.0f,
            "a high pass leaves the top of the band alone");
    require(db(FilterType::highPass, quiet, 0.0f, 10.0f) < -40.0f,
            "a high pass takes the bottom of the band out");
    require(db(FilterType::bandPass, quiet, 0.0f, 10.0f) < -20.0f
                && db(FilterType::bandPass, quiet, 0.0f, 100000.0f) < -20.0f,
            "a band pass takes both ends out");

    // A second-order response, so it falls twelve decibels an octave away from
    // the corner. Measured two octaves out, where the knee is long behind it.
    const auto atFour = db(FilterType::lowPass, quiet, 0.0f, cutoff * 4.0f);
    const auto atEight = db(FilterType::lowPass, quiet, 0.0f, cutoff * 8.0f);
    requireClose(atFour - atEight, 12.0f, 0.6f, "the skirt falls twelve decibels an octave");

    // Resonance is a peak at the corner, and it only ever adds.
    for (const auto type : {FilterType::lowPass, FilterType::highPass, FilterType::bandPass})
    {
        const auto flat = db(type, 0.0f, 0.0f, cutoff);
        const auto peaked = db(type, 0.9f, 0.0f, cutoff);
        require(peaked > flat + 6.0f, "resonance lifts the corner");
        require(peaked <= ui::filterTopDb, "a resonant peak stays inside the window it is drawn in");
    }
    // And the peak of a band pass is the corner itself, not somewhere else.
    const auto atCorner = db(FilterType::bandPass, 0.5f, 0.0f, cutoff);
    require(atCorner > db(FilterType::bandPass, 0.5f, 0.0f, cutoff * 1.5f)
                && atCorner > db(FilterType::bandPass, 0.5f, 0.0f, cutoff / 1.5f),
            "a band pass peaks at the frequency the knob is holding");

    // NOTCH and PEAK are the two new basic taps, and they are opposites: one
    // is a hole at the corner with the band either side of it untouched, the
    // other is a lift at the corner with the band either side of it untouched.
    require(db(FilterType::notch, 0.3f, 0.0f, cutoff) < -20.0f,
            "a notch cuts at the corner");
    require(db(FilterType::notch, 0.3f, 0.0f, 20.0f) > -1.0f
                && db(FilterType::notch, 0.3f, 0.0f, 20000.0f) > -1.0f,
            "a notch leaves both ends of the band alone");
    require(db(FilterType::peak, 0.5f, 0.0f, cutoff) > 6.0f,
            "a peak lifts the corner");
    require(db(FilterType::peak, 0.5f, 0.0f, 20.0f) > -1.0f
                && db(FilterType::peak, 0.5f, 0.0f, 20000.0f) > -1.0f,
            "a peak takes nothing out either side of what it lifts");

    // A dual filter's second corner is a frequency of its own, on the same
    // travel the cutoff knob has — not an interval from the cutoff. That is
    // what the Serum manual says FREQ is, and it is what lets the notch in
    // PK+NT sit several kilohertz above a cutoff parked at the bottom of its
    // range, which is the setting the manual's own screenshots use.
    {
        for (const auto hz : {60.0f, 440.0f, 2000.0f, 12000.0f})
        {
            const auto at = rhino::forge::filterSecondAt(hz);
            const auto placed = shape(FilterType::lowNotch, 0.2f, at);
            requireClose(rhino::forge::filterSecondHz(placed), hz, hz * 0.001f,
                         "FREQ lands the second filter on the frequency it names");
            // And it does not move when the cutoff does, which is the whole
            // difference from an interval.
            const rhino::forge::FilterShape elsewhere {FilterType::lowNotch, 9000.0f, 0.2f,
                                                       at, 48000.0};
            requireClose(rhino::forge::filterSecondHz(elsewhere), hz, hz * 0.001f,
                         "and stays there when the cutoff moves");
        }

        const auto notchAt2k = shape(FilterType::lowNotch, 0.2f,
                                     rhino::forge::filterSecondAt(2000.0f));
        require(ui::filterMagnitudeDb(notchAt2k, 2000.0f) < -20.0f,
                "the second filter of a dual cuts where FREQ puts it");
        // And nowhere near where it is not: below the cutoff is inside the low
        // pass and outside the notch.
        require(ui::filterMagnitudeDb(notchAt2k, cutoff * 0.5f) > -3.0f,
                "a dual filter leaves the band neither of its halves touches");
        // LP+HP passes what is between its two corners and stops what is
        // outside them, which is the whole reason the family exists.
        const auto band = shape(FilterType::lowHigh, 0.0f,
                                rhino::forge::filterSecondAt(120.0f));
        const auto between = ui::filterMagnitudeDb(band, 350.0f);
        require(between > ui::filterMagnitudeDb(band, 16000.0f) + 20.0f
                    && between > ui::filterMagnitudeDb(band, 20.0f) + 20.0f,
                "LP+HP passes the band between its two corners");
    }

    // The reference case, read off the Serum 2 manual's own PN 12 screenshot:
    // CUTOFF at the bottom of its range, RES at half, FREQ a few kilohertz up.
    // What that draws is a flat band with one deep notch in it and no visible
    // peak at all — the peak is at the bottom of the knob, below the left edge
    // of the display, and only its shoulder reaches the band.
    //
    // This is the check that would have caught FREQ being an interval: with one
    // the notch could not have been up there at all.
    {
        const rhino::forge::FilterShape reference {FilterType::peakNotch,
                                                   rhino::forge::filterCutoffLowHz, 0.5f,
                                                   rhino::forge::filterSecondAt(3000.0f),
                                                   48000.0};
        requireClose(rhino::forge::filterSecondHz(reference), 3000.0f, 3.0f,
                     "the notch sits where FREQ put it, four octaves above the cutoff");
        require(ui::filterMagnitudeDb(reference, 3000.0f) < -18.0f,
                "and it is a deep notch there");
        // Flat either side of it, and never far above unity anywhere in the
        // band: the peak is off the bottom of the knob.
        for (const auto hz : {100.0f, 400.0f, 1000.0f, 12000.0f})
        {
            const auto reading = ui::filterMagnitudeDb(reference, hz);
            if (!(reading > -3.0f && reading < 3.0f))
            {
                require(false, "the rest of the band is flat, as the reference draws it");
                std::cerr << "       " << hz << " Hz read " << reading << " dB\n";
            }
        }
    }

    // A morph type's middle is the middle response on its own, not a blend of
    // the outer two: at 0.5, LP-BP-HP is a band pass and reads like one.
    {
        const auto middle = shape(FilterType::morphLowBandHigh, 0.4f, 0.5f);
        const auto plainBand = shape(FilterType::bandPass, 0.4f, 0.0f);
        for (const auto hz : {100.0f, 500.0f, 1000.0f, 4000.0f, 12000.0f})
            requireClose(ui::filterMagnitudeDb(middle, hz), ui::filterMagnitudeDb(plainBand, hz),
                         0.01f, "the middle of a morph is the middle response on its own");
        // The ends are the outer two, and they are not each other.
        const auto low = shape(FilterType::morphLowBandHigh, 0.4f, 0.0f);
        const auto high = shape(FilterType::morphLowBandHigh, 0.4f, 1.0f);
        require(ui::filterMagnitudeDb(low, 100.0f) > ui::filterMagnitudeDb(high, 100.0f) + 20.0f,
                "a morph swept to one end is the low pass and to the other is the high pass");
    }

    // A ladder is steeper than one state-variable filter because it has more
    // poles: 24 dB an octave on the four-pole ones and 18 on the diode ones.
    //
    // Measured off the gain rather than off the drawn decibels: a four-pole
    // skirt is already past the bottom of the display window two octaves out,
    // so the drawn curve is clamped there and its slope reads as nothing. The
    // window is right for a display and wrong for measuring a slope.
    {
        const auto slope = [] (FilterType type)
        {
            const FilterShape held {type, cutoff, 0.0f, 0.0f, 48000.0};
            const auto near = rhino::forge::filterMagnitude(held, cutoff * 4.0f);
            const auto far = rhino::forge::filterMagnitude(held, cutoff * 8.0f);
            return 20.0f * std::log10(near / far);
        };
        requireClose(slope(FilterType::ladder), 24.0f, 1.0f,
                     "a four-pole ladder falls twenty-four decibels an octave");
        requireClose(slope(FilterType::dirtyLadder), 24.0f, 1.0f, "and so does the dirty one");
        requireClose(slope(FilterType::acid), 18.0f, 1.0f,
                     "a three-pole diode ladder falls eighteen");
    }

    // A comb is a row of peaks on the harmonics of its own tuning, so the
    // troughs between them sit halfway up in frequency.
    {
        const auto comb = shape(FilterType::comb, 0.8f, 1.0f);
        const auto tuning = rhino::forge::filterCombHz(comb);
        require(ui::filterMagnitudeDb(comb, tuning) > ui::filterMagnitudeDb(comb, tuning * 1.5f) + 6.0f,
                "a comb peaks on its own tuning and not between");
        require(ui::filterMagnitudeDb(comb, tuning * 2.0f)
                    > ui::filterMagnitudeDb(comb, tuning * 2.5f) + 6.0f,
                "and on the harmonics of it");
    }

    // The two types with no magnitude response at all say so, and draw the
    // only honest line there is: unity. A flat curve means something quite
    // different from a broken one, which is why the display labels these.
    for (const auto type : {FilterType::ringMod, FilterType::diffusor})
    {
        require(!rhino::forge::filterHasResponse(type),
                "a type whose curve is not its own transfer function is marked as such");
        for (const auto hz : {50.0f, 1000.0f, 15000.0f})
            requireClose(ui::filterMagnitudeDb(shape(type, 0.5f, 0.5f), hz), 0.0f, 0.01f,
                         "a phase-only type draws unity rather than a shape it does not have");
    }
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
    {
        const auto chosen = rhino::forge::filterTypeOf(static_cast<float>(type));
        if (chosen == FilterType::ringMod || chosen == FilterType::diffusor) continue;
        require(rhino::forge::filterHasResponse(chosen),
                "every other type draws its own response");
    }

    // Every type, at the extremes of every knob that shapes it, has to give a
    // finite gain inside the window. A NaN here is a curve that disappears.
    for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
        for (const auto corner : {30.0f, 1000.0f, 18000.0f})
            for (const auto resonance : {0.0f, 0.5f, 1.0f})
                for (const auto second : {0.0f, 0.5f, 1.0f})
                {
                    const FilterShape held {rhino::forge::filterTypeOf(static_cast<float>(type)),
                                            corner, resonance, second, 48000.0};
                    for (const auto hz : {20.0f, 200.0f, 2000.0f, 20000.0f})
                    {
                        const auto reading = ui::filterMagnitudeDb(held, hz);
                        if (!std::isfinite(reading) || reading < ui::filterBottomDb
                            || reading > ui::filterTopDb)
                        {
                            require(false, "every type reads a finite gain inside the window");
                            std::cerr << "       " << rhino::forge::filterTypeName(type) << " at "
                                      << hz << " Hz, corner " << corner << " res " << resonance
                                      << " freq " << second << " read " << reading << '\n';
                        }
                    }
                }

    // Every type's curve stays inside the well it is drawn in, at the size the
    // panel opens at. The three original taps are then swept across every size
    // the panel allows, which is the expensive half and the one that catches a
    // window of the wrong shape rather than a curve of the wrong height.
    for (const auto& module : ui::modules())
    {
        if (module.display != ui::Display::filter) continue;

        const auto opened = ui::displayBounds(
            ui::moduleBounds({0, 0, ui::defaultPanelWidth, ui::defaultPanelHeight}, module),
            module).toFloat().reduced(0.0f, 6.0f);
        for (int type = 0; type < rhino::forge::filterTypeCount; ++type)
            for (const auto corner : {30.0f, 1000.0f, 18000.0f})
                for (const auto resonance : {0.0f, 1.0f})
                    for (const auto second : {0.0f, 0.5f, 1.0f})
                    {
                        const FilterShape held {rhino::forge::filterTypeOf(static_cast<float>(type)),
                                                corner, resonance, second, 48000.0};
                        const auto bounds = ui::filterResponsePath(opened, held).getBounds();
                        if (!opened.expanded(0.5f).contains(bounds))
                        {
                            require(false, "every type's response stays inside the well");
                            std::cerr << "       " << rhino::forge::filterTypeName(type)
                                      << " corner " << corner << " res " << resonance
                                      << " freq " << second << '\n';
                        }
                    }

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
                            const auto bounds =
                                ui::filterResponsePath(plot, shapeAt(type, corner, resonance))
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

    // Except on a formant filter, which has no corner: the knob moves the
    // mouth, so the marker says which vowel rather than inventing a frequency.
    requireText(ui::filterCornerText(shape(FilterType::formant, 0.3f, 0.5f)),
                juce::String("VOWEL ") + rhino::forge::filterVowelName(cutoff),
                "a formant filter's marker names the vowel");
    requireText(ui::filterCornerText(shape(FilterType::lowPass, 0.3f, 0.0f)), "1.00 kHz",
                "every other type's marker names the frequency");
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

void lfoFooterSuite()
{
    auto processor = std::make_unique<rhino::forge::Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    const ui::Module* lfo = nullptr;
    for (const auto& module : ui::modules())
        if (module.display == ui::Display::lfo) lfo = &module;
    require(lfo != nullptr, "the LFO module has a display");
    if (lfo == nullptr) return;

    const auto displayAt = [&]
    {
        return ui::displayBounds(ui::moduleBounds(editor->getLocalBounds(), *lfo), *lfo);
    };
    for (const auto width : {ui::minPanelWidth, ui::defaultPanelWidth})
    {
        editor->setSize(width, ui::minPanelHeight);
        const auto display = displayAt();
        const auto name = ui::lfoNameBounds(display);
        const auto previous = ui::lfoPreviousBounds(display);
        const auto next = ui::lfoNextBounds(display);
        const auto columns = ui::lfoColumnBounds(display);
        const auto rows = ui::lfoRowBounds(display);
        require(display.contains(name) && display.contains(previous) && display.contains(next)
                && display.contains(columns) && display.contains(rows),
                "LFO footer controls remain inside the display at both widths");
        require(!name.intersects(previous) && !previous.intersects(next)
                && !next.intersects(columns) && !columns.intersects(rows),
                "LFO footer targets do not overlap");
    }

    const auto display = displayAt();
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
    const auto when = juce::Time::getCurrentTime();
    const auto eventAt = [&] (juce::Point<int> at, bool dragged = false, int clicks = 1)
    {
        return juce::MouseEvent(mouse, at.toFloat(), juce::ModifierKeys(),
                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                editor.get(), editor.get(), when, at.toFloat(), when, clicks, dragged);
    };
    editor->mouseDown(eventAt(ui::lfoNextBounds(display).getCentre()));
    requireClose(value(*processor, "lfo1Shape"), 1.0f, 0.001f,
                 "the next arrow switches to the next basic shape");
    editor->mouseDown(eventAt(ui::lfoPreviousBounds(display).getCentre()));
    requireClose(value(*processor, "lfo1Shape"), 0.0f, 0.001f,
                 "the previous arrow switches back");

    const auto columns = ui::lfoColumnBounds(display);
    const auto rows = ui::lfoRowBounds(display);
    editor->mouseDown(eventAt(ui::lfoGridStepBounds(columns, true).getCentre()));
    editor->mouseDown(eventAt(ui::lfoGridStepBounds(rows, false).getCentre()));
    require(processor->lfoTable(0).columns == 9 && processor->lfoTable(0).rows == 7,
            "the grid arrows adjust columns and rows independently");
    editor->mouseDown(eventAt(ui::lfoGridStepBounds(columns, true).getCentre(), false, 2));
    require(processor->lfoTable(0).columns == 10,
            "quick repeated clicks on a grid arrow keep stepping");

    const auto grip = juce::Point<int>(columns.getX() + 25, columns.getCentreY());
    editor->mouseDown(eventAt(grip));
    editor->mouseDrag(eventAt(grip.translated(0, -16), true));
    editor->mouseUp(eventAt(grip.translated(0, -16), true));
    require(processor->lfoTable(0).columns == 12,
            "dragging the grid number adjusts its count");
    editor->mouseDown(eventAt(grip, false, 2));
    require(processor->lfoTable(0).columns == 8,
            "double-clicking a grid number restores eight divisions");

    juce::MouseWheelDetails wheel;
    wheel.deltaY = 0.2f;
    wheel.isReversed = false;
    editor->mouseWheelMove(eventAt(rows.getCentre()), wheel);
    require(processor->lfoTable(0).rows == 8 && !processor->lfoTable(0).custom,
            "the wheel adjusts the grid without entering Custom mode");
}
}

void displayTests()
{
    chromeCacheSuite();
    resizeSharpnessSuite();
    filterDisplaySuite();
    envelopeDisplaySuite();
    lfoFooterSuite();
}
}
