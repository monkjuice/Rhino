#include "MainComponent.h"

namespace rhino
{

// The second view: the Arrange/DJ switch in the control bar and Tab open
// the DJ view (wiki/pages/dj-view.md) in the arrangement's place. The two
// views show one project, but each keeps its own selection and focus, as
// Live does. Switching views therefore hands the Device View over to
// whatever the view being shown already had selected.
void MainComponent::setDjViewOpen(bool open)
{
    if (djViewOpen == open) return;
    djViewOpen = open;
    // The decks have transports of their own, and the song's is out of
    // reach while they show: it stops, its keys dim, and Space is the
    // focused deck's play key.
    if (open && session.isPlaying())
        session.stop();
    for (auto* control : std::initializer_list<juce::Component*>{&play, &stop, &rewind, &record})
        control->setEnabled(!open);
    if (open)
        followDeck(djView.selectedDeck(), false);
    else if (const auto track = arrangement.selectedTrackIndex(); track >= 0)
        rack.selectTrack(track);
    logStatus(open ? "DJ view: add a deck and choose what it plays from its name bar; Space plays the focused deck; "
                     "Tab returns to the arrangement"
                   : "Arrangement view");
    resized();
    repaint();
}

// The clip pane belongs to whatever clip is selected. The toggle closes it,
// or re-opens the editor the current selection calls for - there is no
// longer an "empty pane" to protect against, because an empty one is given
// back to the arrangement.
void MainComponent::toggleClipEditor()
{
    if (clipPaneOpen())
        lowerPane = LowerPane::none;
    else if (const auto id = clipForPane(); session.findAudioClip(id) != nullptr)
    {
        audioClip.setClip(id);
        lowerPane = LowerPane::audio;
    }
    else if (session.findClip(id) != nullptr)
    {
        if (!(session.hasPatternClip() && session.pattern().itemID == id))
            session.selectPatternClip(id);
        lowerPane = LowerPane::notes;
    }
    else
    {
        logStatus(djViewOpen ? "Focus a deck playing a track of the song first: its clip opens here"
                             : "Select a clip first: a MIDI clip opens the note editor, an audio clip the audio editor");
        return;
    }
    rememberPaneSelection();
    updateEditorLabel();
    applyPaneLayout();
}

// The clip the Clip toggle opens: in the DJ view the focused deck's clip
// - the cell it plays - and in the arrangement the selected clip. The toggle used to ask the arrangement in both
// views, and the arrangement selects no clip while the consoles show, so
// a clip editor closed for the Device View could not be opened again.
te::EditItemID MainComponent::clipForPane() const
{
    if (djViewOpen)
        if (const auto clip = session.djDeckEditClip(djView.selectedDeck()); clip != te::EditItemID())
            return clip;
    return arrangement.selectedClipID();
}

// The other half of the pair. Pressing Devices while a clip editor is open
// swaps to the devices rather than stacking them, which is the whole point
// of the two being one pane.
void MainComponent::toggleDeviceView()
{
    lowerPane = lowerPane == LowerPane::devices ? LowerPane::none : LowerPane::devices;
    updateEditorLabel();
    applyPaneLayout();
}

// Clicking a track card asks for its devices. Every track has a chain -
// an audio track's effects and the main row's are as much a chain as an
// instrument track's - so this consults no clip, but it opens only when the
// pane is free. A clip editor that is open is being worked in, and having
// it swapped out by a click on the card beside it would be a surprise; the
// Devices toggle is the way across while one is open.
void MainComponent::showDeviceView()
{
    if (lowerPane != LowerPane::none) return;
    lowerPane = LowerPane::devices;
    updateEditorLabel();
    requestPaneLayout();
}

// A console pressed in the DJ view hands its track to the lower pane, as
// a track card does in the arrangement: the Device View shows the track's
// chain, and whichever clip editor is open moves to the track's clip - the
// note editor for a MIDI track, the audio editor for an audio one. With
// openPane a closed pane opens on the devices, as a card click opens it;
// the view switch passes false, so showing the DJ view changes no more
// than the arrangement's own selection would. A file deck has no track of
// the song and changes nothing. The pane state is read before
// the arrangement's selection moves, because that move re-reads the
// arrangement's own clip selection and may close the pane on the way.
void MainComponent::followDeck(int deck, bool openPane)
{
    const auto info = session.djDeckInfo(deck);
    if (info.kind != Session::DjSourceKind::track || info.track < 0) return;
    const auto paneBefore = lowerPane;
    const auto clipPaneWasOpen = clipPaneOpen();
    arrangement.selectTrack(info.track);
    rack.selectTrack(info.track);
    const auto clip = session.djDeckEditClip(deck);
    const auto audio = clip != te::EditItemID() && session.findAudioClip(clip) != nullptr;
    const auto midi = clip != te::EditItemID() && !audio && session.findClip(clip) != nullptr;
    if (midi && !(session.hasPatternClip() && session.pattern().itemID == clip))
        session.selectPatternClip(clip);
    if (clipPaneWasOpen)
    {
        if (audio) audioClip.setClip(clip);
        lowerPane = audio ? LowerPane::audio : midi ? LowerPane::notes : LowerPane::none;
    }
    else if (openPane && lowerPane == LowerPane::none)
        lowerPane = LowerPane::devices;
    rememberPaneSelection();
    updateEditorLabel();
    // The pane is laid out again only when it changed face or clip: a
    // deck followed again, playing what it played, moves nothing, and
    // laying the window out repainted all of it.
    if (lowerPane != paneBefore || clip != followedClip)
        requestPaneLayout();
    followedClip = clip;
}

// The clip a command would act on, when that clip is an audio clip.
te::EditItemID MainComponent::selectedAudioClipID() const
{
    const auto id = arrangement.selectedClipID();
    return session.findAudioClip(id) != nullptr ? id : te::EditItemID();
}

// What the note editor is for, and the whole of it: a selected MIDI or drum
// clip. A track that runs an instrument no longer counts, however likely it
// is to hold one - it may have no clip at all, and the notes of a clip
// nobody selected are not what the track click was asking to see.
bool MainComponent::isMidiClipSelected() const
{
    const auto id = arrangement.selectedClipID();
    return session.findClip(id) != nullptr && session.findAudioClip(id) == nullptr;
}

void MainComponent::rememberPaneSelection()
{
    paneClip = arrangement.selectedClipID();
    paneMidi = isMidiClipSelected();
}

// Double-clicking a clip opens it: audio in the audio editor, MIDI in the
// note editor. This is the only thing that reveals either of them, which is
// why a single click on a clip - on a waveform or on a bar of notes - still
// only selects it. Whether the Device View is showing is not this
// function's business either way.
void MainComponent::openClip(te::EditItemID id)
{
    if (session.findAudioClip(id) != nullptr)
    {
        audioClip.setClip(id);
        lowerPane = LowerPane::audio;
        logStatus("Audio clip " + audioClip.clipName().quoted()
                  + ": gain, pan, pitch and fades here apply to this clip only");
    }
    else if (session.findClip(id) != nullptr)
        lowerPane = LowerPane::notes;
    else
        return;
    rememberPaneSelection();
    updateEditorLabel();
    requestPaneLayout();
}

// The clip selection moves the clip pane, but only when it actually moved:
// a toggle the user pressed while standing on one clip has to survive the
// next notification about that same clip. The Device View is not touched
// here at all - selecting a clip says nothing about whether its track's
// devices are wanted, and clicking a card is what answers that.
void MainComponent::refreshEditorPanes()
{
    const auto clipID = arrangement.selectedClipID();
    const auto midi = isMidiClipSelected();
    if (clipID == paneClip && midi == paneMidi)
    {
        // The clip the audio editor was showing can still be deleted, or
        // taken away by an undo, without the selection moving at all.
        if (lowerPane == LowerPane::audio && !audioClip.hasClip())
        {
            lowerPane = LowerPane::none;
            requestPaneLayout();
        }
        return;
    }
    paneClip = clipID;
    paneMidi = midi;
    // Selecting a clip never reveals the pane: opening one is a
    // double-click, for a MIDI clip exactly as for an audio clip, so a
    // single click is only ever a selection. What an open pane does is
    // follow that selection the way Live's clip view does - onto the next
    // clip, whichever editor that clip calls for - and close when what was
    // selected is not a clip at all.
    if (clipPaneOpen())
    {
        if (const auto audio = selectedAudioClipID(); audio != te::EditItemID())
        {
            audioClip.setClip(audio);
            lowerPane = LowerPane::audio;
        }
        else
            lowerPane = midi ? LowerPane::notes : LowerPane::none;
    }
    updateEditorLabel();
    requestPaneLayout();
}

void MainComponent::updateEditorLabel()
{
    patternLabel.setText(lowerPane == LowerPane::audio
                             ? "AUDIO CLIP  /  " + audioClip.clipName().toUpperCase()
                         : lowerPane == LowerPane::devices
                             ? juce::String("DEVICES")
                         : session.isPatternDrums() ? "PATTERN 1  /  DRUM EDITOR" : "PATTERN 1  /  NOTE EDITOR",
                         juce::dontSendNotification);
}

}
