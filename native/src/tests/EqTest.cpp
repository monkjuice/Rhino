#include "../Session.h"
#include "../DeviceEditorPanel.h"
#include "EqEngine.h"
#include "SpectrumAnalyser.h"
#include "audio/RhinoEqDevice.h"
#include <algorithm>
#include <cmath>
#include <source_location>
#include <stdexcept>
#include <vector>

// What the EQ claims to do is settled by measuring rendered audio.
//
// The point of this file is one comparison: sweep a sine through the engine,
// measure how much came out, and hold that against the number the panel would
// have drawn at the same frequency. The two come from the same coefficients
// but by completely different routes -- one through a difference equation
// sample by sample, the other by evaluating a transfer function on the unit
// circle -- so they cannot agree by construction. Either one drifting shows up
// here as a drawn curve that lies about the audio, which is the only defect an
// EQ display can really have.
namespace rhino
{
namespace
{
constexpr double testRate = 48000.0;
constexpr int testBlock = 256;
constexpr double pi = 3.14159265358979323846;

void require(bool valid, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error("Rhino EQ check failed at line " + std::to_string(location.line()));
}

// A level check says what it measured when it fails. "Failed at line 210" on a
// number that came out of a transform costs a second run to learn anything.
void requireDb(double measured, double expected, double toleranceDb, const char* what,
               const std::source_location location = std::source_location::current())
{
    if (std::abs(measured - expected) >= toleranceDb)
        throw std::runtime_error((juce::String(what) + ": measured " + juce::String(measured, 2)
            + " dB against " + juce::String(expected, 2) + " dB, off by "
            + juce::String(measured - expected, 2) + ", allowed " + juce::String(toleranceDb, 2)
            + ", at line " + juce::String(static_cast<int>(location.line()))).toStdString());
}

// A windowed single-frequency transform. It shares nothing with the filters:
// it never sees a coefficient, so it cannot agree with them by accident.
double magnitudeAt(const std::vector<float>& audio, int start, int count, double frequency)
{
    const auto omega = 2.0 * pi * frequency / testRate;
    auto real = 0.0, imaginary = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * pi * i / count);
        const auto sample = audio[static_cast<size_t>(start + i)] * window;
        real += sample * std::cos(omega * i);
        imaginary -= sample * std::sin(omega * i);
    }
    return std::sqrt(real * real + imaginary * imaginary);
}

constexpr int settleSamples = 24000;     // half a second, well past every smoother
constexpr int measureSamples = 16384;

std::vector<float> renderSine(const EqEngine::Settings& settings, double frequency)
{
    const auto total = settleSamples + measureSamples;
    std::vector<float> audio(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * pi * frequency * i / testRate));

    EqEngine engine;
    engine.prepare(testRate, 1, testBlock);
    for (int start = 0; start < total; start += testBlock)
    {
        const auto count = std::min(testBlock, total - start);
        float* channels[1] {audio.data() + start};
        engine.setSettings(settings);
        engine.process(channels, 1, count);
    }
    return audio;
}

// The response the audio actually shows, in dB, against an untouched sine of
// the same length so the window normalisation cancels out.
double measuredDbAt(const EqEngine::Settings& settings, double frequency)
{
    const auto processed = renderSine(settings, frequency);
    std::vector<float> reference(processed.size());
    for (size_t i = 0; i < reference.size(); ++i)
        reference[i] = static_cast<float>(std::sin(2.0 * pi * frequency * static_cast<double>(i) / testRate));
    const auto out = magnitudeAt(processed, settleSamples, measureSamples, frequency);
    const auto in = magnitudeAt(reference, settleSamples, measureSamples, frequency);
    return 20.0 * std::log10(std::max(out, 1.0e-9) / std::max(in, 1.0e-9));
}

EqEngine::Settings flatSettings()
{
    EqEngine::Settings settings;
    settings.analyser = EqEngine::AnalyserMode::Off;
    for (auto& band : settings.band)
        band = {};
    return settings;
}

// ---------------------------------------------------------------------------

