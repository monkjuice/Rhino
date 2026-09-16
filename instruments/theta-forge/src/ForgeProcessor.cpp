#include "ForgeProcessor.h"
#include "ForgeEditor.h"

namespace theta::forge
{
namespace
{
// Value formatting belongs to the parameter, not the editor. A
// SliderAttachment overwrites any textFromValueFunction the editor sets, so a
// formatter installed there never runs and every knob reads "0.5500000".
// Declaring it here means the host's automation lane and Forge's own knobs
// show the same text.
using Format = std::function<juce::String (float)>;

juce::String asPercent(float value) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; }

juce::String asSignedPercent(float value)
{
    return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value * 100.0f)) + " %";
}

juce::String asSemitones(float value)
{
    return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " st";
}

juce::String asHertz(float value)
{
    return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz"
                            : juce::String(juce::roundToInt(value)) + " Hz";
}

juce::String asRate(float value) { return juce::String(value, 2) + " Hz"; }

juce::String asSeconds(float value)
{
    return value < 1.0f ? juce::String(juce::roundToInt(value * 1000.0f)) + " ms"
                        : juce::String(value, 2) + " s";
}

juce::String asCount(float value) { return juce::String(juce::roundToInt(value)); }

juce::String asGain(float value) { return juce::String(value, 2); }

juce::String asOctaves(float value)
{
    const auto octaves = juce::roundToInt(value);
    return octaves == 0 ? juce::String("0") : (octaves > 0 ? "+" : "") + juce::String(octaves) + " oct";
}

juce::String asCents(float value)
{
    const auto cents = juce::roundToInt(value);
    return cents == 0 ? juce::String("0") : (cents > 0 ? "+" : "") + juce::String(cents) + " ct";
}

juce::String asPan(float value)
{
    const auto amount = juce::roundToInt(std::abs(value) * 100.0f);
    if (amount == 0) return "C";
    return (value < 0.0f ? "L" : "R") + juce::String(amount);
}

// Format 2 dropped the effects, the macros and the filter envelope. Format 1
// files are not accepted; they described a synth that no longer exists.
//
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

std::unique_ptr<juce::RangedAudioParameter> parameter(const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range,
                                                      float initial, Format format)
{
    // Explicit version hints make VST3 parameter IDs stable from the first
    // release, rather than relying on JUCE's legacy VST2-compatible mapping.
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {id, 1}, name, range, initial,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [format] (float value, int) { return format(value); }));
}

