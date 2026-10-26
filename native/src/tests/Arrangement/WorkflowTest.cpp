#include "ui/arrangement/Arrangement.h"
#include "content/ContentLibrary.h"
// The scenarios drive devices directly, so this runner needs the definitions
// rather than their catalog entries.
#include "instruments/DrumRackDevice.h"
#include "audio/VocoderDevice.h"
#include "audio/UtilityDevice.h"
#include "infra/DjTrackPortDevices.h"
#include "ui/shared/Theme.h"
#include "app/TransportDisplay.h"
#include "app/ControlBarFields.h"
#include "app/LoadingScreen.h"
#include "ui/notes/StepGrid.h"
#include "ui/notes/NoteToolsPanel.h"
#include "ui/audioclip/AudioClipPanel.h"
#include "ui/dj/DjView.h"
#include "ui/shared/Playhead.h"
#include "ui/shared/UiVisibility.h"
#include "app/PushController.h"
#include "ui/preferences/PreferencesPanel.h"
// The DJ booth scenario reads a deck's material, and the booth's edges fill
// its engine's command queue.
#include "dj/DjTrack.h"
#include "dj/DjEngine.h"
#include <algorithm>
#include <array>
#include <functional>
#include <stdexcept>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace rhino
{
int runArrangementTest()
{
    try
    {
        const auto require = [](bool valid, const char* message)
        {
            if (!valid) throw std::runtime_error(message);
        };
        const auto close = [](double a, double b) { return std::abs(a - b) < 0.0001; };
        const auto isMidiNotePixel = [](juce::Colour colour)
        {
            return colour.getRed() > 150 && colour.getGreen() > 165 && colour.getBlue() > 95;
        };
        juce::TemporaryFile source(".wav");
        juce::WavAudioFormat wav;
        {
            std::unique_ptr<juce::OutputStream> stream = source.getFile().createOutputStream();
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            require(writer != nullptr, "Create one-second audio fixture");
            juce::AudioBuffer<float> audio(2, 48000);
            for (int i = 0; i < 48000; ++i)
            {
                const auto sample = static_cast<float>(std::sin(i * 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0))
                    * (i < 24000 ? 0.1f : 0.7f);
                audio.setSample(0, i, sample);
                audio.setSample(1, i, sample);
            }
            require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write fixture");
        }
        Session session;
        const auto scenario = [](const char* name)
        {
            std::fprintf(stderr, "Arrangement scenario: %s\n", name);
            std::fflush(stderr);
        };
        scenario("transport readout");
       #include "scenarios/TransportReadout.inc"
        scenario("control bar fields");
       #include "scenarios/ControlBarFields.inc"
        scenario("browser drops");
       #include "scenarios/BrowserDrops.inc"
        // After the arrangement exists, because the last thing it checks is a
        // track card's own name.
        scenario("interface fonts");
       #include "scenarios/InterfaceFonts.inc"
        scenario("library preview");
       #include "scenarios/LibraryPreview.inc"
        scenario("rendering");
       #include "scenarios/Rendering.inc"
        scenario("bar bands and the grid readouts");
       #include "scenarios/BarBands.inc"
        scenario("audio clip editing");
       #include "scenarios/AudioClipEditing.inc"
        scenario("audio clip mixing");
       #include "scenarios/AudioClipMixing.inc"
        scenario("audio channels");
       #include "scenarios/AudioChannels.inc"
        // Renders, so it has to come before "gestures and persistence" closes
        // the audio device.
        scenario("clip warp");
       #include "scenarios/ClipWarp.inc"
        // Renders too, in a document of its own.
        scenario("clip pitch");
       #include "scenarios/ClipPitch.inc"
        // Renders as well, in a document of its own.
        scenario("clip envelopes");
       #include "scenarios/ClipEnvelopes.inc"
        scenario("clip merge");
       #include "scenarios/ClipMerge.inc"
        scenario("bounce to new track");
       #include "scenarios/ClipBounce.inc"
        scenario("region clipboard");
       #include "scenarios/RegionClipboard.inc"
        scenario("one selection, carried whole");
       #include "scenarios/RegionMove.inc"
        scenario("no clip is left too short to hear");
       #include "scenarios/ClipSlivers.inc"
        scenario("arrangement pan");
       #include "scenarios/ArrangementPan.inc"
        scenario("track management");
       #include "scenarios/TrackManagement.inc"
        scenario("group bus routing");
       #include "scenarios/GroupBusRouting.inc"
        // Renders, so before "gestures and persistence" closes the device.
       #include "scenarios/ReturnTracks.inc"
        // Renders too, in documents of its own.
       #include "scenarios/DeviceSidechain.inc"
        scenario("note editor");
       #include "scenarios/NoteEditor.inc"
        // After the note editor, because reopening a project replaces the
        // document the editor is pointed at, and before "gestures and
        // persistence", which closes the audio device this has to render with.
        scenario("group bus reload");
       #include "scenarios/GroupBusReload.inc"
        // The booth through the session, before the recording scenarios and
        // the audio device closing, as every scenario that drives it is.
        scenario("dj booth");
       #include "scenarios/DjBooth.inc"
        // A document of its own whose decks play its cells.
        scenario("clip cells");
       #include "scenarios/ClipCells.inc"
        // A track's deck heard through the track's own chain, live, in
        // documents of their own.
        scenario("dj live chain");
       #include "scenarios/DjLiveChain.inc"
        // The booth at its edges, in documents of their own.
        scenario("dj booth edges");
       #include "scenarios/DjBoothEdges.inc"
        // After everything that renders and before the audio device closes:
        // arming allocates a playback context to hang its input destinations
        // off, and the offline renders above run a RenderTask directly against
        // an edit that has none.
        scenario("recording");
       #include "scenarios/Recording.inc"
        scenario("vocoder carrier and midi input");
       #include "scenarios/VocoderCarrier.inc"
        scenario("ableton push");
       #include "scenarios/PushController.inc"
        // After the Push, whose reserved port it checks is refused a curve, and
        // before the audio device closes, because it arms a track.
        scenario("keyboard velocity curves");
       #include "scenarios/KeyboardCurves.inc"
        scenario("gestures and persistence");
       #include "scenarios/GesturesAndPersistence.inc"
        scenario("arrangement mixer");
       #include "scenarios/SharedMixer.inc"
        // In a document of its own, which plays its chains by hand and renders
        // nothing, so it may follow the device closing.
        scenario("track meters");
       #include "scenarios/TrackMeters.inc"
        scenario("clip round trip");
       #include "scenarios/ClipRoundTrip.inc"
        scenario("drum kits: whole Drum Racks dropped from the browser");
       #include "scenarios/DrumKitDrops.inc"
        scenario("automation lanes");
       #include "scenarios/AutomationLanes.inc"
        scenario("tempo change");
       #include "scenarios/TempoChange.inc"
        scenario("main track");
       #include "scenarios/MasterTrack.inc"
        scenario("track groups");
       #include "scenarios/TrackGroups.inc"
        scenario("group undo and new lanes");
       #include "scenarios/GroupUndo.inc"
        scenario("track clipboard: whole tracks cut, copied, pasted, duplicated and deleted");
       #include "scenarios/TrackClipboard.inc"
        scenario("clip edits are whole");
       #include "scenarios/ClipEdits.inc"
        scenario("what a drag costs the rest of the interface");
       #include "scenarios/UiCost.inc"
        scenario("a culled repaint matches drawing everything");
       #include "scenarios/PartialRepaint.inc"
        // Last: it clears the pattern to control the whole clip, so nothing
        // downstream should be relying on the notes it replaces.
        scenario("note selection");
       #include "scenarios/NoteSelection.inc"
        scenario("note clipboard");
       #include "scenarios/NoteClipboard.inc"
        // After the clipboard, because it clears the pattern to build a
        // fixture of its own and leaves the clip longer than it found it.
        scenario("note gestures");
       #include "scenarios/NoteGestures.inc"
        scenario("note resize: Alt, the gesture clock and autoscroll");
       #include "scenarios/NoteResize.inc"
        scenario("note pan: the middle button carries the view");
       #include "scenarios/NotePan.inc"
        scenario("note menu: the right button asks and never erases");
       #include "scenarios/NoteMenu.inc"
        scenario("note tools: reverse, legato, humanize and the scale, from the panel beside the editor");
       #include "scenarios/NoteTools.inc"
        scenario("note keys: the keyboard stays clear of the grid and names the key under the pointer");
       #include "scenarios/NoteKeys.inc"
        scenario("note grid: bars and beats, black keys, velocity shades, snapping, key clicks and the preview");
       #include "scenarios/NoteGrid.inc"
        scenario("note drum rows: a drum clip's rows are its assigned keys");
       #include "scenarios/NoteDrumRows.inc"
        // Each builds a document of its own, so none leans on or disturbs the
        // shared clip.
        scenario("velocity lane: markers, drags and keys");
       #include "scenarios/VelocityLane.inc"
        scenario("velocity lane: drawing, the marquee and the pointer");
       #include "scenarios/VelocityLaneDraw.inc"
        scenario("velocity lane: the gesture, the divider and short panes");
       #include "scenarios/VelocityLaneLayout.inc"
        // Last, because it points the note editor at a clip of its own and
        // nothing after it would expect that.
        scenario("clip window");
       #include "scenarios/ClipWindow.inc"
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
