#pragma once

#include "ForgeProcessor.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTablePanel.h"
#include "../ui/ForgeVisuals.h"
#include <memory>
#include <vector>

namespace rhino::forge
{
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    void paint(juce::Graphics&) override;
    // The piano is a child, so the reach marking has to go on after it rather
    // than in paint().
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    // A control is a knob or a stepper (a slider with a label above) or a chip
    // (a button that carries its own label). Only the members its style needs
    // are made visible.
    struct Control
    {
        ui::Style style = ui::Style::knob;
        juce::String id;
        const char* disabledBy = nullptr;
        const char* enabledBy = nullptr;
        int row = 0, index = 0;
        // Which bank of its module this control belongs to. Zero in a module
        // that declares none, which is all of them but the envelopes and the
        // LFOs.
        int bank = 0;
        // The slot whose depth this knob's ring sets, or -1 when the ring is
        // not draggable: nothing is pointed here, or more than one thing is and
        // the ring is a sum with no single slot behind it.
        int ringSlot = -1;
        juce::Label label;
        ui::ModKnob slider;
        std::unique_ptr<ui::ToggleChip> chip;
        // A rack slot's name plate. It drives the same parameter its slider is
        // attached to; the slider itself is never shown, and is kept only
        // because the attachment is what carries the value to and from the
        // host.
        std::unique_ptr<ui::FxPlate> plate;
        std::unique_ptr<ui::RockerSwitch> rocker;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    // One of these per declared module. Controls are held by pointer because
    // Component addresses must not move once they are children.
    struct ModuleUi
    {
        const ui::Module* descriptor = nullptr;
        std::vector<std::unique_ptr<Control>> controls;
        std::unique_ptr<ui::EnableLed> enable;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
        // A module whose controls are declared in banks shows one bank at a
        // time, and carries a numbered button per bank in its header. The other
        // banks stay built and stay attached, so an envelope or an LFO that is
        // not on screen is still driven by the host and still runs.
        std::vector<std::unique_ptr<ui::BankCard>> bankButtons;
        int bank = 0;
        bool on() const { return enable == nullptr || enable->getToggleState(); }
    };

