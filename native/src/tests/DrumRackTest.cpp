#include "../Session.h"
#include "ContentLibrary.h"
#include "DrumKitFile.h"
#include "DrumRackEngine.h"
#include "instruments/DrumRackDevice.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

// What the Drum Rack claims is settled by measuring what it plays: pitch by
// zero crossings, level by peaks and RMS, brightness by a windowed transform.
// None of it reads the engine's state, so none of it can agree with the engine
// by construction. Then the two files, the library's filing of drums, and the
// session's paths onto pads and tracks, undo included.
namespace rhino
{
namespace
{
constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int firstNote = DrumRackEngine::lowestNote;

void require(bool valid, const juce::String& what)
{
    if (!valid)
        throw std::runtime_error(("Drum Rack: " + what).toStdString());
}

int at(double seconds)
{
    return static_cast<int>(seconds * rate);
}

std::unique_ptr<DrumSample> sine(double frequency, double sampleRate, double seconds, float amplitude = 0.5f)
{
    auto sample = std::make_unique<DrumSample>();
    sample->sampleRate = sampleRate;
    const auto frames = static_cast<int>(seconds * sampleRate);
    for (int i = 0; i < frames; ++i)
        sample->left.push_back(amplitude * static_cast<float>(std::sin(2.0 * pi * frequency * i / sampleRate)));
    return sample;
}

struct Played
{
    std::vector<float> left, right;
};

struct Strike
{
    int frame;
    int note;
    float velocity = 1.0f;
};

// Renders frames from the engine, striking each note at its own frame.
Played play(DrumRackEngine& engine, int frames, std::vector<Strike> strikes, const DrumRackEngine::Settings& settings)
{
    std::stable_sort(strikes.begin(), strikes.end(), [] (const Strike& a, const Strike& b) { return a.frame < b.frame; });
    Played played;
    played.left.assign(static_cast<size_t>(frames), 0.0f);
    played.right.assign(static_cast<size_t>(frames), 0.0f);
    size_t next = 0;
    for (int done = 0; done < frames;)
    {
        while (next < strikes.size() && strikes[next].frame <= done)
        {
            engine.noteOn(strikes[next].note, strikes[next].velocity, settings);
            ++next;
        }
        auto end = std::min(frames, done + 512);
        if (next < strikes.size())
            end = std::min(end, strikes[next].frame);
        engine.render(played.left.data() + done, played.right.data() + done, end - done, settings);
        done = end;
    }
    return played;
}

// Positive-going zero crossings, placed to a fraction of a sample.
double frequencyOf(const std::vector<float>& audio, int from, int to)
{
    auto first = -1.0, last = -1.0;
    auto crossings = 0;
    for (int i = std::max(1, from); i < std::min(to, static_cast<int>(audio.size())); ++i)
    {
        const auto before = audio[static_cast<size_t>(i - 1)], after = audio[static_cast<size_t>(i)];
        if (before < 0.0f && after >= 0.0f)
        {
            const auto crossing = (i - 1) + static_cast<double>(before) / (static_cast<double>(before) - after);
            if (first < 0.0)
                first = crossing;
            last = crossing;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
}

// A Hann-windowed single-frequency transform.
double magnitudeAt(const std::vector<float>& audio, int from, int count, double frequency)
{
    const auto omega = 2.0 * pi * frequency / rate;
    auto real = 0.0, imaginary = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * pi * i / (count - 1));
        const auto sample = audio[static_cast<size_t>(from + i)] * window;
        real += sample * std::cos(omega * i);
        imaginary -= sample * std::sin(omega * i);
    }
    return std::sqrt(real * real + imaginary * imaginary);
}

double rmsOf(const std::vector<float>& audio, int from, int count)
{
    auto sum = 0.0;
    for (int i = from; i < from + count; ++i)
        sum += static_cast<double>(audio[static_cast<size_t>(i)]) * audio[static_cast<size_t>(i)];
    return std::sqrt(sum / count);
}

float peakOf(const std::vector<float>& audio, int from = 0, int to = -1)
{
    auto peak = 0.0f;
    for (int i = from; i < (to < 0 ? static_cast<int>(audio.size()) : to); ++i)
        peak = std::max(peak, std::abs(audio[static_cast<size_t>(i)]));
    return peak;
}

double decibels(double ratio)
{
    return 20.0 * std::log10(std::max(1.0e-12, ratio));
}

// ---- the engine, through measurements ------------------------------------------

void checkSamplePads()
{
    DrumRackEngine::Settings settings {};

    // Untuned, at its own rate and struck at full velocity, a pad plays its
    // file exactly, on both sides, and then nothing.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        auto tone = sine(1000.0, rate, 0.1);
        const auto file = tone->left;
        engine.setPadSample(0, std::move(tone));
        const auto played = play(engine, at(0.2), {{0, firstNote}}, settings);
        for (size_t i = 0; i < file.size(); ++i)
            require(played.left[i] == file[i] && played.right[i] == file[i],
                    "an untuned pad plays its file sample for sample, frame " + juce::String(static_cast<int>(i)));
        require(peakOf(played.left, static_cast<int>(file.size())) == 0.0f && engine.activeVoices() == 0,
                "and falls silent at the file's end");
    }

