#include "DeviceEditorPanelDrumsInternal.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// The Drum Rack face's parts, shared by its translation units: where
// everything stands, and the few drawn controls the face is made of.
namespace rhino::drumface
{
std::vector<SampleCell> sampleCellsFor(DrumRackEngine::PlayMode mode)
{
    using Mode = DrumRackEngine::PlayMode;
    const SampleCell start { SampleCell::knob, SampleKnob::start }, end { SampleCell::knob, SampleKnob::end };
    if (mode == Mode::classic)
        return { start, end, { SampleCell::knob, SampleKnob::attack }, { SampleCell::knob, SampleKnob::sustain },
                 { SampleCell::knob, SampleKnob::release } };
    if (mode == Mode::slice)
        return { start, end, { SampleCell::sliceBy }, { SampleCell::knob, SampleKnob::sensitivity },
                 { SampleCell::spread } };
    return { start, end, { SampleCell::knob, SampleKnob::fadeIn }, { SampleCell::knob, SampleKnob::fadeOut } };
}

Layout layoutFor(juce::Rectangle<int> panel)
{
    Layout layout;
    const auto area = panel.withTrimmedTop(DeviceEditorPanel::headerHeight + 1).reduced(4);
    // The map from the top, and Auto Select's switch in what is left below it.
    layout.map = { area.getX(), area.getY(), mapWidth, mapHeight };
    layout.autoSelect = { area.getX(), layout.map.getBottom() + 4, mapWidth,
                          std::max(10, area.getBottom() - layout.map.getBottom() - 4) };
    const auto gridLeft = area.getX() + mapWidth + gapAfterMap;
    const auto gridTop = area.getY() + std::max(0, (area.getHeight() - gridHeight) / 2);
    for (int index = 0; index < padsShown; ++index)
    {
        // The lowest note bottom left, rising along a row and then up a row.
        const auto i = static_cast<size_t>(index);
        const auto column = index % padColumns;
        const auto row = padRows - 1 - index / padColumns;
        const juce::Rectangle<int> cell(gridLeft + column * (padWidth + padGap), gridTop + row * (padHeight + padGap),
                                        padWidth, padHeight);
        layout.pad[i] = cell;
        const auto strip = cell.getBottom() - padButtonHeight - 2;
        layout.mute[i] = {cell.getX() + 3, strip, padButtonWidth, padButtonHeight};
        layout.play[i] = {cell.getX() + 5 + padButtonWidth, strip, padButtonWidth, padButtonHeight};
        layout.solo[i] = {cell.getX() + 7 + 2 * padButtonWidth, strip, padButtonWidth, padButtonHeight};
    }

    auto editor = area.withTrimmedLeft(gridLeft - area.getX() + gridWidth + gapAfterGrid);
    layout.editor = editor;
    auto header = editor.removeFromTop(headerRow);
    layout.chokeChooser = header.removeFromRight(70);
    header.removeFromRight(4);
    layout.soundChooser = header.removeFromRight(106);
    header.removeFromRight(6);
    layout.nameArea = header;
    editor.removeFromTop(2);
    auto top = editor.removeFromTop(pictureHeight);
    const auto modes = top.removeFromLeft(modeWidth);
    top.removeFromLeft(4);
    layout.picture = top;
    layout.loop = { top.getRight() - 40, top.getY() + 4, 36, 12 };
    const auto buttonHeight = (modes.getHeight() - 4) / 3;
    for (int button = 0; button < 3; ++button)
        layout.mode[static_cast<size_t>(button)] = { modes.getX(), modes.getY() + button * (buttonHeight + 2),
                                                     modes.getWidth(), buttonHeight };
    editor.removeFromTop(2);
    for (int index = 0; index < controlCells; ++index)
        layout.cell[static_cast<size_t>(index)] = editor.removeFromLeft(cellWidth);
    layout.divider = editor.removeFromLeft(dividerWidth);
    for (int index = 0; index < sampleCells; ++index)
        layout.cell[static_cast<size_t>(controlCells + index)] = editor.removeFromLeft(cellWidth);
    return layout;
}

juce::Rectangle<int> knobIn(juce::Rectangle<int> cell)
{
    const auto size = std::min({34, cell.getWidth() - 10, cell.getHeight() - 24});
    return juce::Rectangle<int>(cell.getX(), cell.getY() + 12, cell.getWidth(), cell.getHeight() - 24)
        .withSizeKeepingCentre(size, size);
}

juce::Rectangle<int> captionIn(juce::Rectangle<int> cell)
{
    return cell.withHeight(12);
}

juce::Rectangle<int> valueIn(juce::Rectangle<int> cell)
{
    return {cell.getX(), cell.getBottom() - 12, cell.getWidth(), 12};
}

juce::Rectangle<int> automationIn(juce::Rectangle<int> cell)
{
    return {cell.getRight() - 15, cell.getY() + 12, 14, 12};
}

juce::Rectangle<float> mapCell(const Layout& layout, int note)
{
    const auto row = note / padColumns, column = note % padColumns;
    return juce::Rectangle<int>(layout.map.getX() + column * mapCellWidth,
                                layout.map.getBottom() - (row + 1) * mapCellHeight,
                                mapCellWidth - 1, mapCellHeight - 1).toFloat();
}

int mapNoteAt(const Layout& layout, juce::Point<int> position)
{
    if (!layout.map.expanded(2, 0).contains(position))
        return -1;
    const auto row = juce::jlimit(0, mapRows - 1, (layout.map.getBottom() - 1 - position.y) / mapCellHeight);
    const auto column = juce::jlimit(0, padColumns - 1, (position.x - layout.map.getX()) / mapCellWidth);
    return row * padColumns + column;
}

DrumRackEngine::PlayMode modeOfButton(int button)
{
    return button == 0 ? DrumRackEngine::PlayMode::classic
         : button == 1 ? DrumRackEngine::PlayMode::oneShot
                       : DrumRackEngine::PlayMode::slice;
}

DrumRackDevice* drumsIn(Session& session, int track, int slot)
{
    return dynamic_cast<DrumRackDevice*>(session.devicePlugin(track, slot));
}

// drawSnappedText, with each line kept once it is shaped. Shaping a line is
// most of what this face costs to paint -- sixteen names, thirty-two letters,
// a dozen captions and readings -- and nearly all of them are the same frame
// after frame, so each is laid out once, where it stands, and drawn from
// then on. The baseline and justification are drawSnappedText's own, so a
// line comes out exactly as it would have. Painting is the message thread's
// alone, so one cache serves every face.
void drawLine(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
              juce::Justification justification, bool elide)
{
    static std::unordered_map<juce::String, juce::GlyphArrangement> lines;
    const auto font = g.getCurrentFont();
    const auto key = text + "\n" + area.toString() + "\n" + juce::String(justification.getFlags())
                   + (elide ? "\ne" : "\n") + font.toString();
    auto found = lines.find(key);
    if (found == lines.end())
    {
        // Readings change as knobs turn, so the cache is let go of rather
        // than allowed to grow without end.
        if (lines.size() > 2048)
            lines.clear();
        juce::GlyphArrangement glyphs;
        const auto line = elide ? elidedToWidth(font, text, area.getWidth()) : text;
        if (line.isNotEmpty())
        {
            const auto baseline = area.getY()
                + juce::roundToInt((static_cast<float>(area.getHeight()) + font.getAscent() - font.getDescent()) * 0.5f);
            auto x = area.getX();
            if (justification.testFlags(juce::Justification::horizontallyCentred))
                x = area.getCentreX() - juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, line) * 0.5f);
            else if (justification.testFlags(juce::Justification::right))
                x = area.getRight() - juce::GlyphArrangement::getStringWidthInt(font, line);
            glyphs.addLineOfText(font, line, static_cast<float>(x), static_cast<float>(baseline));
        }
        found = lines.emplace(key, std::move(glyphs)).first;
    }
    found->second.draw(g);
}