// Module enables are genuine switches, so they are declared as bools and show
// up in a host's automation lane as on/off rather than as a float that happens
// to be stepped.
std::unique_ptr<juce::RangedAudioParameter> toggle(const juce::String& id, const juce::String& name, bool initial)
{
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID {id, 1}, name, initial);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;

    // The two oscillators are declared identically. Neither is expressed in
    // terms of the other, so each owns its tuning, its stack, its pan and its
    // level outright.
    const auto oscillator = [this, &result] (int which, const char* prefix, const char* label, bool enabled,
                                             float position, float semitone, float level)
    {
        const auto id = [prefix] (const char* suffix) { return juce::String(prefix) + suffix; };
        const auto name = [label] (const char* suffix) { return juce::String(label) + " " + suffix; };
        result.push_back(toggle(id("Enable"), name("Enable"), enabled));
        // POSITION reads as the shape it is on, or as the two it sits between —
        // "where is the saw?" is the question a wavetable knob is asked, and a
        // percentage answers none of it. On a table somebody drew there are no
        // shape names left to give, so it counts frames instead. Either way the
        // answer depends on the table this oscillator is reading, which is why
        // this one formatter closes over the processor and the rest do not.
        //
        // A host can call this from its own thread while the message thread is
        // publishing a new table; what it reads is published as atomics for
        // exactly that reason.
        result.push_back(parameter(id("Position"), name("Position"), {0.0f, 1.0f}, position,
                                   [this, which] (float value)
                                   {
                                       return positionLabel(tables.frameCount(which),
                                                            tables.isBuiltIn(which), value);
                                   }));
        result.push_back(parameter(id("Octave"), name("Octave"), {-4.0f, 4.0f, 1.0f}, 0.0f, asOctaves));
        result.push_back(parameter(id("Semitone"), name("Semitone"), {-12.0f, 12.0f}, semitone, asSemitones));
        result.push_back(parameter(id("Fine"), name("Fine"), {-100.0f, 100.0f, 1.0f}, 0.0f, asCents));
        result.push_back(parameter(id("Unison"), name("Unison"), {1.0f, 8.0f, 1.0f}, 2.0f, asCount));
        result.push_back(parameter(id("Detune"), name("Detune"), {0.0f, 1.0f}, 0.18f, asPercent));
        result.push_back(parameter(id("Blend"), name("Blend"), {0.0f, 1.0f}, 0.5f, asPercent));
        result.push_back(parameter(id("Pan"), name("Pan"), {-1.0f, 1.0f}, 0.0f, asPan));
        result.push_back(parameter(id("Level"), name("Level"), {0.0f, 1.0f}, level, asPercent));
    };
    // 6/9 is SAW and 1/9 is TRI: a fresh patch starts on shapes with names
    // rather than part-way between two of them.
    oscillator(0, "oscA", "Osc A", true, 6.0f / 9.0f, 0.0f, 0.75f);
    oscillator(1, "oscB", "Osc B", true, 1.0f / 9.0f, 7.0f, 0.25f);

    result.push_back(toggle("subEnable", "Sub Enable", true));
    result.push_back(parameter("subLevel", "Sub Level", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(toggle("noiseEnable", "Noise Enable", false));
    result.push_back(parameter("noiseLevel", "Noise Level", {0.0f, 1.0f}, 0.25f, asPercent));
    result.push_back(toggle("filterEnable", "Filter Enable", true));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"filterType", 1}, "Filter Type",
        juce::StringArray {"LP", "HP", "BP"}, 0));
    result.push_back(toggle("routeA", "Filter Route Osc A", true));
    result.push_back(toggle("routeB", "Filter Route Osc B", true));
    result.push_back(toggle("routeSub", "Filter Route Sub", true));
    result.push_back(toggle("routeNoise", "Filter Route Noise", true));
    result.push_back(parameter("cutoff", "Cutoff", {30.0f, 18000.0f, 0.0f, 0.25f}, 7800.0f, asHertz));
    result.push_back(parameter("resonance", "Resonance", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(parameter("drive", "Drive", {0.0f, 1.0f}, 0.08f, asPercent));
    result.push_back(parameter("attack", "Attack", {0.001f, 4.0f, 0.0f, 0.35f}, 0.01f, asSeconds));
    result.push_back(parameter("decay", "Decay", {0.001f, 4.0f, 0.0f, 0.35f}, 0.24f, asSeconds));
    result.push_back(parameter("sustain", "Sustain", {0.0f, 1.0f}, 0.75f, asPercent));
    result.push_back(parameter("release", "Release", {0.001f, 8.0f, 0.0f, 0.35f}, 0.35f, asSeconds));
    juce::StringArray lfoShapeNames;
    for (int i = 0; i < lfoShapeCount; ++i) lfoShapeNames.add(lfoShapeName(i));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"lfoShape", 1}, "LFO Shape", lfoShapeNames, 0));
    result.push_back(parameter("lfoRate", "LFO Rate", {0.05f, 20.0f, 0.0f, 0.35f}, 0.5f, asRate));
    result.push_back(toggle("lfoSync", "LFO Sync", false));
    juce::StringArray lfoDivisionNames;
    for (const auto& division : lfoDivisions()) lfoDivisionNames.add(division.label);
    // 1/4 by default: one cycle per beat is the rate a sync is usually reached
    // for in the first place.
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"lfoDivision", 1}, "LFO Division", lfoDivisionNames, 2));
    result.push_back(parameter("polyphony", "Polyphony", {1.0f, 16.0f, 1.0f}, 8.0f, asCount));
    result.push_back(toggle("mono", "Mono", false));
    result.push_back(toggle("legato", "Legato", true));
    result.push_back(parameter("glide", "Glide", {0.0f, 2.0f, 0.0f, 0.35f}, 0.08f, asSeconds));
    result.push_back(parameter("output", "Output", {0.0f, 1.25f}, 0.75f, asGain));

    for (int macro = 1; macro <= macroCount; ++macro)
        result.push_back(parameter("macro" + juce::String(macro), "Macro " + juce::String(macro),
                                   {0.0f, 1.0f}, 0.0f, asPercent));

    // Eight modulation slots. Each is three parameters so a host can automate a
    // routing as readily as a knob, and so the whole matrix saves with a preset
    // without a separate serialisation path.
    juce::StringArray sourceNames;
    for (int i = 0; i < modSourceCount; ++i) sourceNames.add(modSourceName(i));
    juce::StringArray destinationNames;
    for (const auto& destination : destinations()) destinationNames.add(destination.label);

    for (int slot = 1; slot <= modSlotCount; ++slot)
    {
        const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
        const auto name = [slot] (const char* suffix) { return "Mod " + juce::String(slot) + " " + suffix; };
        // Slot 1 is pre-wired to LFO 1 into the cutoff at zero depth, so the
        // most common first move is one knob rather than three.
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Source"), 1}, name("Source"), sourceNames,
            slot == 1 ? static_cast<int>(ModSource::lfo1) : 0));
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Dest"), 1}, name("Destination"), destinationNames,
            slot == 1 ? 13 : 0));
        result.push_back(parameter(id("Depth"), name("Depth"), {-1.0f, 1.0f}, 0.0f, asSignedPercent));
    }
    return {result.begin(), result.end()};
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "ThetaForgeState", parameterLayout())
{
}