    // Tune moves the pitch by its semitones and shortens the sound with it;
    // a file at another rate keeps its pitch.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSample(0, sine(1000.0, rate, 0.2));
        engine.setPadSample(1, sine(1000.0, 44100.0, 0.2));
        auto tuned = settings;
        tuned[0].tune = 12.0f;
        tuned[1].tune = -7.0f;
        const auto up = play(engine, at(0.3), {{0, firstNote}}, tuned);
        require(std::abs(frequencyOf(up.left, at(0.01), at(0.09)) - 2000.0) < 2.0,
                "a pad tuned up an octave plays its 1 kHz file at 2 kHz");
        require(peakOf(up.left, at(0.102)) == 0.0f && peakOf(up.left, 0, at(0.098)) > 0.4f,
                "and plays it in half the time");
        const auto converted = play(engine, at(0.4), {{0, firstNote + 1}}, settings);
        require(std::abs(frequencyOf(converted.left, at(0.01), at(0.19)) - 1000.0) < 1.0,
                "a file at 44.1 kHz keeps its pitch at 48 kHz");
        const auto down = play(engine, at(0.4), {{0, firstNote + 1}}, tuned);
        require(std::abs(frequencyOf(down.left, at(0.01), at(0.25)) - 1000.0 * std::pow(2.0, -7.0 / 12.0)) < 1.0,
                "and a fifth down from that, at 667 Hz");
    }

    // Decay is the time to fall 60 dB: half way there, 30 dB down.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSample(0, sine(1000.0, rate, 1.0));
        auto shaped = settings;
        shaped[0].decay = 0.1f;
        const auto played = play(engine, at(0.3), {{0, firstNote}}, shaped);
        const auto early = rmsOf(played.left, 0, at(0.002));
        const auto middle = rmsOf(played.left, at(0.049), at(0.002));
        require(std::abs(decibels(middle / early) + 29.4) < 1.5,
                "Decay 100 ms is 30 dB down at 50 ms, measured " + juce::String(decibels(middle / early), 1) + " dB");
        require(peakOf(played.left, at(0.101)) == 0.0f, "and silent once it has fallen 60 dB");
    }

    // Tone is a low-pass: at the bottom it takes 5 kHz away and leaves 50 Hz;
    // at the top it is not there at all.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSample(0, sine(5000.0, rate, 0.2));
        engine.setPadSample(1, sine(50.0, rate, 0.4));
        auto dark = settings;
        dark[0].tone = 0.0f;
        dark[1].tone = 0.0f;
        const auto open = play(engine, at(0.2), {{0, firstNote}}, settings);
        const auto closed = play(engine, at(0.2), {{0, firstNote}}, dark);
        require(decibels(rmsOf(closed.left, at(0.05), at(0.1)) / rmsOf(open.left, at(0.05), at(0.1))) < -30.0,
                "Tone at the bottom takes 5 kHz down by more than 30 dB");
        const auto lowOpen = play(engine, at(0.4), {{0, firstNote + 1}}, settings);
        const auto lowClosed = play(engine, at(0.4), {{0, firstNote + 1}}, dark);
        require(decibels(rmsOf(lowClosed.left, at(0.1), at(0.2)) / rmsOf(lowOpen.left, at(0.1), at(0.2))) > -3.0,
                "and leaves 50 Hz within 3 dB");
    }

    // Velocity, Level and Pan, by level.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSample(0, sine(1000.0, rate, 0.1));
        const auto loudest = peakOf(play(engine, at(0.1), {{0, firstNote, 1.0f}}, settings).left);
        const auto halfway = peakOf(play(engine, at(0.1), {{0, firstNote, 0.5f}}, settings).left);
        require(std::abs(halfway / loudest - 0.5f) < 1.0e-4f, "at full Velocity a note at half velocity is half as loud");
        auto deaf = settings;
        deaf[0].velocity = 0.0f;
        require(std::abs(peakOf(play(engine, at(0.1), {{0, firstNote, 0.5f}}, deaf).left) - loudest) < 1.0e-5f,
                "and with none every note is as loud as the loudest");
        auto quieter = settings;
        quieter[0].level = -6.0f;
        require(std::abs(peakOf(play(engine, at(0.1), {{0, firstNote}}, quieter).left) / loudest - 0.50119f) < 1.0e-3f,
                "Level -6 dB halves the sound");
        auto silent = settings;
        silent[0].level = DrumRackEngine::silentLevel;
        require(peakOf(play(engine, at(0.1), {{0, firstNote}}, silent).left) == 0.0f, "and at the bottom it is silence");
        auto left = settings;
        left[0].pan = -1.0f;
        const auto panned = play(engine, at(0.1), {{0, firstNote}}, left);
        require(peakOf(panned.right) == 0.0f && std::abs(peakOf(panned.left) - loudest) < 1.0e-5f,
                "panned hard left, the right side is silent and the left at full level");
        auto halfRight = settings;
        halfRight[0].pan = 0.5f;
        const auto balanced = play(engine, at(0.1), {{0, firstNote}}, halfRight);
        require(std::abs(peakOf(balanced.left) / loudest - 0.5f) < 1.0e-4f
                    && std::abs(peakOf(balanced.right) - loudest) < 1.0e-5f,
                "half right turns the left side down by half and leaves the right alone");
    }
}

