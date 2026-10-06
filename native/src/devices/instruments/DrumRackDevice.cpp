#include "DrumRackDevice.h"
#include "ContentLibrary.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
const juce::Identifier padsId { "PADS" };
const juce::Identifier padId { "PAD" };
const juce::Identifier sampleId { "sample" };
const juce::Identifier synthId { "synth" };
const juce::Identifier nameId { "name" };
const juce::Identifier chokeId { "choke" };
const juce::Identifier muteId { "mute" };
const juce::Identifier soloId { "solo" };
const juce::Identifier selectedId { "selPad" };

// The longest stretch of a strike a picture shows. A crash rings on for
// longer, and the start of it is what tells one sample from another.
constexpr double pictureSeconds = 3.0;
// The rate a picture is played at. It is drawn as the lows and highs of a few
// hundred columns, each tens of samples wide even at this rate, and a dragged
// Decay asks for a new one every frame: at the device's own rate a long tom
// took a millisecond and a half.
constexpr double pictureRate = 16000.0;

bool validPad(int pad)
{
    return juce::isPositiveAndBelow(pad, DrumRackDevice::padCount);
}

juce::String decayText(float seconds)
{
    if (seconds >= DrumRackEngine::fullDecay - 0.001f)
        return "Full";
    return seconds < 1.0f ? juce::String(juce::roundToInt(seconds * 1000.0f)) + " ms"
                          : juce::String(seconds, 2) + " s";
}

juce::String levelText(float decibels)
{
    return decibels <= DrumRackEngine::silentLevel ? juce::String("-inf dB") : juce::String(decibels, 1) + " dB";
}

juce::String panText(float pan)
{
    const auto percent = juce::roundToInt(std::abs(pan) * 100.0f);
    return percent == 0 ? juce::String("C") : juce::String(percent) + (pan < 0.0f ? "L" : "R");
}
}

DrumRackDevice::DrumRackDevice(te::PluginCreationInfo info) : NativeInstrument(std::move(info), xmlTypeName)
{
    formats.registerBasicFormats();
    // A rack opened from a document already holds its pads.
    syncPads();
}

// Each pad's six controls, declared pad by pad. The names carry the pad's
// number, because an automation lane is read away from the face.
std::array<DrumRackDevice::PadControls, DrumRackDevice::padCount> DrumRackDevice::declarePads()
{
    std::array<PadControls, padCount> declared;
    for (int pad = 0; pad < padCount; ++pad)
    {
        const auto prefix = "p" + juce::String(pad + 1);
        const auto label = "Pad " + juce::String(pad + 1);
        // The pads share one place on a face, a tab each, should a generated
        // face ever be asked to show them.
        const juce::String padTabs = "Pads";
        auto& controls = declared[static_cast<size_t>(pad)];
        controls.tune = param(prefix + "Tune", label + " Tune").range(-24.0f, 24.0f)
                            .unit(ParamUnit::semitones).section(label, padTabs);
        controls.decay = param(prefix + "Decay", label + " Decay").range(0.01f, DrumRackEngine::fullDecay)
                             .defaultValue(DrumRackEngine::fullDecay).skewAround(0.5f).format(decayText)
                             .section(label, padTabs);
        controls.tone = param(prefix + "Tone", label + " Tone").range(0.0f, 1.0f).defaultValue(1.0f)
                            .unit(ParamUnit::percent).section(label, padTabs);
        controls.velocity = param(prefix + "Velocity", label + " Velocity").range(0.0f, 1.0f).defaultValue(1.0f)
                                .unit(ParamUnit::percent).section(label, padTabs);
        controls.level = param(prefix + "Level", label + " Level").range(DrumRackEngine::silentLevel, 6.0f)
                             .defaultValue(0.0f).format(levelText).section(label, padTabs);
        controls.pan = param(prefix + "Pan", label + " Pan").range(-1.0f, 1.0f).defaultValue(0.0f)
                           .format(panText).section(label, padTabs);
    }
    return declared;
}

// ---- reading the pads ---------------------------------------------------------

juce::ValueTree DrumRackDevice::padsTree() const
{
    return state.getChildWithName(padsId);
}

