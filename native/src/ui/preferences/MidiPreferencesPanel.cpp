#include "MidiPreferencesPanel.h"
#include "ui/shared/Theme.h"

namespace rhino
{
namespace
{
constexpr int rowHeight = 72;
constexpr int introHeight = 74;
constexpr int margin = 16;
constexpr int graphSize = 56;
constexpr int chooserWidth = 150;
constexpr int chooserHeight = 24;
// How long the last note's dot stays lit after it is played, in ticks of the
// panel's 20 Hz timer, before it settles to a dim mark of where it landed.
constexpr int glowTicks = 30;
}

// One keyboard: its name, what Rhino is doing with it, the curve chooser and
// the curve drawn with the last note on it.
class MidiPreferencesPanel::KeyboardRow final : public juce::Component
{
public:
    KeyboardRow(Session& s, juce::String port) : session(s), portName(std::move(port))
    {
        for (size_t index = 0; index < velocityCurves.size(); ++index)
        {
            const auto name = velocityCurves[index].name;
            chooser.addItem(juce::String(name.data(), name.size()), static_cast<int>(index) + 1);
        }
        chooser.setTooltip("How hard " + portName + "'s keys have to be struck to play loud. Softer curves "
                           "play a light touch louder; Fixed plays every note at full velocity.");
        chooser.onChange = [this]
        {
            const auto index = chooser.getSelectedId() - 1;
            if (juce::isPositiveAndBelow(index, static_cast<int>(velocityCurves.size())))
                session.setVelocityCurve(portName, velocityCurves[static_cast<size_t>(index)].curve);
            refresh();
            repaint();
        };
        addAndMakeVisible(chooser);
        refresh();
    }

    const juce::String& port() const { return portName; }

    // Reads the model again; repaints only what changed.
    void refresh()
    {
        const auto curve = session.velocityCurve(portName);
        for (size_t index = 0; index < velocityCurves.size(); ++index)
            if (velocityCurves[index].curve == curve && chooser.getSelectedId() != static_cast<int>(index) + 1)
                chooser.setSelectedId(static_cast<int>(index) + 1, juce::dontSendNotification);
        const auto state = session.keyboardPortState(portName);
        const auto note = session.lastKeyboardNote(portName);
        auto changed = state != shownState || curve != shownCurve;
        if (note.notes != shownNote.notes)
        {
            glow = glowTicks;
            changed = true;
        }
        else if (glow > 0)
        {
            --glow;
            changed = true;
        }
        shownState = state;
        shownCurve = curve;
        shownNote = note;
        if (changed)
            repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(margin, 0);
        area.removeFromRight(graphSize);
        area.removeFromRight(12);
        chooser.setBounds(area.removeFromRight(chooserWidth).withSizeKeepingCentre(chooserWidth, chooserHeight));
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(margin, 0);
        g.setColour(palette::border);
        g.fillRect(getLocalBounds().removeFromBottom(1));

        const auto graph = area.removeFromRight(graphSize).withSizeKeepingCentre(graphSize, graphSize);
        drawCurve(g, graph.toFloat());

        area.removeFromRight(12 + chooserWidth + 12);
        const auto text = area.withSizeKeepingCentre(area.getWidth(), 36);
        g.setColour(palette::text);
        g.setFont(uiFontBold(12.0f));
        drawSnappedText(g, portName, text.withHeight(18), juce::Justification::centredLeft, true);
        g.setColour(shownState == Session::KeyboardPortState::busy ? palette::meterHot : palette::textDim);
        g.setFont(uiFont(11.0f));
        drawSnappedText(g, statusText(), text.withTrimmedTop(18), juce::Justification::centredLeft, true);
    }

private:
    juce::String statusText() const
    {
        const auto lastNote = [this]() -> juce::String
        {
            if (shownNote.notes == 0)
                return "play a note to see how hard you struck it";
            if (shownNote.struck == shownNote.played)
                return "last note struck at " + juce::String(shownNote.struck);
            return "last note struck at " + juce::String(shownNote.struck) + ", played at "
                 + juce::String(shownNote.played);
        };
        switch (shownState)
        {
            case Session::KeyboardPortState::engine:
                return "As struck - " + lastNote();
            case Session::KeyboardPortState::open:
                return "Through its curve - " + lastNote();
            case Session::KeyboardPortState::busy:
                return "Another program has it open. Close it and Rhino takes the keyboard over.";
            case Session::KeyboardPortState::absent:
                break;
        }
        return "Not plugged in. Its curve applies once it is.";
    }