void checkChokeMuteAndSolo()
{
    DrumRackEngine::Settings settings {};

    // A pad struck cuts off the others in its choke group, within a few
    // milliseconds, and leaves a pad in no group ringing.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSample(10, sine(1000.0, rate, 1.0));
        engine.setPadSample(11, sine(500.0, rate, 1.0));
        engine.setPadSample(12, sine(1500.0, rate, 1.0));
        engine.setPadChoke(10, 1);
        engine.setPadChoke(11, 1);
        const auto played = play(engine, at(0.3), {{0, firstNote + 11}, {0, firstNote + 12}, {at(0.1), firstNote + 10}},
                                 settings);
        const auto window = at(0.05);
        require(magnitudeAt(played.left, at(0.02), window, 500.0) > 100.0 * magnitudeAt(played.left, at(0.15), window, 500.0)
                    + 1.0e-9,
                "a pad in a choke group stops the other pad in it");
        require(magnitudeAt(played.left, at(0.15), window, 1000.0) > 1.0 && magnitudeAt(played.left, at(0.15), window, 1500.0) > 1.0,
                "and sounds itself, beside a pad in no group");
    }

    // A muted pad is silent; while any pad is soloed, only soloed pads sound.
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSample(0, sine(1000.0, rate, 0.2));
        engine.setPadSample(1, sine(700.0, rate, 0.2));
        engine.setPadMuted(0, true);
        require(peakOf(play(engine, at(0.1), {{0, firstNote}}, settings).left) == 0.0f, "a muted pad is silent");
        engine.setPadMuted(0, false);
        engine.setPadSoloed(1, true);
        // Both struck, the soloed pad alone is heard: sample for sample what
        // it plays struck on its own.
        const auto both = play(engine, at(0.1), {{0, firstNote}, {0, firstNote + 1}}, settings);
        engine.clear();
        const auto alone = play(engine, at(0.1), {{0, firstNote + 1}}, settings);
        require(both.left == alone.left && both.right == alone.right && peakOf(alone.left) > 0.4f,
                "a soloed pad silences the rest");
    }
}

void checkSynthPads()
{
    DrumRackEngine::Settings settings {};
    for (int index = 0; index < drumModelCount; ++index)
    {
        const auto model = static_cast<DrumModel>(index);
        const auto& info = drumModelInfo(model);
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSynth(0, model);
        auto shaped = settings;
        shaped[0].decay = info.decay;
        shaped[0].tone = info.tone;
        const auto played = play(engine, at(2.0), {{0, firstNote}}, shaped);
        const auto peak = peakOf(played.left);
        require(std::all_of(played.left.begin(), played.left.end(), [] (float sample) { return std::isfinite(sample); }),
                juce::String(info.name) + " stays finite");
        require(peak > 0.05f && peak < 2.0f, juce::String(info.name) + " sounds, and within reason: peak " + juce::String(peak, 3));
        const auto over = at(info.decay * 1.5 + 0.11);
        require(peakOf(played.left, over) == 0.0f && engine.activeVoices() == 0,
                juce::String(info.name) + " has died away by half again its Decay");
        require(played.left == played.right, juce::String(info.name) + " stands in the middle");
    }

    // A kick and a tom settle on their pitch, and Tune moves it.
    const auto settled = [&settings] (DrumModel model, float tune)
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSynth(0, model);
        auto shaped = settings;
        shaped[0].decay = 2.0f;
        shaped[0].tone = 0.0f;
        shaped[0].tune = tune;
        const auto played = play(engine, at(0.8), {{0, firstNote}}, shaped);
        return frequencyOf(played.left, at(0.35), at(0.75));
    };
    require(std::abs(settled(DrumModel::Kick, 0.0f) - 52.0) < 1.0, "the kick settles on 52 Hz");
    require(std::abs(settled(DrumModel::Kick, 12.0f) - 104.0) < 2.0, "and an octave up at Tune +12");
    require(std::abs(settled(DrumModel::Tom, -12.0f) - 52.5) < 1.0, "the tom, an octave down, settles on 52.5 Hz");

    // Decay lengthens a synth and Tone brightens it.
    const auto strike = [&settings] (DrumModel model, float decay, float tone)
    {
        DrumRackEngine engine;
        engine.prepare(rate);
        engine.setPadSynth(0, model);
        auto shaped = settings;
        shaped[0].decay = decay;
        shaped[0].tone = tone;
        return play(engine, at(1.0), {{0, firstNote}}, shaped).left;
    };
    require(decibels(rmsOf(strike(DrumModel::Kick, 0.8f, 0.4f), at(0.15), at(0.02))
                     / rmsOf(strike(DrumModel::Kick, 0.2f, 0.4f), at(0.15), at(0.02))) > 10.0,
            "a longer Decay leaves the kick more than 10 dB louder at 150 ms");
    require(magnitudeAt(strike(DrumModel::Snare, 0.3f, 1.0f), at(0.01), at(0.04), 8000.0)
                > 2.0 * magnitudeAt(strike(DrumModel::Snare, 0.3f, 0.0f), at(0.01), at(0.04), 8000.0),
            "Tone brightens the snare's wires");
}

