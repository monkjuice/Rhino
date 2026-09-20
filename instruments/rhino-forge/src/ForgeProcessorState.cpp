#include "ForgeProcessor.h"

// A module's plate colour is not a parameter and rides on the state tree as a
// property; the four it can be are named in the panel's own vocabulary.
#include "../ui/ForgeStyle.h"

// State, presets and the files a table comes from.
//
// A preset saved at one milestone still opens at the next while the control
// set is moving: parameters a preset predates load at their defaults, retired
// ones are dropped rather than kept as ballast, and a source or destination
// index written before something was inserted into the middle of a list is
// remapped on the way in. All of that is migrated().
//
// Host state uses the same path, so a Rhino project holding older Forge state
// opens without carrying dead parameters.
namespace rhino::forge
{
namespace
{
// Within a version, presets are still reconciled against the parameters that
// exist when they are opened — see Processor::migrated. That is what keeps a
// preset saved at one milestone working at the next, while Forge's controls
// are still moving.
constexpr int presetFormatVersion = 2;

// A table travels as one child node holding every frame end to end. The
// samples are deflated and then base64'd, because a ValueTree is written out as
// XML and eighty kilobytes of raw float is not text.
constexpr const char* tableNodeType = "TABLE";

juce::ValueTree tableNode(const WavetableEdit& edit, int oscillator)
{
    juce::ValueTree node(tableNodeType);
    node.setProperty("osc", oscillator, nullptr);
    node.setProperty("frames", edit.frameCount(), nullptr);
    node.setProperty("name", edit.title(), nullptr);

    // Written a float at a time rather than as a block of memory: JUCE's streams
    // are explicitly little-endian, so a preset written on one machine reads the
    // same on another whatever the native order is.
    juce::MemoryOutputStream raw;
    for (const auto sample : edit.samples()) raw.writeFloat(sample);
    juce::MemoryOutputStream packed;
    {
        juce::GZIPCompressorOutputStream deflate(packed, 9);
        deflate.write(raw.getData(), raw.getDataSize());
    }
    node.setProperty("data", packed.getMemoryBlock().toBase64Encoding(), nullptr);
    return node;
}

// Refuses anything it cannot account for exactly. A table whose data does not
// match the frame count it declares is a damaged preset, and loading the part
// of it that parsed would leave an oscillator on a table nobody authored.
bool readTableNode(const juce::ValueTree& node, WavetableEdit& edit)
{
    const auto frames = static_cast<int>(node.getProperty("frames", 0));
    if (frames < 1 || frames > maxEditableFrames) return false;

    juce::MemoryBlock packed;
    if (!packed.fromBase64Encoding(node.getProperty("data").toString())) return false;
    juce::MemoryInputStream source(packed, false);
    juce::GZIPDecompressorInputStream inflate(source);
    juce::MemoryOutputStream raw;
    raw.writeFromInputStream(inflate, -1);

    const auto count = static_cast<size_t>(frames) * wavetableFrameSize;
    if (raw.getDataSize() != count * sizeof(float)) return false;
    std::vector<float> samples(count);
    juce::MemoryInputStream floats(raw.getData(), raw.getDataSize(), false);
    for (auto& sample : samples) sample = floats.readFloat();
    return edit.setFrames(samples.data(), frames, node.getProperty("name", "CUSTOM").toString());
}

// Serum names a wavetable file's frame size in a `clm ` chunk, which JUCE's wav
// reader does not surface, so the RIFF is walked for it directly. Zero means the
// file did not say, and 2048 is then assumed — which is the convention every
// wavetable file that carries no chunk is written to anyway.
int declaredFrameSize(const juce::File& file)
{
    juce::FileInputStream stream(file);
    char header[12] = {};
    if (!stream.openedOk() || stream.read(header, 12) != 12) return 0;
    if (juce::String(header, 4) != "RIFF" || juce::String(header + 8, 4) != "WAVE") return 0;

    while (!stream.isExhausted())
    {
        char id[4] = {};
        if (stream.read(id, 4) != 4) return 0;
        const auto size = stream.readInt();
        if (size < 0) return 0;
        if (juce::String(id, 4) == "clm ")
        {
            juce::MemoryBlock block;
            stream.readIntoMemoryBlock(block, size);
            // "<!>2048 00000000 wavetable" — the size is the digits after the
            // marker, and getIntValue stops at the first character that is not
            // one of them.
            return block.toString().fromFirstOccurrenceOf("<!>", false, false).getIntValue();
        }
        // Chunks are padded to an even length, and the pad byte is not counted
        // in the size, so skipping by the size alone drifts one byte per odd
        // chunk and every chunk after it is read as garbage.
        stream.setPosition(stream.getPosition() + size + (size & 1));
    }
    return 0;
}

juce::ValueTree parameterEntry(const juce::ValueTree& tree, const juce::String& id)
{
    for (const auto child : tree)
        if (child.getProperty("id").toString() == id)
            return child;
    return {};
}
}

// One property per module rather than one list, so a module that gains or loses
// a colour does not shift what the others are reading.
static juce::Identifier panelColourProperty(const juce::String& moduleId)
{
    return juce::Identifier("panelColour_" + moduleId);
}

int Processor::panelColour(const juce::String& moduleId) const
{
    const auto stored = state.state.getProperty(panelColourProperty(moduleId));
    // A state written before this existed has no property at all, and that is
    // the ordinary case rather than an error: it opens on the default.
    return stored.isVoid() ? static_cast<int>(ui::defaultPanelColour)
                           : static_cast<int>(ui::panelColourFrom(static_cast<int>(stored)));
}

void Processor::setPanelColour(const juce::String& moduleId, int choice)
{
    state.state.setProperty(panelColourProperty(moduleId),
                            static_cast<int>(ui::panelColourFrom(choice)), nullptr);
}

// One property per macro, numbered as a player counts them, for the same reason
// the plate colours are one property per module: a macro renamed does not shift
// what the other seven are reading.
static juce::Identifier macroNameProperty(int macro)
{
    return juce::Identifier("macroName_" + juce::String(macro + 1));
}

juce::String Processor::macroName(int macro) const
{
    if (macro < 0 || macro >= macroCount) return {};
    // A state written before this existed carries no property, which is the
    // ordinary case rather than an error: it opens unnamed.
    return state.state.getProperty(macroNameProperty(macro)).toString();
}

void Processor::setMacroName(int macro, const juce::String& name)
{
    if (macro < 0 || macro >= macroCount) return;
    const auto tag = name.trim().toUpperCase().substring(0, maxMacroNameLength);
    // Removed rather than stored empty, so an unnamed macro leaves nothing
    // behind in a preset and reads the same as one that was never named.
    if (tag.isEmpty()) state.state.removeProperty(macroNameProperty(macro), nullptr);
    else state.state.setProperty(macroNameProperty(macro), tag, nullptr);
}

void Processor::getStateInformation(juce::MemoryBlock& destination)
{
    auto tree = state.copyState();
    appendTables(tree);
    if (const auto xml = tree.createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    // Host state gets the same reconciliation as a preset file, so a project
    // saved against an older Forge opens without carrying retired parameters
    // or leaving newer ones unset.
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
        {
            const auto saved = juce::ValueTree::fromXml(*xml);
            // Read before migrated() strips the table nodes back out, so the
            // live parameter state stays parameters only.
            applyTables(saved);
            state.replaceState(migrated(saved));
        }
}

juce::Result Processor::savePreset(const juce::File& destination, const juce::String& name)
{
    if (destination == juce::File {}) return juce::Result::fail("Choose a preset file first.");
    juce::ValueTree preset("RhinoForgePreset");
    preset.setProperty("formatVersion", presetFormatVersion, nullptr);
    preset.setProperty("name", name.trim().isNotEmpty() ? name.trim()
                                                        : destination.getFileNameWithoutExtension(), nullptr);
    auto tree = state.copyState();
    appendTables(tree);
    preset.addChild(tree, -1, nullptr);
    const auto xml = preset.createXml();
    if (xml == nullptr) return juce::Result::fail("Forge could not create the preset data.");

    juce::TemporaryFile temporary(destination);
    if (!xml->writeTo(temporary.getFile(), {}))
        return juce::Result::fail("Forge could not write the preset file.");
    if (!temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Forge could not replace the preset file.");
    return juce::Result::ok();
}

juce::Result Processor::loadPreset(const juce::File& source)
{
    const auto xml = juce::XmlDocument::parse(source);
    if (xml == nullptr) return juce::Result::fail("That file is not readable XML.");
    const auto preset = juce::ValueTree::fromXml(*xml);
    if (!preset.hasType("RhinoForgePreset"))
        return juce::Result::fail("That file is not a Rhino Forge preset.");
    const auto version = static_cast<int>(preset.getProperty("formatVersion", 0));
    if (version != presetFormatVersion)
        return juce::Result::fail("This preset was saved by a different version of Forge.");
    const auto savedState = preset.getChildWithName(state.state.getType());
    if (!savedState.isValid())
        return juce::Result::fail("The preset does not contain Forge parameter state.");
    applyTables(savedState);
    state.replaceState(migrated(savedState));
    return juce::Result::ok();
}

// Reconcile a preset of any supported version with the parameters that exist
// now: drop entries Forge has retired, and fill in entries it has since gained
// with their defaults. replaceState only touches parameters the tree mentions,
// so without the second half an old preset would leave newer controls holding
// the previous patch's values.
juce::ValueTree Processor::migrated(const juce::ValueTree& savedState) const
{
    auto result = savedState.createCopy();

    // Each modulator that arrived as a bank did the same two things to a state
    // written before it: the one that already existed was renamed for its place
    // in the bank, and the rest were inserted into the middle of the source
    // list, moving everything past them. A dropped parameter loads at its
    // default, which is harmless; a parameter or a source index that quietly
    // means something different is not, so both are put right here rather than
    // left to the reconciliation below.
    //
    // Everything a slot's Source sat past a run moved up by the rest of that
    // run. `inserted` is that shift, in the numbering the state is in when it is
    // applied — so these run oldest first, each handing the next a state written
    // as if its own change had always been there.
    const auto inserted = [&result] (int after, int howMany)
    {
        for (int slot = 1; slot <= modSlotCount; ++slot)
        {
            auto entry = parameterEntry(result, "mod" + juce::String(slot) + "Source");
            if (!entry.isValid()) continue;
            const auto was = juce::roundToInt(static_cast<float>(entry.getProperty("value")));
            if (was > after) entry.setProperty("value", static_cast<float>(was + howMany), nullptr);
        }
    };
    const auto renamed = [&result] (const std::vector<std::pair<const char*, const char*>>& pairs)
    {
        for (auto child : result)
        {
            const auto id = child.getProperty("id").toString();
            for (const auto& [was, now] : pairs)
                if (id == was) child.setProperty("id", now, nullptr);
        }
    };

    // What a state is old enough to predate, read before anything is renamed,
    // because renaming is what clears these marks — which is also what makes
    // the whole of this safe to run twice.
    //
    // The one LFO's parameters were named for the only LFO there was, so
    // `lfoShape` says a state predates the other five; the amp envelope's
    // carried no number for the same reason, so `attack` says one predates
    // ENV 2-4. Either mark is enough on its own, and the older one implies the
    // newer: a state from before the LFOs came in banks is from before the
    // envelopes did too, whether or not it happens to name an envelope at all.
    const auto predatesLfoBanks = parameterEntry(result, "lfoShape").isValid();
    const auto predatesEnvBanks = predatesLfoBanks || parameterEntry(result, "attack").isValid();
    // The mixer gave SUB and NOISE a pan, so a state that names neither is one
    // written before those two were panned — and therefore one whose levels are
    // in the old, 3 dB hotter scale. `subPan` is added below like any other
    // missing parameter, which is what clears this mark and makes running the
    // whole of this twice safe.
    const auto predatesMixer = !parameterEntry(result, "subPan").isValid();

    // LFO 2-6.
    if (predatesLfoBanks)
    {
        renamed({{"lfoShape", "lfo1Shape"}, {"lfoMode", "lfo1Mode"}, {"lfoRate", "lfo1Rate"},
                 {"lfoRateUnit", "lfo1RateUnit"}, {"lfoDivision", "lfo1Division"}});
        // LFO 1 sat at 2 in the list as it was numbered then, straight after
        // ENV 1, and five LFOs went in behind it. Written out rather than taken
        // from ModSource::lfo1, because that constant says where LFO 1 is now —
        // it has itself moved since, and reading it here would silently shift
        // this by however far the list has travelled afterwards.
        inserted(2, lfoCount - 1);
    }

    // ENV 2-4, which moved everything past ENV 1 again.
    if (predatesEnvBanks)
    {
        renamed({{"attack", "env1Attack"}, {"decay", "env1Decay"},
                 {"sustain", "env1Sustain"}, {"release", "env1Release"}});
        // ENV 1 has always been 1 and does not move; the three behind it push
        // everything else along. Anything already remapped by the LFO step is in
        // the numbering this step expects, which is why that one runs first.
        inserted(static_cast<int>(ModSource::env1), envCount - 1);
    }

    // SUB and NOISE used to be summed into both channels at full amplitude and
    // are now panned at equal power, which costs them 3 dB at centre. Their
    // saved levels are raised by exactly that, so a patch written before the
    // mixer sounds as it did. A level already at the top of its range cannot be
    // raised and is left there — the only patches this changes are ones whose
    // sub or noise was already at maximum.
    if (predatesMixer)
        for (const auto* id : {"subLevel", "noiseLevel"})
        {
            auto entry = parameterEntry(result, id);
            if (!entry.isValid()) continue;
            const auto was = static_cast<float>(entry.getProperty("value"));
            entry.setProperty("value", juce::jlimit(0.0f, 1.0f, was * juce::MathConstants<float>::sqrt2),
                              nullptr);
        }

    for (int i = result.getNumChildren(); --i >= 0;)
    {
        // A table is data rather than a parameter and is applied separately, so
        // it is taken out here and never reaches the parameter state.
        if (result.getChild(i).hasType(tableNodeType)) { result.removeChild(i, nullptr); continue; }
        const auto id = result.getChild(i).getProperty("id").toString();
        if (id.isNotEmpty() && state.getParameter(id) == nullptr)
            result.removeChild(i, nullptr);
    }
    for (auto* raw : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(raw);
        if (ranged == nullptr || parameterEntry(result, ranged->paramID).isValid()) continue;
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", ranged->paramID, nullptr);
        entry.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
        result.addChild(entry, -1, nullptr);
    }
    return result;
}

void Processor::appendTables(juce::ValueTree& tree) const
{
    for (int osc = 0; osc < oscillatorCount; ++osc)
    {
        // A table nobody has touched is the built-in ten, which every copy of
        // Forge already has. Writing it out would put eighty kilobytes of
        // base64 into a preset to say "unchanged".
        if (tables.edit(osc).isUntouched()) continue;
        tree.addChild(tableNode(tables.edit(osc), osc), -1, nullptr);
    }
}

void Processor::applyTables(const juce::ValueTree& tree)
{
    for (int osc = 0; osc < oscillatorCount; ++osc)
    {
        juce::ValueTree found;
        for (const auto child : tree)
            if (child.hasType(tableNodeType) && static_cast<int>(child.getProperty("osc", -1)) == osc)
                found = child;
        if (found.isValid() && readTableNode(found, tables.edit(osc))) tables.publish(osc);
        else tables.resetToBuiltIn(osc);
    }
}

// A wavetable file is an ordinary audio file holding single-cycle frames end to
// end. Nothing here tries to find cycles in arbitrary recorded audio: that is a
// guess, it is wrong often, and it is its own problem.
juce::Result Processor::importTable(int oscillator, const juce::File& file)
{
    if (!file.existsAsFile()) return juce::Result::fail("that file is not there.");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return juce::Result::fail("Forge cannot read that kind of file.");

    auto sourceFrameSize = declaredFrameSize(file);
    if (sourceFrameSize < 4) sourceFrameSize = wavetableFrameSize;
    const auto available = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, 1 << 22));
    const auto sourceFrames = available / sourceFrameSize;
    if (sourceFrames < 1)
        return juce::Result::fail("that file is shorter than one " + juce::String(sourceFrameSize)
                                  + "-sample frame.");

    // Channel 0 only. A wavetable file is mono by convention, and averaging the
    // channels of one that is not would blur two different tables together.
    juce::AudioBuffer<float> source(1, sourceFrames * sourceFrameSize);
    source.clear();
    if (!reader->read(&source, 0, source.getNumSamples(), 0, true, false))
        return juce::Result::fail("Forge could not read that file's samples.");

    // A table larger than the ceiling is thinned evenly rather than cut short,
    // so a 256-frame table still sweeps from its first shape to its last.
    const auto frames = juce::jmin(sourceFrames, maxEditableFrames);
    std::vector<float> samples(static_cast<size_t>(frames) * wavetableFrameSize);
    const auto* read = source.getReadPointer(0);
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto pick = frames == 1 ? 0
            : juce::jlimit(0, sourceFrames - 1,
                           juce::roundToInt(static_cast<double>(frame) * (sourceFrames - 1)
                                            / static_cast<double>(frames - 1)));
        const auto* from = read + static_cast<size_t>(pick) * sourceFrameSize;
        auto* into = samples.data() + static_cast<size_t>(frame) * wavetableFrameSize;
        if (sourceFrameSize == wavetableFrameSize)
        {
            std::copy_n(from, wavetableFrameSize, into);
            continue;
        }
        // A frame written at some other size is resampled onto Forge's, with the
        // same spline the oscillator reads a frame with, so a table stored at
        // 256 points sits beside a built-in one without a step in it.
        for (int i = 0; i < wavetableFrameSize; ++i)
            into[i] = wavetableInterpolate(from, sourceFrameSize,
                                           static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
    }

    if (!tables.edit(oscillator).setFrames(samples.data(), frames,
                                           file.getFileNameWithoutExtension().toUpperCase()))
        return juce::Result::fail("Forge could not build a table from that file.");
    tables.publish(oscillator);
    return juce::Result::ok();
}
}
