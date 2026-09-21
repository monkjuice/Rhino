#include "../Session.h"
#include "../DeviceEditorPanel.h"
#include "AutoTuneEngine.h"
#include "audio/AutoTuneDevice.h"
#include <algorithm>
#include <cmath>
#include <source_location>
#include <stdexcept>
#include <vector>

namespace rhino
{
namespace
{
constexpr double testRate = 48000.0;
constexpr int testBlock = 256;

void require(bool valid, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error("Rhino Tune check failed at line " + std::to_string(location.line()));
}

float centsBetween(float measured, float expected)
{
    return 1200.0f * std::log2(std::max(measured, 1.0e-6f) / std::max(expected, 1.0e-6f));
}

// A pitch check says what it measured when it fails.  "Failed at line 319" on
// a number that came out of a transform is a second run to find out anything
// at all, and these runs are seconds each.
void requireCents(float measured, float expected, float toleranceCents, const char* what,
                  const std::source_location location = std::source_location::current())
{
    const auto error = centsBetween(measured, expected);
    if (std::abs(error) >= toleranceCents)
        throw std::runtime_error((juce::String(what) + ": measured " + juce::String(measured, 2)
            + " Hz against " + juce::String(expected, 2) + " Hz, off by " + juce::String(error, 1)
            + " cents, allowed " + juce::String(toleranceCents, 0) + ", at line "
            + juce::String(static_cast<int>(location.line()))).toStdString());
}

// What the device claims to have done is settled by measuring the audio, not
// by reading the DSP back.  These share no code with the tracker or the
// shifter: they never look for a period, so they cannot agree with the code
// under test by construction.
double magnitudeAt(const std::vector<float>& audio, int start, int count, double frequency)
{
    const auto omega = 2.0 * 3.14159265358979323846 * frequency / testRate;
    auto real = 0.0, imaginary = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / count);
        const auto sample = audio[static_cast<size_t>(start + i)] * window;
        real += sample * std::cos(omega * i);
        imaginary -= sample * std::sin(omega * i);
    }
    return std::sqrt(real * real + imaginary * imaginary);
}

// The strongest thing in a band, to two cents, with a parabolic peak.
float measureFundamental(const std::vector<float>& audio, int start, int count, float low, float high)
{
    const auto steps = static_cast<int>(std::ceil(1200.0 * std::log2(high / low) / 2.0));
    auto bestMagnitude = -1.0;
    auto bestStep = 0;
    std::vector<double> magnitudes(static_cast<size_t>(steps) + 1, 0.0);
    for (int step = 0; step <= steps; ++step)
    {
        const auto frequency = low * std::pow(2.0, step * 2.0 / 1200.0);
        magnitudes[static_cast<size_t>(step)] = magnitudeAt(audio, start, count, frequency);
        if (magnitudes[static_cast<size_t>(step)] > bestMagnitude)
        {
            bestMagnitude = magnitudes[static_cast<size_t>(step)];
            bestStep = step;
        }
    }
    auto refined = static_cast<double>(bestStep);
    if (bestStep > 0 && bestStep < steps)
    {
        const auto before = magnitudes[static_cast<size_t>(bestStep - 1)];
        const auto here = magnitudes[static_cast<size_t>(bestStep)];
        const auto after = magnitudes[static_cast<size_t>(bestStep + 1)];
        const auto denominator = 2.0 * (2.0 * here - before - after);
        if (std::abs(denominator) > 1.0e-12)
            refined += (after - before) / denominator;
    }
    return static_cast<float>(low * std::pow(2.0, refined * 2.0 / 1200.0));
}

// The harmonic below which most of the magnitude lies -- where the weight of
// the spectrum sits.  Blunter than naming a formant and far steadier: it does
// not care which single partial happens to be tallest, which on a shifted
// vowel can be the second harmonic one moment and the seventh the next.
float harmonicRolloff(const std::vector<float>& audio, int start, int count, float fundamental)
{
    constexpr int harmonics = 40;
    std::vector<double> levels(harmonics, 0.0);
    auto total = 0.0;
    for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
    {
        levels[static_cast<size_t>(harmonic - 1)] =
            magnitudeAt(audio, start, count, fundamental * harmonic);
        total += levels[static_cast<size_t>(harmonic - 1)];
    }
    auto running = 0.0;
    for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
    {
        running += levels[static_cast<size_t>(harmonic - 1)];
        if (running >= total * 0.85)
            return fundamental * harmonic;
    }
    return fundamental * harmonics;
}