void drawChooser(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool usable)
{
    g.setColour(palette::control);
    g.fillRoundedRectangle(area.toFloat(), 2.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 2.0f, 1.0f);
    g.setColour(usable ? palette::text : palette::disabled);
    g.setFont(uiFont(8.5f));
    drawLine(g, text, area.withTrimmedLeft(6).withTrimmedRight(14), juce::Justification::centredLeft, true);
    // The arrow that says it opens.
    const auto tip = area.toFloat().removeFromRight(13.0f).withSizeKeepingCentre(6.0f, 4.0f);
    juce::Path arrow;
    arrow.addTriangle(tip.getX(), tip.getY(), tip.getRight(), tip.getY(), tip.getCentreX(), tip.getBottom());
    g.setColour(usable ? palette::textDim : palette::disabled);
    g.fillPath(arrow);
}

void drawToggle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, juce::Colour onInk, bool lit,
                bool usable, float fontSize)
{
    const auto box = area.toFloat();
    g.setColour(lit ? onInk : padButtonInk);
    g.fillRoundedRectangle(box, 1.5f);
    g.setColour(lit ? palette::appBackground : usable ? palette::textDim : palette::disabled);
    if (text.isEmpty())
    {
        // Play is drawn rather than set as a glyph: Inter has no triangle,
        // and the fallback face's sits off the button's centre.
        const auto centre = box.getCentre();
        juce::Path arrow;
        arrow.addTriangle(centre.x - 2.5f, centre.y - 3.0f, centre.x - 2.5f, centre.y + 3.0f, centre.x + 3.0f, centre.y);
        g.fillPath(arrow);
        return;
    }
    g.setFont(uiFontBold(fontSize));
    drawLine(g, text, area, juce::Justification::centred);
}

juce::String secondsText(double seconds)
{
    return seconds < 1.0 ? juce::String(juce::roundToInt(seconds * 1000.0)) + " ms" : juce::String(seconds, 2) + " s";
}

juce::String padTitle(int note)
{
    return "Pad " + DrumRackDevice::noteName(note);
}
}
