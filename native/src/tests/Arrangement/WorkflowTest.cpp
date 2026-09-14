#include "../../Arrangement.h"
#include "../../Theme.h"
#include "../../StepGrid.h"
#include "../../SessionView.h"
#include "../../Playhead.h"
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

namespace theta
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
        scenario("browser drops");
       #include "scenarios/BrowserDrops.inc"
        scenario("rendering");
       #include "scenarios/Rendering.inc"
        scenario("audio clip editing");
       #include "scenarios/AudioClipEditing.inc"
        scenario("track management");
       #include "scenarios/TrackManagement.inc"
        scenario("note editor");
       #include "scenarios/NoteEditor.inc"
        scenario("gestures and persistence");
       #include "scenarios/GesturesAndPersistence.inc"
        scenario("session view");
       #include "scenarios/SessionView.inc"
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
