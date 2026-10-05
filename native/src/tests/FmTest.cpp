#include "../Session.h"
#include "FmEngine.h"
#include "instruments/RhinoFmDevice.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

// What Rhino FM claims is settled by measuring what it plays.
//
// The strongest of these is the classic one. A sine phase-modulated by another
// at an index of beta puts J_n(beta) of the carrier's amplitude on each
// sideband. With the modulator four times the carrier's frequency, the first
// sidebands land on the third and fifth harmonics and the second on the
// seventh and ninth, with nothing folding onto anything else. So the ratio of
// the fifth harmonic to the fundamental has to be J1(beta) / J0(beta), and the
// test works beta out from the engine's published scale, not from its
// arithmetic. Get the index wrong by any factor and the ratio says so.
//
// The rest measure pitch by zero crossings, envelopes by RMS, and brightness by
// a windowed single-frequency transform. None of them reads the engine's
// state, so none of them can agree with it by construction.
namespace rhino
{
namespace
{
constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;

void require(bool valid, const juce::String& what)
{
    if (!valid)
        throw std::runtime_error(("Rhino FM: " + what).toStdString());
}

struct Event
{
    int frame;
    std::function<void(FmEngine&)> action;
};

// Renders frames of mono output, applying each event at its own frame.
std::vector<float> play(FmEngine& engine, int frames, std::vector<Event> events)
{
    std::stable_sort(events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.frame < b.frame; });
    std::vector<float> audio(static_cast<size_t>(frames), 0.0f);
    size_t next = 0;
    for (int done = 0; done < frames;)
    {
        while (next < events.size() && events[next].frame <= done)
            events[next++].action(engine);
        auto end = frames;
        if (next < events.size())
            end = std::min(end, events[next].frame);
        engine.render(audio.data() + done, nullptr, end - done);
        done = end;
    }
    return audio;
}

Event noteOn(double seconds, int note, float velocity = 1.0f)
{
    return { static_cast<int>(seconds * rate), [note, velocity] (FmEngine& engine) { engine.noteOn(note, velocity); } };
}

Event noteOff(double seconds, int note)
{
    return { static_cast<int>(seconds * rate), [note] (FmEngine& engine) { engine.noteOff(note); } };
}

int at(double seconds)
{
    return static_cast<int>(seconds * rate);
}

// Positive-going zero crossings, placed to a fraction of a sample. Only
// meaningful on a signal that crosses once a cycle: a lone sine.
double frequencyOf(const std::vector<float>& audio, int from, int to)
{
    auto first = -1.0, last = -1.0;
    auto crossings = 0;
    for (int i = std::max(1, from); i < to; ++i)
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

// Every operator a carrier and silent, so a test turns on only what it
// measures. Notes hold at full level with no attack or decay, and velocity
// changes nothing.
FmEngine::Settings plain()
{
    FmEngine::Settings settings;
    settings.algorithm = FmEngine::algorithms - 1;
    settings.velocity = 0.0f;
    settings.gain = 1.0f;
    for (auto& op : settings.ops)
        op = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.05f };
    return settings;
}

FmEngine prepared(const FmEngine::Settings& settings)
{
    FmEngine engine;
    engine.prepare(rate);
    engine.setSettings(settings);
    return engine;
}

juce::String describe(double value, int places = 4)
{
    return juce::String(value, places);
}

void checkPitch()
{
    const auto measure = [] (float ratio, float detuneCents, int note)
    {
        auto settings = plain();
        settings.ops[0].level = 1.0f;
        settings.ops[0].ratio = ratio;
        settings.ops[0].detuneCents = detuneCents;
        auto engine = prepared(settings);
        const auto audio = play(engine, at(1.0), { noteOn(0.0, note) });
        return frequencyOf(audio, at(0.1), at(1.0));
    };
    const auto requirePitch = [] (double measured, double wanted, const juce::String& what)
    {
        require(std::abs(measured - wanted) / wanted < 1.0e-4,
                what + " plays " + describe(measured) + " Hz, wanted " + describe(wanted) + " Hz");
    };
    requirePitch(measure(1.0f, 0.0f, 69), 440.0, "A4");
    requirePitch(measure(2.0f, 0.0f, 69), 880.0, "A4 at ratio 2");
    requirePitch(measure(0.5f, 0.0f, 69), 220.0, "A4 at ratio 0.5");
    requirePitch(measure(1.0f, 100.0f, 69), 440.0 * std::exp2(1.0 / 12.0), "A4 detuned 100 cents");
    requirePitch(measure(1.0f, 0.0f, 60), 440.0 * std::exp2(-9.0 / 12.0), "C4");
}

void checkPureSine()
{
    auto settings = plain();
    settings.ops[0].level = 1.0f;
    auto engine = prepared(settings);
    const auto audio = play(engine, at(1.0), { noteOn(0.0, 69) });
    const auto fundamental = magnitudeAt(audio, at(0.5), at(0.5), 440.0);
    for (const auto harmonic : { 2, 3, 5 })
    {
        const auto relative = magnitudeAt(audio, at(0.5), at(0.5), 440.0 * harmonic) / fundamental;
        require(relative < 3.0e-5, "an unmodulated operator has harmonic " + juce::String(harmonic) + " at "
                                       + describe(20.0 * std::log10(relative + 1.0e-20), 1) + " dB");
    }
}

// The index, measured against Bessel functions. Beta is what the engine's
// published scale says a modulator level should give: modulationCycles of
// phase per unit of output, an output of level squared, times the depth.
void checkModulationIndex()
{
    const auto ratioFor = [] (float level, float depth, int note)
    {
        auto settings = plain();
        settings.algorithm = 0;          // 4>3>2>1, with 3 and 4 silent: 2 into 1
        settings.depth = depth;
        settings.ops[0].level = 1.0f;
        settings.ops[1].level = level;
        settings.ops[1].ratio = 4.0f;
        auto engine = prepared(settings);
        const auto audio = play(engine, at(1.0), { noteOn(0.0, note) });
        const auto frequency = 440.0 * std::exp2((note - 69) / 12.0);
        const auto first = magnitudeAt(audio, at(0.5), at(0.5), frequency);
        return std::pair { magnitudeAt(audio, at(0.5), at(0.5), frequency * 5.0) / first,
                           magnitudeAt(audio, at(0.5), at(0.5), frequency * 7.0) / first };
    };
    const auto betaFor = [] (float level, float depth)
    {
        return 2.0 * pi * FmEngine::modulationCycles * level * level * depth;
    };
    // Beta of 1, 2 and 1.9, all clear of J0's first zero at 2.405, where the
    // fundamental vanishes and any ratio to it is noise.
    for (const auto& [level, depth] : { std::pair { 0.3257f, 1.0f }, std::pair { 0.3257f, 2.0f },
                                        std::pair { 0.45f, 1.0f } })
    {
        const auto beta = betaFor(level, depth);
        const auto [fifth, seventh] = ratioFor(level, depth, 69);
        const auto wantedFifth = std::abs(std::cyl_bessel_j(1.0, beta) / std::cyl_bessel_j(0.0, beta));
        const auto wantedSeventh = std::abs(std::cyl_bessel_j(2.0, beta) / std::cyl_bessel_j(0.0, beta));
        require(std::abs(fifth - wantedFifth) < 0.02 * wantedFifth + 1.0e-4,
                "at beta " + describe(beta, 3) + " the fifth harmonic is " + describe(fifth)
                    + " of the fundamental, Bessel says " + describe(wantedFifth));
        require(std::abs(seventh - wantedSeventh) < 0.02 * wantedSeventh + 1.0e-4,
                "at beta " + describe(beta, 3) + " the seventh harmonic is " + describe(seventh)
                    + " of the fundamental, Bessel says " + describe(wantedSeventh));
    }

    // Above sampleRate / 24 a modulator's depth falls with the note, so the
    // highest C has less than half the index the same patch has at A4.
    const auto beta = betaFor(0.3257f, 1.0f);
    const auto note = 108;
    const auto frequency = 440.0 * std::exp2((note - 69) / 12.0);
    const auto taperedBeta = beta * std::min(1.0, (rate / 24.0) / frequency);
    const auto [fifth, seventh] = ratioFor(0.3257f, 1.0f, note);
    juce::ignoreUnused(seventh);
    const auto wanted = std::abs(std::cyl_bessel_j(1.0, taperedBeta) / std::cyl_bessel_j(0.0, taperedBeta));
    require(std::abs(fifth - wanted) < 0.03 * wanted,
            "at C8 the fifth harmonic is " + describe(fifth) + " of the fundamental, the taper says " + describe(wanted));
}

// More modulation is a brighter tone, all the way up. Brightness is the
// spectrum's centroid, in harmonics: a ratio to the fundamental would not do,
// because the fundamental itself passes through zero as the index grows.
void checkBrightness()
{
    auto last = 0.0;
    for (const auto level : { 0.0f, 0.3f, 0.6f, 0.9f, 1.0f })
    {
        auto settings = plain();
        settings.algorithm = 0;
        settings.ops[0].level = 1.0f;
        settings.ops[1].level = level;
        auto engine = prepared(settings);
        const auto audio = play(engine, at(1.0), { noteOn(0.0, 57) });
        auto weighted = 0.0, total = 0.0;
        for (int harmonic = 1; harmonic <= 24; ++harmonic)
        {
            const auto magnitude = magnitudeAt(audio, at(0.5), at(0.5), 220.0 * harmonic);
            weighted += harmonic * magnitude;
            total += magnitude;
        }
        const auto centroid = weighted / total;
        require(centroid > last + 0.2, "a modulator at " + describe(level, 2) + " centres the spectrum on harmonic "
                                           + describe(centroid, 2) + ", no brighter than the one below it");
        last = centroid;
    }
}

// Feedback turns operator 4 from a sine into something close to a saw, and it
// stays periodic: no energy between the harmonics.
void checkFeedback()
{
    const auto spectrum = [] (float feedback)
    {
        auto settings = plain();
        settings.feedback = feedback;
        settings.ops[3].level = 1.0f;
        auto engine = prepared(settings);
        const auto audio = play(engine, at(1.0), { noteOn(0.0, 69) });
        const auto first = magnitudeAt(audio, at(0.5), at(0.5), 440.0);
        return std::pair { magnitudeAt(audio, at(0.5), at(0.5), 880.0) / first,
                           magnitudeAt(audio, at(0.5), at(0.5), 660.0) / first };
    };
    const auto [secondWithout, betweenWithout] = spectrum(0.0f);
    juce::ignoreUnused(betweenWithout);
    require(secondWithout < 3.0e-5, "operator 4 without feedback is not a sine");
    // A saw's second harmonic is half its fundamental.
    const auto [second, between] = spectrum(1.0f);
    require(second > 0.45 && second < 0.6, "full feedback puts the second harmonic at " + describe(second)
                                               + " of the fundamental, wanted a saw's half");
    require(between < 1.0e-3, "full feedback puts " + describe(between) + " of the fundamental between harmonics");
}

void checkEnvelopes()
{
    auto settings = plain();
    settings.ops[0].level = 1.0f;
    settings.ops[0].attack = 0.1f;
    settings.ops[0].release = 0.2f;
    auto engine = prepared(settings);
    const auto audio = play(engine, at(1.0), { noteOn(0.0, 69), noteOff(0.3, 69) });
    const auto steady = rmsOf(audio, at(0.2), at(0.1));
    const auto halfway = rmsOf(audio, at(0.045), at(0.01)) / steady;
    require(std::abs(halfway - 0.5) < 0.05, "half way through a linear attack the level is " + describe(halfway, 3));
    const auto afterRelease = rmsOf(audio, at(0.495), at(0.01)) / steady;
    const auto releasedDb = 20.0 * std::log10(afterRelease);
    require(releasedDb < -55.0 && releasedDb > -65.0,
            "a 0.2 s release is at " + describe(releasedDb, 1) + " dB 0.2 s after the key, wanted -60");
    require(engine.activeVoices() == 0, "a released voice comes free");
}

void checkVelocity()
{
    const auto levelAt = [] (float sensitivity, float velocity)
    {
        auto settings = plain();
        settings.velocity = sensitivity;
        settings.ops[0].level = 1.0f;
        auto engine = prepared(settings);
        const auto audio = play(engine, at(0.5), { noteOn(0.0, 69, velocity) });
        return rmsOf(audio, at(0.25), at(0.2));
    };
    const auto ratio = levelAt(1.0f, 0.25f) / levelAt(1.0f, 1.0f);
    require(std::abs(ratio - 0.25) < 0.0025, "at full sensitivity, velocity 0.25 plays at " + describe(ratio) + " of full");
    require(std::abs(levelAt(0.0f, 0.25f) / levelAt(0.0f, 1.0f) - 1.0) < 1.0e-6, "at no sensitivity, velocity matters");
}

void checkPolyphonyAndPedal()
{
    auto settings = plain();
    settings.ops[0].level = 1.0f;
    {
        auto engine = prepared(settings);
        std::vector<Event> chord;
        for (int note = 60; note < 80; ++note)
            chord.push_back(noteOn(0.0, note));
        const auto audio = play(engine, at(0.2), chord);
        require(engine.activeVoices() == FmEngine::maxVoices, "twenty keys sound on " + juce::String(engine.activeVoices())
                                                                  + " voices, wanted every voice");
        require(std::all_of(audio.begin(), audio.end(), [] (float sample) { return std::isfinite(sample); }),
                "a full chord stays finite");
    }
    {
        auto engine = prepared(settings);
        const auto pedal = [] (double seconds, bool down)
        {
            return Event { at(seconds), [down] (FmEngine& fm) { fm.setSustainPedal(down); } };
        };
        const auto audio = play(engine, at(1.0), { pedal(0.0, true), noteOn(0.0, 69), noteOff(0.1, 69),
                                                    pedal(0.5, false) });
        require(rmsOf(audio, at(0.4), at(0.1)) / rmsOf(audio, at(0.05), at(0.05)) > 0.99,
                "a note let go under the pedal keeps sounding");
        require(engine.activeVoices() == 0 && rmsOf(audio, at(0.9), at(0.1)) < 1.0e-7,
                "lifting the pedal releases it");
    }
}

// In mono, an overlapping note glides there without a new attack, and
// letting it go glides back to the key still down.
void checkMonoGlide()
{
    auto settings = plain();
    settings.mono = true;
    settings.glide = 0.03f;
    settings.ops[0].level = 1.0f;
    settings.ops[0].decay = 0.05f;
    settings.ops[0].sustain = 0.5f;
    auto engine = prepared(settings);
    const auto audio = play(engine, at(1.2), { noteOn(0.0, 60), noteOn(0.2, 72), noteOff(0.6, 72) });
    const auto settled = rmsOf(audio, at(0.15), at(0.04));
    const auto afterLegato = rmsOf(audio, at(0.21), at(0.04)) / settled;
    require(std::abs(afterLegato - 1.0) < 0.02, "a legato note restarted the envelope: its level moved to "
                                                    + describe(afterLegato, 3) + " of the held one");
    const auto up = frequencyOf(audio, at(0.45), at(0.6));
    require(std::abs(up - 523.2511) / 523.2511 < 1.0e-3, "mono glides to " + describe(up) + " Hz, wanted 523.25");
    const auto back = frequencyOf(audio, at(0.95), at(1.2));
    require(std::abs(back - 261.6256) / 261.6256 < 1.0e-3, "mono returns to " + describe(back) + " Hz, wanted 261.63");
    require(engine.activeVoices() == 1, "mono plays on one voice");
}

// An operator that would sound above the guard is silent rather than folded
// back down.
void checkNyquistGuard()
{
    const auto peakFor = [] (float ratio)
    {
        auto settings = plain();
        settings.ops[0].level = 1.0f;
        settings.ops[0].ratio = ratio;
        auto engine = prepared(settings);
        const auto audio = play(engine, at(0.1), { noteOn(0.0, 108) });
        auto peak = 0.0f;
        for (const auto sample : audio)
            peak = std::max(peak, std::abs(sample));
        return peak;
    };
    require(peakFor(16.0f) == 0.0f, "C8 at ratio 16 is above the guard and still sounds");
    require(peakFor(1.0f) > 0.01f, "C8 at ratio 1 does not sound");
}

// The device is a shell: its controls reach the engine, and the bottom of the
// Output knob is silence.
void checkDevice(Session& session)
{
    auto plugin = session.edit->getPluginCache().createNewPlugin(RhinoFmDevice::xmlTypeName, {});
    require(plugin != nullptr, "the engine creates Rhino FM");
    const auto parameters = plugin->getAutomatableParameters();
    require(parameters.size() == 7 + 7 * FmEngine::operators, "Rhino FM declares " + juce::String(parameters.size())
                                                                   + " controls");
    require(parameters[0]->getAllLabels().size() == FmEngine::algorithms
                && parameters[0]->getAllLabels()[0] == FmEngine::algorithmName(0),
            "the algorithm chooser names every routing");

    const auto peakWithOutput = [&plugin] (float outputDb)
    {
        plugin->getAutomatableParameterByID("outputDb")->setParameter(outputDb, juce::sendNotificationSync);
        plugin->initialise({ {}, rate, 512 });
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 60, 1.0f), 0.0, {});
        auto peak = 0.0f;
        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            te::PluginRenderContext context(&buffer, 0, 512, &midi, 0.0, {}, true, false, true, false);
            plugin->applyToBuffer(context);
            midi.clear();
            peak = std::max(peak, buffer.getMagnitude(0, 512));
        }
        plugin->deinitialise();
        return peak;
    };
    require(peakWithOutput(0.0f) > 0.01f, "the default patch plays");
    require(peakWithOutput(-36.0f) == 0.0f, "the bottom of the Output knob is not silence");
}

