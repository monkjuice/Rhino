#include "DrumRackDevice.h"
#include "DrumRackDeviceInternal.h"
#include "ContentLibrary.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
using namespace drumrack;

namespace
{
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

// A sample pad's playback as its PAD holds it. Anything a PAD leaves out is a
// plain one-shot of the whole file.
DrumRackEngine::Playback playbackFrom(const juce::ValueTree& tree)
{
    DrumRackEngine::Playback playback;
    if (const auto mode = playModeFromId(tree[modeId].toString()))
        playback.mode = *mode;
    const auto number = [&tree] (const juce::Identifier& id, float fallback)
    {
        return tree.hasProperty(id) ? static_cast<float>(tree[id]) : fallback;
    };
    playback.start = number(startId, playback.start);
    playback.end = number(endId, playback.end);
    playback.fadeIn = number(fadeInId, playback.fadeIn);
    playback.fadeOut = number(fadeOutId, playback.fadeOut);
    playback.attack = number(attackId, playback.attack);
    playback.sustain = number(sustainId, playback.sustain);
    playback.release = number(releaseId, playback.release);
    playback.sensitivity = number(sensitivityId, playback.sensitivity);
    playback.loop = static_cast<bool>(tree[loopId]);
    playback.sliceBy = tree[sliceById].toString() == "divisions" ? DrumRackEngine::SliceBy::divisions
                                                                  : DrumRackEngine::SliceBy::transients;
    if (tree.hasProperty(divisionsId))
        playback.divisions = static_cast<int>(tree[divisionsId]);
    return playback.clamped();
}
}

DrumRackDevice::DrumRackDevice(te::PluginCreationInfo info) : NativeInstrument(std::move(info), xmlTypeName)
{
    formats.registerBasicFormats();
    // A rack opened from a document already holds its pads.
    syncPads();
}