float rmsOf(const std::vector<float>& audio, int start, int count)
{
    auto sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto sample = audio[static_cast<size_t>(start + i)];
        sum += static_cast<double>(sample) * sample;
    }
    return static_cast<float>(std::sqrt(sum / std::max(1, count)));
}

// A sawtooth, which is close enough to a glottal source to exercise the same
// paths a voice does and has a fundamental strong enough to measure.
std::vector<float> sawtooth(float frequency, int count, float amplitude = 0.5f)
{
    std::vector<float> audio(static_cast<size_t>(count), 0.0f);
    auto phase = 0.0;
    const auto increment = frequency / testRate;
    for (int i = 0; i < count; ++i)
    {
        audio[static_cast<size_t>(i)] = amplitude * static_cast<float>(2.0 * phase - 1.0);
        phase += increment;
        phase -= std::floor(phase);
    }
    return audio;
}

// Two formants over a buzz, so a formant shift has something to move that the
// pitch measurement will not see.
std::vector<float> vowel(float frequency, int count)
{
    std::vector<float> audio(static_cast<size_t>(count), 0.0f);
    for (int harmonic = 1; harmonic * frequency < 5000.0f; ++harmonic)
    {
        const auto partial = frequency * harmonic;
        const auto shape = [partial] (float centre, float width)
        {
            const auto distance = (partial - centre) / width;
            return std::exp(-distance * distance);
        };
        const auto level = (0.35f + shape(700.0f, 350.0f) + 0.7f * shape(1200.0f, 450.0f)) / harmonic;
        for (int i = 0; i < count; ++i)
            audio[static_cast<size_t>(i)] += level
                * std::sin(2.0f * 3.14159265f * partial * static_cast<float>(i) / static_cast<float>(testRate));
    }
    auto peak = 1.0e-6f;
    for (const auto sample : audio) peak = std::max(peak, std::abs(sample));
    for (auto& sample : audio) sample *= 0.5f / peak;
    return audio;
}

std::vector<float> runEngine(AutoTuneEngine& engine, const std::vector<float>& source)
{
    auto audio = source;
    const auto count = static_cast<int>(audio.size());
    std::vector<float> scratch(static_cast<size_t>(testBlock), 0.0f);
    for (int offset = 0; offset < count; offset += testBlock)
    {
        const auto block = std::min(testBlock, count - offset);
        std::copy(audio.begin() + offset, audio.begin() + offset + block, scratch.begin());
        float* channels[1] {scratch.data()};
        engine.process(channels, 1, block);
        std::copy(scratch.begin(), scratch.begin() + block, audio.begin() + offset);
    }
    return audio;
}

void checkScaleQuantizer()
{
    const auto cMajor = maskForScale(MusicalScale::Major, 0);
    require(cMajor[0] && cMajor[2] && cMajor[4] && cMajor[5] && cMajor[7] && cMajor[9] && cMajor[11]);
    require(!cMajor[1] && !cMajor[3] && !cMajor[6] && !cMajor[8] && !cMajor[10]);

    // D minor is C major's notes moved by two, which is what a mode is; if
    // the rotation in maskForScale were off this would land a semitone out.
    const auto dDorian = maskForScale(MusicalScale::Dorian, 2);
    require(dDorian == cMajor);

    // 60 is C4. A quarter tone above it stays on C; past the halfway point to
    // D it moves up, and C# is not in the scale so it is never a target.
    require(nearestAllowedNote(60.0f, cMajor) == 60.0f);
    require(nearestAllowedNote(60.4f, cMajor) == 60.0f);
    require(nearestAllowedNote(61.2f, cMajor) == 62.0f);
    require(nearestAllowedNote(65.4f, cMajor) == 65.0f);
    require(nearestAllowedNote(66.0f, cMajor) == 65.0f);   // F# ties to F below

    // A scale degree is as wide as the scale makes it: a third in a
    // pentatonic is further than a third in a major scale.
    require(shiftByScaleDegrees(60.0f, cMajor, 1) == 62.0f);
    require(shiftByScaleDegrees(60.0f, cMajor, 7) == 72.0f);
    require(shiftByScaleDegrees(60.0f, cMajor, -1) == 59.0f);
    const auto pentatonic = maskForScale(MusicalScale::MajorPentatonic, 0);
    require(shiftByScaleDegrees(60.0f, pentatonic, 1) == 62.0f);
    require(shiftByScaleDegrees(60.0f, pentatonic, 5) == 72.0f);

    // Every key switched off must leave the note alone rather than snap it to
    // C or divide by nothing.
    const PitchClassMask empty {};
    require(nearestAllowedNote(60.37f, empty) == 60.37f);
    require(shiftByScaleDegrees(60.0f, empty, 3) == 60.0f);
}