juce::ValueTree DrumRackDevice::padState(int pad)
{
    auto* undo = getUndoManager();
    auto tree = padsTree();
    if (!tree.isValid())
    {
        tree = juce::ValueTree(padsId);
        state.appendChild(tree, undo);
    }
    // A pads tree from anywhere else is made whole before it is written to.
    while (tree.getNumChildren() < padCount)
        tree.appendChild(juce::ValueTree(padId), undo);
    return tree.getChild(pad);
}

bool DrumRackDevice::concernsPads(const juce::ValueTree& tree) const
{
    return tree.hasType(padsId) || tree.getParent().hasType(padsId);
}

DrumRackEngine::PadSettings DrumRackDevice::controlsOf(int pad, bool asSet) const
{
    const auto& controls = pads[static_cast<size_t>(pad)];
    const auto read = [asSet] (const Param& control)
    {
        return asSet ? control.automatable().getCurrentBaseValue() : control.value();
    };
    return { read(controls.tune), read(controls.decay), read(controls.tone),
             read(controls.velocity), read(controls.level), read(controls.pan) };
}

DrumRackDevice::Pad DrumRackDevice::pad(int index) const
{
    Pad view;
    if (!validPad(index))
        return view;
    const auto tree = padsTree().getChild(index);
    view.muted = static_cast<bool>(tree[muteId]);
    view.soloed = static_cast<bool>(tree[soloId]);
    DrumSound sound;
    if (tree.hasProperty(synthId))
    {
        const auto model = drumModelFromId(tree[synthId].toString());
        if (!model.has_value())
            return view;
        sound.source = DrumRackEngine::Source::synth;
        sound.model = *model;
    }
    else if (tree.hasProperty(sampleId))
    {
        sound.source = DrumRackEngine::Source::sample;
        sound.sample = tree[sampleId].toString();
        view.unreadable = unreadable[static_cast<size_t>(index)];
    }
    else
    {
        return view;
    }
    sound.name = tree[nameId].toString();
    sound.choke = juce::jlimit(0, DrumRackEngine::chokeGroupCount, static_cast<int>(tree[chokeId]));
    sound.settings = controlsOf(index, true);
    view.sound = std::move(sound);
    return view;
}

juce::String DrumRackDevice::padName(int index) const
{
    const auto view = pad(index);
    return view.sound.has_value() ? view.sound->displayName() : noteName(index);
}

juce::String DrumRackDevice::noteName(int pad)
{
    // Middle C is C3, as the note editor names its rows.
    return juce::MidiMessage::getMidiNoteName(lowestNote + pad, true, true, 3);
}

DrumKit DrumRackDevice::kit() const
{
    DrumKit whole;
    for (int index = 0; index < padCount; ++index)
        whole.pads[static_cast<size_t>(index)] = pad(index).sound;
    return whole;
}

bool DrumRackDevice::isBlank() const
{
    for (int index = 0; index < padCount; ++index)
        if (pad(index).sound.has_value())
            return false;
    return true;
}

int DrumRackDevice::firstEmptyPad() const
{
    for (int index = 0; index < padCount; ++index)
        if (!pad(index).sound.has_value())
            return index;
    return -1;
}

// ---- writing the pads ---------------------------------------------------------

void DrumRackDevice::writeControls(int index, const DrumRackEngine::PadSettings& wanted)
{
    const auto& controls = pads[static_cast<size_t>(index)];
    const std::pair<const Param*, float> values[] {
        { &controls.tune, wanted.tune },         { &controls.decay, wanted.decay },
        { &controls.tone, wanted.tone },         { &controls.velocity, wanted.velocity },
        { &controls.level, wanted.level },       { &controls.pan, wanted.pan },
    };
    for (const auto& [control, value] : values)
    {
        auto& parameter = control->automatable();
        const auto clamped = control->spec().clamp(value);
        // An unchanged value would still be an undoable action.
        if (parameter.getCurrentBaseValue() != clamped)
            parameter.setParameter(clamped, juce::sendNotificationSync);
    }
}