// A sample a pad has let go of is freed once nothing plays it, and not before.
void checkSampleHandOff()
{
    DrumRackEngine::Settings settings {};
    DrumRackEngine engine;
    engine.prepare(rate);
    auto first = sine(1000.0, rate, 1.0);
    const auto* held = first.get();
    engine.setPadSample(0, std::move(first));
    std::vector<float> left(512), right(512);
    engine.noteOn(firstNote, 1.0f, settings);
    engine.render(left.data(), right.data(), 512, settings);
    require(held->playing.load() == 1, "a voice counts itself onto the sample it plays");
    engine.setPadSample(0, sine(500.0, rate, 1.0));
    require(engine.collect() == 1, "a sample let go of while a voice plays it is kept");
    // The voice rings out; the next strike plays the new sample.
    const auto after = play(engine, at(1.2), {{at(1.05), firstNote}}, settings);
    require(engine.collect() == 0, "and freed once the voice has rung out");
    require(std::abs(frequencyOf(after.left, at(1.06), at(1.15)) - 500.0) < 1.0,
            "a strike after the swap plays the new sample");
    // Nothing playing, nothing waits.
    engine.clear();
    engine.setPadSample(0, sine(250.0, rate, 0.1));
    require(engine.collect() == 0, "a sample nothing plays is freed at once");
    engine.clearPad(0);
    require(engine.padSource(0) == DrumRackEngine::Source::empty && engine.collect() == 0,
            "and an emptied pad keeps nothing");
}

// The face pictures a pad with a voice of its own, which must play what the
// pad plays.
void checkPictures()
{
    DrumRackEngine::Settings settings {};
    settings[0] = { 5.0f, 0.3f, 0.4f, 1.0f, -3.0f, 0.25f };
    settings[1] = { -2.0f, 0.25f, 0.7f, 1.0f, 0.0f, -0.5f };
    DrumRackEngine engine;
    engine.prepare(rate);
    auto tone = sine(440.0, 44100.0, 0.5);
    const auto copy = std::make_unique<DrumSample>();
    copy->left = tone->left;
    copy->sampleRate = tone->sampleRate;
    engine.setPadSample(0, std::move(tone));
    engine.setPadSynth(1, DrumModel::Cowbell);
    for (int pad = 0; pad < 2; ++pad)
    {
        const auto live = play(engine, at(0.6), {{0, firstNote + pad}}, settings);
        std::vector<float> left(static_cast<size_t>(at(0.6))), right(left.size());
        const auto frames = DrumRackEngine::renderStrike(pad == 0 ? DrumRackEngine::Source::sample : DrumRackEngine::Source::synth,
                                                         DrumModel::Cowbell, copy.get(), settings[static_cast<size_t>(pad)],
                                                         1.0f, rate, left.data(), right.data(), at(0.6));
        require(frames > 0 && frames < at(0.6), "a picture lasts as long as the strike");
        require(left == live.left && right == live.right,
                juce::String(pad == 0 ? "a sample pad's" : "a synth pad's") + " picture is exactly what it plays");
    }
}

// ---- the files ----------------------------------------------------------------