// Each pad's six controls, declared pad by pad from the lowest note. The names
// carry the pad's note, because an automation lane is read away from the face.
std::array<DrumRackDevice::PadControls, DrumRackDevice::padCount> DrumRackDevice::declarePads()
{
    std::array<PadControls, padCount> declared;
    for (int note = 0; note < padCount; ++note)
    {
        const auto prefix = "n" + juce::String(note);
        const auto label = noteName(note);
        // The pads share one place on a face, a tab each, should a generated
        // face ever be asked to show them.
        const juce::String padTabs = "Pads";
        auto& controls = declared[static_cast<size_t>(note)];
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

juce::ValueTree DrumRackDevice::padTree(int note) const
{
    const auto tree = padsTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (const auto child = tree.getChild(i); child.hasType(padId) && child.hasProperty(noteId)
                                                  && static_cast<int>(child[noteId]) == note)
            return child;
    return {};
}

juce::ValueTree DrumRackDevice::padState(int note)
{
    if (auto existing = padTree(note); existing.isValid())
        return existing;
    auto* undo = getUndoManager();
    auto tree = padsTree();
    if (!tree.isValid())
    {
        tree = juce::ValueTree(padsId);
        state.appendChild(tree, undo);
    }
    juce::ValueTree made(padId);
    made.setProperty(noteId, note, nullptr);
    tree.appendChild(made, undo);
    return made;
}

bool DrumRackDevice::concernsPads(const juce::ValueTree& tree) const
{
    return tree.hasType(padsId) || tree.getParent().hasType(padsId);
}

DrumRackEngine::PadSettings DrumRackDevice::controlsOf(int note, bool asSet) const
{
    const auto& controls = pads[static_cast<size_t>(note)];
    const auto read = [asSet] (const Param& control)
    {
        return asSet ? control.automatable().getCurrentBaseValue() : control.value();
    };
    return { read(controls.tune), read(controls.decay), read(controls.tone),
             read(controls.velocity), read(controls.level), read(controls.pan) };
}

DrumRackDevice::Pad DrumRackDevice::pad(int note) const
{
    Pad view;
    if (!validPad(note))
        return view;
    const auto tree = padTree(note);
    if (!tree.isValid())
        return view;
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
        sound.playback = playbackFrom(tree);
        view.unreadable = unreadable[static_cast<size_t>(note)];
    }
    else
    {
        return view;
    }
    sound.name = tree[nameId].toString();
    sound.choke = juce::jlimit(0, DrumRackEngine::chokeGroupCount, static_cast<int>(tree[chokeId]));
    sound.settings = controlsOf(note, true);
    view.sound = std::move(sound);
    return view;
}

juce::String DrumRackDevice::padName(int note) const
{
    const auto view = pad(note);
    return view.sound.has_value() ? view.sound->displayName() : noteName(note);
}

juce::String DrumRackDevice::noteName(int note)
{
    return padNoteName(note);
}

DrumKit DrumRackDevice::kit() const
{
    DrumKit whole;
    const auto tree = padsTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (const auto note = static_cast<int>(tree.getChild(i)[noteId]); validPad(note))
            whole.pads[static_cast<size_t>(note)] = pad(note).sound;
    return whole;
}

bool DrumRackDevice::isBlank() const
{
    const auto tree = padsTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (const auto child = tree.getChild(i); child.hasProperty(sampleId) || child.hasProperty(synthId))
            return false;
    return true;
}

std::array<bool, DrumRackDevice::padCount> DrumRackDevice::filledPads() const
{
    std::array<bool, padCount> filled {};
    const auto tree = padsTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (const auto child = tree.getChild(i); child.hasProperty(sampleId) || child.hasProperty(synthId))
            if (const auto note = static_cast<int>(child[noteId]); validPad(note))
                filled[static_cast<size_t>(note)] = true;
    return filled;
}

int DrumRackDevice::firstEmptyPad(int from) const
{
    const auto filled = filledPads();
    from = juce::jlimit(0, padCount - 1, from);
    for (int step = 0; step < padCount; ++step)
        if (const auto note = (from + step) % padCount; !filled[static_cast<size_t>(note)])
            return note;
    return -1;
}

// ---- writing the pads ---------------------------------------------------------

void DrumRackDevice::writeControls(int note, const DrumRackEngine::PadSettings& wanted)
{
    const auto& controls = pads[static_cast<size_t>(note)];
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

void DrumRackDevice::writePlayback(juce::ValueTree& tree, const DrumRackEngine::Playback& wanted,
                                   juce::UndoManager* undo)
{
    const auto playback = wanted.clamped();
    const DrumRackEngine::Playback plain;
    const auto write = [&tree, undo] (const juce::Identifier& id, bool isPlain, const juce::var& value)
    {
        if (isPlain)
            tree.removeProperty(id, undo);
        else if (tree[id] != value)
            tree.setProperty(id, value, undo);
    };
    write(modeId, playback.mode == plain.mode, playModeId(playback.mode));
    write(startId, playback.start == plain.start, playback.start);
    write(endId, playback.end == plain.end, playback.end);
    write(fadeInId, playback.fadeIn == plain.fadeIn, playback.fadeIn);
    write(fadeOutId, playback.fadeOut == plain.fadeOut, playback.fadeOut);
    write(attackId, playback.attack == plain.attack, playback.attack);
    write(sustainId, playback.sustain == plain.sustain, playback.sustain);
    write(releaseId, playback.release == plain.release, playback.release);
    write(loopId, playback.loop == plain.loop, true);
    write(sliceById, playback.sliceBy == plain.sliceBy, "divisions");
    write(divisionsId, playback.divisions == plain.divisions, playback.divisions);
    write(sensitivityId, playback.sensitivity == plain.sensitivity, playback.sensitivity);
}

void DrumRackDevice::writePad(int note, const std::optional<DrumSound>& sound)
{
    auto* undo = getUndoManager();
    if (!sound.has_value())
    {
        // An empty pad holds nothing at all, mute and solo included.
        if (const auto tree = padTree(note); tree.isValid())
            padsTree().removeChild(tree, undo);
        writeControls(note, {});
        return;
    }
    auto tree = padState(note);
    if (sound->source == DrumRackEngine::Source::synth)
    {
        tree.setProperty(synthId, juce::String(drumModelInfo(sound->model).id), undo);
        tree.removeProperty(sampleId, undo);
        writePlayback(tree, {}, undo);
    }
    else
    {
        tree.setProperty(sampleId, sound->sample, undo);
        tree.removeProperty(synthId, undo);
        writePlayback(tree, sound->playback, undo);
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
    writeControls(note, sound->settings);
}

void DrumRackDevice::setPadSound(int note, const std::optional<DrumSound>& sound)
{
    if (!validPad(note))
        return;
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        writePad(note, sound);
    }
    syncPads();
}

void DrumRackDevice::setPadSample(int note, const juce::File& file)
{
    if (!validPad(note))
        return;
    auto sound = DrumSound::forSample(file);
    if (const auto before = pad(note).sound; before.has_value())
    {
        sound.choke = before->choke;
        if (before->source == DrumRackEngine::Source::sample)
        {
            sound.settings = before->settings;
            // The way the pad plays stays, but not the part of the last file
            // it played: a new file is played from its start to its end.
            sound.playback = before->playback;
            sound.playback.start = 0.0f;
            sound.playback.end = 1.0f;
        }
        else
        {
            sound.settings.level = before->settings.level;
            sound.settings.pan = before->settings.pan;
            sound.settings.velocity = before->settings.velocity;
        }
    }
    setPadSound(note, sound);
}

void DrumRackDevice::setPadSynth(int note, DrumModel model)
{
    if (!validPad(note))
        return;
    const auto before = pad(note).sound;
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
    setPadSound(note, sound);
}

void DrumRackDevice::clearPad(int note)
{
    setPadSound(note, std::nullopt);
}

void DrumRackDevice::setPadMuted(int note, bool muted)
{
    auto tree = padTree(note);
    if (!tree.isValid())
        return;
    if (muted)
        tree.setProperty(muteId, true, getUndoManager());
    else
        tree.removeProperty(muteId, getUndoManager());
}

void DrumRackDevice::setPadSoloed(int note, bool soloed)
{
    auto tree = padTree(note);
    if (!tree.isValid())
        return;
    if (soloed)
        tree.setProperty(soloId, true, getUndoManager());
    else
        tree.removeProperty(soloId, getUndoManager());
}

void DrumRackDevice::setPadChoke(int note, int group)
{
    auto tree = padTree(note);
    if (!tree.isValid())
        return;
    group = juce::jlimit(0, DrumRackEngine::chokeGroupCount, group);
    if (group > 0)
        tree.setProperty(chokeId, group, getUndoManager());
    else
        tree.removeProperty(chokeId, getUndoManager());
}

void DrumRackDevice::setPadName(int note, const juce::String& name)
{
    auto tree = padTree(note);
    if (!tree.isValid())
        return;
    if (name.trim().isNotEmpty())
        tree.setProperty(nameId, name.trim(), getUndoManager());
    else
        tree.removeProperty(nameId, getUndoManager());
}

void DrumRackDevice::setPadPlayback(int note, const DrumRackEngine::Playback& playback, bool undoable)
{
    auto tree = padTree(note);
    if (!tree.isValid() || !tree.hasProperty(sampleId))
        return;
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        writePlayback(tree, playback, undoable ? getUndoManager() : nullptr);
    }
    syncPads();
}

// A kit is a fresh start: every pad takes what the kit gives it, nothing for
// a pad it leaves empty, and nothing stays muted or soloed.
void DrumRackDevice::setKit(const DrumKit& kit)
{
    {
        const juce::ScopedValueSetter<bool> batch(writing, true);
        for (int note = 0; note < padCount; ++note)
        {
            const auto& sound = kit.pads[static_cast<size_t>(note)];
            if (!sound.has_value() && !padTree(note).isValid())
                continue;
            writePad(note, sound);
            if (auto tree = padTree(note); tree.isValid())
            {
                tree.removeProperty(muteId, getUndoManager());
                tree.removeProperty(soloId, getUndoManager());
            }
        }
    }
    syncPads();
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
    // Each note's PAD, found once rather than searched for per note.
    std::array<juce::ValueTree, padCount> byNote;
    const auto tree = padsTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild(i);
        const auto note = child.hasProperty(noteId) ? static_cast<int>(child[noteId]) : -1;
        if (validPad(note) && !byNote[static_cast<size_t>(note)].isValid())
            byNote[static_cast<size_t>(note)] = child;
    }
    for (int note = 0; note < padCount; ++note)
    {
        const auto& padTree = byNote[static_cast<size_t>(note)];
        engine.setPadMuted(note, static_cast<bool>(padTree[muteId]));
        engine.setPadSoloed(note, static_cast<bool>(padTree[soloId]));
        engine.setPadChoke(note, static_cast<int>(padTree[chokeId]));
        engine.setPadPlayback(note, padTree.hasProperty(sampleId) ? playbackFrom(padTree) : DrumRackEngine::Playback {});

        // What the pad's engine slot should hold, written as a single string
        // so that one comparison says whether anything has to be read.
        juce::String wanted;
        if (padTree.hasProperty(synthId))
            wanted = "synth:" + padTree[synthId].toString();
        else if (padTree.hasProperty(sampleId))
            wanted = "sample:" + padTree[sampleId].toString();
        auto& slot = loaded[static_cast<size_t>(note)];
        if (wanted == slot)
            continue;
        slot = wanted;
        unreadable[static_cast<size_t>(note)] = false;
        if (padTree.hasProperty(synthId))
        {
            if (const auto model = drumModelFromId(padTree[synthId].toString()))
                engine.setPadSynth(note, *model);
            else
                engine.clearPad(note);
        }
        else if (padTree.hasProperty(sampleId))
        {
            auto sample = readSample(ContentLibrary::resolveStoredPath(padTree[sampleId].toString()), formats);
            unreadable[static_cast<size_t>(note)] = sample == nullptr;
            engine.setPadSample(note, std::move(sample));
        }
        else
        {
            engine.clearPad(note);
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
    // Only the pads that hold something are read: six controls each on every
    // pad would be 768 reads each time.
    for (int note = 0; note < padCount; ++note)
        if (engine.padSource(note) != DrumRackEngine::Source::empty)
            settings[static_cast<size_t>(note)] = controlsOf(note, false);
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

// A one-shot plays its sound out whatever happens to its key, as a drum does;
// a classic pad lets go and releases.
void DrumRackDevice::noteOff(int note, float, int)
{
    engine.noteOff(note);
}

void DrumRackDevice::allNotesOff()
{
    engine.releaseAll();
}

bool DrumRackDevice::hasNameForMidiNoteNumber(int note, int, juce::String& name)
{
    if (!validPad(note) || !pad(note).sound.has_value())
        return false;
    name = padName(note);
    return true;
}
}
