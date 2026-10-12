#include "midi/RhinoArpDevice.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <vector>

namespace rhino
{
namespace
{
constexpr double sampleRate = 48000.0;

void require(bool valid, const juce::String& message)
{
    if (!valid)
        throw std::runtime_error(message.toStdString());
}

int frames(double seconds) { return juce::roundToInt(seconds * sampleRate); }

struct Input
{
    int frame;
    juce::MidiMessage message;
};

Input on(double seconds, int pitch = 60, int channel = 1)
{
    return { frames(seconds), juce::MidiMessage::noteOn(channel, pitch, 0.8f) };
}

Input off(double seconds, int pitch = 60, int channel = 1)
{
    return { frames(seconds), juce::MidiMessage::noteOff(channel, pitch) };
}

struct Output
{
    double seconds;
    juce::MidiMessage message;
    bool allNotesOff = false;
};

// Drive the actual plugin with a moving edit clock, or a frozen playhead and
// advancing audio blocks. Output times always use elapsed audio time so the
// same assertions cover live playing while the transport is stopped.
struct Harness
{
    explicit Harness(te::Edit& edit)
        : plugin(edit.getPluginCache().createNewPlugin(RhinoArpDevice::xmlTypeName, {})),
          arp(dynamic_cast<RhinoArpDevice*>(plugin.get()))
    {
        require(arp != nullptr, "could not construct Rhino Arp");
        arp->initialise({{}, sampleRate, 24000});
    }

    ~Harness() { arp->deinitialise(); }

    void set(const char* id, float value)
    {
        auto parameter = arp->getAutomatableParameterByID(id);
        require(parameter != nullptr, "missing parameter " + juce::String(id));
        parameter->setParameter(value, juce::dontSendNotification);
    }

    void plain()
    {
        set("style", 0.0f);
        set("rate", 1.0f);
        set("gate", 200.0f);
        set("distance", 0.0f);
        set("steps", 0.0f);
        set("groove", 0.0f);
        set("hold", 0.0f);
        set("retrigger", 0.0f);
        set("repeats", 0.0f);
        set("root", 0.0f);
        set("scale", 0.0f);
    }

    void render(double seconds, std::vector<Input> input = {}, bool playing = true, int blockSize = 256)
    {
        std::stable_sort(input.begin(), input.end(), [] (const Input& a, const Input& b)
        { return a.frame < b.frame; });
        const auto total = frames(seconds);
        size_t next = 0;
        te::MidiMessageArray midi;
        for (int done = 0; done < total;)
        {
            const auto count = std::min(blockSize, total - done);
            midi.clear();
            midi.isAllNotesOff = allNotesOffNextBlock;
            allNotesOffNextBlock = false;
            while (next < input.size() && input[next].frame < done + count)
            {
                midi.addMidiMessage(input[next].message, (input[next].frame - done) / sampleRate, {});
                ++next;
            }
            const auto start = playing ? elapsed / sampleRate + editOffset : 2.0;
            const auto end = playing ? start + count / sampleRate : start;
            const tracktion::core::TimeRange time {
                tracktion::core::TimePosition::fromSeconds(start),
                tracktion::core::TimePosition::fromSeconds(end) };
            te::PluginRenderContext context(nullptr, 0, count, &midi, 0.0, time,
                                             playing, false, playing, false);
            arp->applyToBuffer(context);
            if (midi.isAllNotesOff)
                output.push_back({ elapsed / sampleRate, {}, true });
            for (const auto& message : midi)
            {
                require(message.getTimeStamp() >= -1.0e-8
                            && message.getTimeStamp() <= count / sampleRate + 1.0e-8,
                        "output timestamp is outside its audio block");
                output.push_back({ elapsed / sampleRate + message.getTimeStamp(), message, false });
            }
            elapsed += count;
            done += count;
        }
    }

    std::vector<Output> onsets(double from = 0.0, int channel = 0) const
    {
        std::vector<Output> result;
        for (const auto& event : output)
            if (!event.allNotesOff && event.message.isNoteOn() && event.seconds >= from - 1.0e-8
                && (channel == 0 || event.message.getChannel() == channel))
                result.push_back(event);
        return result;
    }