void checkFiles()
{
    DrumKit kit;
    auto kick = DrumSound::forSynth(DrumModel::Kick);
    kick.settings = { 1.0f / 3.0f, 0.1f, 1.0e-7f, 0.7f, -2.25f, -0.25f };
    kit.pads[0] = kick;
    DrumSound snare;
    snare.source = DrumRackEngine::Source::sample;
    snare.name = "Snare & \"friends\"";
    snare.sample = "library:Samples/VinylDrums/Snare/Snare 16 Warm.wav";
    snare.choke = 2;
    kit.pads[5] = snare;
    DrumKit back;
    require(DrumFiles::fromXml(*DrumFiles::toXml(kit), back).wasOk(), "a kit reads back");
    for (size_t pad = 0; pad < kit.pads.size(); ++pad)
    {
        require(back.pads[pad].has_value() == kit.pads[pad].has_value(), "a kit keeps which pads are empty");
        if (!kit.pads[pad].has_value())
            continue;
        const auto& wrote = *kit.pads[pad];
        const auto& read = *back.pads[pad];
        require(read.source == wrote.source && read.name == wrote.name && read.sample == wrote.sample
                    && read.model == wrote.model && read.settings == wrote.settings && read.choke == wrote.choke,
                "a kit's pad reads back exactly as written, pad " + juce::String(static_cast<int>(pad)));
    }
    DrumSound one;
    require(DrumFiles::fromXml(*DrumFiles::toXml(kick), one).wasOk() && one.settings == kick.settings
                && one.source == DrumRackEngine::Source::synth && one.model == DrumModel::Kick,
            "a drum preset reads back exactly as written");

    const auto rejects = [] (const char* xml, const char* what)
    {
        const auto parsed = juce::parseXML(juce::String(xml));
        require(parsed != nullptr, juce::String("the test's own XML parses: ") + what);
        DrumKit kitInto;
        DrumSound soundInto;
        const auto refused = parsed->hasTagName("RHINO_DRUM_SOUND") ? DrumFiles::fromXml(*parsed, soundInto).failed()
                                                                     : DrumFiles::fromXml(*parsed, kitInto).failed();
        require(refused, juce::String("a file ") + what + " is accepted");
    };
    rejects(R"(<SOMETHING format="1"/>)", "that is not a kit");
    rejects(R"(<RHINO_DRUM_KIT format="2"/>)", "in a format this build does not read");
    rejects(R"(<RHINO_DRUM_KIT format="1"><PAD index="16" synth="Kick"/></RHINO_DRUM_KIT>)", "with a pad past the last");
    rejects(R"(<RHINO_DRUM_KIT format="1"><PAD index="-1" synth="Kick"/></RHINO_DRUM_KIT>)", "with a pad before the first");
    rejects(R"(<RHINO_DRUM_KIT format="1"><PAD index="0" synth="Kick"/><PAD index="0" synth="Snare"/></RHINO_DRUM_KIT>)",
            "filling one pad twice");
    rejects(R"(<RHINO_DRUM_SOUND format="1" synth="Kick" sample="a.wav"/>)", "naming a synth and a sample");
    rejects(R"(<RHINO_DRUM_SOUND format="1"/>)", "naming neither");
    rejects(R"(<RHINO_DRUM_SOUND format="1" synth="Gong"/>)", "naming a synth there is not");
    rejects(R"(<RHINO_DRUM_SOUND format="1" synth="Kick" tune="high"/>)", "with a setting that is not a number");
    rejects(R"(<RHINO_DRUM_SOUND format="1" synth="Kick" choke="5"/>)", "with a choke group past the fourth");

    // Every factory kit and drum preset reads, names only samples the library
    // holds, and sets each control to a value the control can hold.
    const auto inRange = [] (const DrumRackEngine::PadSettings& settings)
    {
        return settings.tune >= -24.0f && settings.tune <= 24.0f && settings.decay >= 0.01f
            && settings.decay <= DrumRackEngine::fullDecay && settings.tone >= 0.0f && settings.tone <= 1.0f
            && settings.velocity >= 0.0f && settings.velocity <= 1.0f && settings.level >= DrumRackEngine::silentLevel
            && settings.level <= 6.0f && settings.pan >= -1.0f && settings.pan <= 1.0f;
    };
    const auto checkSound = [&inRange] (const DrumSound& sound, const juce::String& where)
    {
        if (sound.source == DrumRackEngine::Source::sample)
            require(sound.sample.startsWith("library:") && ContentLibrary::resolveStoredPath(sound.sample).existsAsFile(),
                    where + " names a sample the library holds: " + sound.sample);
        require(inRange(sound.settings), where + " sets its controls within their ranges");
    };
    auto kits = 0, presets = 0, synthesisedKits = 0, sampledKits = 0;
    for (const auto& entry : ContentLibrary::drumKits())
    {
        if (entry.user)
            continue;
        ++kits;
        DrumKit factory;
        const auto read = DrumFiles::read(entry.file, factory);
        require(read.wasOk(), entry.name + ": " + read.getErrorMessage());
        auto samples = 0, synths = 0;
        for (size_t pad = 0; pad < factory.pads.size(); ++pad)
            if (factory.pads[pad].has_value())
            {
                checkSound(*factory.pads[pad], entry.name + " pad " + juce::String(static_cast<int>(pad) + 1));
                ++(factory.pads[pad]->source == DrumRackEngine::Source::synth ? synths : samples);
            }
        synthesisedKits += samples == 0 ? 1 : 0;
        sampledKits += synths == 0 ? 1 : 0;
    }
    for (const auto& entry : ContentLibrary::drumPresets())
    {
        if (entry.user)
            continue;
        ++presets;
        DrumSound factory;
        const auto read = DrumFiles::read(entry.file, factory);
        require(read.wasOk(), entry.name + ": " + read.getErrorMessage());
        checkSound(factory, entry.name);
        require(ContentLibrary::drumTypes().contains(entry.type), entry.name + " is filed under a kind of drum");
    }
    require(kits == 8 && presets >= 20 && synthesisedKits >= 1 && sampledKits >= 1,
            "the library ships eight kits, sampled and synthesised, and drum presets: found " + juce::String(kits)
                + " kits and " + juce::String(presets) + " presets");
}

void checkLibrary()
{
    const std::pair<std::pair<const char*, const char*>, const char*> types[] {
        { { "Kick", "Kick 02 High Long Crash" }, "Kick" },
        { { "", "TR808ClosedHat" }, "Hat" },
        { { "", "HandClap-01_09" }, "Clap" },
        { { "Tom", "Low 01" }, "Tom" },
        { { "", "bd01" }, "Kick" },
        { { "Percussion", "Rimshot 02 Crisp" }, "Percussion" },
        { { "", "Ride Bell" }, "Cymbal" },
        { { "", "Whistle" }, "" },
        { { "", "What that is" }, "" },
    };
    for (const auto& [names, type] : types)
        require(ContentLibrary::drumTypeOf(names.first, names.second) == juce::String(type),
                juce::String(names.second) + " is filed as a " + (juce::String(type).isEmpty() ? "nothing" : type));

    if (ContentLibrary::isAvailable())
    {
        const auto kick = ContentLibrary::file("Samples/TR808/TR808Kick.wav");
        require(ContentLibrary::storedPath(kick) == "library:Samples/TR808/TR808Kick.wav"
                    && ContentLibrary::resolveStoredPath(ContentLibrary::storedPath(kick)) == kick,
                "a sound in the library is named by its place in the library");
    }
    const juce::TemporaryFile outside(".wav");
    require(ContentLibrary::storedPath(outside.getFile()) == outside.getFile().getFullPathName()
                && ContentLibrary::resolveStoredPath(outside.getFile().getFullPathName()) == outside.getFile(),
            "a sound anywhere else is named by its full path");
    require(ContentLibrary::resolveStoredPath("Samples/relative.wav") == juce::File(),
            "a path that is neither names nothing");
}

