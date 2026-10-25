#include "ArrangementInternal.h"
#include <algorithm>

// Cut, copy, paste, duplicate and delete on whole cards: the card menu's, and
// the shortcuts' while the last click landed on a card. The model decides what
// a card covers - a bus its whole group - and where the copies land
// (SessionTrackClipboard.cpp); this keeps the clipboard, moves the selection
// onto what was made and says what happened.

namespace rhino
{
namespace
{
juce::String trackCountText(size_t count)
{
    return count == 1 ? juce::String("1 track") : juce::String(static_cast<int>(count)) + " tracks";
}

bool namesMainRow(const Session& session, const std::vector<int>& cards)
{
    return std::any_of(cards.begin(), cards.end(), [&session](int track) { return session.isMasterTrack(track); });
}

bool onlyReturns(const Session& session, const std::vector<int>& cards)
{
    return !cards.empty()
        && std::all_of(cards.begin(), cards.end(), [&session](int track) { return session.isReturnTrack(track); });
}
}

// The cards a paste or a duplicate made, top to bottom. The last becomes the
// working track, so a second paste lands under the first.
void Arrangement::selectCards(const std::vector<int>& cards)
{
    if (cards.empty())
        return;
    selectTrack(cards.front());
    for (size_t i = 1; i < cards.size(); ++i)
        toggleTrackSelection(cards[i]);
    setSelection({});
    focus = Focus::track;
}

void Arrangement::copyCards(std::vector<int> cards)
{
    auto copied = session.copyTracks(cards);
    if (copied.isEmpty())
    {
        if (status) status(namesMainRow(session, cards)   ? "The main row cannot be copied"
                           : onlyReturns(session, cards) ? "A return track is not copied: add another from the + menu"
                                                         : "Select a track to copy");
        return;
    }
    trackClipboard = std::move(copied);
    clipboard = {};
    if (status) status("Copied " + trackCountText(trackClipboard.tracks.size()));
}

void Arrangement::cutCards(std::vector<int> cards)
{
    cancelDrag();
    auto copied = session.copyTracks(cards);
    if (copied.isEmpty())
    {
        if (status) status(namesMainRow(session, cards)   ? "The main row cannot be cut"
                           : onlyReturns(session, cards) ? "A return track is not cut: delete it instead"
                                                         : "Select a track to cut");
        return;
    }
    // What the copy left out stays: a cut never deletes a return it could
    // not put on the clipboard.
    std::erase_if(cards, [this](int track) { return session.isReturnTrack(track); });
    const auto first = *std::min_element(cards.begin(), cards.end());
    // A cut the model refuses - the last track - copies nothing either, so
    // the clipboard keeps what it held.
    if (const auto removed = session.removeTracks(cards); removed.failed())
    {
        if (status) status(removed.getErrorMessage());
        return;
    }
    trackClipboard = std::move(copied);
    clipboard = {};
    selectTrack(juce::jlimit(0, std::max(0, session.trackCount() - 1), first));
    focus = Focus::track;
    if (status) status("Cut " + trackCountText(trackClipboard.tracks.size()));
}

void Arrangement::pasteCards(int below)
{
    cancelDrag();
    std::vector<int> pasted;
    if (const auto result = session.pasteTracks(trackClipboard, below, &pasted); result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    selectCards(pasted);
    if (status && !pasted.empty())
        status("Pasted " + trackCountText(pasted.size()) + " at position " + juce::String(pasted.front() + 1));
    repaint();
}

void Arrangement::duplicateCards(std::vector<int> cards)
{
    cancelDrag();
    std::vector<int> duplicated;
    if (const auto result = session.duplicateTracks(cards, &duplicated); result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    selectCards(duplicated);
    if (status) status("Duplicated " + trackCountText(duplicated.size()));
    repaint();
}

// The card that moves up into the first gap is selected, so the selection
// never silently lands on the main row, and Delete pressed again takes the
// next one, as it does in Live.
void Arrangement::deleteCards(std::vector<int> cards)
{
    cancelDrag();
    if (cards.empty())
        return;
    const auto first = *std::min_element(cards.begin(), cards.end());
    const auto name = cards.size() == 1 ? session.trackName(cards.front()) : juce::String();
    const auto before = session.trackCount();
    if (const auto result = session.removeTracks(cards); result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    // A bus takes its members with it, so the count is read off the document.
    const auto removed = static_cast<size_t>(before - session.trackCount());
    selectTrack(juce::jlimit(0, std::max(0, session.trackCount() - 1), first));
    focus = Focus::track;
    if (status) status("Deleted " + (removed == 1 && name.isNotEmpty() ? name : trackCountText(removed)));
}

}