    void requireBalanced() const
    {
        std::array<int, 16 * 128> sounding {};
        for (const auto& event : output)
        {
            if (event.allNotesOff)
            {
                sounding.fill(0);
                continue;
            }
            const auto& message = event.message;
            const auto channel = message.getChannel();
            if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                for (int pitch = 0; pitch < 128; ++pitch)
                    sounding[static_cast<size_t>((channel - 1) * 128 + pitch)] = 0;
                continue;
            }
            if (!message.isNoteOnOrOff())
                continue;
            auto& count = sounding[static_cast<size_t>((channel - 1) * 128 + message.getNoteNumber())];
            const auto where = " at " + juce::String(event.seconds, 6) + " s, channel "
                + juce::String(channel) + ", pitch " + juce::String(message.getNoteNumber());
            if (message.isNoteOn())
            {
                require(count == 0, "same pitch retriggered before its previous note-off" + where);
                ++count;
            }
            else
            {
                require(count == 1, "note-off has no sounding note" + where);
                --count;
            }
        }
        require(std::all_of(sounding.begin(), sounding.end(), [] (int count) { return count == 0; }),
                "generated notes remain sounding after input release");
    }

    te::Plugin::Ptr plugin;
    RhinoArpDevice* arp;
    std::vector<Output> output;
    std::int64_t elapsed = 0;
    bool allNotesOffNextBlock = false;
    double editOffset = 0.0;
};
}