void checkPitchTracker()
{
    struct Case { PitchTracker::Range range; float frequency; };
    const Case cases[] {
        {PitchTracker::Range::High, 220.0f}, {PitchTracker::Range::High, 880.0f},
        {PitchTracker::Range::Mid, 98.0f},   {PitchTracker::Range::Mid, 440.0f},
        {PitchTracker::Range::Bass, 55.0f},  {PitchTracker::Range::Bass, 196.0f}};

    for (const auto& test : cases)
    {
        PitchTracker tracker;
        tracker.prepare(testRate, test.range);
        const auto audio = sawtooth(test.frequency, 24000);
        tracker.process(audio.data(), static_cast<int>(audio.size()));
        const auto estimate = tracker.latest();
        require(estimate.voiced);
        requireCents(estimate.frequency, test.frequency, 5.0f, "tracked pitch");
        require(std::abs(estimate.midiNote - frequencyToMidiNote(test.frequency)) < 0.05f);
    }

    // Noise is not a note. Without this the corrector would chase consonants
    // and room tone and pull them onto the scale.
    PitchTracker tracker;
    tracker.prepare(testRate, PitchTracker::Range::Mid);
    juce::Random random (1234);
    std::vector<float> noise(24000, 0.0f);
    for (auto& sample : noise) sample = random.nextFloat() * 0.6f - 0.3f;
    tracker.process(noise.data(), static_cast<int>(noise.size()));
    require(!tracker.latest().voiced);

    // So is silence, and it must not divide by its own energy on the way to
    // saying so.
    std::vector<float> quiet(24000, 0.0f);
    tracker.reset();
    tracker.process(quiet.data(), static_cast<int>(quiet.size()));
    require(!tracker.latest().voiced);
    require(tracker.latest().frequency == 0.0f);
}

void checkEngineTransparency()
{
    AutoTuneEngine engine;
    engine.prepare(testRate, 1, testBlock);
    AutoTuneEngine::Settings settings;
    settings.strength = 0.0f;
    engine.setSettings(settings);

    const auto source = sawtooth(200.0f, 96000);
    const auto processed = runEngine(engine, source);
    const auto latency = engine.latencySamples();
    require(latency > 0);

    // Asked to change nothing, the overlap-add has to give the signal back as
    // it found it, only later. Anything else here is the grain scheduler
    // sliding against the input, which on real material is a slow wobble.
    const auto start = 48000;
    const auto length = 24000;
    auto error = 0.0, energy = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const auto wet = processed[static_cast<size_t>(start + i)];
        const auto dry = source[static_cast<size_t>(start + i - latency)];
        error += static_cast<double>(wet - dry) * (wet - dry);
        energy += static_cast<double>(dry) * dry;
    }
    const auto errorDb = 10.0 * std::log10(std::max(error, 1.0e-30) / std::max(energy, 1.0e-30));
    require(errorDb < -30.0);
}

void checkPitchShift()
{
    AutoTuneEngine engine;
    engine.prepare(testRate, 1, testBlock);
    AutoTuneEngine::Settings settings;
    settings.strength = 0.0f;
    settings.pitchSemitones = 7.0f;
    engine.setSettings(settings);

    const auto source = sawtooth(220.0f, 96000);
    const auto processed = runEngine(engine, source);
    const auto expected = 220.0f * std::pow(2.0f, 7.0f / 12.0f);
    const auto measured = measureFundamental(processed, 60000, 16384, 180.0f, 420.0f);
    requireCents(measured, expected, 20.0f, "seven semitones up");

    // Transposing must not empty the signal out; the overlap has changed and
    // the compensation for it is the only thing holding the level.
    const auto wetRms = rmsOf(processed, 60000, 16384);
    const auto dryRms = rmsOf(source, 60000, 16384);
    require(wetRms > dryRms * 0.5f && wetRms < dryRms * 2.0f);
}

