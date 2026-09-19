#include "Session.h"
#include <cmath>

namespace rhino
{
namespace
{
constexpr double sampleRate = 48000.0;

juce::String sampleName(Session::BuiltInSample sample)
{
    switch (sample)
    {
        case Session::BuiltInSample::Whistle: return "Rhino Whistle";
        case Session::BuiltInSample::Siren:   return "Rhino Siren";
    }
    return {};
}

juce::File sampleFile(Session::BuiltInSample sample)
{
    const auto directory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino").getChildFile("Built-in audio");
    return directory.getChildFile(sampleName(sample) + ".wav");
}

float envelope(double time, double length)
{
    constexpr double fade = 0.035;
    if (time < fade)
        return static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * time / fade));
    if (time > length - fade)
        return static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * (length - time) / fade));
    return 1.0f;
}

juce::Result createSample(Session::BuiltInSample sample, const juce::File& file)
{
    if (file.existsAsFile() && file.getSize() > 44)
        return juce::Result::ok();
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("Rhino could not create its built-in audio folder.");

    const auto length = sample == Session::BuiltInSample::Whistle ? 1.5 : 4.0;
    const auto frames = static_cast<int>(length * sampleRate);
    juce::AudioBuffer<float> audio(2, frames);
    double phase = 0.0;
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto time = static_cast<double>(frame) / sampleRate;
        double frequency = 0.0;
        float value = 0.0f;
        if (sample == Session::BuiltInSample::Whistle)
        {
            frequency = 2350.0 * (1.0 + 0.018 * std::sin(juce::MathConstants<double>::twoPi * 5.7 * time));
            phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
            value = static_cast<float>(0.52 * std::sin(phase) + 0.10 * std::sin(phase * 2.0));
        }
        else
        {
            frequency = 720.0 + 230.0 * std::sin(juce::MathConstants<double>::twoPi * 0.42 * time);
            phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
            value = static_cast<float>(0.44 * std::sin(phase) + 0.06 * std::sin(phase * 3.0));
        }
        value *= envelope(time, length);
        audio.setSample(0, frame, value);
        audio.setSample(1, frame, value);
    }

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail("Rhino could not write its built-in audio sample.");
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(24));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(audio, 0, frames))
        return juce::Result::fail("Rhino could not write its built-in audio sample.");
    return juce::Result::ok();
}
}

juce::Result Session::importBuiltInSample(BuiltInSample sample, int track, double startSeconds)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto file = sampleFile(sample);
    if (const auto result = createSample(sample, file); result.failed())
        return result;
    if (startSeconds < 0.0)
        return importAudio(file);
    return importAudioAt(file, track, startSeconds);
}

// The generated samples are auditioned from the same cache, so clicking one in
// the browser renders it exactly once and every later use finds it there.
juce::Result Session::previewBuiltInSample(BuiltInSample sample)
{
    if (!previewEnabled())
        return juce::Result::ok();
    const auto file = sampleFile(sample);
    if (const auto result = createSample(sample, file); result.failed())
        return result;
    return previewSample(file);
}

// Rendered on demand into the same cache the timeline import uses, so a slot
// clip and an arrangement clip reference one file rather than two copies.
juce::Result Session::insertBuiltInSampleInSlot(BuiltInSample sample, int track, int scene)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto file = sampleFile(sample);
    if (const auto result = createSample(sample, file); result.failed())
        return result;
    return insertAudioFileInSlot(file, track, scene);
}
}