    // The curve over the whole range, the straight line it bends away from,
    // and where the last note landed on it.
    void drawCurve(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        g.setColour(palette::displayInset);
        g.fillRect(area);
        g.setColour(palette::border);
        g.drawRect(area, 1.0f);
        const auto plot = area.reduced(5.0f);
        const auto at = [plot](int struck, int played)
        {
            return juce::Point<float>(plot.getX() + plot.getWidth() * static_cast<float>(struck) / 127.0f,
                                      plot.getBottom() - plot.getHeight() * static_cast<float>(played) / 127.0f);
        };
        g.setColour(palette::border);
        g.drawLine(juce::Line<float>(at(0, 0), at(127, 127)), 1.0f);
        juce::Path path;
        path.startNewSubPath(at(0, 0));
        for (int struck = 1; struck <= 127; ++struck)
            path.lineTo(at(struck, applyVelocityCurve(shownCurve, struck)));
        g.setColour(palette::deviceAccent);
        g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (shownNote.notes == 0)
            return;
        const auto lit = static_cast<float>(glow) / static_cast<float>(glowTicks);
        g.setColour(palette::text.withAlpha(0.35f + 0.65f * lit));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(at(shownNote.struck, shownNote.played)));
    }

    Session& session;
    const juce::String portName;
    juce::ComboBox chooser;
    Session::KeyboardPortState shownState = Session::KeyboardPortState::engine;
    VelocityCurve shownCurve = VelocityCurve::linear;
    Session::KeyboardNote shownNote;
    int glow = 0;
};

MidiPreferencesPanel::MidiPreferencesPanel(Session& s) : session(s)
{
    viewport.setViewedComponent(&rowHolder, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    rebuildRows();
}

MidiPreferencesPanel::~MidiPreferencesPanel()
{
    stopTimer();
}

// Polled while showing: the notes arrive on the MIDI thread, and the model
// announces neither them nor a keyboard plugged in.
void MidiPreferencesPanel::visibilityChanged()
{
    if (isVisible())
    {
        rebuildRows();
        startTimerHz(20);
    }
    else
    {
        stopTimer();
    }
}

void MidiPreferencesPanel::timerCallback()
{
    if (--ticksToRescan <= 0)
    {
        ticksToRescan = 20;
        if (session.keyboardPortNames() != shownPorts)
            rebuildRows();
    }
    for (auto& row : rows)
        row->refresh();
}

void MidiPreferencesPanel::rebuildRows()
{
    shownPorts = session.keyboardPortNames();
    rows.clear();
    for (const auto& port : shownPorts)
    {
        rows.push_back(std::make_unique<KeyboardRow>(session, port));
        rowHolder.addAndMakeVisible(*rows.back());
    }
    resized();
    repaint();
}

void MidiPreferencesPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    auto intro = getLocalBounds().reduced(margin, 0).removeFromTop(introHeight).withTrimmedTop(14);
    g.setColour(palette::text);
    g.setFont(uiFontBold(13.0f));
    drawSnappedText(g, "Keyboards", intro.removeFromTop(20));
    g.setColour(palette::textDim);
    g.setFont(uiFont(11.0f));
    drawSnappedText(g, "How hard each keyboard's keys have to be struck to play loud.", intro.removeFromTop(17),
                    juce::Justification::centredLeft, true);
    drawSnappedText(g, "Rhino applies the curve as each note arrives, so a take records what you heard.",
                    intro.removeFromTop(17), juce::Justification::centredLeft, true);
    g.setColour(palette::border);
    g.fillRect(0, introHeight - 1, getWidth(), 1);
    if (rows.empty())
    {
        g.setColour(palette::textDim);
        drawSnappedText(g, "No MIDI keyboard is plugged in.", getLocalBounds().withTrimmedTop(introHeight),
                        juce::Justification::centred);
    }
}

void MidiPreferencesPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop(introHeight);
    viewport.setBounds(area);
    const auto width = area.getWidth() - (static_cast<int>(rows.size()) * rowHeight > area.getHeight()
                                              ? viewport.getScrollBarThickness() : 0);
    rowHolder.setSize(width, static_cast<int>(rows.size()) * rowHeight);
    auto y = 0;
    for (auto& row : rows)
    {
        row->setBounds(0, y, width, rowHeight);
        y += rowHeight;
    }
}

}
