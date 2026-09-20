#pragma once

#include "ForgeProcessor.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeTablePanel.h"
#include "../ui/ForgeVisuals.h"
#include "../ui/ForgeFxDisplay.h"
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
        // A slot's mode field. Like the plate, it drives the parameter its
        // slider is attached to and that slider is never shown.
        std::unique_ptr<ui::FxSelector> selector;
        // The sub's waveform picker. Like the plate and the mode field, it
        // drives the parameter its slider is attached to and that slider is
        // never shown.
        std::unique_ptr<ui::WaveGrid> waves;
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
    //
    // Five, so the first letter key is middle C — MIDI 60, the C3 a DAW means
    // when it says C3. It was six, which started the reach an octave above
    // that, so typing a C and playing a DAW's C3 gave two different notes and
    // the synth read an octave sharp.
    int computerKeyOctave = 5;
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
    ui::PresetButton loadPreset {"LOAD"}, savePreset {"SAVE"};
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

    // The module plates, which do not change from one frame to the next.
    // Painted once into an image and blitted thereafter: the panel repaints
    // whole at 24Hz, and drawing this layer every time measured at fifty points
    // of one core on its own. Everything that does move -- the displays, the
    // header readings, the rack -- is still drawn live over it.
    void paintPlates(juce::Graphics&);
    // Everything the cached layer depends on, in one string. When this changes
    // the image is thrown away and drawn again; when it does not, nothing in
    // the layer can have moved.
    juce::String chromeKey(float scale) const;
    juce::Image chrome;
    juce::String chromeState;
    // The chassis alone, cached apart from the plates that sit on it.
    //
    // It is the more expensive half of the layer -- measured at 30ms of the
    // 50ms a full rebuild costs -- and it depends on the panel's size and
    // nothing else. So a tab switch, a module switched off or an oscillator
    // recoloured throws away the plates and blits this back underneath them
    // rather than drawing the whole chassis again for a change it cannot see.
    juce::Image chassisLayer;
    juce::String chassisState;
    // When it was last drawn, and how stale it is allowed to get while the
    // window is being dragged. In between rebuilds the layer from the previous
    // size is blitted stretched onto the new one -- the plates over it are
    // still drawn at the true size, so everything that has to line up with a
    // control still does, and what stretches is the frame around the outside.
    juce::uint32 chassisDrawnMs = 0;
    static constexpr juce::uint32 chassisHoldMs = 90;
    // The layer the tab before this one was showing, kept whole.
    //
    // Flipping back and forth between two tabs is most of what tab switching
    // is, and the panel the eye came from is by then exactly the panel it is
    // going back to: nothing but the page has moved. Holding the one it left
    // turns the return trip into a swap of two pointers. A second image is the
    // whole price, and it buys the case the hand actually performs.
    juce::Image previousChrome;
    juce::String previousChromeState;
    // Just the size and the raster scale, which is the whole of what the
    // chassis is drawn from.
    juce::String chassisKey(float scale) const;
    // The raster scale the cached layers are drawn at: the display's own, or
    // half of it while the window is being dragged. The geometry is worked out
    // at the panel's real size either way, so nothing shifts against the live
    // controls drawn over it -- only the sharpness of the metal changes, and
    // only while an edge is under the pointer.
    float chromeScale(float physical) const;
    // True while a hand is on a window edge.
    //
    // Counting size changes does not answer this: setResizeLimits, the
    // constructor and a host restoring a stored size each deliver one, and a
    // snapshot rendered at a given size would come back soft. What a drag
    // actually looks like is a size change landing on a layer that was rebuilt
    // moments ago and is about to be rebuilt again -- so that, and not the
    // count, is what this asks.
    bool sizeIsMoving() const;
    juce::Point<int> lastSize;
    juce::uint32 lastResizeMs = 0;
    // When the cached layer was last built, whatever caused it.
    juce::uint32 lastRebuildMs = 0;
    // Long enough to cover the gap between two frames of a slow drag, short
    // enough that letting go and reading the panel does not wait on it.
    static constexpr juce::uint32 resizeSettleMs = 180;
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
    // Whether a rack knob has anything to do under the modes its slot is set
    // to. The rule is a fact about the effect and lives beside the types; this
    // is the part that works out which slot and which knob is being asked
    // about. Everything that is not a rack knob answers true.
    bool fxKnobLive(const Control&) const;
    // The list of types, opened by clicking a slot's plate. This is how a slot
    // is filled and emptied.
    void showFxTypeMenu(Control&, juce::Rectangle<int> target = {});
    // Sets one slot's knobs and its wet/dry to what its type opens on. Called
    // only when a type is chosen on the panel.
    void initialiseFxSlot(int rack, int slot);
    // One slot's settings, read back out of the parameters so a display can be
    // drawn from the same values the engine is rendering from.
    FxSlot fxSlotOf(int rack, int slot) const;
    // Sets one mode field to one of its choices, spreading the choice across the
    // 0..1 the parameter behind it actually holds.
    void setFxMode(const juce::String& id, int count, int choice);
    // The list a mode field with too many choices to show at once opens.
    void showFxModeMenu(Control&);
    // An oscillator's warp fields wear the rack's mode component and are driven
    // differently behind it: a warp mode is a fixed list of twenty-six, so the
    // parameter is a real choice and its value is the index, where a rack
    // slot's mode is a plain 0..1 spread across whatever its type offers.
    static bool isWarpControl(const juce::String& id);
    void setWarpMode(const juce::String& id, int choice);
    // What each oscillator's two fields are showing, refreshed when one has
    // moved — from the panel, from a preset or from a host.
    void refreshWarpFields();
    // The warp list, grouped by category the way the manual groups it.
    void showWarpMenu(Control&);
    // The two stages of one oscillator exchanged, which is the action at the
    // foot of that list.
    void swapWarpModes(const juce::String& id);
    // One oscillator's two warp stages, resolved as the engine resolves them,
    // so the display draws the warp the voice is rendering.
    std::array<WarpStage, warpSlots> warpStagesOf(const char* prefix) const;
    // The shelves the slots sit on, drawn behind their controls.
    void paintFxShelves(juce::Graphics&, juce::Rectangle<int> area, const ui::Module&);
    // The compact signal-flow overview at the left of the rack. It is a
    // painted view of the same slot parameters, not another component
    // tree or a cached copy of the rack.
    void paintFxList(juce::Graphics&, juce::Rectangle<int> area, const ui::Module&);
    // The rack's box alone, for a knob that is being turned inside it.
    void repaintFxDisplays();
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
    Control* fxTypeControl(int rack, int slot);
    void toggleFxExpanded();
    void toggleFxList();
    void addFxSlot();
    void setFxSlotBypassed(int rack, int slot, bool bypassed);
    void removeFxSlot(int rack, int slot);
    void moveFxSlot(int rack, int from, int to);
    bool moduleShown(const ui::Module&) const;
    juce::Rectangle<int> moduleAreaFor(const ui::Module&) const;
    juce::Rectangle<int> fxRackAreaFor(juce::Rectangle<int> moduleArea, int rack, int slot) const;
    int fxActiveSlotCount(int rack) const;
    int fxDisplayRow(int rack, int slot) const;
    int fxSlotAtDisplayRow(int rack, int row) const;
    int fxFirstVisibleSlot() const;
    void setFxFirstVisibleSlot(int slot);
    void clampFxScroll();

    // View state only. Expansion hides the lower synth row and lets the rack
    // use it; folding the list leaves a mark-only rail. Neither belongs in a
    // preset or in host automation.
    bool fxExpanded = false;
    bool fxListOpen = true;
    std::array<int, rackCount> fxFirstVisibleSlots {};
    float fxWheel = 0.0f;
    int fxSelectedSlot = 0;
    int fxDragSlot = -1;
    int fxDropSlot = -1;
    juce::Point<int> fxDragStart;

    void buildBankButtons();
    void showBank(ModuleUi&, int bank);
    void applyEnableStates();

    // The colour a module is drawn in. Everything that draws one asks this
    // rather than ui::accentFor, because an oscillator's colour is a choice
    // the module descriptor cannot answer for itself.
    juce::Colour accentOf(const ui::Module&) const;
    // Opens the colour menu on an oscillator's LED, and pushes the choice out
    // to every control in that module afterwards.
    void showPanelColourMenu(const ui::Module&);
    void applyPanelColours();
    // What the controls on screen are currently coloured with, so a colour
    // changed from outside this editor is noticed on the next tick.
    juce::String panelColoursShown;
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
