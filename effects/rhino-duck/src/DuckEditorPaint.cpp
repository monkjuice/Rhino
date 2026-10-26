#include "DuckEditor.h"
#include "../ui/DuckTheme.h"

// What the panel paints itself, behind and between its controls: the plates,
// the name, the readings, the dividers and the captions.
namespace rhino::duck
{
using ui::colour::accent;
using ui::colour::text;
using ui::colour::textDim;
using ui::colour::textMuted;

namespace
{
constexpr std::array<const char*, ui::paramCount> paramNames { "ATTACK", "HOLD", "RELEASE", "DEPTH" };

juce::String signedPercent(float value)
{
    const auto percent = juce::roundToInt(value * 100.0f);
    return (percent > 0 ? "+" : "") + juce::String(percent) + "%";
}

// A reading in a recessed box, as the strip and the band show their values.
void drawReading(juce::Graphics& g, juce::Rectangle<int> box, const juce::AttributedString& reading)
{
    ui::drawRecess(g, box.toFloat(), 4.0f);
    reading.draw(g, box.toFloat());
}

juce::AttributedString plainReading(const juce::String& value, juce::Colour ink)
{
    juce::AttributedString reading;
    reading.append(value, ui::font(ui::Face::reading, 13.0f), ink);
    reading.setJustification(juce::Justification::centred);
    return reading;
}

void drawDivider(juce::Graphics& g, float x, float top, float bottom)
{
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRect(juce::Rectangle<float>(1.0f, bottom - top).withPosition(x, top));
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.fillRect(juce::Rectangle<float>(1.0f, bottom - top).withPosition(x + 1.0f, top));
}
}

void Editor::paint(juce::Graphics& g)
{
    g.fillAll(ui::colour::panel);
    g.setTiledImageFill(grain, 0, 0, 0.7f);
    g.fillAll();
    g.setColour(ui::colour::panelEdge);
    g.drawRect(getLocalBounds(), 1);

    for (const auto& plate : { placed.rail, placed.mainPlate, placed.strip, placed.presetBar })
        ui::drawPlate(g, plate.toFloat(), grain);
    ui::drawScrews(g, placed.rail.toFloat(), 8.0f, 3.4f);
    ui::drawScrews(g, placed.mainPlate.toFloat(), 6.0f, 2.8f);
    ui::drawScrews(g, placed.strip.toFloat(), 6.0f, 2.8f);

    paintRail(g);
    paintCurveBank(g);
    paintStrip(g);
    paintPresetBar(g);
}

void Editor::paintRail(juce::Graphics& g)
{
    // The name: RHINO over DUCK beside the mark, and what it is under both.
    auto brand = placed.brand.toFloat();
    ui::drawDuckHead(g, art, brand.removeFromRight(74.0f).removeFromTop(74.0f));
    auto lettering = brand;
    g.setFont(ui::font(ui::Face::header, 31.0f));
    g.setColour(ui::colour::marker.darker(0.25f));
    g.drawText("RHINO", lettering.removeFromTop(30.0f), juce::Justification::bottomLeft, false);
    g.setFont(ui::font(ui::Face::header, 37.0f));
    g.setColour(accent);
    g.drawText("DUCK", lettering.removeFromTop(34.0f), juce::Justification::bottomLeft, false);
    g.setFont(ui::font(ui::Face::label, 12.5f).withExtraKerningFactor(0.24f));
    g.setColour(textDim);
    g.drawText("SIDECHAIN SHAPER", placed.brand.toFloat().removeFromBottom(18.0f), juce::Justification::centredLeft, false);

    g.setFont(ui::font(ui::Face::label, 17.0f));
    g.setColour(text);
    g.drawText("MIX", placed.mixTitle, juce::Justification::centred, false);
    g.setFont(ui::font(ui::Face::label, 13.0f));
    g.setColour(textDim);
    g.drawText("DRY", placed.dryLabel, juce::Justification::centredRight, false);
    g.drawText("WET", placed.wetLabel, juce::Justification::centredLeft, false);
    g.setFont(ui::font(ui::Face::reading, 18.0f));
    g.setColour(accent);
    g.drawText(shownMix, placed.mixReading, juce::Justification::centred, false);
}

void Editor::paintCurveBank(juce::Graphics& g)
{
    const auto curve = shownCurve < 0 ? 0 : shownCurve;
    const auto adjust = processor.curveAdjust(curve);
    juce::AttributedString caption;
    caption.append(juce::String(curve + 1) + "  ", ui::font(ui::Face::reading, 14.0f), accent);
    caption.append(juce::String(curveName(curve)).toUpperCase(), ui::font(ui::Face::header, 14.0f), text);
    caption.append("   " + juce::String(familyName(curveFamily(curve))).toUpperCase(), ui::font(ui::Face::label, 13.0f), textDim);
    if (adjust.shift != 0.0f)
        caption.append("   SHIFT " + signedPercent(adjust.shift), ui::font(ui::Face::label, 13.0f), textDim);
    caption.setJustification(juce::Justification::centred);
    caption.draw(g, placed.curveCaption.toFloat());

    // A divider where one family ends and the next begins.
    g.setColour(ui::colour::divider);
    for (const auto [before, after] : { std::pair { 3, 8 }, std::pair { 8, 10 }, std::pair { 10, 12 }, std::pair { 12, 14 } })
    {
        const auto& left = placed.curveCells[static_cast<size_t>(before)];
        const auto& right = placed.curveCells[static_cast<size_t>(after)];
        const auto x = (left.getRight() + right.getX()) / 2;
        g.fillRect(x, left.getY(), 1, placed.curveCells[static_cast<size_t>(before == 3 ? 7 : before + 1)].getBottom() - left.getY());
    }
}

void Editor::paintStrip(juce::Graphics& g)
{
    const auto top = static_cast<float>(placed.strip.getY() + 12);
    const auto bottom = static_cast<float>(placed.strip.getBottom() - 12);
    for (size_t i = 0; i < ui::paramCount; ++i)
    {
        g.setFont(ui::font(ui::Face::label, 15.0f));
        g.setColour(paramKnobs[i].isEnabled() ? text : textMuted);
        g.drawText(paramNames[i], placed.paramTitles[i], juce::Justification::centred, false);
        g.setFont(ui::font(ui::Face::label, 12.0f));
        g.setColour(textMuted);
        g.drawText(shownLows[i], placed.paramLows[i], juce::Justification::centredLeft, false);
        g.drawText(shownHighs[i], placed.paramHighs[i], juce::Justification::centredRight, false);
        drawReading(g, placed.paramValues[i], plainReading(shownValues[i], paramKnobs[i].isEnabled() ? text : textMuted));
        drawDivider(g, static_cast<float>(placed.paramTitles[i].getRight() + 5), top, bottom);
    }

    // The band split: dimmed, with its values still shown, while the whole
    // signal is ducked.
    g.setFont(ui::font(ui::Face::label, 15.0f));
    g.setColour(shownBand ? text : textDim);
    g.drawText("BAND", placed.bandTitle, juce::Justification::centredLeft, false);
    g.setFont(ui::font(ui::Face::label, 12.0f));
    g.setColour(textMuted);
    g.drawText("20 Hz", placed.cutoffLow, juce::Justification::centredLeft, false);
    g.drawText("5.12 kHz", placed.cutoffHigh, juce::Justification::centredRight, false);
    juce::AttributedString cutoff;
    cutoff.append("BELOW  ", ui::font(ui::Face::label, 13.0f), textDim);
    cutoff.append(shownCutoff, ui::font(ui::Face::reading, 13.0f), shownBand ? text : textMuted);
    cutoff.setJustification(juce::Justification::centred);
    drawReading(g, placed.cutoffReading, cutoff);
}

void Editor::paintPresetBar(juce::Graphics& g)
{
    g.setFont(ui::font(ui::Face::label, 16.0f));
    g.setColour(textDim);
    g.drawText("PRESET", placed.presetTitle, juce::Justification::centredLeft, false);

    auto footer = placed.footer.toFloat();
    ui::drawDuckHead(g, art, footer.removeFromRight(30.0f));
    footer.removeFromRight(10.0f);
    g.setFont(ui::font(ui::Face::label, 13.0f).withExtraKerningFactor(0.3f));
    g.setColour(textMuted);
    g.drawText("RHINO", footer, juce::Justification::centredRight, false);
}
}
