#pragma once

#include "../core/ForgeCore.h"
#include "../core/ForgeMidiMap.h"
#include "../core/ForgeNoteFlash.h"
#include "../core/ForgeTableStore.h"
#include "../core/ForgeTableFiles.h"
#include "../core/ForgeSampleStore.h"
#include "../ui/ForgeTheme.h"
#include "ForgePresetBank.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <bitset>
#include <map>

namespace rhino::forge
{
// The pieces of a saved state that are not parameters, built and read the same
// way whether the state came from Forge itself or from a converted preset.
//
// A table as the state tree carries it: every frame end to end, deflated and
// base64'd, for one oscillator.
juce::ValueTree wavetableStateNode(const float* samples, int frames, const juce::String& name, int oscillator);
// A wavetable file's frames, resampled to Forge's frame size and thinned evenly
// to the most a table holds. Message thread only: it reads a file.
juce::Result readWavetableFrames(const juce::File&, std::vector<float>& samples, int& frames);
// A drawn LFO shape as the state tree carries it.
juce::ValueTree lfoTableStateNode(int lfo, const LfoTable&, const juce::String& name);

// The Timer is what applies a learned controller. A parameter may only be
// written from the message thread, and the MIDI that drives it only exists on
// the audio thread, so the crossing needs something on this side to drain it —
// and it has to be the Processor's own, not the editor's, or a knob would stop
// working the moment the window was closed.
class Processor final : public juce::AudioProcessor,
                        private juce::Timer
{
    // Declared before `state`, and deliberately first in the class. Members are
    // initialised in declaration order, and POSITION's value formatter reads the
    // table's frame count to say which frame it has landed on — so the store has
    // to exist before parameterLayout() runs, which happens while `state` is
    // being constructed.
    WavetableStore tables;
    // Beside the tables and for the same reason: a spectral oscillator's
    // sample is handed to the audio thread exactly as its table is.
    SampleStore samples;

public:
    Processor();
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Rhino Forge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    // Writes the patch as it stands, with this name and whatever else the
    // patch already says about itself.
    juce::Result savePreset(const juce::File&, const juce::String& name);
    // Writes the patch under the info given, which becomes the patch's own.
    juce::Result savePreset(const juce::File&, const PresetInfo&);
    // Opens a preset of any format Forge reads: its own, or a Serum 2 or Vital
    // preset converted on the way in (ForgePresetImport.h). A converted patch
    // keeps a line per thing that did not survive, in presetInfo().warnings.
    juce::Result loadPreset(const juce::File&);
    // Converts a preset of another format into a Forge preset file without
    // touching the patch that is playing: what "copy to the bank" is.
    juce::Result convertPreset(const juce::File& source, const juce::File& destination);
    // Back to every default, the built-in tables and no name: the INIT patch.
    void initialisePatch();
    // Writes a state tree as a preset file, through a temporary file, so a
    // failed write leaves the old file whole.
    static juce::Result writePresetFile(const juce::File&, const juce::ValueTree& state, const PresetInfo&);

    // What the patch says about itself. See PresetInfo.
    PresetInfo presetInfo() const;
    void setPresetInfo(const PresetInfo&);
    // The file the patch was opened from or last saved to, or nothing for a
    // patch that came from a host or was never filed.
    juce::File presetFile() const { return currentPresetFile; }
    // The browser renamed or moved a file or a folder; if the patch came from
    // it, the patch follows, and takes the new name when the file was renamed.
    void presetFileMoved(const juce::File& from, const juce::File& to);
    // Moves whenever the patch is replaced or its info changes, from anywhere —
    // a preset, the host's state, the browser — so the panel can notice on its
    // tick by comparing one number. Message thread.
    int presetRevision() const { return presetChanges; }
    // Favourite presets, by path. A setting of the machine, kept beside the
    // theme: a favourite is the player's, not the preset's, and a Serum or
    // Vital preset can be one without Forge writing into someone else's folder.
    static juce::StringArray readFavouritePresets();
    static void saveFavouritePresets(const juce::StringArray&);

    // The tables the oscillators read, and the frames behind them. The editor
    // draws on these directly; nothing else outside this class should.
    WavetableStore& tableStore() noexcept { return tables; }

    // The samples the spectral oscillators resynthesise.
    SampleStore& sampleStore() noexcept { return samples; }

    // Read a wavetable file into one oscillator's table: an ordinary .wav of
    // single-cycle frames laid end to end. Message thread only — it opens a
    // file and builds a table.
    juce::Result importTable(int oscillator, const juce::File&);

    // NEW: write a blank one-frame table to a file and load it into one
    // oscillator, so there is a file to draw into from the first stroke.
    juce::Result newTable(int oscillator, const juce::File&);
    // SAVE: write one oscillator's table to a file, and name the table after
    // it so the table field reads the file it is now in. Message thread only,
    // like importTable.
    juce::Result saveTable(int oscillator, const juce::File&);

    // Read an audio file into one oscillator's spectral source: any format
    // JUCE can decode, mixed to mono and analysed into a spectrogram. Message
    // thread only — it opens a file and runs an FFT per frame.
    juce::Result importSample(int oscillator, const juce::File&);
    void clearSample(int oscillator);
    juce::AudioProcessorValueTreeState state;
    // The editor's keyboard plays through this, so notes struck on screen reach
    // the voice the same way notes from the host do.
    juce::MidiKeyboardState keyboardState;

    // Shared between panel gestures and incoming MIDI. Controller state is
    // performance state, so opening a second editor or changing a preset does
    // not create a second wheel position.
    void setPitchWheel(int value) { pitchWheel.store(juce::jlimit(0, 16383, value), std::memory_order_relaxed); }
    void setModWheel(int value) { modWheel.store(juce::jlimit(0, 127, value), std::memory_order_relaxed); }
    int pitchWheelValue() const { return pitchWheel.load(std::memory_order_relaxed); }
    int modWheelValue() const { return modWheel.load(std::memory_order_relaxed); }

    // An envelope's live position, published once per block for the editor to
    // draw. The audio thread writes, the message thread reads; nothing else
    // crosses. All four report the loudest voice's copy of themselves, so the
    // panel shows one voice rather than four unrelated readings.
    float envelopeLevel(int env = ampEnv) const { return meter(meterLevel, env); }
    // How far through its sample a spectral oscillator has scanned, for the
    // playhead on its display. Crosses the same way the envelope's level does.
    float scanPosition(int oscillator) const
    {
        return oscillator >= 0 && oscillator < oscillatorCount
            ? meterScan[static_cast<size_t>(oscillator)].load(std::memory_order_relaxed)
            : 0.0f;
    }
    int envelopeStage(int env = ampEnv) const
    {
        return env >= 0 && env < envCount
            ? meterStage[static_cast<size_t>(env)].load(std::memory_order_relaxed) : 0;
    }

    // Where an LFO is in its shape, and what it last put out. The position
    // moves the indicator across its display; the value is published too
    // because a sample and hold's step cannot be worked back out of the
    // position, and a path's Y beside it, and the sample and hold's whole
    // staircase so the display can draw the steps ahead. An LFO that answers
    // the keyboard reports the loudest voice's copy of itself, exactly as
    // ENV 1's display follows that voice.
    float lfoPhase(int lfo) const { return meter(meterLfoPhase, lfo); }
    float lfoValue(int lfo) const { return meter(meterLfoValue, lfo); }
    float lfoValueY(int lfo) const { return meter(meterLfoValueY, lfo); }
    LfoSteps lfoSteps(int lfo) const;
    LfoTable lfoTable(int lfo) const;
    // The route a PATH LFO travels. Kept beside the table rather than in it,
    // so switching the type away and back loses neither.
    LfoPath lfoPath(int lfo) const;
    bool lfoPathIsCustom(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount
            && lfoPathCustom[static_cast<size_t>(lfo)].load(std::memory_order_relaxed);
    }
    void setLfoPath(int lfo, const LfoPath&);
    bool lfoTableIsCustom(int lfo) const
    {
        return lfo >= 0 && lfo < lfoCount
            && lfoCustom[static_cast<size_t>(lfo)].load(std::memory_order_relaxed);
    }
    void setLfoTable(int lfo, const LfoTable&, const juce::String& name = "Custom");
    juce::String lfoTableName(int lfo) const;
    juce::Result saveLfoTable(int lfo, const juce::File&) const;
    juce::Result loadLfoTable(int lfo, const juce::File&);

    // The rate an LFO actually runs at: the rate knob when it is set in Hertz,
    // a division of the host's tempo when it is set in beats.
    float lfoRateHz(int lfo) const;

    // How long one arp step lasts, in seconds. Worked out rather than
    // published, exactly as an LFO's rate is: it depends only on parameters and
    // the tempo, both of which the message thread can read for itself, so the
    // panel and the arp cannot end up quoting different rates. The triplet and
    // dotted switches are applied here, which is the one place they are.
    float arpStepSeconds() const;

    // The arp's slots. The live arp parameters are one of four configurations
    // kept on the state tree, `arpSlot` names which, and this is what moves
    // the parameters when that changes. Message thread only, because it
    // writes parameters. The Processor's own timer calls it, so a slot chosen
    // by a host lane or a learned pad takes effect with no editor open, and
    // the panel calls it the moment a button is pressed so the knobs follow
    // the press rather than the next tick. Public for the same reason
    // drainMidiControl is: a test drives the crossing directly.
    void applyArpSlot();
    // Which slot the live parameters are. Trails `arpSlot` by at most one
    // tick of the timer.
    int liveArpSlotIndex() const { return liveArpSlot; }

    // The notes the voices have been told to play since the last time anyone
    // asked, in order, for the panel to draw falling. Message thread; the
    // audio thread fills it from inside processBlock.
    bool popNoteFlash(NoteFlash& flash) noexcept { return noteFlashes.pop(flash); }
    // Whether the voices are still holding a note: started and not yet
    // stopped. Ask after popping, never before: a note is marked held before
    // its flash is queued, so one popped here is never found already let go.
    bool noteHeld(int note) const noexcept { return heldNotes.isHeld(note); }

    // The tempo as of the last block. A synced delay draws its repeats where
    // they will actually land, which needs the same tempo the engine divides.
    double hostTempo() const { return hostBpm.load(std::memory_order_relaxed); }

    // How far the matrix is moving a destination right now, in that
    // destination's normalised space, so the knob pointed at it can draw where
    // its value actually is while a source plays it. Published the same way.
    float modulationOffset(int destination) const
    {
        return destination > 0 && destination < destinationCount
            ? meterOffsets[static_cast<size_t>(destination)].load(std::memory_order_relaxed)
            : 0.0f;
    }

    // What one of a rack slot's general knobs reads as, in the unit whichever
    // type that slot holds gives it. Public because the panel asks the same
    // question to label the knob's bubble and to decide whether the knob is in
    // use at all. Message thread and audio thread both reach it; everything it
    // touches is an atomic parameter read.
    juce::String fxKnobText(int rack, int slot, int knob, float value) const;

    // Which type a slot holds, and therefore what its controls are.
    const FxTypeInfo& fxSlotType(int rack, int slot) const;

    // The filter's type as it stands, and what its second field reads as in
    // whichever unit that type gives it. The same pair the rack has, for the
    // same reason: one field whose meaning is the type's, so the reading has to
    // be worked out where the type can be read rather than declared with the
    // parameter. Both reach only atomic parameter reads, so either thread may
    // ask.
    float filterTypeValue() const;
    juce::String filterFreqText(float value) const;

    // The colour a module's panel is drawn in, as a PanelColour's index.
    //
    // Not a parameter. Nothing about it is heard, nothing automates it, and a
    // host that found it in the automation list would be offering to draw a
    // curve that sweeps a plate from red to green. It rides on the state
    // tree's own properties instead, which is what carries it into a preset
    // and into a project without it becoming something a DAW can sequence.
    int panelColour(const juce::String& moduleId) const;
    void setPanelColour(const juce::String& moduleId, int choice);

    // What a macro has been named, or an empty string for one that has not
    // been. Not a parameter, for the same reasons a plate colour is not: it is
    // text, it has no range, and a host offered it in an automation list would
    // be offering to sweep a word. It rides on the state tree's properties, so
    // a preset and a project carry it without a DAW being able to sequence it.
    //
    // The matrix goes on calling this macro by its number whatever it is named:
    // a slot's SOURCE is a choice parameter whose list is fixed when the
    // parameters are built, and text that followed a rename would move under
    // every automation lane already pointed at it.
    juce::String macroName(int macro) const;
    // Trimmed, upper-cased and cut to maxMacroNameLength. Empty clears it.
    void setMacroName(int macro, const juce::String& name);
    // A name is a tag rather than a caption: the strip under a macro's knob
    // holds about a dozen characters, and this is the point past which more
    // text is only text nobody can read on the panel.
    static constexpr int maxMacroNameLength = 24;

    // Binding a controller to the panel. All of this is message thread only:
    // the panel calls it, and the audio thread's half of the arrangement is in
    // ForgeMidiMap.h. Parameters are named by id here rather than by index
    // because an index is a position in a list that moves whenever a control is
    // added, and these bindings outlive the build that made them.
    //
    // Arms a control. The next controller or pad the keyboard moves is bound to
    // it; arming a second control before the first has learned abandons the
    // first, since nobody is waiting on two at once.
    void learnMidi(const juce::String& parameterId);
    void cancelMidiLearn();
    bool isLearningMidi() const { return midiMap.isLearning(); }
    // The id being learned, or empty. The panel draws this control as
    // listening, which is the only feedback there is until the knob moves.
    juce::String midiLearnTarget() const;
    // What drives this control, as the panel prints it — "CC 21", "Note C1" —
    // or empty where nothing does.
    juce::String midiSourceLabel(const juce::String& parameterId) const;
    void forgetMidi(const juce::String& parameterId);
    void clearMidiMap();
    int midiBindingCount() const { return midiMap.boundCount(); }
    // Knobs 1-8 onto the eight macros, on the controller numbers the General
    // MIDI convention leaves free and nearly every controller ships sending.
    // Offered rather than imposed: it is written on a first run, and from the
    // panel's menu, and never over bindings that already exist.
    void applyDefaultMidiMap();
    static constexpr int firstDefaultMacroCc = 21;
    // Applies everything the audio thread has handed over since the last tick,
    // and completes a learn if one is pending. The timer calls this sixty times
    // a second; it is public so a test can drive the crossing directly rather
    // than by spinning a message loop and hoping.
    void drainMidiControl();
    // Where the bindings file is. One file serves every instance on a machine,
    // so a test that bound anything would write over the bindings belonging to
    // whoever ran it — which is what this exists to prevent.
    static void setMidiMapFile(const juce::File&);
    // The machine's own settings — the theme, today — kept beside the MIDI map
    // and for the same reason: they describe the machine, not the sound, so
    // neither a preset nor a project carries them. The override exists for the
    // tests, as the MIDI map's does. See ForgeProcessorSettings.cpp.
    static void setSettingsFile(const juce::File&);
    static juce::File settingsFile();
    static ui::ThemeId readThemeChoice();
    static void saveThemeChoice(ui::ThemeId);
    // Reads the chosen theme the first time it is called in the process and
    // applies it; every later call answers what is applied. createEditor calls
    // it before the Editor exists, which is the one moment that works: the
    // Editor's members copy the colour tokens as they are constructed.
    static ui::ThemeId loadThemeOnce();
    static void forgetLoadedThemeForTests();

private:
    // Audio thread, inside processBlock. Takes the bound messages out of the
    // block and leaves the rest, before the panel's keyboard is merged in.
    void applyMidiMap(juce::MidiBuffer&);
    // Notices one message while a learn is armed, hands a bound one to the
    // message thread, and answers whether the rest of the block should go on
    // seeing it.
    bool consumedByMidiMap(const juce::MidiMessage&);
    void timerCallback() override;
    // A parameter's position in getParameters(), or -1. The bindings hold this
    // because the audio thread cannot compare strings, and it is resolved from
    // the id on the way in and back to the id on the way out.
    int parameterIndexFor(const juce::String& parameterId) const;
    juce::String parameterIdAt(int index) const;
    // Where the bindings are kept, and why they are not in the preset.
    //
    // A binding describes the hardware on the desk, not the sound. Carrying it
    // in a preset would mean a patch from someone else's machine silently
    // repointing your knobs, and carrying it in the plugin state would mean
    // every project remembering a keyboard that may not be plugged in. It goes
    // beside the standalone's own settings instead, so one file serves every
    // instance and every project on this machine.
    static juce::File midiMapFile();
    void loadMidiMap();
    void saveMidiMap() const;
    MidiMap midiMap;
    MidiControlQueue midiQueue;
    // What a control read before a pad was held down, so releasing the pad can
    // put it back. Only continuous controls are in here — a pad on a switch
    // flips it and leaves it flipped.
    std::map<int, float> heldByPad;
    // Not static: POSITION's readout closes over this Processor so it can name
    // the frame it is on in whichever table the oscillator is reading, and a
    // rack knob's closes over it for the same reason.
    juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout();
    Core core;
    std::atomic<int> pitchWheel {8192};
    std::atomic<int> modWheel {0};
    // In front of the voices rather than inside them: it consumes the notes
    // arriving and hands Core the ones the pattern actually plays.
    Arp arp;
    ArpSettings arpSettings() const;
    // Every note handed to Core is handed here too, for the panel.
    NoteFlashQueue noteFlashes;
    // And whether Core is still holding it, for the same panel, which keeps a
    // held note lit for as long as it is down.
    HeldNotes heldNotes;
    // Every start and stop the voices are given, from the keys and from the
    // arp alike, goes through these two, so what the panel is told is exactly
    // what Core is told.
    void playNote(int note, float velocity, const Patch& values);
    void stopNote(int note);
    // Which slot the live arp parameters are. Set from `arpSlot` whenever a
    // state is loaded and moved by applyArpSlot; see ForgeProcessorState.cpp.
    int liveArpSlot = 0;
    int chosenArpSlot() const;
    juce::ValueTree arpSlotNode(int slot, bool create);
    void storeArpSlot(int slot);
    void recallArpSlot(int slot);
    // What the arp was doing last block, so the two transitions that need
    // acting on can be spotted: switched off mid-phrase, which has to let go of
    // the notes its own gate clock was still holding, and the latch released,
    // which has to drop the chord no finger is on.
    bool arpWasEnabled = false;
    bool arpWasLatched = false;
    // The keys that went straight to the voices - the arp off, or THRU on -
    // so their release goes there too, whatever ARP and THRU have been
    // switched to since. Routed by the switches as they stood at the release,
    // a key pressed with the arp off and let go with it on reached only the
    // arp, and its voice sounded until something stole it.
    std::bitset<128> directNotes;
    // The rate prepareToPlay was given, kept rather than read back from
    // AudioProcessor::getSampleRate(). That one is set by the host calling
    // setRateAndBufferSizeDetails, which nothing does when prepareToPlay is
    // called directly — so it reads zero, and an arp dividing by it stepped
    // once per sample and stacked every voice the synth had.
    double preparedSampleRate = 44100.0;
    // Where the host's transport is, for LAUNCH QUANT. An arp started off the
    // grid waits for the next division of the bar, and the only way to know
    // where that is is the position the host reports.
    double hostPpq = 0.0;
    bool hostPlaying = false;
    // Hold the arp's first step until the next LAUNCH QUANT boundary. Worked
    // out here rather than in the arp, which counts in steps and knows nothing
    // about bars.
    void quantiseArpStart(int sampleIndex, const ArpSettings&);
    std::array<std::atomic<float>, envCount> meterLevel {};
    std::array<std::atomic<float>, oscillatorCount> meterScan {};
    std::array<std::atomic<int>, envCount> meterStage {};
    std::array<std::atomic<float>, lfoCount> meterLfoPhase {};
    std::array<std::atomic<float>, lfoCount> meterLfoValue {};
    std::array<std::atomic<float>, lfoCount> meterLfoValueY {};
    std::array<std::array<std::atomic<float>, maxLfoSteps>, lfoCount> meterLfoStep {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPathX {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPathY {};
    std::array<std::atomic<int>, lfoCount> lfoPathCount {};
    std::array<std::atomic<bool>, lfoCount> lfoPathCustom {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPointX {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPointY {};
    // The bend of the segment leaving each point, and where its handle sits
    // along that segment. Stored beside the point rather than in a list of
    // their own so one count covers all four and the audio path reads a bend
    // without a second lookup.
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPointCurve {};
    std::array<std::array<std::atomic<float>, maxLfoPoints>, lfoCount> lfoPointCurveAt {};
    std::array<std::atomic<int>, lfoCount> lfoPointCount {};
    std::array<std::atomic<int>, lfoCount> lfoColumns {};
    std::array<std::atomic<int>, lfoCount> lfoRows {};
    std::array<std::atomic<bool>, lfoCount> lfoCustom {};
    mutable juce::CriticalSection lfoNameLock;
    std::array<juce::String, lfoCount> lfoNames {};
    // One reader for every bank of published meters, whatever it is a bank of:
    // an index outside the bank reads as nothing rather than off the end.
    template <size_t count>
    static float meter(const std::array<std::atomic<float>, count>& from, int index)
    {
        return index >= 0 && index < static_cast<int>(count)
            ? from[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 0.0f;
    }
    // The host's tempo as of the last block, for a synced LFO to divide.
    std::atomic<double> hostBpm {0.0};
    std::array<std::atomic<float>, destinationCount> meterOffsets {};
    // Every parameter a block reads, paired once, by the constructor, with the
    // field it fills. A parameter's id is a juce::String, so building one is a
    // heap allocation and finding it a map lookup; a block did several hundred
    // of each before this. Now a block copies values across these pointers and
    // spells no id at all. Audio thread only, past the constructor.
    struct ParameterBinding
    {
        float* field;
        std::atomic<float>* value;
    };
    void bindParameters();
    Patch blockPatch;
    Modulation blockModulation;
    std::vector<ParameterBinding> patchBindings, modulationBindings;
    // The three an LFO's rate is worked out from, held for lfoRateHz, which
    // both threads call and which therefore must not build ids either.
    struct LfoRateParameters
    {
        std::atomic<float>* unit = nullptr;
        std::atomic<float>* hertz = nullptr;
        std::atomic<float>* division = nullptr;
    };
    std::array<LfoRateParameters, lfoCount> lfoRateParameters {};
    // The messages a block keeps once the bound ones are taken out. Sized in
    // prepareToPlay and reused, so filtering allocates nothing and the host's
    // own buffer keeps its storage.
    juce::MidiBuffer midiKept;
    const Patch& patch();
    const Modulation& modulation();
    juce::ValueTree migrated(const juce::ValueTree& savedState) const;
    // Everything a preset or a converted preset does once its tree is in hand:
    // the tables, samples and LFO shapes first, then the parameters, then the
    // arp's slot. One path, so a converted patch is applied exactly as a saved
    // one is.
    void applySavedState(const juce::ValueTree& savedState);
    juce::File currentPresetFile;
    int presetChanges = 0;
    // A table is not a parameter, so it travels beside the parameter state
    // rather than inside it: written as a child node when an oscillator's table
    // has been drawn on or loaded over, and read back before the parameters are
    // applied. A saved state that names no table resets that oscillator to the
    // built-in ten, for the same reason an omitted parameter returns to its
    // default rather than keeping the previous patch's value.
    void appendTables(juce::ValueTree& tree) const;
    void applyTables(const juce::ValueTree& tree);
    // A spectral sample rides on the state the way a table does, so a patch
    // carrying one stays a patch when it moves between machines.
    void appendSamples(juce::ValueTree& tree) const;
    void applySamples(const juce::ValueTree& tree);
    void appendLfoTables(juce::ValueTree& tree) const;
    void applyLfoTables(const juce::ValueTree& tree);
};
}