// ---- through the session ---------------------------------------------------------

DrumRackDevice* rackOn(Session& session, int track, int* slot = nullptr)
{
    for (const auto& device : session.deviceSlots(track))
        if (device.deviceId == "Drums")
        {
            if (slot != nullptr)
                *slot = device.pluginIndex;
            return dynamic_cast<DrumRackDevice*>(session.devicePlugin(track, device.pluginIndex));
        }
    return nullptr;
}

void checkThroughSession()
{
    Session stack;
    constexpr int midi = 0, audio = 1, otherMidi = 2;
    const auto eightOhEight = ContentLibrary::file("Drums/Kits/808 Kit.rdk");
    require(stack.addDrumKit(eightOhEight, audio).failed(), "an audio track refuses a kit");
    require(stack.addDrumKit(eightOhEight, midi).wasOk(), "a MIDI track takes one");
    int slot = -1;
    auto* rack = rackOn(stack, midi, &slot);
    require(rack != nullptr && rack->pad(0).sound.has_value() && rack->pad(0).sound->name == "Kick"
                && stack.trackName(midi) == "808 Kit",
            "the kit fills the rack and names the track");
    stack.undo();
    require(rackOn(stack, midi) == nullptr && stack.trackName(midi) == "MIDI",
            "one undo takes the rack and its kit away together");
    stack.redo();
    rack = rackOn(stack, midi, &slot);
    require(rack != nullptr && rack->pad(5).sound.has_value(), "and redo brings them back");

    // A sample on a pad, one undo step. Another sample in its place keeps the
    // pad's tuning; a sample in place of a synth starts from the defaults.
    const auto warm = ContentLibrary::file("Samples/VinylDrums/Snare/Snare 16 Warm.wav");
    const auto bright = ContentLibrary::file("Samples/VinylDrums/Snare/Snare 21 Bright.wav");
    require(stack.loadDrumPadSample(midi, slot, 3, warm).wasOk(), "a sample drops on a pad");
    require(rack->pad(3).sound.has_value()
                && rack->pad(3).sound->sample == "library:Samples/VinylDrums/Snare/Snare 16 Warm.wav"
                && rack->selectedPad() == 3 && !rack->pad(3).unreadable,
            "and the pad plays it, selected");
    require(stack.setDeviceParameter(midi, slot, DrumRackDevice::parameterIndex(3, DrumRackDevice::tune), 5.0f).wasOk(),
            "the pad can be tuned");
    require(stack.loadDrumPadSample(midi, slot, 3, bright).wasOk() && rack->pad(3).sound->settings.tune == 5.0f
                && rack->pad(3).sound->sample.contains("Snare 21"),
            "a sample replacing a sample keeps the pad's tuning");
    stack.undo();
    require(rack->pad(3).sound->sample.contains("Snare 16"), "and the replacement is one undo step");
    stack.editDeviceSettings(midi, slot, "Make pad 5 a tom", [rack] { rack->setPadSynth(4, DrumModel::Tom); });
    require(rack->pad(4).sound.has_value() && rack->pad(4).sound->source == DrumRackEngine::Source::synth
                && std::abs(rack->pad(4).sound->settings.decay - drumModelInfo(DrumModel::Tom).decay) < 1.0e-6f,
            "a pad switched to a synth starts at the model's own Decay");
    require(stack.setDeviceParameter(midi, slot, DrumRackDevice::parameterIndex(4, DrumRackDevice::tune), 7.0f).wasOk()
                && stack.setDeviceParameter(midi, slot, DrumRackDevice::parameterIndex(4, DrumRackDevice::level), -6.0f).wasOk()
                && stack.loadDrumPadSample(midi, slot, 4, warm).wasOk(),
            "a tuned, quieter synth pad takes a sample");
    require(rack->pad(4).sound->settings.tune == 0.0f && rack->pad(4).sound->settings.decay == DrumRackEngine::fullDecay
                && rack->pad(4).sound->settings.level == -6.0f,
            "which starts untuned and plays out, at the level the pad had");
    const juce::TemporaryFile notSound(".txt");
    require(notSound.getFile().replaceWithText("not a sound") && stack.loadDrumPadSample(midi, slot, 9, notSound.getFile()).failed()
                && !rack->pad(9).sound.has_value(),
            "a file that is not a sound is refused, and the pad stays as it was");

    // Mute is an undo step, and the note editor names the rows.
    stack.editDeviceSettings(midi, slot, "Mute pad", [rack] { rack->setPadMuted(0, true); });
    require(rack->pad(0).muted, "a pad mutes");
    stack.undo();
    require(!rack->pad(0).muted, "and unmutes with an undo");

    // A drum preset on a pad, saved back out, and an empty pad that has
    // nothing to save.
    const auto subKick = ContentLibrary::file("Drums/Presets/Kick/Sub Kick.rdp");
    require(stack.loadDrumPadPreset(midi, slot, 12, subKick).wasOk(), "a drum preset drops on a pad");
    const auto sub = rack->pad(12).sound;
    require(sub.has_value() && sub->source == DrumRackEngine::Source::synth && sub->model == DrumModel::Kick
                && sub->name == "Sub Kick" && sub->settings.tune == -5.0f,
            "and the pad is that sound, named for the preset");
    const juce::TemporaryFile savedSound(DrumFiles::soundExtension);
    require(stack.saveDrumPadPreset(midi, slot, 12, savedSound.getFile()).wasOk(), "a pad saves as a drum preset");
    DrumSound reread;
    require(DrumFiles::read(savedSound.getFile(), reread).wasOk() && reread.settings == sub->settings
                && reread.model == sub->model && reread.name == sub->name,
            "and the file holds the pad's sound");
    require(stack.saveDrumPadPreset(midi, slot, 15, savedSound.getFile()).failed(), "an empty pad has nothing to save");

    // The person's own kits and drum presets are found under their own Drums
    // folder, a preset filed by the kind of drum its folder names.
    {
        const auto own = juce::File::createTempFile("").getSiblingFile("RhinoDrumsTest" + juce::String(juce::Random().nextInt(1 << 30)));
        require(stack.saveDrumKit(midi, slot, own.getChildFile("Kits").getChildFile("Mine.rdk")).wasOk()
                    && stack.saveDrumPadPreset(midi, slot, 12, own.getChildFile("Presets").getChildFile("Kick")
                                                                     .getChildFile("My Kick.rdp")).wasOk(),
                "a kit and a drum preset save into folders that do not exist yet");
        const auto kits = ContentLibrary::drumKitsIn(own, true);
        const auto presets = ContentLibrary::drumPresetsIn(own, true);
        require(kits.size() == 1 && kits[0].name == "Mine" && kits[0].user && kits[0].type.isEmpty(),
                "a saved kit is found by its file name");
        require(presets.size() == 1 && presets[0].name == "My Kick" && presets[0].type == "Kick" && presets[0].user,
                "and a saved drum preset under the kind of drum it is filed as");
        own.deleteRecursively();
    }

    // A kit saved and loaded back is the same kit.
    const juce::TemporaryFile savedKit(DrumFiles::kitExtension);
    require(stack.saveDrumKit(midi, slot, savedKit.getFile()).wasOk(), "a rack saves as a kit");
    const auto before = rack->kit();
    require(stack.loadDrumKit(midi, slot, ContentLibrary::file("Drums/Kits/Analog Kit.rdk")).wasOk()
                && rack->pad(3).sound->source == DrumRackEngine::Source::synth,
            "another kit replaces every pad");
    require(stack.loadDrumKit(midi, slot, savedKit.getFile()).wasOk(), "the saved kit loads back");
    const auto after = rack->kit();
    for (size_t pad = 0; pad < before.pads.size(); ++pad)
        require(before.pads[pad].has_value() == after.pads[pad].has_value()
                    && (!before.pads[pad].has_value()
                        || (before.pads[pad]->sample == after.pads[pad]->sample
                            && before.pads[pad]->settings == after.pads[pad]->settings
                            && before.pads[pad]->name == after.pads[pad]->name)),
                "a kit saved and loaded back is the same kit, pad " + juce::String(static_cast<int>(pad) + 1));

    // Drum sounds dropped on a track fill its rack's empty pads in turn,
    // bringing a blank rack to a MIDI track that has none.
    require(stack.addDrumSound(warm, otherMidi).failed(), "a bare sample does not replace what a track plays");
    require(stack.addDrumSound(subKick, otherMidi).wasOk() && stack.trackHasDrumRack(otherMidi),
            "a drum preset brings a Drum Rack to a MIDI track");
    int otherSlot = -1;
    auto* second = rackOn(stack, otherMidi, &otherSlot);
    require(second != nullptr && second->pad(0).sound.has_value() && second->pad(0).sound->name == "Sub Kick"
                && !second->pad(1).sound.has_value(),
            "on its first pad");
    require(stack.addDrumSound(warm, otherMidi).wasOk() && second->pad(1).sound.has_value(),
            "and the next sound lands on the next pad");
    require(stack.addDrumSound(subKick, 3).failed(), "an audio track refuses a drum sound");

    // A drum pattern brings the 808 kit to a blank rack, and plays whatever a
    // filled one holds.
    require(stack.addTrack(Session::TrackType::midi).wasOk(), "a fresh MIDI track");
    const auto fresh = stack.trackCount() - 1;
    require(stack.addInstrument(Session::Instrument::Drums, fresh).wasOk() && rackOn(stack, fresh) != nullptr
                && rackOn(stack, fresh)->isBlank(),
            "a Drum Rack added on its own is blank");
    require(stack.insertPatternPreset(Session::PatternPreset::HouseKit, fresh, 0.0).wasOk()
                && rackOn(stack, fresh)->pad(0).sound.has_value() && rackOn(stack, fresh)->pad(0).sound->name == "Kick",
            "a drum pattern on a blank rack brings the kit it was written for");
    require(stack.insertPatternPreset(Session::PatternPreset::HouseKit, otherMidi, 0.0).wasOk()
                && second->pad(0).sound->name == "Sub Kick",
            "and leaves a filled rack's sounds alone");

    // The note editor names a rack's rows after its pads.
    te::EditItemID patternClip;
    for (auto* clip : te::getAudioTracks(*stack.edit)[fresh]->getClips())
        patternClip = clip->itemID;
    require(stack.selectPatternClip(patternClip).wasOk() && stack.isPatternDrums(), "the drum pattern opens in the editor");
    require(stack.patternNoteName(48) == std::optional<juce::String>("Kick")
                && stack.patternNoteName(53) == std::optional<juce::String>("Snare")
                && !stack.patternNoteName(60).has_value(),
            "whose rows are named for the pads, and an empty pad's row for nothing");

    // A drum clip moved or pasted to a track with no instrument brings the
    // rack it played on, sounds and all, not a blank one.
    {
        int freshSlot = -1;
        require(rackOn(stack, fresh, &freshSlot) != nullptr && stack.loadDrumPadPreset(fresh, freshSlot, 0, subKick).wasOk(),
                "the drum track's kick becomes Sub Kick");
        require(stack.addTrack(Session::TrackType::midi).wasOk() && stack.addTrack(Session::TrackType::midi).wasOk(),
                "two more MIDI tracks");
        const auto pastedTo = stack.trackCount() - 2, movedTo = stack.trackCount() - 1;
        const auto bar = 4.0 * 60.0 / stack.tempo();
        std::vector<te::EditItemID> pasted;
        require(stack.pasteClipRegion(stack.copyClipRegion(0.0, bar, fresh, fresh), 0.0, pastedTo, pasted).wasOk()
                    && rackOn(stack, pastedTo) != nullptr && rackOn(stack, pastedTo)->pad(0).sound.has_value()
                    && rackOn(stack, pastedTo)->pad(0).sound->name == "Sub Kick",
                "a pasted drum clip brings its rack's kit");
        auto* drumClip = te::getAudioTracks(*stack.edit)[fresh]->getClips().getFirst();
        require(drumClip != nullptr, "the drum track holds its clip");
        const auto where = drumClip->getPosition();
        require(stack.editClip(drumClip->itemID, {where.time.getStart().inSeconds(), where.time.getEnd().inSeconds(),
                                                  where.offset.inSeconds()},
                               ClipGesture::move, movedTo).wasOk()
                    && rackOn(stack, movedTo) != nullptr && rackOn(stack, movedTo)->pad(0).sound.has_value()
                    && rackOn(stack, movedTo)->pad(0).sound->name == "Sub Kick"
                    && rackOn(stack, movedTo)->pad(5).sound->name == "Snare",
                "and so does a moved one");
    }

    // Everything survives saving and reopening.
    juce::TemporaryFile project(".rhinoedit");
    require(stack.restoreProject(stack.projectSnapshot(), project.getFile()).wasOk(), "the stack reopens");
    auto* reopened = rackOn(stack, midi);
    require(reopened != nullptr && reopened->pad(12).sound.has_value() && reopened->pad(12).sound->name == "Sub Kick"
                && reopened->pad(3).sound.has_value() && reopened->pad(3).sound->sample.contains("Snare 16")
                && !reopened->pad(3).unreadable,
            "and the pads come back, samples read again");
}