// The shapes, checked against what the filter design says they must be. These
// are closed forms, not measurements, and they are here so a wrong formula is
// caught before the audio comparison has to explain itself.
void checkFilterShapes()
{
    EqBandSettings bell {true, EqFilterType::Bell, 1000.0f, 9.0f, 2.0f};
    requireDb(eqBandMagnitudeDbAt(bell, 1000.0, testRate), 9.0, 0.05, "bell at its centre");
    requireDb(eqBandMagnitudeDbAt(bell, 50.0, testRate), 0.0, 0.3, "bell far below");
    requireDb(eqBandMagnitudeDbAt(bell, 16000.0, testRate), 0.0, 0.3, "bell far above");

    bell.gainDb = -12.0f;
    requireDb(eqBandMagnitudeDbAt(bell, 1000.0, testRate), -12.0, 0.05, "cut bell at its centre");

    const EqBandSettings lowShelf {true, EqFilterType::LowShelf, 300.0f, 6.0f, 0.71f};
    requireDb(eqBandMagnitudeDbAt(lowShelf, 20.0, testRate), 6.0, 0.15, "low shelf in its band");
    requireDb(eqBandMagnitudeDbAt(lowShelf, 300.0, testRate), 3.0, 0.15, "low shelf at its corner");
    requireDb(eqBandMagnitudeDbAt(lowShelf, 12000.0, testRate), 0.0, 0.2, "low shelf out of its band");

    const EqBandSettings highShelf {true, EqFilterType::HighShelf, 4000.0f, -8.0f, 0.71f};
    requireDb(eqBandMagnitudeDbAt(highShelf, 18000.0, testRate), -8.0, 0.3, "high shelf in its band");
    requireDb(eqBandMagnitudeDbAt(highShelf, 100.0, testRate), 0.0, 0.2, "high shelf out of its band");

    // A Butterworth cut is three dB down at its corner whatever its order, and
    // falls at six dB an octave per pole below it. Twelve and forty-eight are
    // what the chooser promises, so those are the two numbers to hold it to.
    const EqBandSettings gentle {true, EqFilterType::LowCut12, 1000.0f, 0.0f, 0.70711f};
    requireDb(eqBandMagnitudeDbAt(gentle, 1000.0, testRate), -3.01, 0.05, "12 dB cut at its corner");
    requireDb(eqBandMagnitudeDbAt(gentle, 500.0, testRate), -12.30, 0.1, "12 dB cut an octave below");

    const EqBandSettings steep {true, EqFilterType::LowCut48, 1000.0f, 0.0f, 0.71f};
    requireDb(eqBandMagnitudeDbAt(steep, 1000.0, testRate), -3.01, 0.05, "48 dB cut at its corner");
    requireDb(eqBandMagnitudeDbAt(steep, 500.0, testRate), -48.16, 0.3, "48 dB cut an octave below");
    // Its Q control is a Butterworth cascade and must not move with the band.
    auto resonant = steep;
    resonant.q = 8.0f;
    requireDb(eqBandMagnitudeDbAt(resonant, 1000.0, testRate), -3.01, 0.05, "48 dB cut ignores Q");

    const EqBandSettings notch {true, EqFilterType::Notch, 800.0f, 0.0f, 4.0f};
    require(eqBandMagnitudeDbAt(notch, 800.0, testRate) < -60.0);
    requireDb(eqBandMagnitudeDbAt(notch, 100.0, testRate), 0.0, 0.2, "notch far below");

    // A band that is off is not a filter at unity, it is no filter at all.
    EqBandSettings off = bell;
    off.enabled = false;
    require(eqBandMagnitudeDbAt(off, 1000.0, testRate) == 0.0);
}