void Processor::prepareToPlay(double sampleRate, int)
{
    core.initialise(sampleRate);
    // Hand the engine each destination's range so modulation happens in the
    // same normalised space the knob moves in. Taken from the parameters
    // themselves, so there is only ever one definition of a range.
    for (int i = 1; i < destinationCount; ++i)
        if (const auto* parameter = state.getParameter(destinations()[static_cast<size_t>(i)].id))
            core.setDestinationRange(i, parameter->getNormalisableRange());
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// The rate LFO 1 actually runs at: the rate knob in free mode, and a division of
// the host's tempo in sync. Worked out rather than published, because it depends
// only on parameters and the tempo, both of which the message thread can read
// for itself — so the panel and the voice cannot end up quoting different rates.
float Processor::lfoRateHz() const
{
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    if (value("lfoSync") < 0.5f) return value("lfoRate");

    const auto beats = lfoDivisions()[static_cast<size_t>(
        juce::jlimit(0, lfoDivisionCount - 1, juce::roundToInt(value("lfoDivision"))))].beats;
    const auto bpm = hostBpm.load(std::memory_order_relaxed);
    // Standing in for a host that reports no tempo, so a synced LFO still runs
    // at a musical rate in a standalone rather than stopping dead.
    const auto tempo = bpm > 0.0 ? bpm : 120.0;
    return static_cast<float>(tempo / 60.0) / juce::jmax(0.0001f, beats);
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // Everything that reads a published table happens inside this bracket, which
    // is what lets the message thread tell when a replaced table has stopped
    // being read. See ForgeTableStore.h.
    const WavetableStore::ScopedBlock block(tables);
    buffer.clear();
    // Read before the patch is built, because the patch resolves a synced LFO
    // against it.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                hostBpm.store(*bpm, std::memory_order_relaxed);

    // Anything played on the editor's keyboard joins the host's own notes
    // before a single sample is rendered.
    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    const auto values = patch();
    const auto mods = modulation();
    auto event = midi.cbegin();
    const auto end = midi.cend();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        while (event != end)
        {
            const auto metadata = *event;
            if (metadata.samplePosition > i) break;
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) core.noteOn(message.getNoteNumber(), message.getFloatVelocity(), values);
            else if (message.isNoteOff()) core.noteOff(message.getNoteNumber());
            else if (message.isAllNotesOff()) core.allNotesOff();
            ++event;
        }
        float left, right; core.renderSample(values, mods, left, right);
        buffer.addSample(0, i, left);
        if (buffer.getNumChannels() > 1) buffer.addSample(1, i, right);
    }

    // Published once per block rather than per sample: the display redraws at
    // 24 Hz, so a per-sample store would be pure contention for no extra detail.
    meterLevel.store(core.envelopeLevel(), std::memory_order_relaxed);
    meterStage.store(core.envelopeStage(), std::memory_order_relaxed);
    meterLfoPhase.store(core.lfoPosition(), std::memory_order_relaxed);
    meterLfoValue.store(core.lfoOutput(), std::memory_order_relaxed);
    for (int destination = 1; destination < destinationCount; ++destination)
        meterOffsets[static_cast<size_t>(destination)]
            .store(core.modulationOffset(destination), std::memory_order_relaxed);
}