// What one strike costs to render, per model and for a sample, in nanoseconds
// a sample: the audio thread pays it per voice, and a face per picture.
void logStrikeCosts()
{
    std::vector<float> left(static_cast<size_t>(at(2.0))), right(left.size());
    juce::String line = "Rhino: Drum Rack strike cost, ns a sample:";
    const auto time = [&left, &right] (DrumRackEngine::Source source, DrumModel model, const DrumSample* sample,
                                       const DrumRackEngine::PadSettings& settings)
    {
        std::vector<double> costs;
        for (int round = 0; round < 9; ++round)
        {
            const auto start = juce::Time::getHighResolutionTicks();
            const auto frames = DrumRackEngine::renderStrike(source, model, sample, settings, 1.0f, rate, left.data(),
                                                             right.data(), at(2.0));
            const auto seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
            costs.push_back(seconds * 1.0e9 / std::max(1, frames));
        }
        std::nth_element(costs.begin(), costs.begin() + 4, costs.end());
        return costs[4];
    };
    for (int index = 0; index < drumModelCount; ++index)
    {
        const auto model = static_cast<DrumModel>(index);
        DrumRackEngine::PadSettings settings;
        settings.decay = 0.5f;
        settings.tone = drumModelInfo(model).tone;
        line << " " << drumModelInfo(model).id << " " << juce::String(time(DrumRackEngine::Source::synth, model, nullptr, settings), 1);
    }
    const auto tone = sine(1000.0, 44100.0, 1.0);
    DrumRackEngine::PadSettings shaped;
    shaped.tune = 3.0f;
    shaped.decay = 0.5f;
    shaped.tone = 0.5f;
    line << " sample " << juce::String(time(DrumRackEngine::Source::sample, DrumModel::Kick, tone.get(), shaped), 1);
    juce::Logger::writeToLog(line);
}

}

void checkDrumRack(Session&)
{
    logStrikeCosts();
    checkSamplePads();
    checkChokeMuteAndSolo();
    checkSynthPads();
    checkSampleHandOff();
    checkPictures();
    checkFiles();
    checkLibrary();
    checkThroughSession();
    juce::Logger::writeToLog("Rhino: Drum Rack checks passed");
}
}
