#pragma once
#include "DrumKitFile.h"
#include "DrumRackEngine.h"
#include "sdk/NativeDevice.h"
#include <array>
#include <optional>
#include <vector>

namespace rhino
{
// The Drum Rack: a pad on every MIDI note, as in Live, each holding a sample or
// a synthesised drum with six controls of its own, its own mute, solo and
// choke group, and for a sample the way it plays (one-shot, classic or slice,
// a part of the file, fades and an envelope). A new rack is blank; kits (.rdk)
// and drum presets (.rdp) fill it, and so does dropping a sample on a pad.
// Its face shows sixteen pads at a time, a bank of four rows of four.
//
// The sound is DrumRackEngine's (core/DrumRackEngine.h); this is the shell.
// Each pad's controls are declared on the device SDK, so they save, undo and
// automate like any other device's. What a pad holds -- which sample or which
// synth, its name, mute, solo, choke group and playback -- is content a
// control cannot carry, kept in a PADS child of the device's state, one PAD
// per note that holds something, and read into the engine whenever it
// changes, an undo included.
//
// DrumRackDevice.cpp is the pads, their state and playing them;
// DrumRackDeviceEditing.cpp is what the face asks for: pictures, slices and
// the bank it shows.
class DrumRackDevice final : public NativeInstrument
{
public:
    inline static const char* xmlTypeName = "rhino.drumrack.v1";
    static constexpr int padCount = DrumRackEngine::padCount;
    // A bank is the sixteen pads the face shows, four rows of four from a note
    // that is a multiple of four; the face scrolls a row at a time.
    static constexpr int bankSize = 16;
    static constexpr int rowSize = 4;
    static constexpr int lastFirstNote = padCount - bankSize;

    // Each pad's controls, declared in this order, pad after pad by note, so
    // a control's parameter index is note * controlCount + control. An
    // automation lane stores that index, so a new control cannot simply be
    // appended here: it would move every pad's after the first.
    enum Control { tune, decay, tone, velocity, level, pan, controlCount };
    static constexpr int parameterIndex(int pad, int control) { return pad * controlCount + control; }

    explicit DrumRackDevice(te::PluginCreationInfo);

    // ---- what the pads hold (message thread) -------------------------------
    struct Pad
    {
        // Nothing for an empty pad. The settings are the controls as set,
        // not where a lane has moved them.
        std::optional<DrumSound> sound;
        bool muted = false, soloed = false;
        // A sample pad whose file could not be read: missing, or not audio.
        bool unreadable = false;
    };
    Pad pad(int note) const;
    // What a pad is called: its sound's name, or its note when it is empty.
    juce::String padName(int note) const;
    static juce::String noteName(int note);
    DrumKit kit() const;
    bool isBlank() const;
    // Which notes hold a sound, read from the pads' state without building
    // each sound: what the face's map is drawn from.
    std::array<bool, padCount> filledPads() const;
    // The first pad with nothing on it from a note upward, coming round from
    // the bottom, or -1 when every pad is full.
    int firstEmptyPad(int from = 0) const;

    // Writers. Each goes through the edit's undo manager, so the caller makes
    // it an undo step of its own -- Session does.
    void setPadSound(int note, const std::optional<DrumSound>&);
    // A file dropped on a pad. A pad that already played a sample keeps all
    // its controls and its playback, so auditioning one snare after another
    // keeps the snare's tuning; any pad keeps its level, pan, velocity and
    // choke group.
    void setPadSample(int note, const juce::File&);
    // A synth starts at its model's own Decay and Tone, keeping the pad's
    // level, pan, velocity and choke group.
    void setPadSynth(int note, DrumModel);
    void clearPad(int note);
    void setPadMuted(int note, bool);
    void setPadSoloed(int note, bool);
    void setPadChoke(int note, int group);
    void setPadName(int note, const juce::String&);
    // A sample pad's mode, part, fades and envelope. A drag on the face writes
    // them as it goes without an undo step (undoable false) and then once more,
    // as one, when the drag ends.
    void setPadPlayback(int note, const DrumRackEngine::Playback&, bool undoable = true);
    void setKit(const DrumKit&);
    // Moves what a pad holds to another note, trading places with whatever is
    // there: its sound, name, choke group, mute, solo, playback and six
    // controls all go with it. Notes in clips and automation lanes stay on
    // their notes, so a moved sound is played by the notes of the pad it
    // landed on. False when there is nothing on the pad to move.
    bool movePad(int from, int to);

    // Where Slice cuts a sample pad's part, as the starts of its slices in
    // fractions of the file (DrumSlicer). Empty for anything else.
    const std::vector<float>& padSlices(int note);
    // Puts each slice on a pad of its own, from this pad up: the same sample,
    // a one-shot of the slice, the pad's controls and choke group, named for
    // the sound and the slice's number. Pads past the last note are not
    // reached. Returns how many pads were filled.
    int spreadSlices(int note);