void DrumRackDevice::writePad(int index, const std::optional<DrumSound>& sound)
{
    auto* undo = getUndoManager();
    auto tree = padState(index);
    if (sound.has_value())
    {
        if (sound->source == DrumRackEngine::Source::synth)
        {
            tree.setProperty(synthId, juce::String(drumModelInfo(sound->model).id), undo);
            tree.removeProperty(sampleId, undo);
        }
        else
        {
            tree.setProperty(sampleId, sound->sample, undo);
            tree.removeProperty(synthId, undo);
        }
        if (sound->name.trim().isNotEmpty())
            tree.setProperty(nameId, sound->name.trim(), undo);
        else
            tree.removeProperty(nameId, undo);
        const auto choke = juce::jlimit(0, DrumRackEngine::chokeGroupCount, sound->choke);
        if (choke > 0)
            tree.setProperty(chokeId, choke, undo);
        else
            tree.removeProperty(chokeId, undo);
        writeControls(index, sound->settings);
    }
    else
    {
        for (const auto& id : { sampleId, synthId, nameId, chokeId, muteId, soloId })
            tree.removeProperty(id, undo);
        writeControls(index, {});
    }
}

void DrumRackDevice::setPadSound(int index, const std::optional<DrumSound>& sound)
{
    if (!validPad(index))
        return;
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        writePad(index, sound);
    }
    syncPads();
}

void DrumRackDevice::setPadSample(int index, const juce::File& file)
{
    if (!validPad(index))
        return;
    auto sound = DrumSound::forSample(file);
    if (const auto before = pad(index).sound; before.has_value())
    {
        sound.choke = before->choke;
        if (before->source == DrumRackEngine::Source::sample)
        {
            sound.settings = before->settings;
        }
        else
        {
            sound.settings.level = before->settings.level;
            sound.settings.pan = before->settings.pan;
            sound.settings.velocity = before->settings.velocity;
        }
    }
    setPadSound(index, sound);
}

void DrumRackDevice::setPadSynth(int index, DrumModel model)
{
    if (!validPad(index))
        return;
    const auto before = pad(index).sound;
    // Choosing the synth the pad already plays changes nothing.
    if (before.has_value() && before->source == DrumRackEngine::Source::synth && before->model == model)
        return;
    auto sound = DrumSound::forSynth(model);
    if (before.has_value())
    {
        sound.choke = before->choke;
        sound.settings.level = before->settings.level;
        sound.settings.pan = before->settings.pan;
        sound.settings.velocity = before->settings.velocity;
    }
    setPadSound(index, sound);
}

void DrumRackDevice::clearPad(int index)
{
    setPadSound(index, std::nullopt);
}

void DrumRackDevice::setPadMuted(int index, bool muted)
{
    if (!validPad(index))
        return;
    auto tree = padState(index);
    if (muted)
        tree.setProperty(muteId, true, getUndoManager());
    else
        tree.removeProperty(muteId, getUndoManager());
}

void DrumRackDevice::setPadSoloed(int index, bool soloed)
{
    if (!validPad(index))
        return;
    auto tree = padState(index);
    if (soloed)
        tree.setProperty(soloId, true, getUndoManager());
    else
        tree.removeProperty(soloId, getUndoManager());
}

void DrumRackDevice::setPadChoke(int index, int group)
{
    if (!validPad(index))
        return;
    auto tree = padState(index);
    group = juce::jlimit(0, DrumRackEngine::chokeGroupCount, group);
    if (group > 0)
        tree.setProperty(chokeId, group, getUndoManager());
    else
        tree.removeProperty(chokeId, getUndoManager());
}

void DrumRackDevice::setPadName(int index, const juce::String& name)
{
    if (!validPad(index))
        return;
    auto tree = padState(index);
    if (name.trim().isNotEmpty())
        tree.setProperty(nameId, name.trim(), getUndoManager());
    else
        tree.removeProperty(nameId, getUndoManager());
}

// A kit is a fresh start: every pad takes what the kit gives it, nothing for
// a pad it leaves empty, and nothing stays muted or soloed.
void DrumRackDevice::setKit(const DrumKit& kit)
{
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        for (int index = 0; index < padCount; ++index)
        {
            writePad(index, kit.pads[static_cast<size_t>(index)]);
            auto tree = padState(index);
            tree.removeProperty(muteId, getUndoManager());
            tree.removeProperty(soloId, getUndoManager());
        }
    }
    syncPads();
}

int DrumRackDevice::selectedPad() const
{
    return juce::jlimit(0, padCount - 1, static_cast<int>(state.getProperty(selectedId, 0)));
}