void checkCorrection()
{
    // Forty cents sharp of A4, which is well inside what a singer does and
    // well outside what anyone wants to hear.
    const auto detuned = 440.0f * std::pow(2.0f, 0.4f / 12.0f);

    AutoTuneEngine engine;
    engine.prepare(testRate, 1, testBlock);
    AutoTuneEngine::Settings settings;
    settings.strength = 1.0f;
    settings.retuneMs = 0.0f;
    engine.setSettings(settings);

    const auto source = sawtooth(detuned, 96000);
    const auto corrected = runEngine(engine, source);
    const auto measured = measureFundamental(corrected, 60000, 16384, 380.0f, 520.0f);
    requireCents(measured, 440.0f, 15.0f, "full strength correction");

    // Half strength should land half way, which is what makes Strength a
    // continuous control rather than a switch.
    AutoTuneEngine half;
    half.prepare(testRate, 1, testBlock);
    settings.strength = 0.5f;
    half.setSettings(settings);
    const auto partial = runEngine(half, source);
    const auto partialHz = measureFundamental(partial, 60000, 16384, 380.0f, 520.0f);
    require(std::abs(centsBetween(partialHz, 440.0f) - 20.0f) < 12.0f);

    // Switched off, the same input has to come back out at the pitch it went
    // in at -- the corrector is not allowed to tune anything by accident.
    AutoTuneEngine idle;
    idle.prepare(testRate, 1, testBlock);
    settings.strength = 0.0f;
    idle.setSettings(settings);
    const auto untouched = runEngine(idle, source);
    const auto untouchedHz = measureFundamental(untouched, 60000, 16384, 380.0f, 520.0f);
    requireCents(untouchedHz, detuned, 10.0f, "correction switched off");
}

void checkScaleCorrection()
{
    // Seventy cents above C4, sung against C major.  A chromatic corrector
    // would round that up to the C# it is nearest; the scale has no C# in it,
    // so this has to come down to C instead.  Deliberately not C# exactly:
    // that sits the same distance from C and from D, and which one a tie goes
    // to is not a property worth writing a test against.
    const auto sung = midiNoteToFrequency(60.7f);

    AutoTuneEngine engine;
    engine.prepare(testRate, 1, testBlock);
    AutoTuneEngine::Settings settings;
    settings.strength = 1.0f;
    settings.retuneMs = 0.0f;
    settings.mask = maskForScale(MusicalScale::Major, 0);
    engine.setSettings(settings);

    const auto source = sawtooth(sung, 96000);
    const auto corrected = runEngine(engine, source);
    const auto measured = measureFundamental(corrected, 60000, 16384, 200.0f, 330.0f);
    requireCents(measured, midiNoteToFrequency(60.0f), 20.0f, "corrected into C major");
}

void checkFormantShift()
{
    const auto source = vowel(150.0f, 96000);
    const auto shifted = [&source] (float percent)
    {
        AutoTuneEngine engine;
        engine.prepare(testRate, 1, testBlock);
        AutoTuneEngine::Settings settings;
        settings.strength = 0.0f;
        settings.formantPercent = percent;
        engine.setSettings(settings);
        return runEngine(engine, source);
    };
    const auto up = shifted(60.0f);
    const auto down = shifted(-60.0f);

    // The whole reason to shift formants rather than transpose is that the
    // note does not move.  If this drifts, the grain length and the grain
    // spacing have stopped being independent of each other.
    requireCents(measureFundamental(up, 60000, 16384, 120.0f, 190.0f), 150.0f, 25.0f, "pitch, formants up");
    requireCents(measureFundamental(down, 60000, 16384, 120.0f, 190.0f), 150.0f, 25.0f, "pitch, formants down");

    // And the resonances do move, both ways.  Sixty percent asks for a ratio
    // of 1.52; what lands is that rounded onto the harmonic grid, because a
    // partial can only sit on a multiple of the fundamental.
    const auto reference = harmonicRolloff(source, 60000, 16384, 150.0f);
    const auto raised = harmonicRolloff(up, 60000, 16384, 150.0f);
    const auto lowered = harmonicRolloff(down, 60000, 16384, 150.0f);
    if (!(raised > reference * 1.25f && lowered < reference / 1.25f))
        throw std::runtime_error(("formant shift: spectrum sits below " + juce::String(reference, 0)
            + " Hz untouched, " + juce::String(raised, 0) + " Hz at +60% and "
            + juce::String(lowered, 0) + " Hz at -60%").toStdString());
}

