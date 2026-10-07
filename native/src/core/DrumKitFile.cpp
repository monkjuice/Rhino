#include "DrumKitFile.h"
#include "ContentLibrary.h"
#include "ExactFloatText.h"

namespace rhino
{
namespace
{
const juce::String soundTag { "RHINO_DRUM_SOUND" };
const juce::String kitTag { "RHINO_DRUM_KIT" };
const juce::String padTag { "PAD" };

// A pad's six controls, by the names the files give them.
struct SettingField
{
    const char* attribute;
    float DrumRackEngine::PadSettings::* member;
};

constexpr SettingField settingFields[] {
    { "tune",     &DrumRackEngine::PadSettings::tune },
    { "decay",    &DrumRackEngine::PadSettings::decay },
    { "tone",     &DrumRackEngine::PadSettings::tone },
    { "velocity", &DrumRackEngine::PadSettings::velocity },
    { "level",    &DrumRackEngine::PadSettings::level },
    { "pan",      &DrumRackEngine::PadSettings::pan },
};

// The numbers of a sample's playback, by the names the files give them.
struct PlaybackField
{
    const char* attribute;
    float DrumRackEngine::Playback::* member;
};

constexpr PlaybackField playbackFields[] {
    { "start",       &DrumRackEngine::Playback::start },
    { "end",         &DrumRackEngine::Playback::end },
    { "fadeIn",      &DrumRackEngine::Playback::fadeIn },
    { "fadeOut",     &DrumRackEngine::Playback::fadeOut },
    { "attack",      &DrumRackEngine::Playback::attack },
    { "sustain",     &DrumRackEngine::Playback::sustain },
    { "release",     &DrumRackEngine::Playback::release },
    { "sensitivity", &DrumRackEngine::Playback::sensitivity },
};

bool isWholeNumber(const juce::String& text)
{
    return text.isNotEmpty() && text.containsOnly("0123456789");
}

void writePlayback(const DrumRackEngine::Playback& playback, juce::XmlElement& xml)
{
    const DrumRackEngine::Playback plain;
    if (playback.mode != plain.mode)
        xml.setAttribute("mode", playModeId(playback.mode));
    for (const auto& field : playbackFields)
        if (playback.*(field.member) != plain.*(field.member))
            xml.setAttribute(field.attribute, exactFloatText(playback.*(field.member)));
    if (playback.loop)
        xml.setAttribute("loop", 1);
    if (playback.sliceBy != plain.sliceBy)
        xml.setAttribute("sliceBy", "divisions");
    if (playback.divisions != plain.divisions)
        xml.setAttribute("divisions", playback.divisions);
}

void writeSound(const DrumSound& sound, juce::XmlElement& xml)
{
    if (sound.name.isNotEmpty())
        xml.setAttribute("name", sound.name);
    if (sound.source == DrumRackEngine::Source::synth)
        xml.setAttribute("synth", drumModelInfo(sound.model).id);
    else
        xml.setAttribute("sample", sound.sample);
    for (const auto& field : settingFields)
        xml.setAttribute(field.attribute, exactFloatText(sound.settings.*(field.member)));
    xml.setAttribute("choke", sound.choke);
    if (sound.source == DrumRackEngine::Source::sample)
        writePlayback(sound.playback, xml);
}

juce::Result readPlayback(const juce::XmlElement& xml, DrumRackEngine::Playback& into, const juce::String& what)
{
    DrumRackEngine::Playback playback;
    if (xml.hasAttribute("mode"))
    {
        const auto mode = playModeFromId(xml.getStringAttribute("mode"));
        if (!mode.has_value())
            return juce::Result::fail(what + " plays in a mode this Rhino does not have: "
                                      + xml.getStringAttribute("mode") + ".");
        playback.mode = *mode;
    }
    for (const auto& field : playbackFields)
    {
        if (!xml.hasAttribute(field.attribute))
            continue;
        const auto value = exactFloatFrom(xml.getStringAttribute(field.attribute));
        if (!value.has_value())
            return juce::Result::fail(what + " has a " + juce::String(field.attribute) + " that is not a number.");
        playback.*(field.member) = *value;
    }
    if (xml.hasAttribute("loop"))
    {
        const auto loop = xml.getStringAttribute("loop").trim();
        if (loop != "0" && loop != "1")
            return juce::Result::fail(what + " has a loop that is not 0 or 1.");
        playback.loop = loop == "1";
    }
    if (xml.hasAttribute("sliceBy"))
    {
        const auto by = xml.getStringAttribute("sliceBy").trim();
        if (by != "transients" && by != "divisions")
            return juce::Result::fail(what + " slices by " + by + ", which is not transients or divisions.");
        playback.sliceBy = by == "divisions" ? DrumRackEngine::SliceBy::divisions : DrumRackEngine::SliceBy::transients;
    }
    if (xml.hasAttribute("divisions"))
    {
        const auto divisions = xml.getStringAttribute("divisions").trim();
        if (!isWholeNumber(divisions) || divisions.getIntValue() < 2 || divisions.getIntValue() > 64)
            return juce::Result::fail(what + " has divisions that are not 2 to 64.");
        playback.divisions = divisions.getIntValue();
    }
    if (!(playback.start >= 0.0f && playback.start < playback.end && playback.end <= 1.0f))
        return juce::Result::fail(what + " plays a part of its sample that ends before it starts.");
    into = playback.clamped();
    return juce::Result::ok();
}

// `what` says where the sound is, for the message: "This drum preset", "C2".
juce::Result readSound(const juce::XmlElement& xml, DrumSound& into, const juce::String& what)
{
    DrumSound sound;
    sound.name = xml.getStringAttribute("name").trim();
    const auto hasSample = xml.hasAttribute("sample");
    const auto hasSynth = xml.hasAttribute("synth");
    if (hasSample == hasSynth)
        return juce::Result::fail(what + " has to name a sample or a synth, and not both.");
    if (hasSynth)
    {
        const auto id = xml.getStringAttribute("synth");
        const auto model = drumModelFromId(id);
        if (!model.has_value())
            return juce::Result::fail(what + " names a synth this Rhino does not have: " + id + ".");
        sound.source = DrumRackEngine::Source::synth;
        sound.model = *model;
    }
    else
    {
        sound.source = DrumRackEngine::Source::sample;
        sound.sample = xml.getStringAttribute("sample").trim();
        if (sound.sample.isEmpty())
            return juce::Result::fail(what + " names an empty sample.");
        if (const auto playback = readPlayback(xml, sound.playback, what); playback.failed())
            return playback;
    }
    for (const auto& field : settingFields)
    {
        if (!xml.hasAttribute(field.attribute))
            continue;
        const auto value = exactFloatFrom(xml.getStringAttribute(field.attribute));
        if (!value.has_value())
            return juce::Result::fail(what + " has a " + juce::String(field.attribute) + " that is not a number.");
        sound.settings.*(field.member) = *value;
    }
    if (xml.hasAttribute("choke"))
    {
        const auto choke = xml.getStringAttribute("choke").trim();
        if (!isWholeNumber(choke) || choke.getIntValue() > DrumRackEngine::chokeGroupCount)
            return juce::Result::fail(what + " has a choke group that is not 0 to "
                                      + juce::String(DrumRackEngine::chokeGroupCount) + ".");
        sound.choke = choke.getIntValue();
    }
    into = std::move(sound);
    return juce::Result::ok();
}

juce::Result checkHeader(const juce::XmlElement& xml, const juce::String& tag, const juce::String& noun, int format)
{
    if (!xml.hasTagName(tag))
        return juce::Result::fail("This is not a Rhino " + noun + ".");
    if (xml.getIntAttribute("format") != format)
        return juce::Result::fail("This " + noun + " was saved in format " + xml.getStringAttribute("format")
                                  + ", and this Rhino reads format " + juce::String(format) + ".");
    return juce::Result::ok();
}

juce::Result writeXml(const juce::XmlElement& xml, const juce::File& file, const juce::String& noun)
{
    if (const auto made = file.getParentDirectory().createDirectory(); made.failed())
        return juce::Result::fail("The " + noun + " folder could not be made: " + made.getErrorMessage());
    if (!xml.writeTo(file))
        return juce::Result::fail("The " + noun + " could not be written to " + file.getFullPathName() + ".");
    return juce::Result::ok();
}

juce::Result parse(const juce::File& file, const juce::String& noun, std::unique_ptr<juce::XmlElement>& xml)
{
    if (!file.existsAsFile())
        return juce::Result::fail("That " + noun + " file is missing.");
    xml = juce::parseXML(file);
    if (xml == nullptr)
        return juce::Result::fail(DrumFiles::nameOf(file) + " is not a readable " + noun + ".");
    return juce::Result::ok();
}
}

juce::String padNoteName(int note)
{
    static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (!juce::isPositiveAndBelow(note, DrumRackEngine::padCount))
        return {};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

juce::String playModeId(DrumRackEngine::PlayMode mode)
{
    switch (mode)
    {
        case DrumRackEngine::PlayMode::classic: return "classic";
        case DrumRackEngine::PlayMode::slice:   return "slice";
        case DrumRackEngine::PlayMode::oneShot: break;
    }
    return "oneshot";
}

std::optional<DrumRackEngine::PlayMode> playModeFromId(const juce::String& id)
{
    for (const auto mode : { DrumRackEngine::PlayMode::oneShot, DrumRackEngine::PlayMode::classic,
                             DrumRackEngine::PlayMode::slice })
        if (playModeId(mode).equalsIgnoreCase(id.trim()))
            return mode;
    return std::nullopt;
}

DrumSound DrumSound::forSample(const juce::File& file)
{
    DrumSound sound;
    sound.source = DrumRackEngine::Source::sample;
    sound.sample = ContentLibrary::storedPath(file);
    return sound;
}

DrumSound DrumSound::forSynth(DrumModel model)
{
    DrumSound sound;
    sound.source = DrumRackEngine::Source::synth;
    sound.model = model;
    sound.settings.decay = drumModelInfo(model).decay;
    sound.settings.tone = drumModelInfo(model).tone;
    return sound;
}

juce::String DrumSound::displayName() const
{
    if (name.isNotEmpty())
        return name;
    if (source == DrumRackEngine::Source::synth)
        return drumModelInfo(model).name;
    return sample.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false)
                 .upToLastOccurrenceOf(".", false, false);
}

std::unique_ptr<juce::XmlElement> DrumFiles::toXml(const DrumSound& sound)
{
    auto xml = std::make_unique<juce::XmlElement>(soundTag);
    xml->setAttribute("format", soundFormat);
    writeSound(sound, *xml);
    return xml;
}

std::unique_ptr<juce::XmlElement> DrumFiles::toXml(const DrumKit& kit)
{
    auto xml = std::make_unique<juce::XmlElement>(kitTag);
    xml->setAttribute("format", kitFormat);
    for (size_t note = 0; note < kit.pads.size(); ++note)
    {
        if (!kit.pads[note].has_value())
            continue;
        auto* element = xml->createNewChildElement(padTag);
        element->setAttribute("note", static_cast<int>(note));
        writeSound(*kit.pads[note], *element);
    }
    return xml;
}

juce::Result DrumFiles::fromXml(const juce::XmlElement& xml, DrumSound& into)
{
    if (const auto header = checkHeader(xml, soundTag, "drum preset", soundFormat); header.failed())
        return header;
    return readSound(xml, into, "This drum preset");
}

juce::Result DrumFiles::fromXml(const juce::XmlElement& xml, DrumKit& into)
{
    if (const auto header = checkHeader(xml, kitTag, "drum kit", kitFormat); header.failed())
        return header;
    DrumKit kit;
    for (const auto* element : xml.getChildWithTagNameIterator(padTag))
    {
        const auto note = element->getStringAttribute("note").trim();
        if (!isWholeNumber(note) || note.getIntValue() >= DrumRackEngine::padCount)
            return juce::Result::fail("This kit has a pad on note " + (note.isEmpty() ? juce::String("(none)") : note)
                                      + ", and a rack has pads on notes 0 to "
                                      + juce::String(DrumRackEngine::padCount - 1) + ".");
        auto& slot = kit.pads[static_cast<size_t>(note.getIntValue())];
        const auto where = padNoteName(note.getIntValue());
        if (slot.has_value())
            return juce::Result::fail("This kit fills " + where + " twice.");
        DrumSound sound;
        if (const auto read = readSound(*element, sound, where + " of this kit"); read.failed())
            return read;
        slot = std::move(sound);
    }
    into = std::move(kit);
    return juce::Result::ok();
}

juce::Result DrumFiles::write(const DrumSound& sound, const juce::File& file)
{
    return writeXml(*toXml(sound), file, "drum preset");
}

juce::Result DrumFiles::write(const DrumKit& kit, const juce::File& file)
{
    return writeXml(*toXml(kit), file, "drum kit");
}

juce::Result DrumFiles::read(const juce::File& file, DrumSound& into)
{
    std::unique_ptr<juce::XmlElement> xml;
    if (const auto parsed = parse(file, "drum preset", xml); parsed.failed())
        return parsed;
    return fromXml(*xml, into);
}

juce::Result DrumFiles::read(const juce::File& file, DrumKit& into)
{
    std::unique_ptr<juce::XmlElement> xml;
    if (const auto parsed = parse(file, "drum kit", xml); parsed.failed())
        return parsed;
    return fromXml(*xml, into);
}
}