Patch Processor::patch() const
{
    // Assigned by name, not positionally: the parameter list and the Patch
    // layout no longer have to be kept in the same order to stay correct.
    // Nothing is remapped on the way through — the macros that used to bend
    // these values were hardwired offsets, and return as real assignable
    // sources with the modulation matrix.
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    const auto readOscillator = [&value] (const char* prefix)
    {
        const auto id = [prefix] (const char* suffix) { return juce::String(prefix) + suffix; };
        Oscillator osc;
        osc.enable = value(id("Enable"));
        osc.position = value(id("Position"));
        osc.octave = value(id("Octave"));
        osc.semitone = value(id("Semitone"));
        osc.fine = value(id("Fine"));
        osc.unison = value(id("Unison"));
        osc.detune = value(id("Detune"));
        osc.blend = value(id("Blend"));
        osc.pan = value(id("Pan"));
        osc.level = value(id("Level"));
        return osc;
    };

    Patch result;
    result.a = readOscillator("oscA");
    result.b = readOscillator("oscB");
    // Picked up once per block and used for the whole of it, never re-read
    // mid-block: that is the property the hand-over rule depends on.
    result.a.table = tables.table(0);
    result.b.table = tables.table(1);
    result.subEnable = value("subEnable");
    result.subLevel = value("subLevel");
    result.noiseEnable = value("noiseEnable");
    result.noiseLevel = value("noiseLevel");
    result.filterEnable = value("filterEnable");
    result.filterType = value("filterType");
    result.routeA = value("routeA");
    result.routeB = value("routeB");
    result.routeSub = value("routeSub");
    result.routeNoise = value("routeNoise");
    result.cutoff = value("cutoff");
    result.resonance = value("resonance");
    result.drive = value("drive");
    result.attack = value("attack");
    result.decay = value("decay");
    result.sustain = value("sustain");
    result.release = value("release");
    result.lfoRate = lfoRateHz();
    result.lfoShape = value("lfoShape");
    result.polyphony = value("polyphony");
    result.mono = value("mono");
    result.legato = value("legato");
    result.glide = value("glide");
    result.output = value("output");
    for (int macro = 0; macro < macroCount; ++macro)
        result.macros[static_cast<size_t>(macro)] = value("macro" + juce::String(macro + 1));
    return result;
}

Modulation Processor::modulation() const
{
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    Modulation result;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot + 1) + suffix; };
        result.slots[static_cast<size_t>(slot)] = {value(id("Source")), value(id("Dest")), value(id("Depth"))};
    }
    return result;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

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
    juce::ValueTree preset("ThetaForgePreset");
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
    if (!preset.hasType("ThetaForgePreset"))
        return juce::Result::fail("That file is not a Theta Forge preset.");
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new theta::forge::Processor();
}