void checkDryWet()
{
    AutoTuneEngine engine;
    engine.prepare(testRate, 1, testBlock);
    AutoTuneEngine::Settings settings;
    settings.strength = 1.0f;
    settings.pitchSemitones = 12.0f;
    settings.dryWet = 0.0f;
    engine.setSettings(settings);

    const auto source = sawtooth(220.0f, 96000);
    const auto processed = runEngine(engine, source);
    const auto latency = engine.latencySamples();
    for (int i = 0; i < 8192; ++i)
        require(std::abs(processed[static_cast<size_t>(60000 + i)]
                         - source[static_cast<size_t>(60000 + i - latency)]) < 1.0e-5f);
}

void checkRangeSwitchIsSilentAndSafe()
{
    AutoTuneEngine engine;
    engine.prepare(testRate, 2, testBlock);
    AutoTuneEngine::Settings settings;
    engine.setSettings(settings);

    auto left = sawtooth(180.0f, 48000);
    auto right = left;
    const PitchTracker::Range order[] {PitchTracker::Range::Mid, PitchTracker::Range::High,
                                       PitchTracker::Range::Bass, PitchTracker::Range::Mid};
    auto step = 0;
    for (int offset = 0; offset + testBlock <= 48000; offset += testBlock)
    {
        // Change the range, and Live Mode with it, every few blocks: both move
        // the reported latency, and the switch must neither reallocate nor
        // read past the end of a ring sized for a different one.
        if ((offset / testBlock) % 17 == 0)
        {
            settings.range = order[static_cast<size_t>(step % 4)];
            settings.liveMode = (step % 2) == 1;
            ++step;
            engine.setSettings(settings);
        }
        float* channels[2] {left.data() + offset, right.data() + offset};
        engine.process(channels, 2, testBlock);
    }
    for (int i = 0; i < 48000; ++i)
    {
        require(std::isfinite(left[static_cast<size_t>(i)]));
        require(std::abs(left[static_cast<size_t>(i)]) < 4.0f);
        require(left[static_cast<size_t>(i)] == right[static_cast<size_t>(i)]);
    }
}

// The face is drawn rather than assembled from child components, so what a
// test can check is that the knobs it does own land inside the panel and off
// each other, and that painting the rest of it runs at all.
void checkEditorFace(Session& session)
{
    require(session.addDevice("RhinoTune", 0).wasOk());
    const auto slots = session.deviceSlots(0);
    require(!slots.empty());
    const auto tuneSlot = std::find_if(slots.begin(), slots.end(),
        [] (const Session::DeviceSlot& slot) { return slot.type == AutoTuneDevice::xmlTypeName; });
    require(tuneSlot != slots.end());

    // A scale with notes missing, so the keyboard has both states to draw and
    // the mask is proved to reach the paint path rather than just the DSP.
    auto* tune = dynamic_cast<AutoTuneDevice*>(session.devicePlugin(0, tuneSlot->pluginIndex));
    require(tune != nullptr);
    tune->applyScale(2, MusicalScale::Minor);
    require(tune->readout().latencyMs > 1.0f);

    auto panel = std::make_unique<DeviceEditorPanel>(session);
    panel->setTarget(0, *tuneSlot, true);
    require(panel->preferredWidth() > 600);
    panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);

    std::vector<juce::Rectangle<int>> knobs;
    for (auto* child : panel->getChildren())
    {
        require(panel->getLocalBounds().contains(child->getBounds()));
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
            knobs.push_back(child->getBounds());
    }
    // Thirteen, because the face has room for input gain as well as the
    // twelve the generic panel would show.
    require(knobs.size() == 13);
    for (size_t i = 0; i < knobs.size(); ++i)
        for (auto j = i + 1; j < knobs.size(); ++j)
            require(!knobs[i].intersects(knobs[j]));

    // Painting reaches the device through Session::devicePlugin and reads a
    // meter off it; if that path is wrong this is where it shows.
    const auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds());
    require(snapshot.getWidth() == panel->getWidth());
    require(snapshot.getHeight() == DeviceEditorPanel::standardHeight);

    require(session.deleteDevice(0, tuneSlot->pluginIndex).wasOk());
}

