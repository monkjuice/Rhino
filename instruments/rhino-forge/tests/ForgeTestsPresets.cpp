// Saving, loading, and opening a preset written before the control set moved.
#include "ForgeTestSupport.h"
#include "../ui/ForgeStyle.h"

namespace rhino::forge::tests
{
namespace
{
void presetSuite()
{
    rhino::forge::Processor processor;
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-preset-test", {}, true);
    require(directory.createDirectory(), "temporary preset directory can be created");
    const auto preset = directory.getChildFile("Round Trip.forgepreset");

    // An oscillator's panel colour is not a parameter — it rides on the state
    // tree's own properties — so it is the one setting a preset could silently
    // drop, and nothing else in this suite would notice.
    require(processor.panelColour("oscA") == static_cast<int>(rhino::forge::ui::defaultPanelColour),
            "an oscillator opens on the default colour");
    require(rhino::forge::ui::panelColourFrom(99) == rhino::forge::ui::panelColourFrom(
                rhino::forge::ui::panelColourCount - 1),
            "a colour saved by a build that knew more of them clamps rather than wrapping");
    processor.setPanelColour("oscA", static_cast<int>(rhino::forge::ui::PanelColour::red));
    processor.setPanelColour("oscB", static_cast<int>(rhino::forge::ui::PanelColour::orange));

    setValue(processor, "cutoff", 1320.0f);
    setValue(processor, "env1Release", 2.5f);
    setValue(processor, "noiseEnable", 1.0f);
    setValue(processor, "oscBEnable", 0.0f);
    require(processor.savePreset(preset, "Round Trip").wasOk(), "preset saves");

    setValue(processor, "cutoff", 9000.0f);
    setValue(processor, "env1Release", 0.1f);
    setValue(processor, "noiseEnable", 0.0f);
    setValue(processor, "oscBEnable", 1.0f);
    processor.setPanelColour("oscA", static_cast<int>(rhino::forge::ui::PanelColour::blue));
    processor.setPanelColour("oscB", static_cast<int>(rhino::forge::ui::PanelColour::blue));
    require(processor.loadPreset(preset).wasOk(), "preset loads");
    require(processor.panelColour("oscA") == static_cast<int>(rhino::forge::ui::PanelColour::red)
                && processor.panelColour("oscB") == static_cast<int>(rhino::forge::ui::PanelColour::orange),
            "a preset restores each oscillator's panel colour");
    requireClose(value(processor, "cutoff"), 1320.0f, 1.0f, "preset restores cutoff");
    requireClose(value(processor, "env1Release"), 2.5f, 0.001f, "preset restores release");
    requireClose(value(processor, "noiseEnable"), 1.0f, 0.001f, "preset restores an enabled module");
    requireClose(value(processor, "oscBEnable"), 0.0f, 0.001f, "preset restores a disabled module");

    // A table is data rather than a parameter, so it travels beside the
    // parameter state. A preset that carries one has to give back the frames
    // that were drawn, and a preset that carries none has to put the oscillator
    // back on the built-in ten rather than leave the previous patch's table
    // behind — the same rule an omitted parameter follows.
    {
        rhino::forge::Processor drawn;
        drawn.tableStore().edit(0).draw(0, 0.0f, -1.0f, 1.0f, 1.0f);
        drawn.tableStore().edit(0).insertFrame(0, true);
        drawn.tableStore().publish(0);
        const auto frames = drawn.tableStore().edit(0).frameCount();
        std::vector<float> authored(drawn.tableStore().edit(0).samples());

        const auto withTable = directory.getChildFile("Drawn.forgepreset");
        require(drawn.savePreset(withTable, "Drawn").wasOk(), "a preset holding a table saves");

        rhino::forge::Processor reopened;
        require(reopened.loadPreset(withTable).wasOk(), "a preset holding a table loads");
        require(reopened.tableStore().edit(0).frameCount() == frames,
                "a preset gives back the frames it was saved with");
        require(reopened.tableStore().edit(0).samples() == authored,
                "a preset gives back the samples it was saved with, exactly");
        require(!reopened.tableStore().edit(0).isUntouched(),
                "a table that came out of a preset is not the built-in one");
        require(reopened.tableStore().frameCount(0) == frames,
                "and POSITION is told how many frames it now has");
        require(reopened.tableStore().edit(1).isUntouched(),
                "an oscillator the preset said nothing about keeps the built-in table");

        require(reopened.loadPreset(preset).wasOk(), "a preset with no table loads over one with a table");
        require(reopened.tableStore().edit(0).isUntouched(),
                "a preset that carries no table puts the built-in one back");

        // Host state is the same payload by the same path, so a project reopens
        // on the table it was saved with.
        juce::MemoryBlock block;
        drawn.getStateInformation(block);
        rhino::forge::Processor hosted;
        hosted.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        require(hosted.tableStore().edit(0).samples() == authored,
                "host state carries the table too");
        // The table travels beside the parameters, never inside them.
        for (const auto child : hosted.state.copyState())
            require(!child.hasType("TABLE"), "the live parameter state holds no table node");
    }

    // Format 1 described a synth that no longer exists and is deliberately not
    // accepted.
    const auto version1 = directory.getChildFile("Old.forgepreset");
    require(version1.replaceWithText("<RhinoForgePreset formatVersion=\"1\"><RhinoForgeState/></RhinoForgePreset>"),
            "format 1 fixture writes");
    require(processor.loadPreset(version1).failed(), "a format 1 preset is refused");

    const auto future = directory.getChildFile("Future.forgepreset");
    require(future.replaceWithText("<RhinoForgePreset formatVersion=\"99\"><RhinoForgeState/></RhinoForgePreset>"),
            "future-version fixture writes");
    require(processor.loadPreset(future).failed(), "a newer preset version is refused");

    // Within format 2, a preset is reconciled against the parameters that exist
    // when it opens. This is what keeps presets working while Forge's controls
    // are still being built out milestone by milestone: entries Forge no longer
    // has are dropped, and controls the preset predates return to their
    // defaults instead of inheriting the previous patch.
    setValue(processor, "cutoff", 2000.0f);
    setValue(processor, "noiseEnable", 1.0f);
    const auto partial = directory.getChildFile("Partial.forgepreset");
    require(partial.replaceWithText(
                "<RhinoForgePreset formatVersion=\"2\"><RhinoForgeState>"
                "<PARAM id=\"cutoff\" value=\"5000.0\"/>"
                "<PARAM id=\"retiredKnob\" value=\"0.5\"/>"
                "</RhinoForgeState></RhinoForgePreset>"),
            "partial fixture writes");
    require(processor.loadPreset(partial).wasOk(), "a preset missing parameters still loads");
    requireClose(value(processor, "cutoff"), 5000.0f, 1.0f, "a partial preset restores what it names");
    requireClose(value(processor, "noiseEnable"), 0.0f, 0.001f,
                 "a parameter the preset omits returns to its default, not the previous patch's value");
    requireClose(value(processor, "env1Release"), 0.35f, 0.001f,
                 "an omitted float parameter returns to its default");

    const auto resaved = directory.getChildFile("Resaved.forgepreset");
    require(processor.savePreset(resaved, "Resaved").wasOk(), "a reconciled preset saves");
    const auto text = resaved.loadFileAsString();
    require(text.contains("formatVersion=\"2\""), "a saved preset declares format 2");
    require(!text.contains("retiredKnob"), "an unknown entry is dropped, not carried as ballast");
    require(text.contains("oscAEnable"), "the module enables are saved");
    require(text.contains("env1Release"), "an omitted parameter is written back out at its default");

    const auto invalid = directory.getChildFile("Invalid.forgepreset");
    require(invalid.replaceWithText("<NotForge />"), "invalid fixture writes");
    const auto before = value(processor, "cutoff");
    require(processor.loadPreset(invalid).failed(), "a foreign preset is rejected");
    require(value(processor, "cutoff") == before, "a rejected preset leaves state untouched");

    // The readout bug: a SliderAttachment overwrites any formatter the editor
    // installs, so the formatting has to belong to the parameter itself.
    requireText(textFor(processor, "cutoff", 7800.0f), "7.80 kHz", "cutoff reads as kHz");
    requireText(textFor(processor, "cutoff", 440.0f), "440 Hz", "a low cutoff reads as Hz");
    requireText(textFor(processor, "env1Sustain", 0.75f), "75 %", "sustain reads as a percentage");
    requireText(textFor(processor, "env1Attack", 0.01f), "10 ms", "a short attack reads in milliseconds");
    requireText(textFor(processor, "env1Release", 2.5f), "2.50 s", "a long release reads in seconds");
    requireText(textFor(processor, "oscBSemitone", 7.0f), "+7 st", "tune reads as signed semitones");
    requireText(textFor(processor, "oscAUnison", 4.0f), "4", "unison reads as a plain count");
    requireText(textFor(processor, "mod1Depth", -0.5f), "-50 %", "a bipolar depth keeps its sign");
    require(!textFor(processor, "cutoff", 7800.0f).contains("7800.0004"),
            "no knob falls back to a raw float readout");

    directory.deleteRecursively();
}

// --------------------------------------------------------------- presets ---

// States written before a modulator arrived as a bank. Each time one did, the
// one that already existed was renamed for its place in the bank and the rest
// were inserted into the middle of the source list. A dropped parameter loads at
// its default and no harm is done; a source index that quietly means something
// else is a slot silently pointed somewhere nobody asked for, so it is remapped
// rather than left.
//
// Two eras are checked, because a state can predate either both changes or only
// the later one, and the shifts have to compose in the first case without being
// applied twice in the second.
void legacyStateSuite()
{
    // Whatever `saved` holds, put through the processor and read back out.
    const auto reopened = [] (rhino::forge::Processor& processor, const juce::ValueTree& saved)
    {
        juce::MemoryBlock block;
        const auto xml = saved.createXml();
        require(xml != nullptr, "the legacy state serialises");
        if (xml == nullptr) return;
        juce::AudioProcessor::copyXmlToBinary(*xml, block);
        processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    };
    const auto value = [] (rhino::forge::Processor& processor, const juce::String& id)
    {
        const auto* raw = processor.state.getRawParameterValue(id);
        return raw == nullptr ? std::numeric_limits<float>::quiet_NaN() : raw->load();
    };

    // Before LFO 2-6, and so before ENV 2-4 as well: both shifts apply, one
    // after the other, and a slot has to land where the second one leaves it.
    {
        rhino::forge::Processor processor;
        juce::ValueTree saved(processor.state.state.getType());
        const auto add = [&saved] (const char* id, float value)
        {
            juce::ValueTree entry("PARAM");
            entry.setProperty("id", id, nullptr);
            entry.setProperty("value", value, nullptr);
            saved.addChild(entry, -1, nullptr);
        };
        add("lfoShape", 2.0f);                      // SAW
        add("lfoMode", 1.0f);                       // ENV
        add("lfoRate", 3.0f);
        add("lfoRateUnit", 0.0f);
        add("lfoDivision", 4.0f);
        add("cutoff", 900.0f);
        // As the sources were numbered then: ENV 1 at 1, LFO 1 at 2, VELOCITY
        // straight after it at 3, NOTE at 4, and the macros from 5.
        add("mod1Source", 2.0f);
        add("mod2Source", 3.0f);
        add("mod3Source", 4.0f);
        add("mod4Source", 5.0f);
        add("mod5Source", 1.0f);
        reopened(processor, saved);

        const auto read = [&] (const juce::String& id) { return value(processor, id); };
        requireClose(read("lfo1Shape"), 2.0f, 0.001f, "the one LFO's shape becomes LFO 1's");
        requireClose(read("lfo1Mode"), 1.0f, 0.001f, "the one LFO's mode becomes LFO 1's");
        requireClose(read("lfo1Rate"), 3.0f, 0.001f, "the one LFO's rate becomes LFO 1's");
        requireClose(read("lfo1Division"), 4.0f, 0.001f, "the one LFO's division becomes LFO 1's");
        requireClose(read("cutoff"), 900.0f, 0.5f, "everything else is left alone");

        requireClose(read("mod1Source"), static_cast<float>(srcLfo1), 0.001f,
                     "a slot on LFO 1 stays on LFO 1 across both shifts");
        requireClose(read("mod2Source"), static_cast<float>(srcVelocity), 0.001f,
                     "a slot on velocity is still on velocity");
        requireClose(read("mod3Source"), static_cast<float>(srcNote), 0.001f,
                     "a slot on note is still on note");
        requireClose(read("mod4Source"), static_cast<float>(static_cast<int>(rhino::forge::ModSource::macro1)),
                     0.001f, "a slot on the first macro is still on it");
        requireClose(read("mod5Source"), static_cast<float>(srcEnv1), 0.001f,
                     "ENV 1 has never moved, so a slot on it stays put");

        // Running it again must change nothing: state already migrated no longer
        // carries the old ids, which is what the migration keys off.
        juce::MemoryBlock again;
        processor.getStateInformation(again);
        processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        requireClose(read("mod2Source"), static_cast<float>(srcVelocity), 0.001f,
                     "saving and reopening migrated state does not move the sources again");
        requireClose(read("lfo1Rate"), 3.0f, 0.001f,
                     "saving and reopening migrated state keeps LFO 1's rate");
    }

    // After the LFOs came in banks but before the envelopes did: the LFO names
    // are already current, so only the envelope shift may fire.
    {
        rhino::forge::Processor processor;
        juce::ValueTree saved(processor.state.state.getType());
        const auto add = [&saved] (const char* id, float value)
        {
            juce::ValueTree entry("PARAM");
            entry.setProperty("id", id, nullptr);
            entry.setProperty("value", value, nullptr);
            saved.addChild(entry, -1, nullptr);
        };
        add("attack", 0.5f);
        add("decay", 0.4f);
        add("sustain", 0.3f);
        add("release", 1.5f);
        add("lfo3Rate", 2.0f);
        // As the sources were numbered then: ENV 1 at 1, the six LFOs from 2,
        // VELOCITY at 8, NOTE at 9, and the macros from 10.
        add("mod1Source", 1.0f);
        add("mod2Source", 2.0f);
        add("mod3Source", 8.0f);
        add("mod4Source", 10.0f);
        reopened(processor, saved);

        const auto read = [&] (const juce::String& id) { return value(processor, id); };
        requireClose(read("env1Attack"), 0.5f, 0.001f, "the one envelope's attack becomes ENV 1's");
        requireClose(read("env1Decay"), 0.4f, 0.001f, "the one envelope's decay becomes ENV 1's");
        requireClose(read("env1Sustain"), 0.3f, 0.001f, "the one envelope's sustain becomes ENV 1's");
        requireClose(read("env1Release"), 1.5f, 0.001f, "the one envelope's release becomes ENV 1's");
        requireClose(read("env2Attack"), 0.01f, 0.001f,
                     "an envelope the state predates opens at its default");
        requireClose(read("lfo3Rate"), 2.0f, 0.001f, "an LFO already in a bank is left alone");

        requireClose(read("mod1Source"), static_cast<float>(srcEnv1), 0.001f,
                     "a slot on ENV 1 stays on ENV 1");
        requireClose(read("mod2Source"), static_cast<float>(srcLfo1), 0.001f,
                     "a slot on LFO 1 follows the three envelopes inserted ahead of it");
        requireClose(read("mod3Source"), static_cast<float>(srcVelocity), 0.001f,
                     "a slot on velocity is still on velocity");
        requireClose(read("mod4Source"), static_cast<float>(static_cast<int>(rhino::forge::ModSource::macro1)),
                     0.001f, "a slot on the first macro is still on it");

        juce::MemoryBlock again;
        processor.getStateInformation(again);
        processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        requireClose(read("mod2Source"), static_cast<float>(srcLfo1), 0.001f,
                     "saving and reopening does not move the sources again");
        requireClose(read("env1Release"), 1.5f, 0.001f,
                     "saving and reopening keeps ENV 1's release");
    }

    // Before the mixer. SUB and NOISE were summed into both channels at full
    // amplitude and are now panned at equal power, which costs them 3 dB at
    // centre, so their saved levels are raised by exactly that on the way in.
    // Everything else about a state of this era is already current.
    {
        rhino::forge::Processor processor;
        juce::ValueTree saved(processor.state.state.getType());
        const auto add = [&saved] (const char* id, float value)
        {
            juce::ValueTree entry("PARAM");
            entry.setProperty("id", id, nullptr);
            entry.setProperty("value", value, nullptr);
            saved.addChild(entry, -1, nullptr);
        };
        add("subLevel", 0.12f);
        add("noiseLevel", 0.25f);
        add("oscALevel", 0.75f);
        add("cutoff", 900.0f);
        reopened(processor, saved);

        const auto read = [&] (const juce::String& id) { return value(processor, id); };
        requireClose(read("subLevel"), 0.12f * juce::MathConstants<float>::sqrt2, 0.001f,
                     "a sub level written before the mixer is raised by the 3 dB its pan law costs");
        requireClose(read("noiseLevel"), 0.25f * juce::MathConstants<float>::sqrt2, 0.001f,
                     "a noise level written before the mixer is raised the same way");
        requireClose(read("oscALevel"), 0.75f, 0.001f,
                     "an oscillator was already panned at equal power, so its level is left alone");
        requireClose(read("cutoff"), 900.0f, 0.5f, "everything else is left alone");
        requireClose(read("subPan"), 0.0f, 0.001f, "a pan the state predates opens centred");
        requireClose(read("filterMix"), 1.0f, 0.001f, "the filter channel opens all wet");
        // The sub had one shape and one pitch before it had a picker, so a
        // state written then has to reopen on them: the first frame is the sine
        // and zero octaves is the octave below the note it has always played.
        requireClose(read("subWave"), 0.0f, 0.001f, "a sub shape the state predates opens on the sine");
        requireClose(read("subOctave"), 0.0f, 0.001f,
                     "a sub octave the state predates opens where the sub has always sat");

        // The raise must not compound. Saving and reopening carries subPan,
        // which is the mark that says the state has already been through this.
        juce::MemoryBlock again;
        processor.getStateInformation(again);
        processor.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
        requireClose(read("subLevel"), 0.12f * juce::MathConstants<float>::sqrt2, 0.001f,
                     "reopening migrated state does not raise the sub a second time");

        // A level already at the top of its range has nowhere to be raised to,
        // and must stay in range rather than leaving it.
        rhino::forge::Processor loud;
        juce::ValueTree maxed(loud.state.state.getType());
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", "subLevel", nullptr);
        entry.setProperty("value", 1.0f, nullptr);
        maxed.addChild(entry, -1, nullptr);
        reopened(loud, maxed);
        requireClose(value(loud, "subLevel"), 1.0f, 0.001f,
                     "a sub already at maximum stays at maximum rather than leaving its range");
    }
}
}

void presetTests()
{
    presetSuite();
    legacyStateSuite();
}
}
