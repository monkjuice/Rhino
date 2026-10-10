#include "DjView.h"
#include "UiVisibility.h"
#include <algorithm>

// The DJ view: see DjView.h.

namespace rhino
{
namespace
{
constexpr Session::DjQuantise quantiseChoices[] {Session::DjQuantise::off, Session::DjQuantise::beat,
                                                 Session::DjQuantise::bar, Session::DjQuantise::fourBars};
}

DjView::DjView(Session& s)
    : session(s), mixer(s), vblank(this, [this]
      {
          if (isHiddenInShell(*this)) return;
          for (auto& deck : decks)
              deck->tickDisplay();
      })
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    addDeckButton.setButtonText("+ Deck");
    addDeckButton.setTooltip("Add a deck, up to six. Each deck is a channel of the mixer.");
    addDeckButton.onClick = [this] { addDeck(); };
    stopAllButton.setButtonText("Stop all");
    stopAllButton.setTooltip("Pause every deck.");
    stopAllButton.onClick = [this] { session.djStopAll(); };
    for (int i = 0; i < 4; ++i)
        quantiseBox.addItem(Session::djQuantiseName(quantiseChoices[i]), i + 1);
    quantiseBox.setTooltip("Quantise - when a start or a hot cue lands: at once, or on the master deck's next beat, "
                           "bar or four bars, the way Live launches clips. Cue points snap to the beat while it is on.");
    quantiseBox.onChange = [this]
    {
        if (updatingQuantise) return;
        const auto index = quantiseBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow(index, 4))
            session.setDjQuantise(quantiseChoices[index]);
    };
    phaseLock.setTooltip("Phase lock - a synced deck is nudged back onto the master's beat when it drifts, rather than "
                         "only aligned when Sync is pressed.");
    phaseLock.onClick = [this] { session.setDjPhaseLock(!session.djPhaseLock()); };
    for (auto* button : {&addDeckButton, &stopAllButton})
    {
        button->setColour(juce::TextButton::buttonColourId, palette::control);
        button->setColour(juce::TextButton::textColourOffId, palette::text);
        button->setWantsKeyboardFocus(false);
    }
    mixer.status = [this](const juce::String& message) { if (status) status(message); };
    for (auto* component : std::initializer_list<juce::Component*>{&addDeckButton, &stopAllButton, &quantiseBox, &phaseLock, &mixer})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    session.listeners.add(this);
    rebuildDecks();
    sync();
    startTimerHz(30);
}

DjView::~DjView()
{
    session.removeChangeListener(this);
    session.listeners.remove(this);
}

int DjView::selectedTrack() const
{
    return session.djDeckInfo(selected).track;
}

void DjView::addDeck()
{
    const auto result = session.addDjDeck();
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    if (status) status("Deck " + juce::String(session.djDeckCount()) + " added: choose what it plays from its name bar");
}

// Deferred, because the request comes from a key on the panel that the
// removal may destroy: the last panel goes when the deck count falls.
void DjView::removeDeck(int deck)
{
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<DjView>(this), deck]
    {
        if (safe == nullptr) return;
        const auto result = safe->session.removeDjDeck(deck);
        if (result.failed() && safe->status) safe->status(result.getErrorMessage());
    });
}

void DjView::rebuildDecks()
{
    const auto count = session.djDeckCount();
    while (static_cast<int>(decks.size()) > count)
        decks.pop_back();
    while (static_cast<int>(decks.size()) < count)
    {
        const auto index = static_cast<int>(decks.size());
        auto deck = std::make_unique<DjDeckPanel>(session, index);
        deck->status = [this](const juce::String& message) { if (status) status(message); };
        deck->selected = [this, index] { focusDeck(index); };
        deck->editRequested = [this](int track, te::EditItemID clip) { if (editRequested) editRequested(track, clip); };
        deck->removeRequested = [this, index] { removeDeck(index); };
        addAndMakeVisible(*deck);
        decks.push_back(std::move(deck));
    }
    selected = juce::jlimit(0, std::max(0, count - 1), selected);
    markFocus();
}

// A press on a console focuses its deck: the deck wears the mark, Space
// plays and pauses it, and the shell is told so the lower pane can follow
// its track.
void DjView::focusDeck(int index)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(decks.size()))) return;
    selected = index;
    markFocus();
    if (deckSelected) deckSelected(index);
}

void DjView::markFocus()
{
    for (size_t i = 0; i < decks.size(); ++i)
        decks[i]->setFocused(static_cast<int>(i) == selected);
}

void DjView::togglePlayFocused()
{
    if (juce::isPositiveAndBelow(selected, static_cast<int>(decks.size())))
        session.djTogglePlay(selected);
}