void DrumRackDevice::setSelectedPad(int index)
{
    state.setProperty(selectedId, juce::jlimit(0, padCount - 1, index), nullptr);
}

void DrumRackDevice::previewPad(int index)
{
    engine.previewPad(index);
}

std::uint32_t DrumRackDevice::padStrikes(int index) const
{
    return engine.strikes(index);
}

void DrumRackDevice::collectSamples()
{
    engine.collect();
}

// ---- the pads into the engine ------------------------------------------------

std::unique_ptr<DrumSample> DrumRackDevice::readSample(const juce::File& file, juce::AudioFormatManager& formats)
{
    // On the message thread, never the audio thread: a pad is filled when its
    // state changes, which is a drop, a kit, an undo or a document opening.
    if (!file.existsAsFile())
    {
        juce::Logger::writeToLog("Rhino: drum sample missing: " + file.getFullPathName());
        return nullptr;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        // Present but not audio, which is what a Git LFS pointer that was
        // never pulled looks like.
        juce::Logger::writeToLog("Rhino: drum sample unreadable: " + file.getFullPathName());
        return nullptr;
    }

    // The whole file, up to a ceiling only a mistake would reach. Reading one
    // second of it cut the 1.5 s 808 kick off mid-decay with an audible step.
    const auto rate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    const auto frames = static_cast<int>(std::min<juce::int64>(
        reader->lengthInSamples,
        static_cast<juce::int64>(std::ceil(rate * DrumRackEngine::longestSampleSeconds))));
    const auto channels = static_cast<int>(std::min(2u, reader->numChannels));
    juce::AudioBuffer<float> buffer(channels, frames);
    reader->read(&buffer, 0, frames, 0, true, true);
    // A file past the ceiling ends in a short fade rather than a cut.
    if (frames < reader->lengthInSamples)
    {
        const auto fade = std::min(frames, static_cast<int>(rate * 0.005));
        buffer.applyGainRamp(frames - fade, fade, 1.0f, 0.0f);
    }

    auto sample = std::make_unique<DrumSample>();
    sample->sampleRate = rate;
    sample->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    if (channels > 1)
        sample->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    return sample;
}

void DrumRackDevice::syncPads()
{
    const auto tree = padsTree();
    for (int index = 0; index < padCount; ++index)
    {
        const auto padTree = tree.getChild(index);
        engine.setPadMuted(index, static_cast<bool>(padTree[muteId]));
        engine.setPadSoloed(index, static_cast<bool>(padTree[soloId]));
        engine.setPadChoke(index, static_cast<int>(padTree[chokeId]));

        // What the pad's engine slot should hold, written as a single string
        // so that one comparison says whether anything has to be read.
        juce::String wanted;
        if (padTree.hasProperty(synthId))
            wanted = "synth:" + padTree[synthId].toString();
        else if (padTree.hasProperty(sampleId))
            wanted = "sample:" + padTree[sampleId].toString();
        auto& slot = loaded[static_cast<size_t>(index)];
        if (wanted == slot)
            continue;
        slot = wanted;
        unreadable[static_cast<size_t>(index)] = false;
        if (padTree.hasProperty(synthId))
        {
            if (const auto model = drumModelFromId(padTree[synthId].toString()))
                engine.setPadSynth(index, *model);
            else
                engine.clearPad(index);
        }
        else if (padTree.hasProperty(sampleId))
        {
            auto sample = readSample(ContentLibrary::resolveStoredPath(padTree[sampleId].toString()), formats);
            unreadable[static_cast<size_t>(index)] = sample == nullptr;
            engine.setPadSample(index, std::move(sample));
        }
        else
        {
            engine.clearPad(index);
        }
    }
    engine.collect();
}

void DrumRackDevice::loadData(const juce::ValueTree& source)
{
    // A copy first: the tree restored from may share its pads with this one.
    const auto incoming = source.getChildWithName(padsId);
    const auto current = padsTree();
    if (incoming == current)
        return;
    const auto copy = incoming.isValid() ? incoming.createCopy() : juce::ValueTree();
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        if (current.isValid())
            state.removeChild(current, getUndoManager());
        if (copy.isValid())
            state.appendChild(copy, getUndoManager());
    }
    syncPads();
}

