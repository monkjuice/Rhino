#include "../Session.h"
#include "../BrowserPanel.h"
#include "../CountInClick.h"
#include "../ComputerKeyboard.h"
#include "ContentLibrary.h"
// A test drives the DSP directly, so unlike the rest of the app it needs the
// device definitions rather than their catalog entries.
#include "audio/UtilityDevice.h"
#include "audio/RhinoBloomDevice.h"
#include "audio/RhinoSpaceDevice.h"
#include "instruments/DrumDevice.h"
#include "instruments/RhinoWaveDevice.h"
#include "midi/RhinoArpDevice.h"
#include <algorithm>
#include <cmath>
#include <source_location>
#include <stdexcept>

namespace rhino
{
int runSelfTest()
{
    try
    {
        auto require = [](bool valid, const std::source_location location = std::source_location::current())
        {
            if (!valid)
                throw std::runtime_error("Native device check failed at line " + std::to_string(location.line()));
        };
        Session session;
        require(session.utility != nullptr);
        auto& device = *session.utility;
        device.gain().setParameter(-6.0f, juce::dontSendNotification);
        device.initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> buffer(2, 520);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i) buffer.setSample(c, i, 1.0f);
        te::PluginRenderContext context(&buffer, 4, 512, nullptr, 0.0, {}, false, false, true, false);
        device.applyToBuffer(context);
        const auto expected = std::pow(10.0f, -6.0f / 20.0f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i)
                require(std::abs(buffer.getSample(c, i) - (i >= 4 && i < 516 ? expected : 1.0f)) < 1.0e-5f);

        // Exercise smoothing and equal gain on every channel; frame offsets
        // and stereo consistency catch errors hidden by single-channel tones.
        device.gain().setParameter(0.0f, juce::dontSendNotification);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i) buffer.setSample(c, i, 1.0f);
        device.applyToBuffer(context);
        require(buffer.getSample(0, 4) > expected && buffer.getSample(0, 4) < 1.0f);
        require(std::abs(buffer.getSample(0, 515) - 1.0f) < 1.0e-5f);
        for (int i = 4; i < 516; ++i)
            require(buffer.getSample(0, i) == buffer.getSample(1, i));
        te::PluginRenderContext noAudio(nullptr, 0, 0, nullptr, 0.0, {}, false, false, true, false);
        device.applyToBuffer(noAudio);

        auto restored = device.state.createCopy();
        restored.setProperty("gainDb", -12.0f, nullptr);
        device.restorePluginStateFromValueTree(restored);
        require(std::abs(device.gain().getCurrentValue() + 12.0f) < 1.0e-5f);
        auto xml = device.state.createXml();
        auto roundTrip = juce::ValueTree::fromXml(*xml);
        roundTrip.removeProperty(te::IDs::id, nullptr);
        auto reloaded = session.edit->getPluginCache().createNewPlugin(roundTrip);
        require(dynamic_cast<UtilityDevice*>(reloaded.get()) != nullptr);
        require(std::abs(reloaded->getAutomatableParameterByID("gainDb")->getCurrentValue() + 12.0f) < 1.0e-5f);
        device.deinitialise();

        // The drum DSP is exercised on its own instance rather than the one on
        // the pattern track, which is replaced whenever the instrument changes.
        // The device catalog is meant to be the only place a device is
        // declared: the browser, the add path and the engine all read it. A
        // device added there and nowhere else must therefore be offered by
        // the browser and be addable, which is what these two check.
        {
            BrowserPanel browser(session);
            const auto& rows = browser.libraryItems();
            for (const auto& device : DeviceCatalog::all())
            {
                if (!device.browsable)
                    continue;
                const auto listed = std::any_of(rows.begin(), rows.end(),
                    [&device](const BrowserPanel::Item& row) { return row.deviceId == device.id; });
                require(listed);   // browsable catalog device has a browser row
            }
            for (const auto& row : rows)
                if (row.deviceId.isNotEmpty())
                    require(DeviceCatalog::byId(row.deviceId) != nullptr);

            // The sample library is content on disk, so the browser has to be
            // showing what is actually there rather than a list written into
            // the source. Zero on both sides is a pass: a build without the
            // library present still has a working browser.
            const auto libraryRows = std::count_if(rows.begin(), rows.end(),
                [](const BrowserPanel::Item& row) { return row.file != juce::File(); });
            require(static_cast<size_t>(libraryRows) == ContentLibrary::samples().size());
            for (const auto& row : rows)
                if (row.file != juce::File())
                    require(row.file.existsAsFile());
            juce::Logger::writeToLog("Rhino: browser lists " + juce::String(libraryRows)
                                     + " library samples");
        }
        for (const auto& device : DeviceCatalog::all())
        {
            // An external device needs its plugin installed, which a test
            // machine may not have, so it is the one thing skipped here.
            if (device.external)
                continue;
            require(session.addDevice(device.id, 0).wasOk());
        }
        require(session.addDevice("NoSuchDevice", 0).failed());


