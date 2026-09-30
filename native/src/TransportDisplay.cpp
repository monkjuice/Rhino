#include "TransportDisplay.h"
#include "Theme.h"

namespace rhino
{
namespace
{
// The three type sizes the panel uses. The position is set to be read across a
// desk; the primary readings beside it at about half that; the load meters
// smaller again, because a figure nobody acts on should not compete with the
// clock. All three are whole pixels per em, which is what keeps their stems on
// pixel columns - see uiFont in Theme.h.
constexpr float positionEm = 26.0f;
constexpr float primaryEm = 15.0f;
constexpr float secondaryEm = 11.0f;

// Space between the columns, and the panel's own padding. Generous, because
// spacing is the only thing separating the three regions: at anything less the
// clock and the loop start to read as one run of characters.
constexpr int columnGap = 26;
// What the gap closes to before any reading is given up: spacing is the first
// thing the box spends when it runs short, exactly as it should be.
constexpr int tightColumnGap = 14;
constexpr int horizontalPadding = 16;
constexpr int chevronWidth = 16;

int widthOf(const juce::Font& font, const juce::String& text)
{
    return text.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt(font, text);
}
}

TransportDisplay::TransportDisplay()
{
    setOpaque(true);
    configuration.setTooltip("Choose what the display shows");
    configuration.setAccessible(true);
    configuration.setButtonText("v");
    configuration.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    configuration.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    configuration.setColour(juce::TextButton::textColourOffId, palette::displayTextDim);
    configuration.setColour(juce::TextButton::textColourOnId, palette::displayText);
    configuration.onClick = [this]
    {
        if (configurationRequested) configurationRequested();
    };
    addAndMakeVisible(configuration);
}

void TransportDisplay::GlyphButton::paintButton(juce::Graphics& g, bool highlighted, bool pressed)
{
    auto colour = findColour(juce::TextButton::textColourOffId);
    if (highlighted || pressed) colour = colour.brighter(0.4f);
    g.setColour(colour);
    g.setFont(uiFont(9.0f));
    drawSnappedText(g, getButtonText(), getLocalBounds(), juce::Justification::centred);
}

void TransportDisplay::refresh(juce::String& field, const juce::String& next)
{
    if (field == next) return;
    field = next;
    repaint();
}

void TransportDisplay::setPosition(const juce::String& next) { refresh(position, next); }
void TransportDisplay::setClock(const juce::String& next) { refresh(clock, next); }
void TransportDisplay::setLoop(const juce::String& next) { refresh(loop, next); }
void TransportDisplay::setStatistics(const juce::String& next) { refresh(statistics, next); }
void TransportDisplay::setDeviceInfo(const juce::String& next) { refresh(deviceInfo, next); }

void TransportDisplay::setTempoAndSignature(const juce::String& nextTempo, const juce::String& nextSignature)
{
    if (tempo == nextTempo && signature == nextSignature) return;
    tempo = nextTempo;
    signature = nextSignature;
    repaint();
}

void TransportDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // A recess rather than a box: the inset colour is darker than the bar it
    // sits in, and a single hairline a shade lighter than the bar reads as the
    // lip of the well without becoming an outline in its own right.
    g.setColour(palette::displayInset);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    auto area = getLocalBounds().reduced(horizontalPadding, 4).withTrimmedRight(chevronWidth);
    if (area.getWidth() <= 0) return;

    const juce::Font positionFont(uiFontBold(positionEm));
    const juce::Font primaryFont(uiFont(primaryEm));
    const juce::Font secondaryFont(uiFont(secondaryEm));

    // What the box gives up, and in what order, when it is not wide enough for
    // everything. Spacing and wording go before any reading does, and then the
    // readings go lowest priority first: the audio device, the load meters,
    // and the loop last of all. Each is dropped whole. A reading truncated to
    // a stub is worse than one that is simply not there, and the position and
    // the clock never move to make room for either.
    const auto joined = [](const juce::String& a, const juce::String& b)
    {
        return a.isEmpty() ? b : b.isEmpty() ? a : a + "   " + b;
    };
    const auto positionWidth = widthOf(positionFont, position);
    const auto middleText = joined(tempo, signature);
    const auto middleWidth = std::max(widthOf(primaryFont, middleText), widthOf(primaryFont, clock));
    const auto labelledLoop = loop.isEmpty() ? juce::String() : "LOOP  " + loop;

    struct Layout { int gap; juce::String upper, lower; };
    const Layout layouts[]
    {
        {columnGap,      labelledLoop, joined(statistics, deviceInfo)},
        {columnGap,      labelledLoop, statistics},
        {tightColumnGap, labelledLoop, statistics},
        {tightColumnGap, labelledLoop, {}},
        {tightColumnGap, loop,         {}},
        {tightColumnGap, {},           {}},
    };
    auto chosen = layouts[0];
    auto rightWidth = 0;
    for (const auto& layout : layouts)
    {
        chosen = layout;
        rightWidth = std::max(widthOf(secondaryFont, layout.upper), widthOf(secondaryFont, layout.lower));
        // One gap between the position and the middle column, and a second
        // before the right one only when there is a right one.
        const auto gaps = layout.gap * (rightWidth > 0 ? 2 : 1);
        if (positionWidth + middleWidth + rightWidth + gaps <= area.getWidth()) break;
    }
    const auto gap = chosen.gap;

    const auto row = [](juce::Rectangle<int> column)
    {
        // Two rows split by weight rather than in half, so the upper reading
        // sits above the panel's midline and the lower one below it, level
        // with the middle column's own pair whatever each of them contains.
        const auto half = column.getHeight() / 2;
        return std::make_pair(column.withHeight(half), column.withTrimmedTop(half));
    };

    if (rightWidth > 0)
    {
        const auto column = area.removeFromRight(rightWidth);
        area.removeFromRight(gap);
        const auto [upper, lower] = row(column);
        g.setFont(secondaryFont);
        g.setColour(palette::displayTextDim);
        drawSnappedText(g, chosen.upper, upper, juce::Justification::centredRight);
        g.setColour(palette::displayTextFaint);
        drawSnappedText(g, chosen.lower, lower, juce::Justification::centredRight);
    }

    // The position takes what it needs and the middle column takes the rest,
    // so a wider window opens the gap between them rather than stretching
    // either one. Below the width the position needs there is no readout worth
    // showing, and the shell hides the whole panel instead.
    const auto positionColumn = area.removeFromLeft(std::min(area.getWidth(), positionWidth));
    g.setFont(positionFont);
    g.setColour(palette::displayText);
    drawSnappedText(g, position, positionColumn, juce::Justification::centredLeft);

    if (area.getWidth() <= gap) return;
    area.removeFromLeft(gap);
    const auto [upper, lower] = row(area);
    g.setFont(primaryFont);
    g.setColour(palette::displayTextDim);
    drawSnappedText(g, middleText, upper, juce::Justification::centredLeft, true);
    drawSnappedText(g, clock, lower, juce::Justification::centredLeft, true);
}

void TransportDisplay::resized()
{
    configuration.setBounds(getWidth() - chevronWidth - 2, 2, chevronWidth, std::max(1, getHeight() - 4));
}
}
