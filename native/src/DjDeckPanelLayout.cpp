#include "DjDeckPanelInternal.h"
#include "DjConsoleLayout.h"

// A deck's console, painted and laid out: see DjDeckPanel.h. Where
// everything stands is worked out in DjConsoleLayout.h.

namespace rhino
{
juce::Rectangle<int> DjDeckPanel::headerArea() const
{
    return getLocalBounds().removeFromTop(djconsole::headerHeight);
}

bool DjDeckPanel::reaches(const juce::Graphics& g, juce::Rectangle<int> area) const
{
    return !cullRepaints || g.clipRegionIntersects(area);
}

// The console's ground and frame, the name bar, and the two captions. The
// keys and the wheel paint themselves; this runs under any of them that is
// not opaque, so it draws only what the repaint reaches.
void DjDeckPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    const auto header = headerArea();
    const auto paintsHeader = reaches(g, header);
    if (paintsHeader)
    {
        g.setColour(palette::trackCard);
        g.fillRect(header);
        g.setColour(palette::border);
        g.fillRect(0, header.getBottom() - 1, getWidth(), 1);
    }
    g.setColour(palette::border);
    g.drawRect(getLocalBounds());
    if (paintsHeader)
    {
        // The deck's number, as a player's. The focused deck's lights up and
        // the header wears the strip a selected track card wears, because
        // Space and the lower pane answer to this deck.
        g.setColour(focused ? palette::selection : palette::displayInset);
        g.fillRect(header.withWidth(26).reduced(3));
        if (focused)
            g.fillRect(0, 0, 3, header.getHeight());
        g.setColour(focused ? palette::sideSurface : palette::displayText);
        numberLine.draw(g);
    }
    // The small captions: TEMPO over the fader, BRAKE under its knob.
    g.setColour(palette::textDim);
    for (const auto* caption : {&tempoCaption, &brakeCaption})
        if (!cullRepaints || caption->isIn(g))
            caption->draw(g);
}

void DjDeckPanel::resized()
{
    djconsole::ConsoleSpace space;
    space.bounds = getLocalBounds();
    space.gridShown = grid.isVisible();
    space.screenMinimum = DjDeckDisplay::readoutHeight + DjDeckDisplay::overviewHeight + 36;
    space.gridCellHeight = DjClipGrid::cellHeight;
    const auto layout = djconsole::layOutConsole(space);
    source.setBounds(layout.source);
    live.setBounds(layout.live);
    edit.setBounds(layout.edit);
    eject.setBounds(layout.eject);
    display.setBounds(layout.display);
    for (size_t i = 0; i < hotCues.size(); ++i)
        hotCues[i]->setBounds(layout.hotCues[i]);
    loopIn.setBounds(layout.loopIn);
    loopOut.setBounds(layout.loopOut);
    reloop.setBounds(layout.reloop);
    beatLoop.setBounds(layout.beatLoop);
    loopHalve.setBounds(layout.loopHalve);
    loopDouble.setBounds(layout.loopDouble);
    jumpBack.setBounds(layout.jumpBack);
    jumpSize.setBounds(layout.jumpSize);
    jumpForward.setBounds(layout.jumpForward);
    searchBack.setBounds(layout.searchBack);
    searchForward.setBounds(layout.searchForward);
    reverse.setBounds(layout.reverse);
    quantise.setBounds(layout.quantise);
    cue.setBounds(layout.cue);
    play.setBounds(layout.play);
    grid.setBounds(layout.grid);
    syncKey.setBounds(layout.sync);
    master.setBounds(layout.master);
    tempoRange.setBounds(layout.tempoRange);
    tempo.setBounds(layout.tempo);
    tempoReset.setBounds(layout.tempoReset);
    // A short or narrow console drops the jog wheel first: it and its two
    // controls take empty bounds, so a check that wants every key inside the
    // console still holds.
    jog.setVisible(layout.showsJog);
    vinyl.setVisible(layout.showsJog);
    brake.setVisible(layout.showsJog);
    jog.setBounds(layout.jog);
    vinyl.setBounds(layout.vinyl);
    brake.setBounds(layout.brake);
    const auto captionFont = uiFontBold(8.0f);
    numberLine.set(juce::String(deck + 1), uiFontBold(12.0f), layout.number, juce::Justification::centred);
    tempoCaption.set("TEMPO", captionFont, layout.tempoCaption, juce::Justification::centred);
    if (layout.showsJog)
        brakeCaption.set("BRAKE", captionFont, layout.brakeCaption, juce::Justification::centred);
    else
        brakeCaption.clear();
}
}