// Not a check: what sixteen voices cost, for the record.
void reportCost()
{
    FmEngine::Settings settings;   // the electric piano the device starts as
    settings.ops[0] = { 1.0f, 0.0f, 0.85f, 0.002f, 2.5f, 0.0f, 0.5f };
    settings.ops[1] = { 1.0f, 0.0f, 0.42f, 0.002f, 1.2f, 0.15f, 0.5f };
    settings.ops[2] = { 1.0f, 6.0f, 0.6f, 0.002f, 1.6f, 0.0f, 0.4f };
    settings.ops[3] = { 14.0f, 0.0f, 0.22f, 0.001f, 0.18f, 0.0f, 0.2f };
    auto engine = prepared(settings);
    for (int note = 48; note < 48 + FmEngine::maxVoices; ++note)
        engine.noteOn(note, 0.8f);
    std::vector<float> left(512), right(512);
    const auto blocks = static_cast<int>(2.0 * rate / 512.0);
    const auto start = juce::Time::getHighResolutionTicks();
    for (int block = 0; block < blocks; ++block)
        engine.render(left.data(), right.data(), 512);
    const auto seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
    juce::Logger::writeToLog("Rhino FM: sixteen voices took " + juce::String(seconds / 2.0 * 100.0, 2)
                             + "% of real time at 48 kHz");
}
}

void checkFmDsp(Session& session)
{
    checkPitch();
    checkPureSine();
    checkModulationIndex();
    checkBrightness();
    checkFeedback();
    checkEnvelopes();
    checkVelocity();
    checkPolyphonyAndPedal();
    checkMonoGlide();
    checkNyquistGuard();
    checkDevice(session);
    reportCost();
}
}