// The comparison this file exists for.
void checkCurveMatchesMeasuredAudio()
{
    auto settings = flatSettings();
    settings.band[0] = {true, EqFilterType::LowCut12, 120.0f, 0.0f, 0.70711f};
    settings.band[2] = {true, EqFilterType::Bell, 400.0f, 7.5f, 1.4f};
    settings.band[4] = {true, EqFilterType::Bell, 2500.0f, -9.0f, 3.0f};
    settings.band[6] = {true, EqFilterType::HighShelf, 7000.0f, 4.0f, 0.71f};
    settings.outputGainDb = -2.0f;

    // Spread across the spectrum and deliberately off every corner, so a
    // frequency that happens to sit where two bands cancel cannot hide a
    // wrong one.
    for (const auto frequency : {60.0, 130.0, 400.0, 900.0, 2500.0, 6000.0, 11000.0})
        requireDb(measuredDbAt(settings, frequency),
                  EqEngine::responseDbAt(settings, frequency, testRate),
                  0.35, "drawn curve against measured audio");

    // The same again through the steep cuts, where an error in the cascade
    // would be worth tens of dB rather than tenths.
    auto steep = flatSettings();
    steep.band[0] = {true, EqFilterType::LowCut48, 200.0f, 0.0f, 0.71f};
    steep.band[7] = {true, EqFilterType::HighCut48, 5000.0f, 0.0f, 0.71f};
    for (const auto frequency : {150.0, 200.0, 1000.0, 5000.0, 7000.0})
        requireDb(measuredDbAt(steep, frequency),
                  EqEngine::responseDbAt(steep, frequency, testRate),
                  0.5, "steep cuts against measured audio");
}

void checkBypassScaleAndOutput()
{
    // Nothing switched on is a wire.
    const auto flat = flatSettings();
    for (const auto frequency : {80.0, 1000.0, 9000.0})
        requireDb(measuredDbAt(flat, frequency), 0.0, 0.02, "an EQ with no bands on");

    auto boosted = flatSettings();
    boosted.band[3] = {true, EqFilterType::Bell, 1000.0f, 10.0f, 1.0f};
    requireDb(measuredDbAt(boosted, 1000.0), 10.0, 0.1, "a bell boost");

    // Scale rides every band at once. It is the one control whose effect the
    // curve has to apply itself, so it is the one most likely to be drawn
    // from the unscaled numbers by mistake.
    auto halved = boosted;
    halved.scalePercent = 50.0f;
    requireDb(measuredDbAt(halved, 1000.0), 5.0, 0.1, "a bell boost at half scale");
    requireDb(EqEngine::responseDbAt(halved, 1000.0, testRate), 5.0, 0.05, "the drawn curve at half scale");

    auto flattened = boosted;
    flattened.scalePercent = 0.0f;
    requireDb(measuredDbAt(flattened, 1000.0), 0.0, 0.05, "a bell boost at zero scale");

    // A cut has no gain for Scale to ride, so it must come through untouched.
    auto cut = flatSettings();
    cut.band[0] = {true, EqFilterType::LowCut12, 1000.0f, 0.0f, 0.70711f};
    cut.scalePercent = 0.0f;
    requireDb(measuredDbAt(cut, 1000.0), -3.01, 0.1, "a cut at zero scale");

    auto trimmed = flatSettings();
    trimmed.outputGainDb = -6.0f;
    requireDb(measuredDbAt(trimmed, 1000.0), -6.0, 0.05, "output trim");
}

