#include "DjMixerPanelInternal.h"

// The booth's mixer: see DjMixerPanel.h. The panel is here: what it is made
// of, how it follows the decks and the booth, its paint and its layout. The
// strips are in DjMixerPanelStrips.cpp and the master section in
// DjMixerPanelMaster.cpp, each with the table its knobs are described in.

namespace rhino
{
using namespace mixerpanel;

DjMixerPanel::DjMixerPanel(Session& s) : session(s)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    dressMaster();
    for (auto* component : std::initializer_list<juce::Component*>{&masterMeterLeft, &masterMeterRight, &faderCurve, &monoSplit,
                                                                   &micMode, &micMeter, &sendType, &fxType, &fxTarget, &fxLow,
                                                                   &fxMid, &fxHigh, &fxBeatDown, &fxBeatUp, &fxAuto, &fxTap,
                                                                   &fxOn, &crossfader, &crossfaderCurve})
        addAndMakeVisible(component);
    for (auto& knob : masterKnobs)
        addAndMakeVisible(knob);
    sync();
}

int DjMixerPanel::preferredWidth() const
{
    return static_cast<int>(strips.size()) * stripWidth + masterWidth + 8;
}

int DjMixerPanel::minimumWidth() const
{
    return static_cast<int>(strips.size()) * minimumStripWidth + masterWidth + 8;
}

void DjMixerPanel::sync()
{
    std::vector<Session::DjDeckInfo> decks;
    for (int i = 0; i < session.djDeckCount(); ++i)
        decks.push_back(session.djDeckInfo(i));
    sync(decks);
}

// The panel's shape follows the decks; the values follow in refresh().
void DjMixerPanel::sync(const std::vector<Session::DjDeckInfo>& decks)
{
    const auto count = static_cast<int>(decks.size());
    if (static_cast<int>(strips.size()) != count)
    {
        buildStrips(count);
        {
            const juce::ScopedValueSetter<bool> scope(updating, true);
            fxTarget.clear(juce::dontSendNotification);
            fxTarget.addItem("Master", 1);
            for (int i = 0; i < count; ++i)
                fxTarget.addItem("CH " + juce::String(i + 1), i + 2);
        }
        resized();
        // The rules between the strips and the captions over their knobs
        // have moved with them.
        repaint();
    }
    for (auto& strip : strips)
    {
        const auto& info = decks[static_cast<size_t>(strip->channel)];
        strip->name.setText(info.name.isNotEmpty() ? juce::String(strip->channel + 1) + "  " + info.name : "CH " + juce::String(strip->channel + 1),
                            juce::dontSendNotification);
    }
    refresh(false);
}

void DjMixerPanel::tick()
{
    refresh(true);
}

// Every setter the two halves call compares before it repaints, so a
// refresh that finds the booth as it was repaints nothing.
void DjMixerPanel::refresh(bool meters)
{
    const juce::ScopedValueSetter<bool> scope(updating, true);
    refreshStrips(meters);
    refreshMaster(meters);
}

bool DjMixerPanel::reaches(const juce::Graphics& g, juce::Rectangle<int> area) const
{
    return !cullRepaints || g.clipRegionIntersects(area);
}

// The ground, the rules and the words. Every control paints itself; this
// runs under a knob, a key or a combo box that repaints, so a word is drawn
// only where the repaint reaches it, and none is shaped here.
void DjMixerPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setColour(palette::border);
    g.drawRect(getLocalBounds());
    const auto top = 4, bottom = getHeight() - crossfaderHeight - 8;
    // A rule between strips, and one before the master section.
    for (size_t i = 1; i < strips.size(); ++i)
        g.fillRect(4 + static_cast<int>(i) * stripPitch, top, 1, bottom - top);
    const auto masterX = getWidth() - masterWidth - 4;
    g.fillRect(masterX, top, 1, bottom - top);
    g.fillRect(4, getHeight() - crossfaderHeight - 2, getWidth() - 8, 1);
    const auto draw = [this, &g](const ShapedLine& line)
    {
        if (!cullRepaints || line.isIn(g))
            line.draw(g);
    };
    g.setColour(palette::textDim);
    for (auto& strip : strips)
        for (const auto& caption : strip->captions)
            draw(caption);
    for (const auto& caption : masterCaptions)
        draw(caption);
    draw(faderCurveCaption);
    for (const auto* title : {&masterTitle, &isolatorTitle, &phonesTitle, &micTitle, &sendTitle, &fxTitle})
        draw(*title);
    // The readouts: the tempo the effect follows, and its beat.
    for (const auto& [area, line] : {std::pair<juce::Rectangle<int>, const ShapedLine*>{bpmArea, &bpmLine}, {beatArea, &beatLine}})
    {
        if (!reaches(g, area)) continue;
        g.setColour(palette::displayInset);
        g.fillRect(area);
        g.setColour(palette::displayText);
        line->draw(g);
    }
    // A and B at the crossfader's ends.
    g.setColour(palette::textDim);
    draw(aLine);
    draw(bLine);
}

void DjMixerPanel::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    auto foot = bounds.removeFromBottom(crossfaderHeight);
    crossfaderCurve.setBounds(foot.removeFromRight(56).reduced(0, 12));
    foot.removeFromRight(4);
    crossfader.setBounds(foot.withTrimmedLeft(16).withTrimmedRight(16).reduced(0, 6));
    bounds.removeFromBottom(4);
    layoutStrips(bounds);
    layoutMaster(bounds.removeFromRight(masterWidth));
    const auto footArea = juce::Rectangle<int>(4, getHeight() - crossfaderHeight, getWidth() - 8, crossfaderHeight);
    const auto footFont = uiFontBold(9.0f);
    aLine.set("A", footFont, footArea.withWidth(16), juce::Justification::centred);
    bLine.set("B", footFont, juce::Rectangle<int>(crossfader.getRight(), footArea.getY(), 16, footArea.getHeight()),
              juce::Justification::centred);
}
}
