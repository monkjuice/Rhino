#include "DuckTestTools.h"
#include "DuckTestSupport.h"
#include "../src/DuckEditor.h"

#include <iostream>

namespace rhino::duck::tests
{
int runSnapshot(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: --snapshot <out.png> [SYNC|MIDI|AUDIO] [BAND] [SOLO] [ONESHOT] [SHAPED] [CURVE=<1-16>] [SECONDS=<s>] [SCALE=<factor>]\n";
        return 2;
    }
    Processor processor;
    auto shaped = false;
    auto scale = 1.0f;
    auto seconds = 2.0;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String option = juce::String(argv[i]).toUpperCase();
        if (option == "MIDI") setValue(processor, id::trigger, 1.0f);
        if (option == "AUDIO") setValue(processor, id::trigger, 2.0f);
        if (option == "BAND") setValue(processor, id::band, 1.0f);
        if (option == "SOLO") setValue(processor, id::bandSolo, 1.0f);
        if (option == "ONESHOT") setValue(processor, id::loop, 0.0f);
        if (option == "SHAPED") shaped = true;
        if (option.startsWith("CURVE=")) setValue(processor, id::curve, static_cast<float>(option.fromFirstOccurrenceOf("=", false, false).getIntValue() - 1));
        if (option.startsWith("SCALE=")) scale = option.fromFirstOccurrenceOf("=", false, false).getFloatValue();
        if (option.startsWith("SECONDS=")) seconds = option.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
    }
    // A curve bent by every knob on the strip, to see them all off their
    // defaults at once.
    if (shaped)
    {
        const auto curve = juce::roundToInt(value(processor, id::curve));
        processor.setCurveAttack(curve, 6.0f);
        processor.setCurveRelease(curve, slopedPart(curve, {}).length * 0.7f);
        processor.setCurveHold(curve, slopedPart(curve, processor.curveAdjust(curve)).start + 0.03f);
        processor.setCurveDepth(curve, 0.8f);
    }

    // Two seconds (or SECONDS) of a bassline under a four-to-the-floor kick
    // at 124 bpm, the kick on the sidechain and, in MIDI mode, as notes too.
    TestPlayHead head;
    head.bpm = 124.0;
    constexpr int block = 480;
    const auto frames = static_cast<int>(testRate * std::clamp(seconds, 0.1, 30.0));
    const auto beat = static_cast<int>(testRate * 60.0 / head.bpm);
    std::vector<float> bass(static_cast<size_t>(frames)), kicks(static_cast<size_t>(frames), 0.0f);
    for (int i = 0; i < frames; ++i)
    {
        const auto t = i / testRate;
        const auto fundamental = std::sin(2.0 * 3.14159265358979323846 * 55.0 * t);
        const auto harmonics = 0.3 * std::sin(2.0 * 3.14159265358979323846 * 110.0 * t) + 0.15 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * t);
        bass[static_cast<size_t>(i)] = static_cast<float>(0.55 * (fundamental + harmonics));
    }
    for (int at = 0; at < frames; at += beat) addKick(kicks, at, 0.8f);

    processor.setPlayHead(&head);
    processor.prepareToPlay(testRate, block);
    // The panel is open while the audio runs, so the duck meter moves with
    // it, a frame per block, as its own clock would move it.
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* panel = dynamic_cast<Editor*>(editor.get());
    juce::AudioBuffer<float> buffer(4, block);
    for (int start = 0; start + block <= frames; start += block)
    {
        buffer.clear();
        for (int channel = 0; channel < 2; ++channel) buffer.copyFrom(channel, 0, bass.data() + start, block);
        for (int channel = 2; channel < 4; ++channel) buffer.copyFrom(channel, 0, kicks.data() + start, block);
        juce::MidiBuffer midi;
        for (int at = 0; at < frames; at += beat)
            if (at >= start && at < start + block) midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), at - start);
        head.ppq = start / testRate * head.bpm / 60.0;
        processor.processBlock(buffer, midi);
        if (panel != nullptr) panel->advanceMeter(static_cast<float>(block / testRate));
    }

    if (panel != nullptr) panel->refresh();
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    // Deleted first: JUCE opens an existing file for output at its end.
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream))
    {
        std::cerr << "snapshot: could not write " << file.getFullPathName() << '\n';
        return 1;
    }
    std::cout << "wrote " << file.getFullPathName() << '\n';
    return 0;
}
}