// Switching bands, types and frequencies while audio runs must stay finite and
// must not produce a bang. The engine crossfades a band in and out rather than
// dropping its filter into the path, and this is what holds that.
void checkSwitchingIsSafeAndQuiet()
{
    const int total = 96000;
    std::vector<float> left(static_cast<size_t>(total)), right(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i)
    {
        const auto sample = static_cast<float>(0.5 * std::sin(2.0 * pi * 220.0 * i / testRate));
        left[static_cast<size_t>(i)] = sample;
        right[static_cast<size_t>(i)] = sample;
    }

    EqEngine engine;
    engine.prepare(testRate, 2, testBlock);
    auto settings = flatSettings();
    settings.band[3] = {true, EqFilterType::Bell, 1000.0f, 0.0f, 1.0f};

    int step = 0;
    for (int offset = 0; offset + testBlock <= total; offset += testBlock)
    {
        if ((offset / testBlock) % 11 == 0)
        {
            static const EqFilterType order[]
                {EqFilterType::Bell, EqFilterType::LowCut48, EqFilterType::Notch, EqFilterType::HighShelf};
            settings.band[3].type = order[step % 4];
            settings.band[3].enabled = (step % 3) != 0;
            settings.band[3].frequency = 120.0f * static_cast<float>(1 + (step % 8));
            settings.band[3].gainDb = (step % 2) == 0 ? 12.0f : -12.0f;
            settings.band[0].enabled = (step % 2) == 0;
            settings.band[0].type = EqFilterType::LowCut48;
            settings.band[0].frequency = 300.0f;
            ++step;
        }
        engine.setSettings(settings);
        float* channels[2] {left.data() + offset, right.data() + offset};
        engine.process(channels, 2, testBlock);
    }

    for (int i = 0; i < total; ++i)
    {
        require(std::isfinite(left[static_cast<size_t>(i)]));
        // A 220 Hz tone at half scale through a 12 dB boost is about 2.0 at
        // worst; four is a band being dropped in rather than faded in.
        require(std::abs(left[static_cast<size_t>(i)]) < 4.0f);
        require(left[static_cast<size_t>(i)] == right[static_cast<size_t>(i)]);
    }
    // No sample-to-sample jump big enough to be a click. At 220 Hz a clean
    // signal moves by well under a hundredth of full scale per sample, so this
    // is loose by two orders of magnitude and still catches a hard switch.
    // It reports the jump it found and where, because "a click somewhere in
    // two seconds of audio" is a second run before it means anything.
    auto worst = 0.0f;
    auto worstAt = 0;
    for (int i = 1; i < total; ++i)
    {
        const auto jump = std::abs(left[static_cast<size_t>(i)] - left[static_cast<size_t>(i - 1)]);
        if (jump > worst) { worst = jump; worstAt = i; }
    }
    if (worst >= 0.2f)
        throw std::runtime_error(("a switch clicked: " + juce::String(worst, 3)
            + " between samples " + juce::String(worstAt - 1) + " and " + juce::String(worstAt)
            + ", allowed 0.2").toStdString());
}

// The display is only honest if it puts energy where the energy is, and says
// how much of it there is.
void checkSpectrumReader()
{
    SpectrumReader reader;
    std::vector<float> tone(SpectrumReader::fftSize);
    for (int i = 0; i < SpectrumReader::fftSize; ++i)
        tone[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * pi * 1000.0 * i / testRate));

    reader.analyse(tone.data(), SpectrumReader::fftSize, testRate, 100.0f);
    const auto& bins = reader.decibels();
    auto peak = 0;
    for (int i = 1; i < SpectrumReader::binCount; ++i)
        if (bins[static_cast<size_t>(i)] > bins[static_cast<size_t>(peak)])
            peak = i;
    const auto peakHz = SpectrumReader::binFrequency(peak);
    require(peakHz > 940.0f && peakHz < 1065.0f);
    // A full-scale sine is meant to read about zero dBFS. The window spreads
    // it over a couple of bins, so the peak sits a little under.
    requireDb(bins[static_cast<size_t>(peak)], 0.0, 1.5, "a full-scale sine in the analyser");

    // Away from the tone there is nothing, and it must look like nothing.
    const auto quiet = SpectrumReader::binCount / 8;
    require(bins[static_cast<size_t>(quiet)] < -60.0f);

    // Silence does not snap the display to the floor, it walks it down.
    std::vector<float> silence(SpectrumReader::fftSize, 0.0f);
    reader.analyse(silence.data(), SpectrumReader::fftSize, testRate, 6.0f);
    const auto afterOne = bins[static_cast<size_t>(peak)];
    requireDb(afterOne, -6.0, 1.6, "one frame of release");
    for (int i = 0; i < 400; ++i)
        reader.analyse(silence.data(), SpectrumReader::fftSize, testRate, 6.0f);
    require(bins[static_cast<size_t>(peak)] <= SpectrumReader::floorDb);

    // The tap is what feeds all of that, and it has to hand back the most
    // recent window whatever the block size was that filled it.
    SpectrumTap tap;
    tap.prepare(testRate);
    require(!tap.hasRun());
    std::vector<float> ramp(300);
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 300; ++i)
            ramp[static_cast<size_t>(i)] = static_cast<float>(block * 300 + i);
        const float* channels[1] {ramp.data()};
        tap.write(channels, 1, 300);
    }
    require(tap.hasRun());
    std::vector<float> tail(64);
    tap.readTail(tail.data(), 64);
    for (int i = 0; i < 64; ++i)
        require(tail[static_cast<size_t>(i)] == static_cast<float>(40 * 300 - 64 + i));
}

