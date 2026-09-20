#pragma once

#include "ForgeModule.h"
#include <deque>

// Every module Forge has, declared once: what it holds, in what order, on which
// tabs. This is the list the editor walks to build the panel and the list the
// layout test walks to check that every control here names a parameter and
// every parameter is named here.
//
// Adding a control is a line in one of these rows plus the parameter in
// ForgeProcessor.cpp. Nothing else should need editing.
namespace rhino::forge::ui
{
inline std::vector<Row> completeFxRows(std::vector<Row> rows)
{
    static std::deque<juce::String> ids;
    const auto keep = [] (juce::String id)
    {
        ids.push_back(std::move(id));
        return ids.back().toRawUTF8();
    };

    for (int slot = static_cast<int>(rows.size()); slot < fxSlotCount; ++slot)
    {
        std::vector<Control> controls;
        controls.reserve(static_cast<size_t>(rackCount * 12));
        for (int rack = 0; rack < rackCount; ++rack)
        {
            const auto id = [rack, slot, &keep] (const char* suffix)
            {
                return keep(fxParameterId(rack, slot, suffix));
            };
            controls.push_back({id("Type"), "TYPE", Style::plate, nullptr, 2});
            controls.push_back({id("ModeA"), "MODE", Style::selector, nullptr, 2});
            controls.push_back({id("ModeB"), "MODE", Style::selector, nullptr, 2});
            for (int knob = 0; knob < fxKnobCount; ++knob)
                controls.push_back({id(("Knob" + juce::String(knob + 1)).toRawUTF8()), "KNOB"});
            controls.push_back({id("Mix"), "MIX"});
            controls.push_back({id("Level"), "LEVEL"});
            controls.push_back({id("Bypass"), "BYP", Style::chip});
        }
        rows.push_back({1, std::move(controls), rackCount, fxDisplayWeight, 3});
    }
    return rows;
}

inline const std::vector<Module>& modules()
{
    static const std::vector<Module> declared {
        // Three rows now that each oscillator warps: the tuning, the stack, and
        // the pair of warp stages under it — a depth knob at each end with the
        // two mode fields between them, which is the arrangement the Serum
        // manual's warp figure has and the order the stages are applied in.
        //
        // The warp row weighs the same as the knob row above it and is divided
        // into the same six cells, so WARP 1 stands under POSITION and WARP 2
        // under LEVEL rather than the row drifting out of column with the one
        // it belongs to. A mode field takes two of those cells because it
        // spells a word out where a knob draws a circle.
        //
        // Paying for the third row out of the display rather than out of the
        // knobs is deliberate: the waveform is still the largest thing on the
        // panel, and a knob small enough to be hard to hit would have cost more
        // than the picture does. The knobs do come down — see displayShare.
        {"oscA", "OSC A", "MORPH", "oscAEnable", false, Display::oscillator, 0, 4, 8, false,
         {{20, {{"oscAOctave", "OCT", Style::stepper}, {"oscASemitone", "SEMI", Style::stepper},
                {"oscAFine", "FINE", Style::stepper}}},
          {40, {{"oscAPosition", "POSITION"}, {"oscAUnison", "UNISON"}, {"oscADetune", "DETUNE"},
                {"oscABlend", "BLEND"}, {"oscAPan", "PAN"}, {"oscALevel", "LEVEL"}}},
          {40, {{"oscAWarp1", "WARP 1", Style::knob, nullptr, 1, "oscAWarp1Mode"},
                {"oscAWarp1Mode", "MODE 1", Style::selector, nullptr, 2},
                {"oscAWarp2Mode", "MODE 2", Style::selector, nullptr, 2},
                {"oscAWarp2", "WARP 2", Style::knob, nullptr, 1, "oscAWarp2Mode"}}}},
         0, only(Page::oscillators), 1, 0, 0, oscillatorDisplayPercent, 0, "OSCILLATOR A"},
        {"oscB", "OSC B", "MORPH", "oscBEnable", false, Display::oscillator, 0, 12, 8, false,
         {{20, {{"oscBOctave", "OCT", Style::stepper}, {"oscBSemitone", "SEMI", Style::stepper},
                {"oscBFine", "FINE", Style::stepper}}},
          {40, {{"oscBPosition", "POSITION"}, {"oscBUnison", "UNISON"}, {"oscBDetune", "DETUNE"},
                {"oscBBlend", "BLEND"}, {"oscBPan", "PAN"}, {"oscBLevel", "LEVEL"}}},
          {40, {{"oscBWarp1", "WARP 1", Style::knob, nullptr, 1, "oscBWarp1Mode"},
                {"oscBWarp1Mode", "MODE 1", Style::selector, nullptr, 2},
                {"oscBWarp2Mode", "MODE 2", Style::selector, nullptr, 2},
                {"oscBWarp2", "WARP 2", Style::knob, nullptr, 1, "oscBWarp2Mode"}}}},
         0, only(Page::oscillators), 1, 0, 0, oscillatorDisplayPercent, 0, "OSCILLATOR B"},

        // The matrix takes the two oscillators' columns — not the whole row,
        // because SUB, NOISE and FILTER sit either side of them and stay on
        // screen whichever tab is open. It reads as a table: one row per slot,
        // numbered down the side, the amount between the source driving it and
        // the control it moves. Only the first slot names its columns, because
        // those names are drawn once in the title strip rather than above all
        // eight rows.
        {"matrix", "MATRIX", "8 SLOTS", nullptr, true, Display::none, 0, 4, 16, false,
         {{1, {{"mod1Source", "SOURCE", Style::stepper, nullptr, 2},
               {"mod1Depth", "AMOUNT", Style::bar, nullptr, 3},
               {"mod1Bipolar", "BI", Style::chip},
               {"mod1Dest", "DESTINATION", Style::stepper, nullptr, 2}}},
          {1, {{"mod2Source", "", Style::stepper, nullptr, 2},
               {"mod2Depth", "", Style::bar, nullptr, 3},
               {"mod2Bipolar", "BI", Style::chip},
               {"mod2Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod3Source", "", Style::stepper, nullptr, 2},
               {"mod3Depth", "", Style::bar, nullptr, 3},
               {"mod3Bipolar", "BI", Style::chip},
               {"mod3Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod4Source", "", Style::stepper, nullptr, 2},
               {"mod4Depth", "", Style::bar, nullptr, 3},
               {"mod4Bipolar", "BI", Style::chip},
               {"mod4Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod5Source", "", Style::stepper, nullptr, 2},
               {"mod5Depth", "", Style::bar, nullptr, 3},
               {"mod5Bipolar", "BI", Style::chip},
               {"mod5Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod6Source", "", Style::stepper, nullptr, 2},
               {"mod6Depth", "", Style::bar, nullptr, 3},
               {"mod6Bipolar", "BI", Style::chip},
               {"mod6Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod7Source", "", Style::stepper, nullptr, 2},
               {"mod7Depth", "", Style::bar, nullptr, 3},
               {"mod7Bipolar", "BI", Style::chip},
               {"mod7Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod8Source", "", Style::stepper, nullptr, 2},
               {"mod8Depth", "", Style::bar, nullptr, 3},
               {"mod8Bipolar", "BI", Style::chip},
               {"mod8Dest", "", Style::stepper, nullptr, 2}}}},
         0, only(Page::matrix), 1, columnTitleHeight, rowNumberGutter},

        // The wavetable editor takes the same columns as the matrix does,
        // and declares no controls at all. Nothing on it is a parameter: a table
        // is data, not a value a host can automate, so the panel is one
        // component the editor drops into this box rather than a row of knobs
        // the framework lays out. The framework needs no special case for that
        // — a module with no rows simply has nothing to place.
        {"table", "WAVETABLE", "EDITOR", nullptr, true, Display::none, 0, 4, 16, false, {},
         0, only(Page::table)},

        // SUB and NOISE stand at the left-hand end of the signal row, beside the
        // oscillators they are mixed with, and FILTER at the right-hand end,
        // where everything above it arrives.
        // Absent from MIX, where the mixer's own SUB and NOISE strips stand in
        // their place: the mixer is a second view of these two sources rather
        // than a panel that happens to sit beside them.
        //
        // Three rows now that the sub has a shape and a pitch of its own: the
        // six waveforms, the octave they are read at, and the level they are
        // mixed in at. The order is the one Serum's own sub strip reads in, and
        // the one the signal takes — what the wave is, where it sits, how much
        // of it there is.
        //
        // The grid takes half because it is the only thing here that is
        // recognised rather than read, and because the alternative was a knob
        // floating in a tall empty cell: a knob is centred in whatever it is
        // given, so room LEVEL does not need becomes a gap above it rather than
        // a larger control. Thirty per cent is still well over what the shared
        // knob diameter needs, so nothing else on the panel shrinks for it.
        {"sub", "SUB", "", "subEnable", false, Display::none, 0, 0, 2, false,
         {{50, {{"subWave", "", Style::wave}}},
          {20, {{"subOctave", "OCT", Style::stepper}}},
          {30, {{"subLevel", "LEVEL"}}}},
         0, everyPageBut(Page::mix, Page::fx), 1, 0, 0, 0, 0, nullptr, "SUB / NOISE"},
        {"noise", "NOISE", "", "noiseEnable", true, Display::none, 0, 2, 2, false,
         {{100, {{"noiseLevel", "LEVEL"}}}},
         0, everyPageBut(Page::mix, Page::fx), 1, 0, 0, 0, 0, nullptr, "SUB / NOISE"},
        // Four columns for three knobs, against the oscillators' eight for six:
        // the same width per knob, so nothing in the row is drawn at a size its
        // neighbours are not.
        //
        // Built like an oscillator, and for the same reason: a display of what
        // it is doing, a row of short controls under it, then its knobs. The
        // weights below are the oscillators' own, so the two kinds of module
        // line up down the whole of the top row instead of each being centred
        // in its own height.
        //
        // The routing chips name their source the way Serum's do: A, B, S, N.
        // TYPE is given twice a chip's width, because it is the one field here
        // that spells a word out.
        {"filter", "FILTER", "", "filterEnable", false, Display::filter, 0, 20, 4, false,
         {{26, {{"filterType", "TYPE", Style::stepper, nullptr, 2}, {"routeA", "A", Style::chip},
                {"routeB", "B", Style::chip}, {"routeSub", "S", Style::chip},
                {"routeNoise", "N", Style::chip}}},
          {74, {{"cutoff", "CUTOFF"}, {"resonance", "RES"}, {"drive", "DRIVE"}}}},
         0, everyPageBut(Page::mix, Page::fx)},
        // GLOBAL is a narrow column rather than a wide box, and it opens the
        // lower row: what the voice is, before anything that shapes it. Five
        // controls in a row want more width than this panel has to give beside
        // the envelope and the LFO, so they stack — the two numeric ones, then
        // the two switches, then OUTPUT on its own at the foot, which is where
        // it belongs anyway. Output is applied once after the voices are
        // summed; everything above it is per-voice.
        {"global", "GLOBAL", "VOICING", nullptr, false, Display::none, 1, 0, 3, false,
         {{33, {{"polyphony", "POLY", Style::knob, "mono"}, {"glide", "GLIDE"}}},
          {33, {{"mono", "MONO", Style::rocker}, {"legato", "LEGATO", Style::rocker}}},
          {34, {{"output", "OUTPUT"}}}}},

        // Nearly two thirds of the body each for the envelope's and the LFO's
        // display, against the oscillators' rather smaller share. Both of these are drawings of something
        // happening over time, and a shape read at a glance is what the module
        // is for; four knobs under it are how you change the shape, not how you
        // read it.
        //
        // Four envelopes in one module, one shown at a time, the same
        // arrangement the LFOs use and for the same reason: four boxes of this
        // size would not fit, and the display is the point of the module. ENV 1
        // is the amplitude and the rest are sources, but they are the same
        // control set, so they are the same bank repeated.
        //
        // No detail is declared: the header says which stage the envelope
        // showing is in, and at rest what that envelope is for — which is not
        // the same answer for ENV 1 as for the three behind it. The editor
        // supplies it.
        {"env", "ENV", "", nullptr, false, Display::envelope, 1, 3, 9, false,
         {{100, {{"env1Attack", "ATTACK"}, {"env1Decay", "DECAY"},
                 {"env1Sustain", "SUSTAIN"}, {"env1Release", "RELEASE"},

                 {"env2Attack", "ATTACK"}, {"env2Decay", "DECAY"},
                 {"env2Sustain", "SUSTAIN"}, {"env2Release", "RELEASE"},

                 {"env3Attack", "ATTACK"}, {"env3Decay", "DECAY"},
                 {"env3Sustain", "SUSTAIN"}, {"env3Release", "RELEASE"},

                 {"env4Attack", "ATTACK"}, {"env4Decay", "DECAY"},
                 {"env4Sustain", "SUSTAIN"}, {"env4Release", "RELEASE"}},
           envCount}},
         static_cast<int>(ModSource::env1), everyPage, 1, 0, 0, 62},
        // Six LFOs in one module, one shown at a time, chosen by the numbered
        // buttons in the header. Six boxes side by side would not fit, and six
        // that did would each be too small to read — Serum shows its eight the
        // same way for the same reason.
        //
        // Within a bank: RATE is one knob in one place, and UNIT decides what it
        // counts in. The two parameters behind it share a cell, so only the
        // reading in charge is on screen — Hertz free-running, or a division of
        // the host's beat. MODE is the other half of the same question, whether
        // the shape answers the keyboard at all.
        //
        // Written out rather than generated: this file is read to find out what
        // the panel is, and a loop would mean reading the loop instead. The
        // layout test holds every bank to the same shape.
        {"lfo", "LFO", "SOURCE", nullptr, true, Display::lfo, 1, 12, 9, false,
         {{100, {{"lfo1Shape", "SHAPE", Style::stepper},
                 {"lfo1Mode", "MODE", Style::stepper},
                 {"lfo1RateUnit", "UNIT", Style::stepper},
                 {"lfo1Rate", "RATE", Style::knob, "lfo1RateUnit"},
                 {"lfo1Division", "RATE", Style::knob, nullptr, 1, "lfo1RateUnit", true},

                 {"lfo2Shape", "SHAPE", Style::stepper},
                 {"lfo2Mode", "MODE", Style::stepper},
                 {"lfo2RateUnit", "UNIT", Style::stepper},
                 {"lfo2Rate", "RATE", Style::knob, "lfo2RateUnit"},
                 {"lfo2Division", "RATE", Style::knob, nullptr, 1, "lfo2RateUnit", true},

                 {"lfo3Shape", "SHAPE", Style::stepper},
                 {"lfo3Mode", "MODE", Style::stepper},
                 {"lfo3RateUnit", "UNIT", Style::stepper},
                 {"lfo3Rate", "RATE", Style::knob, "lfo3RateUnit"},
                 {"lfo3Division", "RATE", Style::knob, nullptr, 1, "lfo3RateUnit", true},

                 {"lfo4Shape", "SHAPE", Style::stepper},
                 {"lfo4Mode", "MODE", Style::stepper},
                 {"lfo4RateUnit", "UNIT", Style::stepper},
                 {"lfo4Rate", "RATE", Style::knob, "lfo4RateUnit"},
                 {"lfo4Division", "RATE", Style::knob, nullptr, 1, "lfo4RateUnit", true},

                 {"lfo5Shape", "SHAPE", Style::stepper},
                 {"lfo5Mode", "MODE", Style::stepper},
                 {"lfo5RateUnit", "UNIT", Style::stepper},
                 {"lfo5Rate", "RATE", Style::knob, "lfo5RateUnit"},
                 {"lfo5Division", "RATE", Style::knob, nullptr, 1, "lfo5RateUnit", true},

                 {"lfo6Shape", "SHAPE", Style::stepper},
                 {"lfo6Mode", "MODE", Style::stepper},
                 {"lfo6RateUnit", "UNIT", Style::stepper},
                 {"lfo6Rate", "RATE", Style::knob, "lfo6RateUnit"},
                 {"lfo6Division", "RATE", Style::knob, nullptr, 1, "lfo6RateUnit", true}},
           lfoCount}},
         static_cast<int>(ModSource::lfo1), everyPage, 1, 0, 0, 62},

        // --- The effects rack -------------------------------------------------
        //
        // One module, eight rows, three banks. A row is a slot and a bank is a
        // rack, so the named cards in the header are the MAIN / BUS 1 / BUS 2
        // chooser and everything else falls out of the machinery the envelopes
        // and the LFOs already use: every bank declares the same controls in
        // the same order, and the banks not showing stay built, stay attached
        // and keep running.
        //
        // The signal goes down the rows, top to bottom, exactly as Serum's rack
        // does. Like the mixer it takes the whole signal row, because a rack is
        // where everything upstream has already arrived.
        //
        // Every slot declares the same twelve controls whatever type it holds.
        // The six in the middle are labelled from that type and the ones it
        // does not use are hidden — see Editor::refreshFxSlots. That is why
        // they are declared as "KNOB 1" here: this file says where a control
        // is, and the type says what it is.
        {"fx", "FX", "RACK", nullptr, true, Display::none, 0, 0, 24, true,
         completeFxRows({
          {25, {{"fx1s1Type", "TYPE", Style::plate, nullptr, 2},
                {"fx1s1ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx1s1ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx1s1Knob1", "KNOB 1"}, {"fx1s1Knob2", "KNOB 2"}, {"fx1s1Knob3", "KNOB 3"},
                {"fx1s1Knob4", "KNOB 4"}, {"fx1s1Knob5", "KNOB 5"}, {"fx1s1Knob6", "KNOB 6"},
                {"fx1s1Mix", "MIX"}, {"fx1s1Level", "LEVEL"},
                {"fx1s1Bypass", "BYP", Style::chip},

                {"fx2s1Type", "TYPE", Style::plate, nullptr, 2},
                {"fx2s1ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx2s1ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx2s1Knob1", "KNOB 1"}, {"fx2s1Knob2", "KNOB 2"}, {"fx2s1Knob3", "KNOB 3"},
                {"fx2s1Knob4", "KNOB 4"}, {"fx2s1Knob5", "KNOB 5"}, {"fx2s1Knob6", "KNOB 6"},
                {"fx2s1Mix", "MIX"}, {"fx2s1Level", "LEVEL"},
                {"fx2s1Bypass", "BYP", Style::chip},

                {"fx3s1Type", "TYPE", Style::plate, nullptr, 2},
                {"fx3s1ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx3s1ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx3s1Knob1", "KNOB 1"}, {"fx3s1Knob2", "KNOB 2"}, {"fx3s1Knob3", "KNOB 3"},
                {"fx3s1Knob4", "KNOB 4"}, {"fx3s1Knob5", "KNOB 5"}, {"fx3s1Knob6", "KNOB 6"},
                {"fx3s1Mix", "MIX"}, {"fx3s1Level", "LEVEL"},
                {"fx3s1Bypass", "BYP", Style::chip}},
           rackCount, fxDisplayWeight, 3},
          {25, {{"fx1s2Type", "TYPE", Style::plate, nullptr, 2},
                {"fx1s2ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx1s2ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx1s2Knob1", "KNOB 1"}, {"fx1s2Knob2", "KNOB 2"}, {"fx1s2Knob3", "KNOB 3"},
                {"fx1s2Knob4", "KNOB 4"}, {"fx1s2Knob5", "KNOB 5"}, {"fx1s2Knob6", "KNOB 6"},
                {"fx1s2Mix", "MIX"}, {"fx1s2Level", "LEVEL"},
                {"fx1s2Bypass", "BYP", Style::chip},

                {"fx2s2Type", "TYPE", Style::plate, nullptr, 2},
                {"fx2s2ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx2s2ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx2s2Knob1", "KNOB 1"}, {"fx2s2Knob2", "KNOB 2"}, {"fx2s2Knob3", "KNOB 3"},
                {"fx2s2Knob4", "KNOB 4"}, {"fx2s2Knob5", "KNOB 5"}, {"fx2s2Knob6", "KNOB 6"},
                {"fx2s2Mix", "MIX"}, {"fx2s2Level", "LEVEL"},
                {"fx2s2Bypass", "BYP", Style::chip},

                {"fx3s2Type", "TYPE", Style::plate, nullptr, 2},
                {"fx3s2ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx3s2ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx3s2Knob1", "KNOB 1"}, {"fx3s2Knob2", "KNOB 2"}, {"fx3s2Knob3", "KNOB 3"},
                {"fx3s2Knob4", "KNOB 4"}, {"fx3s2Knob5", "KNOB 5"}, {"fx3s2Knob6", "KNOB 6"},
                {"fx3s2Mix", "MIX"}, {"fx3s2Level", "LEVEL"},
                {"fx3s2Bypass", "BYP", Style::chip}},
           rackCount, fxDisplayWeight, 3},
          {25, {{"fx1s3Type", "TYPE", Style::plate, nullptr, 2},
                {"fx1s3ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx1s3ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx1s3Knob1", "KNOB 1"}, {"fx1s3Knob2", "KNOB 2"}, {"fx1s3Knob3", "KNOB 3"},
                {"fx1s3Knob4", "KNOB 4"}, {"fx1s3Knob5", "KNOB 5"}, {"fx1s3Knob6", "KNOB 6"},
                {"fx1s3Mix", "MIX"}, {"fx1s3Level", "LEVEL"},
                {"fx1s3Bypass", "BYP", Style::chip},

                {"fx2s3Type", "TYPE", Style::plate, nullptr, 2},
                {"fx2s3ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx2s3ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx2s3Knob1", "KNOB 1"}, {"fx2s3Knob2", "KNOB 2"}, {"fx2s3Knob3", "KNOB 3"},
                {"fx2s3Knob4", "KNOB 4"}, {"fx2s3Knob5", "KNOB 5"}, {"fx2s3Knob6", "KNOB 6"},
                {"fx2s3Mix", "MIX"}, {"fx2s3Level", "LEVEL"},
                {"fx2s3Bypass", "BYP", Style::chip},

                {"fx3s3Type", "TYPE", Style::plate, nullptr, 2},
                {"fx3s3ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx3s3ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx3s3Knob1", "KNOB 1"}, {"fx3s3Knob2", "KNOB 2"}, {"fx3s3Knob3", "KNOB 3"},
                {"fx3s3Knob4", "KNOB 4"}, {"fx3s3Knob5", "KNOB 5"}, {"fx3s3Knob6", "KNOB 6"},
                {"fx3s3Mix", "MIX"}, {"fx3s3Level", "LEVEL"},
                {"fx3s3Bypass", "BYP", Style::chip}},
           rackCount, fxDisplayWeight, 3},
          {25, {{"fx1s4Type", "TYPE", Style::plate, nullptr, 2},
                {"fx1s4ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx1s4ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx1s4Knob1", "KNOB 1"}, {"fx1s4Knob2", "KNOB 2"}, {"fx1s4Knob3", "KNOB 3"},
                {"fx1s4Knob4", "KNOB 4"}, {"fx1s4Knob5", "KNOB 5"}, {"fx1s4Knob6", "KNOB 6"},
                {"fx1s4Mix", "MIX"}, {"fx1s4Level", "LEVEL"},
                {"fx1s4Bypass", "BYP", Style::chip},

                {"fx2s4Type", "TYPE", Style::plate, nullptr, 2},
                {"fx2s4ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx2s4ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx2s4Knob1", "KNOB 1"}, {"fx2s4Knob2", "KNOB 2"}, {"fx2s4Knob3", "KNOB 3"},
                {"fx2s4Knob4", "KNOB 4"}, {"fx2s4Knob5", "KNOB 5"}, {"fx2s4Knob6", "KNOB 6"},
                {"fx2s4Mix", "MIX"}, {"fx2s4Level", "LEVEL"},
                {"fx2s4Bypass", "BYP", Style::chip},

                {"fx3s4Type", "TYPE", Style::plate, nullptr, 2},
                {"fx3s4ModeA", "MODE", Style::selector, nullptr, 2},
                {"fx3s4ModeB", "MODE", Style::selector, nullptr, 2},
                {"fx3s4Knob1", "KNOB 1"}, {"fx3s4Knob2", "KNOB 2"}, {"fx3s4Knob3", "KNOB 3"},
                {"fx3s4Knob4", "KNOB 4"}, {"fx3s4Knob5", "KNOB 5"}, {"fx3s4Knob6", "KNOB 6"},
                {"fx3s4Mix", "MIX"}, {"fx3s4Level", "LEVEL"},
                {"fx3s4Bypass", "BYP", Style::chip}},
           rackCount, fxDisplayWeight, 3}
         }),
         0, only(Page::fx), 1, 0, 0, 0, fxBankWidth},

        // --- The mixer -------------------------------------------------------
        //
        // Eight channels across the whole signal row, three grid columns each.
        // The MIX tab is the only one that takes the row entire: the mixer is
        // the view of SUB, NOISE and the filter, so those three modules stand
        // down rather than sitting beside strips of themselves.
        //
        // The order is the signal path read left to right, the same order the
        // OSC tab is in, and it ends where the signal does: the two busses the
        // sends arrive at, then the main output.
        //
        // A strip declares the same four rows whatever it carries, so TO sits
        // on one line across the mixer, the sends on the next, the pans on the
        // third and the faders along the foot. A channel with nothing to put in
        // a row leaves the row out and the rows below it keep their weights, so
        // its fader still lands on the same line as every other.
        //
        // Every strip is compact: its knobs are sized to its own cells and held
        // under the diameter the rest of the panel shares. Without that, eight
        // narrow channels would be the tightest cells on the panel and every
        // knob in Forge would shrink to match them.
        //
        // The header enable is the source's own — switching OSC A off here is
        // switching it off, exactly as Serum's mixer header does, which is also
        // what gives every channel the mute it has no separate control for.
        {"mixSub", "SUB", "", "subEnable", false, Display::none, 0, 0, 3, true,
         {{16, {{"routeSub", "TO", Style::stepper}}},
          {26, {{"subSend1", "BUS 1"}, {"subSend2", "BUS 2"}}},
          {26, {{"subPan", "PAN"}}},
          {32, {{"subLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixOscA", "OSC A", "", "oscAEnable", false, Display::none, 0, 3, 3, true,
         {{16, {{"routeA", "TO", Style::stepper}}},
          {26, {{"oscASend1", "BUS 1"}, {"oscASend2", "BUS 2"}}},
          {26, {{"oscAPan", "PAN"}}},
          {32, {{"oscALevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixOscB", "OSC B", "", "oscBEnable", false, Display::none, 0, 6, 3, true,
         {{16, {{"routeB", "TO", Style::stepper}}},
          {26, {{"oscBSend1", "BUS 1"}, {"oscBSend2", "BUS 2"}}},
          {26, {{"oscBPan", "PAN"}}},
          {32, {{"oscBLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixNoise", "NOISE", "", "noiseEnable", true, Display::none, 0, 9, 3, true,
         {{16, {{"routeNoise", "TO", Style::stepper}}},
          {26, {{"noiseSend1", "BUS 1"}, {"noiseSend2", "BUS 2"}}},
          {26, {{"noisePan", "PAN"}}},
          {32, {{"noiseLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        // The filter's own channel. It has no TO field, because everything the
        // filter passes goes to the main output and the only other place it
        // could go is a bus, which the sends already reach. MIX shares the pan
        // row instead, where TYPE would have been.
        {"mixFilter", "FILTER", "", "filterEnable", false, Display::none, 0, 12, 3, true,
         {{16, {}},
          {26, {{"filterSend1", "BUS 1"}, {"filterSend2", "BUS 2"}}},
          {26, {{"filterPan", "PAN"}, {"filterMix", "MIX"}}},
          {32, {{"filterLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        // The two busses. They carry no sends — a bus receives them rather than
        // making them, and where it goes afterwards is the TO field — so that
        // row holds the switch that takes the bus's own effects rack out of
        // the signal, which is the button Serum puts on this channel too.
        {"mixBus1", "BUS 1", "", "bus1Enable", true, Display::none, 0, 15, 3, true,
         {{16, {{"bus1Dest", "TO", Style::stepper}}},
          {26, {{"fx2Bypass", "FX", Style::chip}}},
          {26, {{"bus1Pan", "PAN"}}},
          {32, {{"bus1Level", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixBus2", "BUS 2", "", "bus2Enable", true, Display::none, 0, 18, 3, true,
         {{16, {{"bus2Dest", "TO", Style::stepper}}},
          {26, {{"fx3Bypass", "FX", Style::chip}}},
          {26, {{"bus2Pan", "PAN"}}},
          {32, {{"bus2Level", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        // Where everything arrives. It is the OUTPUT knob from GLOBAL, drawn as
        // the fader at the end of the row: one setting, and the mixer is where
        // a final level is actually read against the channels feeding it.
        {"mixMain", "MAIN", "", nullptr, false, Display::none, 0, 21, 3, true,
         {{16, {}},
          {26, {{"fx1Bypass", "FX", Style::chip}}},
          {26, {}},
          {32, {{"output", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},

        // The macros close the lower row, two across and four down in the
        // narrowest column the panel has: they are deliberately smaller than
        // the controls they drive, as Serum's are. They are sources only — a
        // macro reaches a control through a slot or not at all.
        {"macros", "MACROS", "SOURCES", nullptr, false, Display::none, 1, 21, 3, true,
         {{25, {{"macro1", "1"}, {"macro2", "2"}}},
          {25, {{"macro3", "3"}, {"macro4", "4"}}},
          {25, {{"macro5", "5"}, {"macro6", "6"}}},
          {25, {{"macro7", "7"}, {"macro8", "8"}}}}},
    };
    return declared;
}

// How many banks a module's controls are declared in. One unless it says
// otherwise, and the rows all have to agree — the layout test holds them to it.
inline int bankCount(const Module& module)
{
    return module.rows.empty() ? 1 : juce::jmax(1, module.rows.front().banks);
}

// One bank button in a module's header. They start after whatever the header
// already carries on the left: the enable LED, and the drag handle of a module
// that is itself a source.
// Room for a short title, for a banked module that draws one. The envelopes and
// the LFOs do not: their drag handle carries the name and stands where the
// title would. The rack has no handle, so its title is drawn and its cards have
// to start past it. The layout test holds such a title to three characters,
// which is what this width covers at the header's font.
inline constexpr int bankTitleGutter = 34;

inline juce::Rectangle<int> bankButtonBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                             int bank, int handleWidth)
{
    auto left = moduleArea.getX() + 10;
    if (module.enableId != nullptr) left += headerHeight;
    if (module.handleSource != 0) left += handleWidth + 8;
    else left += bankTitleGutter;
    const auto width = bankWidthOf(module);
    return {left + bank * (width + bankButtonGap), moduleArea.getY(), width, headerHeight};
}

// Whether a module is shown while the given tab is chosen.
inline bool onPage(const Module& module, Page page)
{
    return (module.pages & only(page)) != 0;
}

// The part number stamped on a plate's foot: the grid row it sits in as a
// letter, and its place across that row as a number.
//
// Worked out against the tab being shown rather than against the declaration,
// so the numbers read left to right across the panel on every tab instead of
// carrying the gaps left by whatever that tab is hiding. Two modules declared
// at the same column are broken apart by declaration order, which only the
// overlaid pages can produce and which no tab shows at once anyway.
inline juce::String plateCode(const Module& module, Page page)
{
    auto ordinal = 1;
    for (const auto& other : modules())
    {
        if (&other == &module || !onPage(other, page) || other.row != module.row) continue;
        if (other.column < module.column
            || (other.column == module.column && &other < &module)) ++ordinal;
    }
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + module.row))
           + "-" + juce::String(ordinal).paddedLeft('0', 2);
}

// Two modules can only collide if some tab shows both of them at once, which
// is exactly the two sets of pages overlapping.
inline bool sharePage(const Module& a, const Module& b)
{
    return (a.pages & b.pages) != 0;
}

// --- What the window may be ---------------------------------------------------
}