        auto drumPlugin = session.edit->getPluginCache().createNewPlugin(DrumDevice::xmlTypeName, {});
        auto* drums = dynamic_cast<DrumDevice*>(drumPlugin.get());
        require(drums != nullptr);
        drums->initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> drumBuffer(2, 4096);
        drumBuffer.clear();
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 56, 1.0f), 0.0, {});
        te::PluginRenderContext drumContext(&drumBuffer, 0, drumBuffer.getNumSamples(), &midi, 0.0, {}, true, false, true, false);
        drums->applyToBuffer(drumContext);
        float clapPeak = 0.0f;
        for (int c = 0; c < drumBuffer.getNumChannels(); ++c)
            for (int i = 0; i < drumBuffer.getNumSamples(); ++i)
            {
                const auto sample = drumBuffer.getSample(c, i);
                require(std::isfinite(sample));
                clapPeak = std::max(clapPeak, std::abs(sample));
            }
        require(clapPeak > 0.0001f && clapPeak < 1.0f);

        // Tracktion timestamps plugin MIDI relative to the render block. A hit
        // must begin at that sample rather than at the start of the callback.
        drums->reset();
        drumBuffer.clear();
        midi.clear();
        constexpr int eventOffset = 128;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 48, 1.0f), eventOffset / 48000.0, {});
        te::PluginRenderContext offsetDrumContext(&drumBuffer, 4, 512, &midi, 0.0, {}, true, false, true, false);
        drums->applyToBuffer(offsetDrumContext);
        for (int c = 0; c < drumBuffer.getNumChannels(); ++c)
            for (int i = 4; i < 4 + eventOffset; ++i)
                require(drumBuffer.getSample(c, i) == 0.0f);
        float offsetKickPeak = 0.0f;
        for (int c = 0; c < drumBuffer.getNumChannels(); ++c)
            for (int i = 4 + eventOffset; i < 4 + 512; ++i)
                offsetKickPeak = std::max(offsetKickPeak, std::abs(drumBuffer.getSample(c, i)));
        require(offsetKickPeak > 0.0001f);

        // A note that lands on a block boundary. The engine hands the block
        // only the messages that belong to it, but the timestamps saying so
        // are floating point, and a note sitting exactly on a boundary can
        // arrive at the end of the block before rather than the start of the
        // one after - stamped at the block's whole length. Rounding that to
        // the sample past the end and then never reaching it dropped the note
        // outright. Live this is not an edge case: at 48 kHz in 480 sample
        // blocks every beat at 120 bpm falls on a boundary, and a clip whose
        // start carries a residue from being dragged puts every note it holds
        // on the wrong side of one.
        constexpr int liveBlock = 480;
        const auto blockEnergy = [&](std::initializer_list<double> stamps)
        {
            drumBuffer.clear();
            midi.clear();
            for (const auto stamp : stamps)
                midi.addMidiMessage(juce::MidiMessage::noteOn(1, 48, 1.0f), stamp, {});
            te::PluginRenderContext boundary(&drumBuffer, 0, liveBlock, &midi, 0.0, {}, true, false, true, false);
            drums->applyToBuffer(boundary);
            // Energy, not peak: two strikes a few samples apart barely move the
            // peak, because the first is already past its own by then, but they
            // plainly double what the block carries.
            auto energy = 0.0f;
            for (int i = 0; i < liveBlock; ++i)
                energy += std::abs(drumBuffer.getSample(0, i));
            return energy;
        };
        const auto struckOnce = [&](std::initializer_list<double> stamps)
        {
            drums->reset();
            return blockEnergy(stamps);
        };
        const auto onTheBeat = struckOnce({0.0});
        require(onTheBeat > 0.0001f);
        // Stamped at the block's whole length - the case that was dropped - and
        // inside its last sample, which rounds to the same place. Either plays
        // on the final sample of its own block, so the block after it is where
        // the hit is plainly there or plainly missing.
        for (const auto lateStamp : {liveBlock / 48000.0, (liveBlock - 0.25) / 48000.0})
        {
            drums->reset();
            blockEnergy({lateStamp});
            require(blockEnergy({}) > onTheBeat * 0.5f);
        }

        // One written note can also arrive twice that way: once at the end of
        // the block it straddles and once a few samples into the next. Two
        // strikes on one pad that close are not two hits, they are one at twice
        // the level, so the pad refuses the second. Whether it did is read off
        // the block itself rather than off its level: two kicks a few samples
        // apart cancel as readily as they add, so a sum says little.
        const auto blockAfter = [&](std::initializer_list<double> stamps)
        {
            drums->reset();
            juce::ignoreUnused(blockEnergy(stamps));
            std::vector<float> rendered(static_cast<size_t>(liveBlock));
            for (int i = 0; i < liveBlock; ++i)
                rendered[static_cast<size_t>(i)] = drumBuffer.getSample(0, i);
            return rendered;
        };
        const auto oneStrike = blockAfter({0.0});
        require(blockAfter({0.0, 4.8 / 48000.0}) == oneStrike);
        // A hit outside the guard is a hit of its own. Two milliseconds is
        // already far closer than a pattern can write: the shortest thing the
        // grid offers, a sixty-fourth at 200 bpm, is nineteen.
        require(blockAfter({0.0, 96.0 / 48000.0}) != oneStrike);
        drums->deinitialise();

        auto bloomPlugin = session.edit->getPluginCache().createNewPlugin(RhinoBloomDevice::xmlTypeName, {});
        auto* bloom = dynamic_cast<RhinoBloomDevice*>(bloomPlugin.get());
        require(bloom != nullptr);
        require(bloom->getAutomatableParameters().size() == 6);
        bloom->getAutomatableParameterByID("bloom")->setParameter(0.7f, juce::dontSendNotification);
        bloom->getAutomatableParameterByID("clouds")->setParameter(0.55f, juce::dontSendNotification);
        bloom->getAutomatableParameterByID("plate")->setParameter(0.42f, juce::dontSendNotification);
        bloom->initialise({{}, 48000.0, 256});
        juce::AudioBuffer<float> bloomBuffer(2, 1024);
        for (int i = 0; i < bloomBuffer.getNumSamples(); ++i)
        {
            const auto impulse = i == 0 ? 0.5f : 0.0f;
            bloomBuffer.setSample(0, i, impulse);
            bloomBuffer.setSample(1, i, impulse * 0.8f);
        }
        te::PluginRenderContext bloomContext(&bloomBuffer, 0, bloomBuffer.getNumSamples(), nullptr, 0.0, {}, false, false, true, false);
        bloom->applyToBuffer(bloomContext);
        float bloomEnergy = 0.0f;
        for (int c = 0; c < bloomBuffer.getNumChannels(); ++c)
            for (int i = 0; i < bloomBuffer.getNumSamples(); ++i)
            {
                const auto sample = bloomBuffer.getSample(c, i);
                require(std::isfinite(sample));
                bloomEnergy += std::abs(sample);
            }
        require(bloomEnergy > 0.01f);
        auto bloomState = bloom->state.createCopy();
        bloomState.setProperty("chorus", 0.25f, nullptr);
        bloom->restorePluginStateFromValueTree(bloomState);
        require(std::abs(bloom->getAutomatableParameterByID("chorus")->getCurrentValue() - 0.25f) < 1.0e-5f);
        bloom->deinitialise();

        auto wavePlugin = session.edit->getPluginCache().createNewPlugin(RhinoWaveDevice::xmlTypeName, {});
        auto* wave = dynamic_cast<RhinoWaveDevice*>(wavePlugin.get());
        require(wave != nullptr);
        wave->initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> waveBuffer(2, 4096);
        waveBuffer.clear();
        te::MidiMessageArray lowMidi;
        lowMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 5, 1.0f), 0.0, {});
        te::PluginRenderContext waveContext(&waveBuffer, 0, waveBuffer.getNumSamples(), &lowMidi, 0.0, {}, true, false, true, false);
        wave->applyToBuffer(waveContext);
        float wavePeak = 0.0f;
        for (int c = 0; c < waveBuffer.getNumChannels(); ++c)
            for (int i = 0; i < waveBuffer.getNumSamples(); ++i)
            {
                const auto sample = waveBuffer.getSample(c, i);
                require(std::isfinite(sample));
                wavePeak = std::max(wavePeak, std::abs(sample));
            }
        require(wavePeak < 1.0f);
        wave->reset();
        auto wavePosition = wave->getAutomatableParameterByID("position");
        auto waveShape = wave->getAutomatableParameterByID("shape");
        auto waveMotion = wave->getAutomatableParameterByID("motion");
        auto waveOsc2 = wave->getAutomatableParameterByID("osc2Level");
        auto waveTune2 = wave->getAutomatableParameterByID("osc2Tune");
        auto waveCutoff = wave->getAutomatableParameterByID("cutoff");
        auto waveLfoRate = wave->getAutomatableParameterByID("lfoRate");
        auto waveLfoPosition = wave->getAutomatableParameterByID("lfoPosition");
        auto waveLfoCutoff = wave->getAutomatableParameterByID("lfoCutoff");
        auto waveLfoPitch = wave->getAutomatableParameterByID("lfoPitch");
        auto waveLfoMotion = wave->getAutomatableParameterByID("lfoMotion");
        require(wavePosition != nullptr && waveShape != nullptr && waveMotion != nullptr
                && waveOsc2 != nullptr && waveTune2 != nullptr && waveCutoff != nullptr
                && waveLfoRate != nullptr && waveLfoPosition != nullptr && waveLfoCutoff != nullptr
                && waveLfoPitch != nullptr && waveLfoMotion != nullptr);
        juce::AudioBuffer<float> sweepBuffer(2, 512);
        te::MidiMessageArray sweepMidi;
        float sweepPeak = 0.0f, maxJump = 0.0f, previousSample = 0.0f;
        for (int block = 0; block < 32; ++block)
        {
            sweepBuffer.clear();
            sweepMidi.clear();
            if (block == 0)
                sweepMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 61, 0.8f), 0.0, {});
            if (block == 16)
                sweepMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 73, 0.55f), 0.0, {});
            const auto phase = static_cast<float>(block) / 31.0f;
            wavePosition->setParameter(phase, juce::dontSendNotification);
            waveShape->setParameter(1.0f - phase * 0.7f, juce::dontSendNotification);
            waveMotion->setParameter(0.1f + phase * 0.75f, juce::dontSendNotification);
            waveOsc2->setParameter(phase, juce::dontSendNotification);
            waveTune2->setParameter(block % 2 == 0 ? 12.0f : -12.0f, juce::dontSendNotification);
            waveCutoff->setParameter(350.0f + phase * 12000.0f, juce::dontSendNotification);
            waveLfoRate->setParameter(0.25f + phase * 12.0f, juce::dontSendNotification);
            waveLfoPosition->setParameter(-0.6f + phase * 1.2f, juce::dontSendNotification);
            waveLfoCutoff->setParameter(0.5f - phase, juce::dontSendNotification);
            waveLfoPitch->setParameter(-4.0f + phase * 8.0f, juce::dontSendNotification);
            waveLfoMotion->setParameter(phase * 0.75f, juce::dontSendNotification);
            te::PluginRenderContext sweepContext(&sweepBuffer, 0, sweepBuffer.getNumSamples(), &sweepMidi, 0.0, {}, true, false, true, false);
            wave->applyToBuffer(sweepContext);
            for (int i = 0; i < sweepBuffer.getNumSamples(); ++i)
            {
                const auto sample = sweepBuffer.getSample(0, i);
                require(std::isfinite(sample));
                sweepPeak = std::max(sweepPeak, std::abs(sample));
                maxJump = std::max(maxJump, std::abs(sample - previousSample));
                previousSample = sample;
            }
        }
        require(sweepPeak > 0.0001f && sweepPeak < 1.0f && maxJump < 0.9f);
        wave->deinitialise();

        // The count-in generator, driven directly rather than through a device.
        // It is a pure function of tempo, meter and sample rate, so the whole
        // of it is checkable here: the right number of clicks, in the right
        // places, and a count that ends on the beat the transport starts on.
        {
            CountInClick click;
            constexpr double rate = 48000.0;
            CountInClick::Settings settings;
            settings.tempoBpm = 120.0;   // half a second a beat
            settings.beatsPerBar = 4.0;
            settings.bars = 3;
            settings.gainDb = 0.0f;
            settings.emphasiseBars = true;
            auto finished = false;
            click.start(settings, rate, [&finished] { finished = true; });
            require(click.isRunning());
            require(click.beatsTotal() == 12);
            require(click.barsRemaining() == 3);

            constexpr int blockSize = 512;
            juce::AudioBuffer<float> block(1, blockSize);
            std::vector<int> onsets;
            auto rendered = 0;
            const auto samplesPerBeat = static_cast<int>(rate * 60.0 / settings.tempoBpm);
            const auto countInSamples = samplesPerBeat * click.beatsTotal();
            // A click is a decaying sine, so it crosses zero many times on the
            // way down: an onset is the first loud sample after a gap, not
            // every loud sample after a quiet one.
            const auto minimumGap = samplesPerBeat / 2;
            // A generous ceiling on the loop, so a generator that never stops
            // fails here rather than hanging the runner.
            for (auto guard = 0; click.isRunning() && guard < 4000; ++guard)
            {
                block.clear();
                auto* channels = block.getArrayOfWritePointers();
                click.renderBlock(channels, 1, blockSize);
                for (int i = 0; i < blockSize; ++i)
                    if (std::abs(block.getSample(0, i)) > 0.05f
                        && (onsets.empty() || rendered + i - onsets.back() > minimumGap))
                        onsets.push_back(rendered + i);
                rendered += blockSize;
            }
            require(!click.isRunning());
            require(rendered >= countInSamples && rendered < countInSamples + blockSize);
            require(static_cast<int>(onsets.size()) == click.beatsTotal());
            // Every click lands on its beat, within the block the transient
            // takes to rise past the threshold.
            for (int beat = 0; beat < static_cast<int>(onsets.size()); ++beat)
            {
                const auto beatSample = static_cast<double>(beat) * rate * 60.0 / settings.tempoBpm;
                require(std::abs(onsets[static_cast<size_t>(beat)] - beatSample) < 64.0);
            }
            require(click.barsRemaining() == 0);
            // The finished callback is posted to the message thread, so it has
            // not run yet; cancelling before it does must not leave it armed.
            require(!finished);
            click.cancel();

            // Off means off: no clicks, and the caller is told at once so a
            // count-in of nothing and a count-in that has ended take one path.
            auto immediate = false;
            CountInClick::Settings none = settings;
            none.bars = 0;
            click.start(none, rate, [&immediate] { immediate = true; });
            require(immediate && !click.isRunning() && click.barsRemaining() == 0);
        }

        // The same generator through the interface the device actually calls.
        // The device manager gives its second and later callbacks a scratch
        // buffer rather than the mix, adds what comes back, and never clears
        // that scratch between blocks - so what arrives is this callback's own
        // previous block. Mixing into it without clearing summed every block
        // onto the last one and then, the count over and renderBlock returning
        // early, emitted the total on every block for as long as it stayed
        // registered: the loud tone that followed pressing Record.
        {
            CountInClick click;
            constexpr double rate = 48000.0;
            constexpr int blockSize = 480, channels = 2;
            CountInClick::Settings settings;
            settings.tempoBpm = 120.0;
            settings.beatsPerBar = 4.0;
            settings.bars = 2;
            settings.gainDb = 0.0f;
            settings.emphasiseBars = true;
            const juce::AudioIODeviceCallbackContext callbackContext {};
            // One buffer, reused and never cleared by the caller, exactly as
            // the device manager reuses its own.
            juce::AudioBuffer<float> scratch(channels, blockSize);
            const auto dirty = [&scratch]
            {
                for (int channel = 0; channel < channels; ++channel)
                    juce::FloatVectorOperations::fill(scratch.getWritePointer(channel), 1.0f, blockSize);
            };
            // Idle, and holding the loudest thing a previous block could have
            // left behind. What the callback owes is silence, not that.
            dirty();
            click.audioDeviceIOCallbackWithContext(nullptr, 0, scratch.getArrayOfWritePointers(),
                                                   channels, blockSize, callbackContext);
            require(scratch.getMagnitude(0, blockSize) == 0.0f);

            auto done = false;
            click.start(settings, rate, [&done] { done = true; });
            auto peak = 0.0f;
            for (auto guard = 0; click.isRunning() && guard < 4000; ++guard)
            {
                click.audioDeviceIOCallbackWithContext(nullptr, 0, scratch.getArrayOfWritePointers(),
                                                       channels, blockSize, callbackContext);
                peak = juce::jmax(peak, scratch.getMagnitude(0, blockSize));
            }
            require(!click.isRunning());
            // A click at 0 dB peaks at one. Block summed onto block it would
            // have passed that inside the first bar and gone on climbing.
            require(peak > 0.5f && peak <= 1.0f);
            // And the count being over is not a licence to play one more.
            click.audioDeviceIOCallbackWithContext(nullptr, 0, scratch.getArrayOfWritePointers(),
                                                   channels, blockSize, callbackContext);
            require(scratch.getMagnitude(0, blockSize) == 0.0f);
            click.cancel();
        }

        // The computer keyboard, as far as it goes without a keyboard being
        // held down. Which notes sound is read from the real key state and is
        // not reachable here; the toggle, the octave, the velocity and which
        // keys it takes are all decisions it makes on its own.
        {
            ComputerKeyboard keys;
            std::vector<std::pair<int, bool>> played;
            keys.note = [&played](int midiNote, int, bool on) { played.emplace_back(midiNote, on); };
            const auto press = [&keys](int code, juce::ModifierKeys mods = {})
            {
                return keys.keyPressed(juce::KeyPress(code, mods, 0), nullptr);
            };
            require(!keys.isEnabled());
            // Off, it takes the toggle and nothing else, so every letter
            // shortcut still belongs to the editor underneath.
            require(press('m'));
            require(keys.isEnabled());
            require(!press('q'));

            // The octave numbering is Forge's, so the two keyboards agree on
            // what a key plays rather than being a semitone or an octave apart.
            require(keys.octave() == ComputerKeyboard::defaultOctave);
            const auto octave = keys.octave();
            const auto velocity = keys.velocity();
            require(press('x') && keys.octave() == octave + 1);
            require(press('z') && keys.octave() == octave);
            require(press('v') && keys.velocity() > velocity);
            require(press('c') && keys.velocity() == velocity);
            // A note key is swallowed so the letter cannot also reach the
            // shortcut it carries; the note itself comes from the key state.
            for (const auto held : {'a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j', 'k', 'o', 'l', 'p', ';'})
                require(press(held));
            require(!press('q') && !press('r'));
            // A function key must never fold onto a letter. F9's code is
            // 0x10078, and squeezing that into a character gives 'x', which
            // would turn the record key into an octave shift.
            const auto beforeFunctionKeys = keys.octave();
            require(!press(juce::KeyPress::F9Key));
            require(!press(juce::KeyPress::F2Key));
            require(!press(juce::KeyPress::F12Key));
            require(keys.octave() == beforeFunctionKeys);
            // A shortcut is still a shortcut: Ctrl+C has to copy, not drop the
            // velocity, or the keyboard would make the editor unusable.
            require(!press('C', juce::ModifierKeys::commandModifier));
            require(!press('V', juce::ModifierKeys::commandModifier));
            require(keys.velocity() == velocity);

            // Neither end runs away.
            for (int i = 0; i < 40; ++i) press('x');
            require(keys.octave() == ComputerKeyboard::highestOctave);
            for (int i = 0; i < 40; ++i) press('z');
            require(keys.octave() == ComputerKeyboard::lowestOctave);
            for (int i = 0; i < 40; ++i) press('v');
            require(keys.velocity() == ComputerKeyboard::maximumVelocity);
            for (int i = 0; i < 40; ++i) press('c');
            require(keys.velocity() == ComputerKeyboard::minimumVelocity);

            require(press('m') && !keys.isEnabled());
            // Switched off, the letters go back to being shortcuts.
            require(!press('a'));
            // Nothing was ever sounded, so nothing should have been released.
            require(played.empty());
        }

        checkAutoTuneDsp(session);
        checkRhinoEqDsp(session);
        checkVocoderDsp(session);

        session.releaseAudioDevice();
        return 0;
    }
    catch (const std::exception& error)
    {
        juce::Logger::writeToLog(error.what());
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