void checkDevice(Session& session)
{
    auto plugin = session.edit->getPluginCache().createNewPlugin(RhinoEqDevice::xmlTypeName, {});
    auto* eq = dynamic_cast<RhinoEqDevice*>(plugin.get());
    require(eq != nullptr);

    // The catalog is the only place the device is declared, so the engine has
    // to be able to make one from the type name alone -- and the id the
    // Tracktion EQ used has to still resolve, because presets name it.
    require(DeviceCatalog::byTypeName(RhinoEqDevice::xmlTypeName) != nullptr);
    require(DeviceCatalog::byId("Equaliser") != nullptr);
    require(DeviceCatalog::byId("Equaliser")->typeName == RhinoEqDevice::xmlTypeName);

    eq->initialise({{}, testRate, testBlock});
    require(static_cast<int>(eq->getAutomatableParameters().size()) == RhinoEqDevice::parameterCount);

    // A fresh EQ is flat: one band on, and it at zero gain.
    auto enabledCount = 0;
    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
        if (eq->bandEnabled(band)) ++enabledCount;
    require(enabledCount == 1);
    for (const auto frequency : {50.0, 1000.0, 10000.0})
        requireDb(EqEngine::responseDbAt(eq->currentSettings(), frequency, testRate), 0.0, 0.001,
                  "a new EQ");

    eq->setBandType(1, EqFilterType::Notch);
    eq->setBandEnabled(1, true);
    eq->setSelectedBand(1);
    eq->setAnalyserMode(EqEngine::AnalyserMode::Pre);
    require(eq->bandType(1) == EqFilterType::Notch);
    require(eq->bandEnabled(1));
    require(eq->selectedBand() == 1);
    require(eq->analyserMode() == EqEngine::AnalyserMode::Pre);

    eq->getAutomatableParameterByID("b2f")->setParameter(3200.0f, juce::dontSendNotification);
    eq->getAutomatableParameterByID("b5g")->setParameter(-7.5f, juce::dontSendNotification);
    eq->getAutomatableParameterByID("scale")->setParameter(65.0f, juce::dontSendNotification);

    // Everything it holds has to survive being written out and read back,
    // including the properties that are not automatable parameters.
    auto xml = eq->state.createXml();
    auto roundTrip = juce::ValueTree::fromXml(*xml);
    roundTrip.removeProperty(te::IDs::id, nullptr);
    auto reloaded = session.edit->getPluginCache().createNewPlugin(roundTrip);
    auto* restored = dynamic_cast<RhinoEqDevice*>(reloaded.get());
    require(restored != nullptr);
    require(std::abs(restored->getAutomatableParameterByID("b2f")->getCurrentValue() - 3200.0f) < 0.5f);
    require(std::abs(restored->getAutomatableParameterByID("b5g")->getCurrentValue() + 7.5f) < 1.0e-3f);
    require(std::abs(restored->getAutomatableParameterByID("scale")->getCurrentValue() - 65.0f) < 1.0e-2f);
    require(restored->bandType(1) == EqFilterType::Notch);
    require(restored->bandEnabled(1));
    require(restored->selectedBand() == 1);
    require(restored->analyserMode() == EqEngine::AnalyserMode::Pre);

    // Driven with no audio at all it must still be well behaved: the rack
    // calls this on every block whether or not anything is playing.
    juce::AudioBuffer<float> buffer(2, testBlock);
    buffer.clear();
    te::PluginRenderContext context(&buffer, 0, testBlock, nullptr, 0.0, {}, false, false, true, false);
    for (int i = 0; i < 8; ++i) eq->applyToBuffer(context);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < testBlock; ++i)
            require(buffer.getSample(channel, i) == 0.0f);

    // The analyser tap, end to end: a tone through the device has to reach
    // the ring the panel reads. This is done on a device that is not in any
    // graph, so nothing else is calling applyToBuffer at the same time.
    eq->setAnalyserMode(EqEngine::AnalyserMode::Post);
    for (int block = 0; block < 24; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < testBlock; ++i)
                buffer.setSample(channel, i, static_cast<float>(
                    std::sin(2.0 * pi * 1000.0 * (block * testBlock + i) / testRate)));
        eq->applyToBuffer(context);
    }
    require(eq->spectrumTap().hasRun());
    SpectrumReader reader;
    require(reader.update(eq->spectrumTap(), 100.0f));
    const auto& bins = reader.decibels();
    auto peak = 0;
    for (int i = 1; i < SpectrumReader::binCount; ++i)
        if (bins[static_cast<size_t>(i)] > bins[static_cast<size_t>(peak)])
            peak = i;
    const auto peakHz = SpectrumReader::binFrequency(peak);
    require(peakHz > 900.0f && peakHz < 1100.0f);

    te::PluginRenderContext noAudio(nullptr, 0, 0, nullptr, 0.0, {}, false, false, true, false);
    eq->applyToBuffer(noAudio);
    eq->deinitialise();
}