void checkDevice(Session& session)
{
    auto plugin = session.edit->getPluginCache().createNewPlugin(AutoTuneDevice::xmlTypeName, {});
    auto* tune = dynamic_cast<AutoTuneDevice*>(plugin.get());
    require(tune != nullptr);

    // The catalog is the only place the device is declared, so the engine has
    // to be able to make one from the type name alone.
    require(DeviceCatalog::byTypeName(AutoTuneDevice::xmlTypeName) != nullptr);
    require(DeviceCatalog::byId("RhinoTune") != nullptr);

    tune->initialise({{}, testRate, testBlock});
    require(tune->getLatencySeconds() > 0.0);
    require(tune->getLatencySeconds() < 0.25);

    // A new device corrects to every semitone, which is the setting that does
    // something useful before anyone has chosen a key.
    for (const auto allowed : tune->scaleMask())
        require(allowed);
    require(tune->maskMatchesNamedScale());

    tune->applyScale(2, MusicalScale::Minor);
    require(tune->scaleRoot() == 2);
    require(tune->scale() == MusicalScale::Minor);
    require(tune->scaleMask() == maskForScale(MusicalScale::Minor, 2));
    require(tune->maskMatchesNamedScale());

    // Switching one key off is what makes it a custom scale, and the editor
    // reads exactly that to decide what to put in the Scale chooser.
    auto mask = tune->scaleMask();
    mask[9] = !mask[9];
    tune->setScaleMask(mask);
    require(!tune->maskMatchesNamedScale());

    tune->setTrackingRange(PitchTracker::Range::Bass);
    require(tune->trackingRange() == PitchTracker::Range::Bass);
    tune->setLiveMode(true);
    require(tune->liveMode());

    // Everything the device holds has to survive being written out and read
    // back, including the properties that are not automatable parameters.
    tune->getAutomatableParameterByID("strength")->setParameter(0.4f, juce::dontSendNotification);
    tune->getAutomatableParameterByID("formant")->setParameter(-35.0f, juce::dontSendNotification);
    auto xml = tune->state.createXml();
    auto roundTrip = juce::ValueTree::fromXml(*xml);
    roundTrip.removeProperty(te::IDs::id, nullptr);
    auto reloaded = session.edit->getPluginCache().createNewPlugin(roundTrip);
    auto* restored = dynamic_cast<AutoTuneDevice*>(reloaded.get());
    require(restored != nullptr);
    require(std::abs(restored->getAutomatableParameterByID("strength")->getCurrentValue() - 0.4f) < 1.0e-5f);
    require(std::abs(restored->getAutomatableParameterByID("formant")->getCurrentValue() + 35.0f) < 1.0e-4f);
    require(restored->scaleMask() == mask);
    require(restored->scaleRoot() == 2);
    require(restored->trackingRange() == PitchTracker::Range::Bass);
    require(restored->liveMode());

    // Driven with no audio at all it must still be well behaved: the rack
    // calls this on every block whether or not anything is playing.
    juce::AudioBuffer<float> buffer(2, testBlock);
    buffer.clear();
    te::PluginRenderContext context(&buffer, 0, testBlock, nullptr, 0.0, {}, false, false, true, false);
    for (int i = 0; i < 8; ++i) tune->applyToBuffer(context);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < testBlock; ++i)
            require(buffer.getSample(channel, i) == 0.0f);
    const auto idle = tune->readout();
    require(!idle.voiced);
    require(idle.latencyMs > 0.0f);

    te::PluginRenderContext noAudio(nullptr, 0, 0, nullptr, 0.0, {}, false, false, true, false);
    tune->applyToBuffer(noAudio);
    tune->deinitialise();
}
}

void checkAutoTuneDsp(Session& session)
{
    checkScaleQuantizer();
    checkPitchTracker();
    checkEngineTransparency();
    checkPitchShift();
    checkCorrection();
    checkScaleCorrection();
    checkFormantShift();
    checkDryWet();
    checkRangeSwitchIsSilentAndSafe();
    checkDevice(session);
    checkEditorFace(session);
}
}