void DrumRackDevice::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    NativeInstrument::valueTreePropertyChanged(tree, property);
    if (!writing && concernsPads(tree))
        syncPads();
}

void DrumRackDevice::valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child)
{
    NativeInstrument::valueTreeChildAdded(parent, child);
    if (!writing && (child.hasType(padsId) || concernsPads(parent)))
        syncPads();
}

void DrumRackDevice::valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int index)
{
    NativeInstrument::valueTreeChildRemoved(parent, child, index);
    if (!writing && (child.hasType(padsId) || concernsPads(parent)))
        syncPads();
}

// ---- playing ------------------------------------------------------------------

void DrumRackDevice::prepare(double rate, int)
{
    engine.prepare(rate);
    readSettings();
}

void DrumRackDevice::clear()
{
    engine.clear();
}

void DrumRackDevice::readSettings() noexcept
{
    for (int index = 0; index < padCount; ++index)
        settings[static_cast<size_t>(index)] = controlsOf(index, false);
}

void DrumRackDevice::process(RenderBlock& block)
{
    if (block.numChannels == 0)
        return;
    // Envelopes fall towards zero all through a strike, and a float that
    // small is slow on every x86 that does not flush it.
    juce::ScopedNoDenormals noDenormals;
    readSettings();
    engine.render(block.channels[0], block.numChannels > 1 ? block.channels[1] : nullptr, block.numSamples, settings);
}

void DrumRackDevice::noteOn(int note, float velocity, int)
{
    readSettings();
    engine.noteOn(note, velocity, settings);
}

// A pad plays its sound out whatever happens to its key, as a drum does, so
// only all-notes-off and a panic stop one early.
void DrumRackDevice::allNotesOff()
{
    engine.releaseAll();
}

bool DrumRackDevice::hasNameForMidiNoteNumber(int note, int, juce::String& name)
{
    const auto index = note - lowestNote;
    if (!validPad(index) || !pad(index).sound.has_value())
        return false;
    name = padName(index);
    return true;
}

// ---- the face's picture --------------------------------------------------------

const DrumRackDevice::Picture& DrumRackDevice::padPicture(int index, int columns)
{
    static const Picture none;
    if (!validPad(index) || columns <= 0)
        return none;
    auto& cached = pictures[static_cast<size_t>(index)];
    const auto controls = controlsOf(index, false);
    const auto& sound = loaded[static_cast<size_t>(index)];
    if (cached.columns == columns && cached.sound == sound && cached.settings == controls)
        return cached.picture;
    cached.sound = sound;
    cached.settings = controls;
    cached.columns = columns;
    auto& picture = cached.picture;
    picture = {};
    picture.lows.assign(static_cast<size_t>(columns), 0.0f);
    picture.highs.assign(static_cast<size_t>(columns), 0.0f);

    const auto source = engine.padSource(index);
    const auto most = static_cast<int>(pictureRate * pictureSeconds);
    pictureLeft.resize(static_cast<size_t>(most));
    pictureRight.resize(static_cast<size_t>(most));
    auto& left = pictureLeft;
    auto& right = pictureRight;
    const auto frames = DrumRackEngine::renderStrike(source, engine.padModel(index), engine.padSample(index), controls,
                                                     1.0f, pictureRate, left.data(), right.data(), most);
    picture.seconds = frames / pictureRate;
    if (frames <= 0)
        return picture;
    for (int column = 0; column < columns; ++column)
    {
        const auto from = static_cast<int>(static_cast<juce::int64>(frames) * column / columns);
        const auto to = std::max(from + 1, static_cast<int>(static_cast<juce::int64>(frames) * (column + 1) / columns));
        auto low = 0.0f, high = 0.0f;
        for (int i = from; i < to && i < frames; ++i)
        {
            // The louder side, so a pad panned hard over still shows its sound.
            const auto l = left[static_cast<size_t>(i)], r = right[static_cast<size_t>(i)];
            const auto value = std::abs(l) >= std::abs(r) ? l : r;
            low = std::min(low, value);
            high = std::max(high, value);
        }
        picture.lows[static_cast<size_t>(column)] = low;
        picture.highs[static_cast<size_t>(column)] = high;
        picture.peak = std::max({ picture.peak, -low, high });
    }
    return picture;
}
}
