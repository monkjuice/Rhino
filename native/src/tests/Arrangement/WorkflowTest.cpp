#include "../../Arrangement.h"
// The scenarios drive devices directly, so this runner needs the definitions
// rather than their catalog entries.
#include "instruments/DrumDevice.h"
#include "instruments/RhinoWaveDevice.h"
#include "audio/VocoderDevice.h"
#include "../../Theme.h"
#include "../../TransportDisplay.h"
#include "../../ControlBarFields.h"
#include "../../StepGrid.h"
#include "../../AudioClipPanel.h"
#include "../../SessionView.h"
#include "../../Playhead.h"
#include <algorithm>
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
        scenario("clip merge");
       #include "scenarios/ClipMerge.inc"
        scenario("region clipboard");
       #include "scenarios/RegionClipboard.inc"
        scenario("arrangement pan");
       #include "scenarios/ArrangementPan.inc"
        scenario("track management");
       #include "scenarios/TrackManagement.inc"
        scenario("group bus routing");
       #include "scenarios/GroupBusRouting.inc"
        scenario("note editor");
       #include "scenarios/NoteEditor.inc"
        // After the note editor, because reopening a project replaces the
        // document the editor is pointed at, and before "gestures and
        // persistence", which closes the audio device this has to render with.
        scenario("group bus reload");
       #include "scenarios/GroupBusReload.inc"
        // After everything that renders and before the audio device closes:
        // arming allocates a playback context to hang its input destinations
        // off, and the offline renders above run a RenderTask directly against
        // an edit that has none.
        scenario("recording");
       #include "scenarios/Recording.inc"
        scenario("vocoder carrier and midi input");
       #include "scenarios/VocoderCarrier.inc"
        scenario("gestures and persistence");
       #include "scenarios/GesturesAndPersistence.inc"
        scenario("session view");
       #include "scenarios/SessionView.inc"
        scenario("shared mixer");
       #include "scenarios/SharedMixer.inc"
        scenario("clip round trip");
       #include "scenarios/ClipRoundTrip.inc"
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
        scenario("clip edits are whole");
       #include "scenarios/ClipEdits.inc"
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
