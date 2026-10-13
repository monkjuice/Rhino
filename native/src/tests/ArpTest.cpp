#include "ArpTestHarness.h"
#include <cstdio>
#include <functional>

namespace rhino
{

void runArpMechanicsTests(te::Edit& edit)
{
    using namespace arp_test;
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
        check(playing ? "factory C major chord" : "factory C major chord, stopped", [&]
        {
            Harness test(edit);
            // Exercise the actual factory sound; plain() used to hide the
            // octave expansion and periodic retrigger enabled by default.
            test.set("scale", 1.0f);
            test.render(1.6, {on(0.03, 60), on(0.04, 64), on(0.05, 67),
                              off(1.44, 60), off(1.44, 64), off(1.44, 67)}, playing, 127);
            const auto notes = test.onsets();
            requirePitches(notes, {60, 64, 67, 60, 64, 67, 60, 64, 67, 60, 64, 67});
            for (size_t i = 0; i < notes.size(); ++i)
                require(std::abs(notes[i].seconds - (0.03 + i * 0.125)) < 1.0e-6,
                        "factory arpeggiation must keep its 1/16 cadence through repeated cycles");
            test.requireBalanced();
        });

    for (const auto playing : {true, false})
        for (const auto block : {64, 127, 480, 512, 24000})
            for (const auto start : {0.03, 0.99})
                check(juce::String(playing ? "playing" : "stopped") + " off-grid Beat retrigger / "
                      + juce::String(block) + " / " + juce::String(start), [&]
                {
                    Harness test(edit);
                    test.plain();
                    test.set("scale", 1.0f);
                    test.set("retrigger", 2.0f);
                    test.set("interval", 1.0f);
                    test.render(start + 1.4, {on(start, 60), on(start + 0.01, 64), on(start + 0.02, 67),
                                              off(start + 1.3, 60), off(start + 1.3, 64), off(start + 1.3, 67)},
                                playing, block);
                    const auto notes = test.onsets();
                    requirePitches(notes, start < 0.5
                        ? std::vector<int>{60, 64, 67, 60, 64, 67, 60, 64, 60, 64, 67}
                        : std::vector<int>{60, 60, 64, 67, 60, 64, 67, 60, 64, 60, 64});
                    for (size_t i = 0; i < notes.size(); ++i)
                        require(std::abs(notes[i].seconds - (start + i * 0.125)) < 1.0e-6,
                                "Beat retrigger must reset the pattern on the existing rate clock");
                    test.requireBalanced();
                });

    for (const auto playing : {true, false})
        for (const auto block : {64, 127, 480, 512, 24000})
            check(juce::String(playing ? "playing" : "stopped") + " second chord after silence / "
                  + juce::String(block), [&]
            {
                Harness test(edit);
                test.plain();
                test.set("retrigger", 2.0f);
                test.set("interval", 1.0f);
                test.render(1.6, {on(0.0, 60), on(0.0, 64), on(0.0, 67),
                                  off(0.2, 60), off(0.2, 64), off(0.2, 67),
                                  on(0.3, 60), on(0.3, 64), on(0.3, 67),
                                  off(1.5, 60), off(1.5, 64), off(1.5, 67)}, playing, block);
                const auto notes = test.onsets(0.3);
                requirePitches(notes, {60, 64, 67, 60, 64, 67, 60, 64, 67, 60});
                // The 1.0 s beat boundary resets the pattern at 1.05 s.
                // Silence must not move that boundary to 1.3 s in small blocks.
                for (size_t i = 0; i < notes.size(); ++i)
                    require(std::abs(notes[i].seconds - (0.3 + i * 0.125)) < 1.0e-6,
                            "a second chord must preserve cadence independently of callback boundaries");
                test.requireBalanced();
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
            tap.set("retrigger", 2.0f);
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

    check("saved choices survive changes to factory defaults", [&]
    {
        for (const auto scale : {2, 12})
        {
            Harness test(edit);
            test.set("hold", 1.0f);
            test.set("scale", static_cast<float>(scale));
            const std::array<std::pair<const char*, float>, 6> saved {{
                {"style", 2.0f}, {"distance", -7.0f}, {"steps", 2.0f},
                {"groove", 2.0f}, {"retrigger", 2.0f}, {"root", 8.0f}
            }};
            for (const auto& [id, value] : saved) test.set(id, value);
            auto xml = test.arp->state.createXml();
            auto state = juce::ValueTree::fromXml(*xml);
            state.removeProperty(te::IDs::id, nullptr);
            auto reloaded = edit.getPluginCache().createNewPlugin(state);
            require(reloaded != nullptr, "could not reload saved arp state");
            require(reloaded->getAutomatableParameterByID("hold")->getCurrentValue() == 1.0f,
                    "changing the default must preserve explicitly saved Hold On");
            require(reloaded->getAutomatableParameterByID("scale")->getCurrentValue() == static_cast<float>(scale),
                    "saved scale index changed during reload");
            for (const auto& [id, value] : saved)
                require(reloaded->getAutomatableParameterByID(id)->getCurrentValue() == value,
                        "new defaults must preserve saved " + juce::String(id));
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

    check("Beat retrigger preserves free-rate steps", [&]
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
                           off(1.05, 60), off(1.05, 64), off(1.05, 67) }, true, 127);
        const auto notes = test.onsets();
        // A 500 ms Beat boundary resets the pattern on the next existing
        // 80 ms tick, at 560 ms; it must never shorten a rate interval.
        const std::array<double, 7> expected {0.0, 0.08, 0.16, 0.56, 0.64, 0.72, 1.04};
        require(notes.size() == expected.size(), "Beat retrigger must preserve the free-rate clock");
        for (size_t i = 0; i < expected.size(); ++i)
            require(std::abs(notes[i].seconds - expected[i]) < 1.0e-6,
                    "onset " + juce::String(static_cast<int>(i)) + " was " + juce::String(notes[i].seconds, 6)
                    + " s; expected " + juce::String(expected[i], 6) + " s");
        test.requireBalanced();
    });

    check("Beat retrigger preserves swing phase", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("rateMode", 1.0f);
        test.set("freeRate", 80.0f);
        test.set("groove", 2.0f);
        test.set("retrigger", 2.0f);
        test.set("interval", 0.0f);
        test.render(1.2, {on(0.0, 60), on(0.0, 64), on(0.0, 67),
                          off(1.1, 60), off(1.1, 64), off(1.1, 67)}, true, 127);
        const auto notes = test.onsets();
        requirePitches(notes, {60, 64, 67, 60, 64, 67, 60, 60, 64, 67, 60, 64, 67, 60});
        for (size_t i = 0; i < notes.size(); ++i)
        {
            // Each pair is 160 ms: 92.8 ms then 67.2 ms. A pattern reset
            // at an odd tick must not introduce two long swing gaps.
            const auto expected = (i / 2) * 0.16 + (i % 2 == 0 ? 0.0 : 0.0928);
            require(std::abs(notes[i].seconds - expected) < 1.0e-6,
                    "pattern retrigger must preserve the independent swing phase");
        }
        test.requireBalanced();
    });

    for (const auto releasedFirst : {60, 61})
        check("quantized chord retains shared output / " + juce::String(releasedFirst), [&]
        {
            Harness test(edit);
            test.plain();
            test.set("style", 4.0f);
            test.set("scale", 1.0f);
            test.render(0.1, {on(0.0, 60), on(0.0, 61), off(0.02, releasedFirst),
                              off(0.05, releasedFirst == 60 ? 61 : 60)}, true, 127);
            requirePitches(test.onsets(), {60});
            const auto releases = std::count_if(test.output.begin(), test.output.end(), [] (const Output& event)
            {
                return event.message.isNoteOff() && event.message.getNoteNumber() == 60
                    && std::abs(event.seconds - 0.05) < 1.0e-6;
            });
            require(releases == 1, "a shared quantized note must release only after both input keys end");
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

    check("live scale and root changes correct an untransposed chord", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("scale", 1.0f);
        test.render(0.375, {on(0.0, 60), on(0.0, 64), on(0.0, 67)});
        requirePitches(test.onsets(), {60, 64, 67});
        test.set("scale", 2.0f);
        test.render(0.375);
        requirePitches(test.onsets(0.375), {60, 63, 67});
        test.set("scale", 1.0f);
        test.set("root", 4.0f);
        test.render(0.375);
        requirePitches(test.onsets(0.75), {59, 64, 66});
        test.set("scale", 0.0f);
        test.set("root", 11.0f);
        test.render(0.375);
        requirePitches(test.onsets(1.125), {60, 64, 67});
        test.render(0.2, {off(0.0, 60), off(0.0, 64), off(0.0, 67)});
        const auto notes = test.onsets();
        for (size_t i = 0; i < notes.size(); ++i)
            require(std::abs(notes[i].seconds - i * 0.125) < 1.0e-6,
                    "changing harmony must leave the rate clock running");
        test.requireBalanced();
    });

    for (const auto direction : {-1, 1})
        check("minor chord, seven-degree octave / " + juce::String(direction), [&]
        {
            Harness test(edit);
            test.plain();
            test.set("scale", 2.0f);
            test.set("steps", 1.0f);
            test.set("distance", 7.0f * direction);
            test.render(0.9, {on(0.0, 60), on(0.0, 64), on(0.0, 67),
                              off(0.7, 60), off(0.7, 64), off(0.7, 67)});
            requirePitches(test.onsets(), {60, 63, 67, 60 + direction * 12, 63 + direction * 12, 67 + direction * 12});
            test.requireBalanced();
        });

    check("minor pentatonic chooses the nearest scale tone", [&]
    {
        Harness test(edit);
        test.plain();
        test.set("scale", 10.0f);
        test.render(0.1, {on(0.0, 62), off(0.05, 62)});
        requirePitches(test.onsets(), {63});
        test.requireBalanced();
    });

    for (const auto high : {false, true})
        check(high ? "scale transposition at MIDI 127" : "scale transposition at MIDI zero", [&]
        {
            Harness test(edit);
            test.plain();
            test.set("scale", 1.0f);
            test.set("root", 11.0f);
            test.set("steps", 1.0f);
            test.set("distance", high ? 12.0f : -12.0f);
            const auto pitch = high ? 127 : 0;
            test.render(0.3, {on(0.0, pitch), off(0.2, pitch)});
            requirePitches(test.onsets(), high ? std::vector<int>{126, 126} : std::vector<int>{1, 1});
            test.requireBalanced();
        });

    check("root offsets and nearest-note ties below the root", [&]
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
        const std::array<int, 4> expected {4, 6, 8, 10};
        require(notes.size() == expected.size(), "expected four transpositions from low F in B major");
        for (size_t i = 0; i < expected.size(); ++i)
            require(notes[i].message.getNoteNumber() == expected[i],
                    "an out-of-scale F must choose E on a tie, then transpose in B major");
        test.requireBalanced();
    });

    if (!failures.isEmpty())
        throw std::runtime_error(("Rhino Arp mechanics:\n" + failures.joinIntoString("\n")).toStdString());
    juce::Logger::writeToLog("Rhino: Arp note lifecycle, gate balance, channels and finite repeats passed");
}
}