    Processor& processor;
    ui::LookAndFeel lookAndFeel;
    std::vector<ModuleUi> moduleUis;
    // Which tab is showing. Only the modules that declare a page follow it;
    // everything else stays on screen whichever tab is chosen.
    ui::Page page = ui::Page::oscillators;
    // How much time the envelope display spans, as an index into
    // ui::envelopeZooms. A view setting, not a parameter: it changes nothing
    // that is heard, so it belongs neither in the automation state nor in a
    // preset, and it opens on the default for the same reason `page` does.
    //
    // One per envelope, because the window is how long a shape is read against
    // and four envelopes are not the same length: a percussive ENV 3 under the
    // three-second window ENV 1 wants is a sliver, and switching to it would
    // hand you a display you had to re-zoom every time.
    std::array<int, envCount> envelopeZoom = ui::envelopeZoomDefaults();
    // Wheel travel not yet spent on a zoom step. One notch of a mouse wheel
    // measures about 0.2 here, so the threshold sits below that and a notch is
    // reliably a step — at 0.25 the first notch of a turn did nothing, which
    // reads as the control being dead rather than as it being careful. A
    // trackpad sends a drizzle of much smaller deltas instead, and those still
    // have to add up before anything moves.
    float envelopeWheel = 0.0f;
    static constexpr float wheelPerZoomStep = 0.15f;
    std::vector<std::unique_ptr<ui::PageTab>> tabs;
    // Drag handles live outside the modules: a macro's sits beside its knob, a
    // modulator's in its module header.
    std::vector<std::unique_ptr<ui::SourceHandle>> handles;
    ui::SourceHandle* draggingHandle = nullptr;
    juce::Point<int> dragPosition;
    juce::MidiKeyboardComponent keyboard;
    // Which octave the computer keys play. The keyboard's own mapping is 17
    // notes wide starting at the C of this octave; z and x walk it, and the
    // keys it can reach are washed in on the piano so you can see where you
    // are without having to play a note to find out.
    int computerKeyOctave = 6;
    // Every control has carried a tooltip since M2, but without one of these
    // nothing ever showed them. Parented to the editor rather than given a
    // desktop window of its own, which is what a plugin in a host needs.
    // Declared last of the components so it is added on top of them.
    juce::TooltipWindow tooltips {this, 700};
    // What a knob says while it is being turned. One bubble serves the whole
    // panel: only one knob is ever in hand. Declared after the tooltip window
    // so a value the hand is asking for is drawn over a description it is not.
    ui::ValueBubble valueBubble;
    // The knob the bubble is showing, so a value arriving from the host or the
    // matrix does not redirect a bubble the hand opened on something else.
    Control* bubbleControl = nullptr;
    // True between a drag starting and ending. While it is false the bubble is
    // living out the tail below, which is what lets a wheel notch — a change
    // with no drag around it — put a reading on screen at all.
    bool bubbleHeld = false;
    juce::uint32 bubbleUntil = 0;
    // How long a reading outlives the gesture that set it.
    static constexpr juce::uint32 bubbleTailMs = 700;
    // The whole of the TABLE tab. It owns its own canvas, strip and buttons
    // rather than declaring parameter controls, because nothing on it is a
    // parameter.
    std::unique_ptr<ui::TablePanel> tablePanel;
    // The revision each oscillator's table was last seen at, so a table changed
    // by a preset load or by the host is noticed rather than only one changed
    // by the panel itself.
    std::array<int, oscillatorCount> tableRevisions {-1, -1};
    juce::TextButton loadPreset {"LOAD"}, savePreset {"SAVE"};
    juce::Label presetName;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void buildModules();
    void buildTablePanel();
    // POSITION steps through frames, and how many there are depends on the
    // table. Re-applied whenever a table changes.
    void applyTableCounts();
    void buildTabs();
    void showPage(ui::Page);
    void applyPage();
    void paintTable(juce::Graphics&, juce::Rectangle<int> area, const ui::Module&);
    bool slotIsLive(int slot) const;
    juce::String lfoHeaderDetail() const;
    // What the envelope module's header says: the stage the envelope showing is
    // in, or, at rest, what that envelope is there for.
    juce::String envHeaderDetail() const;
    // Which LFO the panel is showing: the LFO module's chosen bank, and so also
    // the one its display draws, its header reports and its handle drags.
    int shownLfo() const;
    // Which envelope the panel is showing: the ENV module's chosen bank, and so
    // also the one its display draws, its knobs drive and its handle drags.
    int shownEnv() const;
    // A rack slot's six general knobs and its two mode fields say what they are
    // only once a type is in the slot, so their labels, their tooltips and
    // whether they are on screen at all are settled here rather than declared.
    // Called when a type changes, when the rack shown changes, and on the way
    // in — including for the banks that are not showing, so a slot is right the
    // moment it is revealed rather than a repaint later.
    void refreshFxSlots();
    // Whether a rack control is one the type in its slot actually has. A reverb
    // has no fourth knob at all, so the knob declared there is taken off the
    // panel rather than greyed: greying says "not just now", and this is "not
    // ever, while that type is in this slot". Everything else answers true.
    bool fxControlUsed(const Control&) const;
    // The list of types, opened by clicking a slot's plate. This is how a slot
    // is filled and emptied.
    void showFxTypeMenu(Control&);
    // Sets one slot's knobs and its wet/dry to what its type opens on. Called
    // only when a type is chosen on the panel.
    void initialiseFxSlot(int rack, int slot);
    // The shelves the slots sit on, drawn behind their controls.
    void paintFxShelves(juce::Graphics&, juce::Rectangle<int> area, const ui::Module&);
    // The type each slot last showed, so a type arriving from a preset or from
    // host automation re-labels its slot rather than only one chosen by hand.
    std::array<int, rackCount * fxSlotCount> fxTypesShown {};
    // Which rack the FX module is showing: its chosen bank.
    int shownRack() const;
    // What the rack module's header says: the rack being shown, and what is in
    // it.
    juce::String fxHeaderDetail() const;
    // The slot and rack a control belongs to, or false when it is not one of
    // the rack's. Read back from the id, because that is the one place the
    // three numbers are written down.
    static bool fxControlAt(const juce::String& id, int& rack, int& slot);

    void buildBankButtons();
    void showBank(ModuleUi&, int bank);
    void applyEnableStates();
    float value(const juce::String& id) const;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    // The envelope display, for the clicks on the zoom strip down its right and
    // the wheel over the plot beside it. Null or empty when the envelope is not
    // on the tab being shown.
    const ui::Module* envelopeModule() const;
    juce::Rectangle<int> envelopeDisplayBounds() const;
    void setEnvelopeZoom(int zoom);
    bool keyStateChanged(bool isKeyDown) override;
    bool keyPressed(const juce::KeyPress&) override;
    void shiftComputerKeyOctave(int delta);
    void buildHandles();
    void showModulationMenu(const juce::String& parameterId);
    void assignModulation(int source, int destination);
    void setSlotDepth(int slot, float depth);
    void clearSlot(int slot);
    void refreshModulationRings();
    Control* controlAt(juce::Point<int> panelPosition);
    // Puts the bubble beside this knob, showing what it now reads.
    void showValueBubble(Control&);
    void fadeValueBubble();
    void timerCallback() override;
    void choosePresetToLoad();
    void choosePresetToSave();
    void showPresetResult(const juce::Result&, const juce::File&);
};
}