void runArpMechanicsTests(te::Edit& edit)
{
    juce::StringArray failures;
    const auto check = [&] (const juce::String& name, const std::function<void()>& test)
    {
        std::fprintf(stderr, "Rhino Arp check: %s\n", name.toRawUTF8());
        try { test(); }
        catch (const std::exception& error) { failures.add(name + ": " + error.what()); }
    };

    check("default Hold", [&]
    {
        Harness test(edit);
        require(test.arp->getAutomatableParameterByID("hold")->getCurrentValue() < 0.5f,
                "new devices must stop when their input notes end; Hold should default to Off");
    });

    for (const auto playing : {true, false})
        check(playing ? "single note, eight repeats" : "single note, eight repeats, stopped transport", [&]
        {
            Harness test(edit);
            test.plain();
            test.set("gate", 84.0f);
            test.set("repeats", 8.0f);
            test.render(2.0, { on(0.0), off(1.8) }, playing, 480);
            require(test.onsets().size() == 8, "a single held note with eight repeats must produce exactly eight onsets");
            require(test.onsets(1.0).empty(), "eight repeats must finish after one second at 1/16 and 120 bpm");
            test.requireBalanced();

            Harness tap(edit);
            tap.set("hold", 0.0f);
            tap.set("repeats", 8.0f);
            tap.render(2.0, { on(0.0), off(0.05) }, playing, 480);
            require(tap.onsets(0.05).empty(), "releasing a single note must stop even with Beat retrigger enabled");
            tap.requireBalanced();
        });

    for (const auto playing : {true, false})
        for (const auto block : {64, 127, 512, 24000})
            check(juce::String(playing ? "playing" : "stopped") + " release / " + juce::String(block), [&]
            {
                Harness test(edit);
                test.plain();
                test.render(0.6, { on(0.005), off(0.020) }, playing, block);
                const auto notes = test.onsets();
                require(notes.size() == 1 && std::abs(notes.front().seconds - 0.005) < 1.0e-6,
                        "expected one onset at the input timestamp");
                require(std::any_of(test.output.begin(), test.output.end(), [] (const Output& event)
                { return event.message.isNoteOff() && std::abs(event.seconds - 0.020) < 1.0e-6; }),
                        "final input release must release the generated gate at 0.020 s");
                test.requireBalanced();
            });

    for (const auto playing : {true, false})
        check(playing ? "overlapping gates" : "stopped late release", [&]
        {
            Harness test(edit);
            test.plain();
            test.render(0.9, { on(0.0), off(0.53) }, playing, 127);
            require(test.onsets().size() == 5 && test.onsets(0.53).empty(),
                    "expected five 1/16 onsets and no new notes after final release");
            test.requireBalanced();
        });

    check("Hold latch, replace, disable", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("style", 4.0f);
        test.set("hold", 1.0f);
        test.render(0.2, { on(0.0, 60), on(0.0, 64), off(0.04, 60), off(0.05, 64) });
        require(test.onsets(0.1).size() == 2, "Hold must continue the chord after every input releases");
        test.render(0.2, { on(0.0, 67), off(0.03, 67) });
        const auto replacement = test.onsets(0.2);
        require(replacement.size() == 2 && std::all_of(replacement.begin(), replacement.end(),
                    [] (const Output& event) { return event.message.getNoteNumber() == 67; }),
                "the first key after lifting all keys must replace the latched chord");
        test.set("hold", 0.0f);
        test.render(0.4);
        require(test.onsets(0.4).empty(), "turning Hold off must stop future steps");
        test.requireBalanced();
    });

    check("same pitch on independent channels", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("style", 4.0f);
        test.render(0.5, { on(0.0, 60, 1), on(0.0, 60, 2), off(0.02, 60, 1), off(0.2, 60, 2) });
        require(test.onsets(0.0, 1).size() == 1 && test.onsets(0.0, 2).size() == 2,
                "releasing channel 1 must leave the same pitch held on channel 2");
        test.requireBalanced();
    });

    for (const auto controller : {120, 123})
        check("channel panic CC" + juce::String(controller), [&]
        {
            Harness test(edit);
            test.plain();
            test.set("style", 4.0f);
            test.set("hold", 1.0f);
            test.render(0.2, { on(0.0, 60, 1), on(0.0, 64, 2),
                { frames(0.02), juce::MidiMessage::controllerEvent(1, controller, 0) } });
            require(test.onsets(0.02, 1).empty(), "a MIDI panic controller must clear the channel's held chord");
            require(test.onsets(0.02, 2).size() == 1, "a MIDI panic controller must preserve other channels");
            test.render(0.4, { { 0, juce::MidiMessage::controllerEvent(2, controller, 0) } });
            require(test.onsets(0.2).empty(), "panic must stop latched notes on later blocks");
            test.requireBalanced();
        });

    for (const auto panic : {true, false})
        check(panic ? "midiPanic releases downstream notes" : "reset releases downstream notes", [&]
        {
            Harness test(edit);
            test.plain();
            test.render(0.02, { on(0.0) });
            if (panic) test.arp->midiPanic(); else test.arp->reset();
            test.render(0.4);
            require(test.onsets(0.02).empty(), "panic/reset must stop future ticks");
            test.requireBalanced();
        });

    check("transport stop and subsequent live input", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("hold", 1.0f);
        test.render(0.03, { on(0.0) });
        test.render(0.3, {}, false);
        require(test.onsets(0.03).empty(), "stopping transport must clear its previous chord");
        test.requireBalanced();
        test.set("hold", 0.0f);
        test.render(0.5, { on(0.01, 67), off(0.3, 67) }, false);
        const auto live = test.onsets(0.33);
        require(live.size() == 3 && std::all_of(live.begin(), live.end(),
                    [] (const Output& event) { return event.message.getNoteNumber() == 67; }),
                "fresh live input must still arpeggiate with the transport stopped");
        test.requireBalanced();
    });

    check("combined reset and input panic consumes both requests", [&]
    {
        Harness test(edit);
        test.plain();
        test.render(0.02, { on(0.0) });
        test.arp->reset();
        test.allNotesOffNextBlock = true;
        test.render(0.5, { on(0.0, 67), off(0.3, 67) });
        const auto fresh = test.onsets(0.02);
        require(fresh.size() == 3 && std::all_of(fresh.begin(), fresh.end(),
                    [] (const Output& event) { return event.message.getNoteNumber() == 67; }),
                "fresh input in the panic block must survive into subsequent blocks");
        test.requireBalanced();
    });

    check("seek clears a latched pattern", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("hold", 1.0f);
        test.render(0.03, { on(0.0), off(0.02) });
        test.editOffset = 4.0;
        test.render(0.3);
        require(test.onsets(0.03).empty(), "a transport discontinuity must clear old latched notes");
        test.requireBalanced();
        test.set("hold", 0.0f);
        test.render(0.5, { on(0.01, 67), off(0.3, 67) });
        require(test.onsets(0.33).size() == 3, "fresh input must still play after a seek");
        test.requireBalanced();
    });

    check("saved Hold and scale choices survive reload", [&]
    {
        for (const auto scale : {2, 12})
        {
            Harness test(edit);
            test.set("hold", 1.0f);
            test.set("scale", static_cast<float>(scale));
            auto xml = test.arp->state.createXml();
            auto state = juce::ValueTree::fromXml(*xml);
            state.removeProperty(te::IDs::id, nullptr);
            auto reloaded = edit.getPluginCache().createNewPlugin(state);
            require(reloaded != nullptr, "could not reload saved arp state");
            require(reloaded->getAutomatableParameterByID("hold")->getCurrentValue() == 1.0f,
                    "changing the default must preserve explicitly saved Hold On");
            require(reloaded->getAutomatableParameterByID("scale")->getCurrentValue() == static_cast<float>(scale),
                    "saved scale index changed during reload");
        }
    });

    check("finite repeats and explicit Beat retrigger", [&]
    {
        Harness finite(edit);
        finite.plain();
        finite.set("gate", 50.0f);
        finite.set("repeats", 1.0f);
        finite.set("steps", 1.0f);
        finite.set("distance", 12.0f);
        finite.render(2.0, { on(0.0, 60), on(0.0, 64), on(0.0, 67),
                             off(1.8, 60), off(1.8, 64), off(1.8, 67) });
        const auto notes = finite.onsets();
        const std::array<int, 6> expected {60, 64, 67, 72, 76, 79};
        require(notes.size() == expected.size(), "one repeat must end after the complete transposed pattern");
        for (size_t i = 0; i < notes.size(); ++i)
            require(notes[i].message.getNoteNumber() == expected[i], "finite repeat played an incorrect pattern pitch");
        finite.requireBalanced();

        Harness beat(edit);
        beat.plain();
        beat.set("gate", 50.0f);
        beat.set("repeats", 1.0f);
        beat.set("retrigger", 2.0f);
        beat.set("interval", 0.0f);
        beat.render(1.4, { on(0.0), off(1.1) });
        const auto restarts = beat.onsets();
        require(restarts.size() == 3, "Beat retrigger should restart a finished finite repeat each quarter note");
        for (size_t i = 0; i < restarts.size(); ++i)
            require(std::abs(restarts[i].seconds - i * 0.5) < 1.0e-6,
                    "Beat retrigger must follow its selected interval");
        beat.requireBalanced();
    });

    check("dense chord retriggers retain every release", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("style", 4.0f);
        test.set("retrigger", 1.0f);
        std::vector<Input> input;
        for (int press = 0; press < 40; ++press)
            for (int pitch = 48; pitch < 64; ++pitch)
                input.push_back(on(press * 0.001, pitch));
        for (int pitch = 48; pitch < 64; ++pitch)
            input.push_back(off(0.05, pitch));
        test.render(0.4, std::move(input), true, 127);
        require(test.onsets().size() == 640, "40 retriggers of a 16-note chord must produce 640 onsets");
        require(test.onsets(0.05).empty(), "dense retriggers must still stop when the chord releases");
        test.requireBalanced();
    });

    check("Beat retrigger falls between free-rate steps", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("gate", 50.0f);
        test.set("rateMode", 1.0f);
        test.set("freeRate", 80.0f);
        test.set("repeats", 1.0f);
        test.set("retrigger", 2.0f);
        test.set("interval", 0.0f);
        test.render(1.2, { on(0.0, 60), on(0.0, 64), on(0.0, 67),
                           off(1.01, 60), off(1.01, 64), off(1.01, 67) }, true, 127);
        const auto notes = test.onsets();
        const std::array<double, 7> expected {0.0, 0.08, 0.16, 0.5, 0.58, 0.66, 1.0};
        require(notes.size() == expected.size(), "free-rate finite patterns must restart on each Beat interval");
        for (size_t i = 0; i < expected.size(); ++i)
            require(std::abs(notes[i].seconds - expected[i]) < 1.0e-6,
                    "onset " + juce::String(static_cast<int>(i)) + " was " + juce::String(notes[i].seconds, 6)
                    + " s; expected " + juce::String(expected[i], 6) + " s");
        test.requireBalanced();
    });

    check("style directions and bounce endpoints", [&]
    {
        const std::array<std::vector<int>, 4> expected {{
            {60, 64, 67, 60, 64, 67}, {67, 64, 60, 67, 64, 60},
            {60, 64, 67, 64, 60, 64}, {67, 64, 60, 64, 67, 64}
        }};
        for (int style = 0; style < 4; ++style)
        {
            Harness test(edit);
            test.plain();
            test.set("style", static_cast<float>(style));
            test.set("gate", 50.0f);
            test.render(0.9, { on(0.0, 60), on(0.0, 64), on(0.0, 67),
                              off(0.7, 60), off(0.7, 64), off(0.7, 67) });
            const auto notes = test.onsets();
            const auto& pattern = expected[static_cast<size_t>(style)];
            require(notes.size() == pattern.size(), "style must play six steps before release");
            for (size_t i = 0; i < pattern.size(); ++i)
                require(notes[i].message.getNoteNumber() == pattern[i],
                        "style " + juce::String(style) + " has an incorrect direction or repeated endpoint");
            test.requireBalanced();
        }
    });

    // Expected pitches are musical intervals, independent of the production
    // scale masks, so a wrong dropdown mapping cannot agree with its own test.
    const std::array<std::vector<int>, 13> scales {{
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 4, 5, 7, 9, 11},
        {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11},
        {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
        {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10},
        {0, 1, 3, 5, 6, 8, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10},
        {0, 3, 5, 6, 7, 10}, {0, 2, 4, 6, 8, 10}
    }};
    check("scale choice compatibility", [&]
    {
        require(RhinoArpDevice::parameterChoiceCount(RhinoArpDevice::scaleParameter) == 13,
                "Scale must expose all thirteen supported choices");
        const std::array<const char*, 3> originals {"Chromatic", "Major", "Minor"};
        for (int index = 0; index < 3; ++index)
            require(RhinoArpDevice::parameterChoiceName(RhinoArpDevice::scaleParameter, index)
                        == originals[static_cast<size_t>(index)],
                    "the original saved scale indices must retain their meaning");
    });
    for (size_t scale = 0; scale < scales.size(); ++scale)
        check("scale degrees " + juce::String(static_cast<int>(scale)), [&]
        {
            Harness test(edit);
            test.plain();
            test.set("gate", 50.0f);
            test.set("scale", static_cast<float>(scale));
            test.set("steps", 8.0f);
            test.set("distance", 1.0f);
            test.set("rateMode", 1.0f);
            test.set("freeRate", 10.0f);
            test.render(0.12, { on(0.0), off(0.085) }, true, 127);
            const auto notes = test.onsets();
            require(notes.size() == 9, "expected nine scale-degree steps");
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const auto& intervals = scales[scale];
                const auto expected = 60 + intervals[i % intervals.size()] + 12 * static_cast<int>(i / intervals.size());
                require(notes[i].message.getNoteNumber() == expected,
                        "degree " + juce::String(static_cast<int>(i)) + " produced pitch "
                        + juce::String(notes[i].message.getNoteNumber()) + "; expected " + juce::String(expected));
            }
            test.requireBalanced();
            for (const auto direction : {-1, 1})
            {
                Harness octave(edit);
                octave.plain();
                octave.set("gate", 50.0f);
                octave.set("scale", static_cast<float>(scale));
                octave.set("steps", 1.0f);
                octave.set("distance", static_cast<float>(direction * static_cast<int>(scales[scale].size())));
                octave.render(0.3, { on(0.0), off(0.2) });
                const auto moved = octave.onsets();
                require(moved.size() == 2 && moved[1].message.getNoteNumber() == 60 + 12 * direction,
                        "one full scale must transpose by one octave in either direction");
                octave.requireBalanced();
            }
        });

    check("root offsets and chromatic passing notes", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("gate", 50.0f);
        test.set("scale", 1.0f);
        test.set("root", 11.0f);
        test.set("steps", 3.0f);
        test.set("distance", 1.0f);
        test.render(0.6, { on(0.0, 5), off(0.45, 5) });
        const auto notes = test.onsets();
        const std::array<int, 4> expected {5, 7, 9, 11};
        require(notes.size() == expected.size(), "expected four transpositions from low F in B major");
        for (size_t i = 0; i < expected.size(); ++i)
            require(notes[i].message.getNoteNumber() == expected[i],
                    "transposition must retain the passing note's offset, including pitches below the root");
        test.requireBalanced();
    });

    if (!failures.isEmpty())
        throw std::runtime_error(("Rhino Arp mechanics:\n" + failures.joinIntoString("\n")).toStdString());
    juce::Logger::writeToLog("Rhino: Arp note lifecycle, gate balance, channels and finite repeats passed");
}
}
