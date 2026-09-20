// What the modules draw of themselves: the filter response, the envelope, and
// the cached metal behind them. Each one is computed from the same arithmetic
// the engine runs, so a display cannot claim one thing while the voice does
// another - these checks are what holds that.
#include "ForgeTestSupport.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeVisuals.h"

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
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (left.getPixelColour(x, y) != right.getPixelColour(x, y)) return false;
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
}

void displayTests()
{
    chromeCacheSuite();
    filterDisplaySuite();
    envelopeDisplaySuite();
}
}
