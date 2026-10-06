#pragma once
#include "DrumKitFile.h"
#include "DrumRackEngine.h"
#include "sdk/NativeDevice.h"
#include <array>
#include <optional>
#include <vector>

namespace rhino
{
// The Drum Rack: sixteen pads on the notes from C2, each holding a sample or a
// synthesised drum with six controls of its own, and its own mute, solo and
// choke group. A new rack is blank; kits (.rdk) and drum presets (.rdp) fill
// it, and so does dropping a sample on a pad.
//
// The sound is DrumRackEngine's (core/DrumRackEngine.h); this is the shell.
// Each pad's controls are declared on the device SDK, so they save, undo and
// automate like any other device's. What a pad holds -- which sample or which
// synth, its name, mute, solo and choke group -- is content a control cannot
// carry, kept in a PADS child of the device's state and read into the engine
// whenever it changes, an undo included.
class DrumRackDevice final : public NativeInstrument
{
public:
    inline static const char* xmlTypeName = "rhino.drumrack.v1";
    static constexpr int padCount = DrumRackEngine::padCount;
    static constexpr int lowestNote = DrumRackEngine::lowestNote;

    // Each pad's controls, declared in this order, so a control's parameter
    // index is pad * controlCount + control. Append, never reorder.
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
    Pad pad(int) const;
    // What a pad is called: its sound's name, or its note when it is empty.
    juce::String padName(int) const;
    static juce::String noteName(int pad);
    DrumKit kit() const;
    bool isBlank() const;
    // The first pad with nothing on it, or -1 when every pad is full.
    int firstEmptyPad() const;

    // Writers. Each goes through the edit's undo manager, so the caller makes
    // it an undo step of its own -- Session does.
    void setPadSound(int pad, const std::optional<DrumSound>&);
    // A file dropped on a pad. A pad that already played a sample keeps all
    // its controls, so auditioning one snare after another keeps the snare's
    // tuning; any pad keeps its level, pan, velocity and choke group.
    void setPadSample(int pad, const juce::File&);
    // A synth starts at its model's own Decay and Tone, keeping the pad's
    // level, pan, velocity and choke group.
    void setPadSynth(int pad, DrumModel);
    void clearPad(int pad);
    void setPadMuted(int pad, bool);
    void setPadSoloed(int pad, bool);
    void setPadChoke(int pad, int group);
    void setPadName(int pad, const juce::String&);
    void setKit(const DrumKit&);

    // Which pad the face shows. View state: kept with the device, so it
    // survives the rack rebuilding its faces, and never an undo step.
    int selectedPad() const;
    void setSelectedPad(int);

    // Strikes a pad from the face. Any thread.
    void previewPad(int pad);
    std::uint32_t padStrikes(int pad) const;
    // Lets go of samples nothing plays any more. The face calls it as it
    // animates, and every edit does too.
    void collectSamples();

    // One strike of a pad, as the lows and highs of `columns` slices of it,
    // played by a voice of its own from the pad's controls as they stand, and
    // how long it lasted. Message thread; the last picture is kept while
    // nothing it was drawn from changes, since a dragged knob asks every frame.
    struct Picture
    {
        std::vector<float> lows, highs;
        double seconds = 0.0;
        float peak = 0.0f;
    };
    const Picture& padPicture(int pad, int columns);

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
    void allNotesOff() override;
    void loadData(const juce::ValueTree&) override;
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;

    juce::ValueTree padsTree() const;
    // Makes the pads tree, through the undo manager, when there is none.
    juce::ValueTree padState(int pad);
    bool concernsPads(const juce::ValueTree&) const;
    // `asSet` reads where each control was set, rather than where a lane has
    // it now.
    DrumRackEngine::PadSettings controlsOf(int pad, bool asSet) const;
    void writeControls(int pad, const DrumRackEngine::PadSettings&);
    // A pad's sound and controls written, without reading them into the
    // engine: the writer does that once it has finished.
    void writePad(int pad, const std::optional<DrumSound>&);
    void readSettings() noexcept;
    // The pads' state, applied to the engine: files read, synths chosen.
    void syncPads();

    std::array<PadControls, padCount> pads = declarePads();
    DrumRackEngine engine;
    // The controls as the audio thread last read them.
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
    std::array<CachedPicture, padCount> pictures;
    // A strike rendered for a picture, kept so that a dragged knob does not
    // allocate one every frame.
    std::vector<float> pictureLeft, pictureRight;
};
}