void DjView::sync()
{
    // The mixer first: its width follows its strips, and the layout below
    // reads that width. Laid out before the strips existed, a new channel
    // sat under the master section until the window was next resized.
    mixer.sync();
    const auto count = session.djDeckCount();
    if (static_cast<int>(decks.size()) != count || mixer.preferredWidth() != laidOutMixerWidth)
    {
        rebuildDecks();
        resized();
    }
    for (auto& deck : decks)
        deck->sync();
    {
        const juce::ScopedValueSetter<bool> scope(updatingQuantise, true);
        const auto current = session.djQuantise();
        for (int i = 0; i < 4; ++i)
            if (quantiseChoices[i] == current)
                quantiseBox.setSelectedId(i + 1, juce::dontSendNotification);
    }
    phaseLock.setLit(session.djPhaseLock());
    addDeckButton.setEnabled(count < Session::maximumDjDecks);
    repaint();
}

void DjView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (isHiddenInShell(*this))
    {
        staleWhileHidden = true;
        return;
    }
    sync();
}

void DjView::editDidChange()
{
    if (isHiddenInShell(*this))
    {
        staleWhileHidden = true;
        return;
    }
    sync();
}

void DjView::visibilityChanged()
{
    if (staleWhileHidden && !isHiddenInShell(*this))
    {
        staleWhileHidden = false;
        sync();
    }
}

void DjView::timerCallback()
{
    if (isHiddenInShell(*this)) return;
    if (++blinkTicks >= 8)
    {
        blinkTicks = 0;
        blinkPhase = !blinkPhase;
    }
    for (auto& deck : decks)
        deck->tick(blinkPhase);
    mixer.tick();
}

void DjView::paint(juce::Graphics& g)
{
    g.fillAll(palette::appBackground);
    g.setColour(palette::globalBar);
    g.fillRect(0, 0, getWidth(), toolbarHeight);
    g.setColour(palette::border);
    g.fillRect(0, toolbarHeight - 1, getWidth(), 1);
    g.setColour(palette::textDim);
    g.setFont(uiFontBold(10.0f));
    drawSnappedText(g, "DJ", juce::Rectangle<int>(10, 0, 30, toolbarHeight));
    drawSnappedText(g, "QUANTISE", juce::Rectangle<int>(quantiseBox.getX() - 62, 0, 58, toolbarHeight), juce::Justification::centredRight);
    if (decks.empty())
    {
        g.setColour(palette::textDim);
        g.setFont(uiFont(12.0f));
        drawSnappedText(g, "No decks. Press + Deck, then choose a track, a group or a file for it.",
                        getLocalBounds().withTrimmedTop(toolbarHeight).withTrimmedRight(mixer.isVisible() ? mixer.getWidth() : 0),
                        juce::Justification::centred, true);
    }
}

// Decks stand either side of the mixer, odd on the left and even on the
// right, each column stacking up to three. The mixer keeps its own height
// at the top of the middle; the decks take the full height of their side.
void DjView::resized()
{
    auto bounds = getLocalBounds();
    auto bar = bounds.removeFromTop(toolbarHeight).reduced(8, 4);
    bar.removeFromLeft(32);
    addDeckButton.setBounds(bar.removeFromLeft(70));
    bar.removeFromLeft(6);
    stopAllButton.setBounds(bar.removeFromLeft(70));
    bar.removeFromLeft(70);
    quantiseBox.setBounds(bar.removeFromLeft(90));
    bar.removeFromLeft(6);
    phaseLock.setBounds(bar.removeFromLeft(50));
    bounds.reduce(4, 4);
    const auto count = static_cast<int>(decks.size());
    const auto perSide = (count + 1) / 2;
    const auto narrowest = mixer.minimumWidth();
    const auto mixerWidth = juce::jlimit(narrowest, std::max(narrowest, bounds.getWidth() / 2), mixer.preferredWidth());
    laidOutMixerWidth = mixer.preferredWidth();
    const auto mixerArea = bounds.withSizeKeepingCentre(mixerWidth, bounds.getHeight())
                               .withHeight(std::min(DjMixerPanel::preferredHeight, bounds.getHeight()));
    mixer.setBounds(mixerArea);
    if (count == 0) return;
    const auto left = bounds.withRight(mixerArea.getX() - 4);
    const auto right = bounds.withLeft(mixerArea.getRight() + 4);
    const auto rowHeight = std::max(DjDeckPanel::minimumHeight, (bounds.getHeight() - (perSide - 1) * 4) / std::max(1, perSide));
    for (int i = 0; i < count; ++i)
    {
        const auto column = i % 2 == 0 ? left : right;
        const auto row = i / 2;
        decks[static_cast<size_t>(i)]->setBounds(column.getX(), column.getY() + row * (rowHeight + 4), column.getWidth(),
                                                 std::min(rowHeight, column.getBottom() - (column.getY() + row * (rowHeight + 4))));
    }
}
}