// The face is drawn rather than assembled, so what a test can check is that
// the controls it does own land inside the panel and off each other, that the
// spectrum reaches the paint path, and that painting runs at all.
void checkEditorFace(Session& session)
{
    require(session.addDevice("Equaliser", 0).wasOk());
    const auto slots = session.deviceSlots(0);
    const auto eqSlot = std::find_if(slots.begin(), slots.end(),
        [] (const Session::DeviceSlot& slot) { return slot.type == RhinoEqDevice::xmlTypeName; });
    require(eqSlot != slots.end());

    auto* eq = dynamic_cast<RhinoEqDevice*>(session.devicePlugin(0, eqSlot->pluginIndex));
    require(eq != nullptr);
    // Several bands on, of different kinds, so the handles and the per-band
    // curve all have something to draw rather than one flat bell.
    eq->setBandEnabled(0, true);
    eq->setBandType(0, EqFilterType::LowCut48);
    eq->setBandEnabled(4, true);
    eq->setBandType(4, EqFilterType::Notch);
    eq->setSelectedBand(4);
    eq->getAutomatableParameterByID("b4g")->setParameter(8.0f, juce::dontSendNotification);

    // The device is live in the edit here, so nothing in this check drives
    // its audio: the graph may be calling applyToBuffer on another thread.
    // What the tap does with real signal is checked in checkDevice, on a
    // device that belongs to nobody.
    require(eq->analyserMode() != EqEngine::AnalyserMode::Off);

    auto panel = std::make_unique<DeviceEditorPanel>(session);
    panel->setTarget(0, *eqSlot, true);
    require(panel->preferredWidth() > 600);
    panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);

    std::vector<juce::Rectangle<int>> knobs;
    for (auto* child : panel->getChildren())
    {
        require(panel->getLocalBounds().contains(child->getBounds()));
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
            knobs.push_back(child->getBounds());
    }
    // Five: frequency, gain and Q for whichever band is in hand, plus output
    // and scale. The generic grid's twelve are made and then put away.
    require(knobs.size() == 5);
    for (size_t i = 0; i < knobs.size(); ++i)
        for (auto j = i + 1; j < knobs.size(); ++j)
            require(!knobs[i].intersects(knobs[j]));

    // Painting reaches the device through Session::devicePlugin and reads the
    // spectrum tap off it; if that path is wrong this is where it shows.
    const auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds());
    require(snapshot.getWidth() == panel->getWidth());
    require(snapshot.getHeight() == DeviceEditorPanel::standardHeight);

    require(session.deleteDevice(0, eqSlot->pluginIndex).wasOk());
}
}

void checkRhinoEqDsp(Session& session)
{
    checkFilterShapes();
    checkCurveMatchesMeasuredAudio();
    checkBypassScaleAndOutput();
    checkSwitchingIsSafeAndQuiet();
    checkSpectrumReader();
    checkDevice(session);
    checkEditorFace(session);
}
}