    // Which pad the face shows, and from which note its bank of sixteen
    // starts. View state: kept with the device, so it survives the rack
    // rebuilding its faces, and never an undo step.
    int selectedPad() const;
    void setSelectedPad(int);
    int firstShownNote() const;
    void showNotesFrom(int note);
    // Auto Select, as Live has it: while it is on, the face selects a pad as
    // its note arrives, from a keyboard, a controller or a clip. View state,
    // on until it is switched off.
    bool autoSelect() const;
    void setAutoSelect(bool);

    // Strikes a pad, or a part of its sample, from the face. Any thread.
    void previewPad(int note);
    void previewPart(int note, float start, float end);
    std::uint32_t padStrikes(int note) const;
    std::uint32_t notesReceived(int note) const;
    float padPlayhead(int note) const;
    // Lets go of samples nothing plays any more. The face calls it as it
    // animates, and every edit does too.
    void collectSamples();

    // A picture of a pad: a synth's strike, played by a voice of its own from
    // the pad's controls as they stand, or a sample's whole file; as the lows
    // and highs of `columns` slices of it, and how long it lasts. Message
    // thread; the last picture is kept while nothing it was drawn from
    // changes, since a dragged knob asks every frame.
    struct Picture
    {
        std::vector<float> lows, highs;
        double seconds = 0.0;
        float peak = 0.0f;
    };
    const Picture& padPicture(int note, int columns);
    const Picture& samplePicture(int note, int columns);

    // Reads a sample file the way a pad does: the whole of it, up to the
    // engine's ceiling, which it reaches in a fade. Null, and a line in the
    // log, for a file that is missing or is not audio. Never the audio thread.
    static std::unique_ptr<DrumSample> readSample(const juce::File&, juce::AudioFormatManager&);

    bool hasNameForMidiNoteNumber(int note, int midiChannel, juce::String& name) override;

private:
    struct PadControls
    {
        Param tune, decay, tone, velocity, level, pan;
    };
    std::array<PadControls, padCount> declarePads();

    void prepare(double rate, int maximumBlockSize) override;
    void clear() override;
    void process(RenderBlock&) override;
    void noteOn(int note, float velocity, int channel) override;
    void noteOff(int note, float velocity, int channel) override;
    void allNotesOff() override;
    void loadData(const juce::ValueTree&) override;
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;

    juce::ValueTree padsTree() const;
    // The PAD a note holds, if it holds anything.
    juce::ValueTree padTree(int note) const;
    // The PAD for a note, made through the undo manager when there is none.
    juce::ValueTree padState(int note);
    bool concernsPads(const juce::ValueTree&) const;
    // `asSet` reads where each control was set, rather than where a lane has
    // it now.
    DrumRackEngine::PadSettings controlsOf(int note, bool asSet) const;
    void writeControls(int note, const DrumRackEngine::PadSettings&);
    void writePlayback(juce::ValueTree&, const DrumRackEngine::Playback&, juce::UndoManager*);
    // A pad's sound and controls written, without reading them into the
    // engine: the writer does that once it has finished.
    void writePad(int note, const std::optional<DrumSound>&);
    void readSettings() noexcept;
    // The pads' state, applied to the engine: files read, synths chosen.
    void syncPads();

    std::array<PadControls, padCount> pads = declarePads();
    DrumRackEngine engine;
    // The controls as the audio thread last read them, for the pads that hold
    // something.
    DrumRackEngine::Settings settings {};
    juce::AudioFormatManager formats;
    // What each pad's engine slot was last given, so an edit to one pad does
    // not read every file again. Message thread.
    std::array<juce::String, padCount> loaded;
    std::array<bool, padCount> unreadable {};
    // Set while a writer changes several properties at once, so the pads are
    // read into the engine once, at the end, rather than half-way through.
    bool writing = false;

    struct CachedPicture
    {
        juce::String sound;
        DrumRackEngine::PadSettings settings;
        int columns = 0;
        Picture picture;
    };
    // The selected pad's pictures are the ones a face asks for, so one of
    // each kind is kept.
    int pictureNote = -1, samplePictureNote = -1;
    CachedPicture picture, sampleWave;
    // A strike rendered for a picture, kept so that a dragged knob does not
    // allocate one every frame.
    std::vector<float> pictureLeft, pictureRight;
    struct CachedSlices
    {
        int note = -1;
        juce::String sound;
        DrumRackEngine::Playback playback;
        std::vector<float> starts;
    };
    CachedSlices slices;
};
}
