#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <vector>

// The theme: every chrome colour the panel is drawn with, named once, and the
// two palettes those names can take. Forge is the default and paints exactly
// as it did before there were themes; Merli is the fur-and-brass second look.
//
// The theme is a setting of the machine, read once when Forge starts (see
// Processor::loadThemeOnce) and taking effect on the next start. That is what
// lets the tokens be plain globals assigned once, before any Editor exists:
// nothing re-reads them, no component is recoloured live, and the cached
// chassis and plate layers need no theme in their keys.
namespace rhino::forge::ui
{
enum class ThemeId { forge, merli };
inline constexpr int themeCount = 2;

// One palette. The members are the tokens, in the order of the token table in
// the brief: the sixteen the panel always had, the ones the literals became,
// and the ones only Merli draws.
struct Theme
{
    ThemeId id;
    const char* name;

    juce::Colour electricBlue, signalViolet, text, mutedText, labelText, legendText;
    juce::Colour chassis, chassisLit, plateFaceTop, plateFaceFoot, plateEdgeLit, plateEdgeDark;
    juce::Colour panel, panelRaised, line, markRed;

    juce::Colour fieldFloor, chipFloor, displayFloor, tubeFloor, wellFloor, canvasFloor, rackFloor, shadeFloor;
    juce::Colour lampOff, faderThumbRim;
    juce::Colour knobCollar, knobRimLit, knobRimDark, knobDomeLit, knobDomeMid, knobDomeDark;
    juce::Colour tabOnTop, tabOffTop, tabFoot;
    juce::Colour railLit, railDark, railLine, badgeTop, badgeFoot, plateFaceMid, footerField;
    juce::Colour keyWhite, keyBlack;
    juce::Colour switchMetalLit, switchMetalDark, switchOffLit, switchOffDark, switchShadeTop, switchSlot;
    juce::Colour identityBar, identityBarLit, identityTitle, identityRule, identityHatch, presetOutline;
    juce::Colour tubeVignette, macroCellFloor, switchBezel, buttonWellDown, buttonWellFloor, guideFieldFloor;
    juce::Colour shelfFloor, plateWellFloor, chassisWellFloor, tabWellFloor, sideRailLit, sideRailDark;
    juce::Colour keyboardWellFloor, keyLabelOnBlack, keyLabelOnWhite;
    juce::Colour oscRed, oscOrange, oscGreen;

    juce::Colour valueText, knobPointer, knobArcDot, pawTint, pawHighlight;
    juce::Colour furRimLit, furLip, backdrop;
    juce::Colour meterFace, meterInk, meterRed;
};

// Forge: gunmetal lit from above. Every value here is the literal the panel
// was drawn with before the tokens moved, byte for byte, because the snapshot
// baselines are hashed against them.
inline const Theme& forgeTheme()
{
    static const Theme theme {
        .id = ThemeId::forge,
        .name = "Forge",

        .electricBlue  = juce::Colour(0xff45a8ff),
        .signalViolet  = juce::Colour(0xff9a6cff),
        .text          = juce::Colour(0xffe8eaff),
        .mutedText     = juce::Colour(0xff8f95ad),
        .labelText     = juce::Colour(0xffc0d5eb),
        .legendText    = juce::Colour(0xff6c7386),
        .chassis       = juce::Colour(0xff020304),
        .chassisLit    = juce::Colour(0xff05060a),
        .plateFaceTop  = juce::Colour(0xff191b1e),
        .plateFaceFoot = juce::Colour(0xff0b0d10),
        .plateEdgeLit  = juce::Colour(0xff868d9c),
        .plateEdgeDark = juce::Colour(0xff020407),
        .panel         = juce::Colour(0xff111522),
        .panelRaised   = juce::Colour(0xff191d2d),
        .line          = juce::Colour(0xff34394e),
        .markRed       = juce::Colour(0xffdf503b),

        .fieldFloor        = juce::Colour(0xff0b0e18),
        .chipFloor         = juce::Colour(0xff0d1120),
        .displayFloor      = juce::Colour(0xff0a0d16),
        .tubeFloor         = juce::Colour(0xff01030c),
        .wellFloor         = juce::Colour(0xff05070e),
        .canvasFloor       = juce::Colour(0xff080b14),
        .rackFloor         = juce::Colour(0xff090c15),
        .shadeFloor        = juce::Colour(0xff070a12),
        .lampOff           = juce::Colour(0xff232838),
        .faderThumbRim     = juce::Colour(0xff39405c),
        .knobCollar        = juce::Colour(0xff090b11),
        .knobRimLit        = juce::Colour(0xff858da1),
        .knobRimDark       = juce::Colour(0xff04060c),
        .knobDomeLit       = juce::Colour(0xff32363f),
        .knobDomeMid       = juce::Colour(0xff171a21),
        .knobDomeDark      = juce::Colour(0xff0a0c10),
        .tabOnTop          = juce::Colour(0xff163554),
        .tabOffTop         = juce::Colour(0xff0c131b),
        .tabFoot           = juce::Colour(0xff04080d),
        .railLit           = juce::Colour(0xff51575f),
        .railDark          = juce::Colour(0xff16191c),
        .railLine          = juce::Colour(0xff080a0c),
        .badgeTop          = juce::Colour(0xff22262c),
        .badgeFoot         = juce::Colour(0xff111417),
        .plateFaceMid      = juce::Colour(0xff121518),
        .footerField       = juce::Colour(0xff151925),
        .keyWhite          = juce::Colour(0xffd8dcea),
        .keyBlack          = juce::Colour(0xff10131f),
        .switchMetalLit    = juce::Colour(0xffb4bccf),
        .switchMetalDark   = juce::Colour(0xff4e5568),
        .switchOffLit      = juce::Colour(0xff98a0b4),
        .switchOffDark     = juce::Colour(0xff6a7186),
        .switchShadeTop    = juce::Colour(0xff161a26),
        .switchSlot        = juce::Colour(0xff2b3044),
        .identityBar       = juce::Colour(0xff444b5a),
        .identityBarLit    = juce::Colour(0xff717784),
        .identityTitle     = juce::Colour(0xff9ba5bc),
        .identityRule      = juce::Colour(0xff68758a),
        .identityHatch     = juce::Colour(0xff545d70),
        .presetOutline     = juce::Colour(0xff657389),
        .tubeVignette      = juce::Colour(0xff000208),
        .macroCellFloor    = juce::Colour(0xff0a0d18),
        .switchBezel       = juce::Colour(0xff0a0c14),
        .buttonWellDown    = juce::Colour(0xff151c26),
        .buttonWellFloor   = juce::Colour(0xff070b10),
        .guideFieldFloor   = juce::Colour(0xff090d14),
        .shelfFloor        = juce::Colour(0xff0e1220),
        .plateWellFloor    = juce::Colour(0xff05070c),
        .chassisWellFloor  = juce::Colour(0xff050709),
        .tabWellFloor      = juce::Colour(0xff030609),
        .sideRailLit       = juce::Colour(0xff394047),
        .sideRailDark      = juce::Colour(0xff13171d),
        .keyboardWellFloor = juce::Colour(0xff010203),
        .keyLabelOnBlack   = juce::Colour(0xff8c92a0),
        .keyLabelOnWhite   = juce::Colour(0xff555c69),
        .oscRed            = juce::Colour(0xffff5c52),
        .oscOrange         = juce::Colour(0xffffa235),
        .oscGreen          = juce::Colour(0xff3ddc84),

        // Forge never reads these; they take the nearest Forge colour so the
        // table is total and a Merli-only site still paints something sane.
        .valueText    = juce::Colour(0xffe8eaff),
        .knobPointer  = juce::Colour(0xffe8eaff),
        .knobArcDot   = juce::Colour(0xff45a8ff),
        .pawTint      = juce::Colour(0xff8f95ad),
        .pawHighlight = juce::Colour(0xffe8eaff),
        .furRimLit    = juce::Colour(0xffD4A173),
        .furLip       = juce::Colour(0xff794628),
        .backdrop     = juce::Colour(0xff020304),
        // A studio meter's face: cream card, black scale, red past -10.
        .meterFace    = juce::Colour(0xffefe6cf),
        .meterInk     = juce::Colour(0xff2a2620),
        .meterRed     = juce::Colour(0xffb3261e),
    };
    return theme;
}

// Merli: tortoiseshell fur, dark matte plates with a warm edge, orange and
// purple accents, bronze knobs. Measured off the designer's mockup.
inline const Theme& merliTheme()
{
    static const Theme theme {
        .id = ThemeId::merli,
        .name = "Merli",

        .electricBlue  = juce::Colour(0xffF2AE68),
        .signalViolet  = juce::Colour(0xffA47CFF),
        .text          = juce::Colour(0xffEBE2DA),
        .mutedText     = juce::Colour(0xff9FA19A),
        .labelText     = juce::Colour(0xffDCD5CC),
        .legendText    = juce::Colour(0xff8A8C8F),
        .chassis       = juce::Colour(0xff120F0C),
        .chassisLit    = juce::Colour(0xff1A1713),
        .plateFaceTop  = juce::Colour(0xff141616),
        .plateFaceFoot = juce::Colour(0xff0B0C0C),
        .plateEdgeLit  = juce::Colour(0xff6B6358),
        .plateEdgeDark = juce::Colour(0xff030201),
        .panel         = juce::Colour(0xff0E1012),
        .panelRaised   = juce::Colour(0xff1C1A18),
        .line          = juce::Colour(0xff333533),
        .markRed       = juce::Colour(0xffE78270),

        .fieldFloor        = juce::Colour(0xff0F1111),
        .chipFloor         = juce::Colour(0xff141515),
        .displayFloor      = juce::Colour(0xff0D1012),
        .tubeFloor         = juce::Colour(0xff050807),
        .wellFloor         = juce::Colour(0xff0A0B0A),
        .canvasFloor       = juce::Colour(0xff0C0D0D),
        .rackFloor         = juce::Colour(0xff0E0F0F),
        .shadeFloor        = juce::Colour(0xff0A0A0A),
        .lampOff           = juce::Colour(0xff2A2522),
        .faderThumbRim     = juce::Colour(0xff3A3B3A),
        .knobCollar        = juce::Colour(0xff0B0907),
        .knobRimLit        = juce::Colour(0xffA97A57),
        .knobRimDark       = juce::Colour(0xff352316),
        .knobDomeLit       = juce::Colour(0xff464644),
        .knobDomeMid       = juce::Colour(0xff232221),
        .knobDomeDark      = juce::Colour(0xff0F0F10),
        .tabOnTop          = juce::Colour(0xff1C1E20),
        .tabOffTop         = juce::Colour(0xff131416),
        .tabFoot           = juce::Colour(0xff101314),
        .railLit           = juce::Colour(0xff3A2A1F),
        .railDark          = juce::Colour(0xff1A1713),
        .railLine          = juce::Colour(0xff050403),
        .badgeTop          = juce::Colour(0xff161716),
        .badgeFoot         = juce::Colour(0xff101111),
        .plateFaceMid      = juce::Colour(0xff101111),
        .footerField       = juce::Colour(0xff161716),
        .keyWhite          = juce::Colour(0xffEBE1D7),
        .keyBlack          = juce::Colour(0xff262829),
        .switchMetalLit    = juce::Colour(0xffC9BEAE),
        .switchMetalDark   = juce::Colour(0xff7F5E45),
        .switchOffLit      = juce::Colour(0xff8C8F93),
        .switchOffDark     = juce::Colour(0xff5A544F),
        .switchShadeTop    = juce::Colour(0xff1C1A18),
        .switchSlot        = juce::Colour(0xff3A2A1F),
        .identityBar       = juce::Colour(0xff3F454B),
        .identityBarLit    = juce::Colour(0xff8C8F93),
        .identityTitle     = juce::Colour(0xffC8966B),
        .identityRule      = juce::Colour(0xff6B4B2F),
        .identityHatch     = juce::Colour(0xff3A2A1F),
        .presetOutline     = juce::Colour(0xff3F3E3B),
        .tubeVignette      = juce::Colour(0xff000000),
        .macroCellFloor    = juce::Colour(0xff0F1112),
        .switchBezel       = juce::Colour(0xff0B0907),
        .buttonWellDown    = juce::Colour(0xff1C1A18),
        .buttonWellFloor   = juce::Colour(0xff0F1010),
        .guideFieldFloor   = juce::Colour(0xff0F1111),
        .shelfFloor        = juce::Colour(0xff121313),
        .plateWellFloor    = juce::Colour(0xff0A0B0A),
        .chassisWellFloor  = juce::Colour(0xff060403),
        .tabWellFloor      = juce::Colour(0xff080908),
        .sideRailLit       = juce::Colour(0xff3A2A1F),
        .sideRailDark      = juce::Colour(0xff1A1713),
        .keyboardWellFloor = juce::Colour(0xff0A0806),
        .keyLabelOnBlack   = juce::Colour(0xff8C8F93),
        .keyLabelOnWhite   = juce::Colour(0xff5A544F),
        .oscRed            = juce::Colour(0xffE78270),
        .oscOrange         = juce::Colour(0xffFFB057),
        .oscGreen          = juce::Colour(0xff9ED08A),

        .valueText    = juce::Colour(0xffE0BC82),
        .knobPointer  = juce::Colour(0xffFDF9E8),
        .knobArcDot   = juce::Colour(0xffAD7542),
        .pawTint      = juce::Colour(0xffCEB88A),
        .pawHighlight = juce::Colour(0xffFBF2CB),
        .furRimLit    = juce::Colour(0xffD4A173),
        .furLip       = juce::Colour(0xff794628),
        .backdrop     = juce::Colour(0xff0A0B0C),
        .meterFace    = juce::Colour(0xffEFE2C4),
        .meterInk     = juce::Colour(0xff2A2118),
        .meterRed     = juce::Colour(0xffB3402E),
    };
    return theme;
}

inline const Theme& themeFor(ThemeId id)
{
    return id == ThemeId::merli ? merliTheme() : forgeTheme();
}

// --- The tokens -------------------------------------------------------------
//
// Plain reassignable globals, each starting as Forge's value, so a binary that
// never calls applyTheme paints exactly as it always did. They are read at
// draw time and copied by components as they are constructed, which is why the
// theme has to be applied before the first Editor exists and never after.
//
// electricBlue and signalViolet are the names of the Forge colours, and they
// stay the names in every theme: they mean the signal accent and the modulator
// accent wherever they are read, and a theme may paint them otherwise — in
// Merli, electricBlue is orange. Renaming hundreds of call sites would say
// less than this comment does.

// The signal path's accent: knobs, LEDs, the active tab, the keys under the hand.
inline juce::Colour electricBlue = forgeTheme().electricBlue;
// The modulators' accent: the LFOs, the matrix, every mod ring, the status line.
inline juce::Colour signalViolet = forgeTheme().signalViolet;
// Primary text: module titles, readings, chip captions.
inline juce::Colour text = forgeTheme().text;
// Secondary text: header detail, captions, the preset name at rest.
inline juce::Colour mutedText = forgeTheme().mutedText;
// The name of a control: POSITION, CUTOFF, RATE. Its own colour rather than
// mutedText, which every dimmed or secondary piece of lettering shares — the
// label on the knob under your hand is primary text, and sampling the reference
// puts it at RGB (192,213,235), well clear of the legends stamped beside it.
inline juce::Colour labelText = forgeTheme().labelText;
// Stamped legends: a part number, the markings on a decal.
// Dimmer than mutedText, because a legend is read once and then ignored.
inline juce::Colour legendText = forgeTheme().legendText;

// --- The material's palette -------------------------------------------------
//
// A plate is gunmetal lit from above: the top of its face catches the light,
// the foot of it falls away. The two edge colours are what turns that face into
// something raised rather than a rectangle of paint — a lit rim along the top
// and a black one along the bottom is the whole of the trick.
//
// The numbers are measured off the reference rather than chosen, by sampling a
// column straight down through a plate in both and comparing: its chassis sits
// at about RGB 3, its face at (17,19,23), and the lit band along its top edge
// peaks near 100. Two passes were needed. The first had the chassis as light as
// the plates and every plate disappeared into it, which no amount of bevel
// could rescue; the second left the face at (26,30,38) — half a stop light and
// noticeably bluer than the reference's neutral gunmetal — with a rim
// overshooting at 117.

// The chassis fill under everything; in Merli, the fill under the fur.
inline juce::Colour chassis = forgeTheme().chassis;
// Unused today; kept so the table stays total.
inline juce::Colour chassisLit = forgeTheme().chassisLit;
// A metal plate's face where the light catches it: the top of its gradient.
inline juce::Colour plateFaceTop = forgeTheme().plateFaceTop;
// The same face falling away at its foot.
inline juce::Colour plateFaceFoot = forgeTheme().plateFaceFoot;
// The lit rim along a plate's top; also the knob collar and ticks, and edge wear.
inline juce::Colour plateEdgeLit = forgeTheme().plateEdgeLit;
// The dark rim along a plate's foot, and the wall shading a well's floor.
inline juce::Colour plateEdgeDark = forgeTheme().plateEdgeDark;
// The panel surface a chip's keyboard glyph is cut against.
inline juce::Colour panel = forgeTheme().panel;
// The stock JUCE button and keyboard up/down button background.
inline juce::Colour panelRaised = forgeTheme().panelRaised;
// Hairlines and outlines: field rims, grids, rules, an idle tab's edge.
inline juce::Colour line = forgeTheme().line;
// The red unit mark on the identity plate.
inline juce::Colour markRed = forgeTheme().markRed;

// The dark floor of every field, slot, cell and chip at rest.
inline juce::Colour fieldFloor = forgeTheme().fieldFloor;
// A slightly lifted floor: a source handle at rest, the even table rows, the FX add button.
inline juce::Colour chipFloor = forgeTheme().chipFloor;
// The floor of a flat display well, and the LFO footer.
inline juce::Colour displayFloor = forgeTheme().displayFloor;
// The glass floor of a CRT tube and of the spectral well.
inline juce::Colour tubeFloor = forgeTheme().tubeFloor;
// The deepest small well: the rocker recess, the value bubble, a macro name being edited, the key separator, the tooltip.
inline juce::Colour wellFloor = forgeTheme().wellFloor;
// The wavetable editor's canvas and its frame strip.
inline juce::Colour canvasFloor = forgeTheme().canvasFloor;
// The floor of the FX rack's list-view and expand buttons.
inline juce::Colour rackFloor = forgeTheme().rackFloor;
// A shaded recess: the filter response's box, the rocker's sunken foot, the FX list.
inline juce::Colour shadeFloor = forgeTheme().shadeFloor;
// An LED's fill when it is off.
inline juce::Colour lampOff = forgeTheme().lampOff;
// The outline of a fader's thumb.
inline juce::Colour faderThumbRim = forgeTheme().faderThumbRim;
// The knob's collar, under the lit rim gradient.
inline juce::Colour knobCollar = forgeTheme().knobCollar;
// The knob cap's rim where the light catches it; in Merli, the bronze ring lit.
inline juce::Colour knobRimLit = forgeTheme().knobRimLit;
// The knob cap's rim in shadow.
inline juce::Colour knobRimDark = forgeTheme().knobRimDark;
// The knob's dome at its lit centre.
inline juce::Colour knobDomeLit = forgeTheme().knobDomeLit;
// The dome midway down its gradient.
inline juce::Colour knobDomeMid = forgeTheme().knobDomeMid;
// The dome at its dark edge.
inline juce::Colour knobDomeDark = forgeTheme().knobDomeDark;
// The top of a page tab's gradient when the tab is on.
inline juce::Colour tabOnTop = forgeTheme().tabOnTop;
// The top of the gradient when the tab is off.
inline juce::Colour tabOffTop = forgeTheme().tabOffTop;
// The foot of the tab's gradient, on or off.
inline juce::Colour tabFoot = forgeTheme().tabFoot;
// A metal rail's gradient top: the module rims and the long chassis rails.
inline juce::Colour railLit = forgeTheme().railLit;
// The rail's foot.
inline juce::Colour railDark = forgeTheme().railDark;
// The rail's outline.
inline juce::Colour railLine = forgeTheme().railLine;
// A module's title badge, the top of its gradient.
inline juce::Colour badgeTop = forgeTheme().badgeTop;
// The badge's foot.
inline juce::Colour badgeFoot = forgeTheme().badgeFoot;
// The mid stop of a metal piece's face gradient, between plateFaceTop and plateFaceFoot.
inline juce::Colour plateFaceMid = forgeTheme().plateFaceMid;
// The LFO footer's menu field and grid fields.
inline juce::Colour footerField = forgeTheme().footerField;
// A white key.
inline juce::Colour keyWhite = forgeTheme().keyWhite;
// A black key.
inline juce::Colour keyBlack = forgeTheme().keyBlack;
// The rocker switch's raised face, lit edge, when it is on.
inline juce::Colour switchMetalLit = forgeTheme().switchMetalLit;
// The raised face's dark edge when it is on.
inline juce::Colour switchMetalDark = forgeTheme().switchMetalDark;
// The raised face, lit edge, when it is off.
inline juce::Colour switchOffLit = forgeTheme().switchOffLit;
// The raised face's dark edge when it is off.
inline juce::Colour switchOffDark = forgeTheme().switchOffDark;
// The rocker's sunken face, the top of its gradient; its foot is shadeFloor.
inline juce::Colour switchShadeTop = forgeTheme().switchShadeTop;
// The rocker's indicator bar when off; in Merli, a knob's unlit value track too.
inline juce::Colour switchSlot = forgeTheme().switchSlot;
// The identity plate's vertical serial bar.
inline juce::Colour identityBar = forgeTheme().identityBar;
// The serial bar's lit edge.
inline juce::Colour identityBarLit = forgeTheme().identityBarLit;
// The RHINO FORGE lettering on the identity plate.
inline juce::Colour identityTitle = forgeTheme().identityTitle;
// The rule under that lettering.
inline juce::Colour identityRule = forgeTheme().identityRule;
// The identity plate's hatching.
inline juce::Colour identityHatch = forgeTheme().identityHatch;
// The LOAD and SAVE buttons' outline at rest.
inline juce::Colour presetOutline = forgeTheme().presetOutline;
// The near-black shade at the top and foot of a CRT tube.
inline juce::Colour tubeVignette = forgeTheme().tubeVignette;
// A macro plate at rest, one step quieter than a source tag.
inline juce::Colour macroCellFloor = forgeTheme().macroCellFloor;
// The rocker switch's dark bezel.
inline juce::Colour switchBezel = forgeTheme().switchBezel;
// A pressed button's well: the preset buttons and the note-guide fields.
inline juce::Colour buttonWellDown = forgeTheme().buttonWellDown;
// The preset buttons' well at rest.
inline juce::Colour buttonWellFloor = forgeTheme().buttonWellFloor;
// The note-guide fields and the quality button at rest.
inline juce::Colour guideFieldFloor = forgeTheme().guideFieldFloor;
// An FX shelf slot.
inline juce::Colour shelfFloor = forgeTheme().shelfFloor;
// A well cut into a housing plate: the note-guide plate and the arp plate's face.
inline juce::Colour plateWellFloor = forgeTheme().plateWellFloor;
// The chassis' main well inside the frame, behind the modules.
inline juce::Colour chassisWellFloor = forgeTheme().chassisWellFloor;
// The well the page tabs sit in.
inline juce::Colour tabWellFloor = forgeTheme().tabWellFloor;
// The lit hairline down each side of the chassis.
inline juce::Colour sideRailLit = forgeTheme().sideRailLit;
// The shadow hairline two pixels inside it.
inline juce::Colour sideRailDark = forgeTheme().sideRailDark;
// The well the keyboard sits in.
inline juce::Colour keyboardWellFloor = forgeTheme().keyboardWellFloor;
// A note-guide letter on a black key.
inline juce::Colour keyLabelOnBlack = forgeTheme().keyLabelOnBlack;
// A note-guide letter on a white key.
inline juce::Colour keyLabelOnWhite = forgeTheme().keyLabelOnWhite;
// An oscillator painted Red from its LED's menu.
inline juce::Colour oscRed = forgeTheme().oscRed;
// An oscillator painted Orange.
inline juce::Colour oscOrange = forgeTheme().oscOrange;
// An oscillator painted Green, which is where a new one starts.
inline juce::Colour oscGreen = forgeTheme().oscGreen;

// Only Merli reads the rest; Forge's values are the nearest Forge colour.

// Value text in choosers, LOAD and SAVE, and the active tab.
inline juce::Colour valueText = forgeTheme().valueText;
// The knob's pointer tick.
inline juce::Colour knobPointer = forgeTheme().knobPointer;
// The dots at either end of the knob's value ring.
inline juce::Colour knobArcDot = forgeTheme().knobArcDot;
// The paw icon before a module's title, and the EnableLed's paw when lit.
inline juce::Colour pawTint = forgeTheme().pawTint;
// The paw's highlight.
inline juce::Colour pawHighlight = forgeTheme().pawHighlight;
// The one-pixel lit rim along the fur band's top edge.
inline juce::Colour furRimLit = forgeTheme().furRimLit;
// The two-pixel lip where the fur meets the window's bottom edge.
inline juce::Colour furLip = forgeTheme().furLip;
// The strip above the fur band, outside the ears, that makes them a silhouette.
inline juce::Colour backdrop = forgeTheme().backdrop;
// The compressor's gain-reduction meter: its face, its scale and its red.
inline juce::Colour meterFace = forgeTheme().meterFace;
inline juce::Colour meterInk = forgeTheme().meterInk;
inline juce::Colour meterRed = forgeTheme().meterRed;

// --- Applying a theme -------------------------------------------------------

// One row per token: the global it lives in and the palette member it takes.
// The one list applyTheme walks and a test can walk after it, so no token can
// be in the struct and forgotten here without the compiler saying so.
struct TokenBinding
{
    const char* name;
    juce::Colour* global;
    juce::Colour Theme::* member;
};

inline const std::vector<TokenBinding>& tokenBindings()
{
#define RHINO_FORGE_TOKEN(token) TokenBinding { #token, &token, &Theme::token }
    static const std::vector<TokenBinding> bindings {
        RHINO_FORGE_TOKEN(electricBlue), RHINO_FORGE_TOKEN(signalViolet), RHINO_FORGE_TOKEN(text),
        RHINO_FORGE_TOKEN(mutedText), RHINO_FORGE_TOKEN(labelText), RHINO_FORGE_TOKEN(legendText),
        RHINO_FORGE_TOKEN(chassis), RHINO_FORGE_TOKEN(chassisLit), RHINO_FORGE_TOKEN(plateFaceTop),
        RHINO_FORGE_TOKEN(plateFaceFoot), RHINO_FORGE_TOKEN(plateEdgeLit), RHINO_FORGE_TOKEN(plateEdgeDark),
        RHINO_FORGE_TOKEN(panel), RHINO_FORGE_TOKEN(panelRaised), RHINO_FORGE_TOKEN(line),
        RHINO_FORGE_TOKEN(markRed),

        RHINO_FORGE_TOKEN(fieldFloor), RHINO_FORGE_TOKEN(chipFloor), RHINO_FORGE_TOKEN(displayFloor),
        RHINO_FORGE_TOKEN(tubeFloor), RHINO_FORGE_TOKEN(wellFloor), RHINO_FORGE_TOKEN(canvasFloor),
        RHINO_FORGE_TOKEN(rackFloor), RHINO_FORGE_TOKEN(shadeFloor), RHINO_FORGE_TOKEN(lampOff),
        RHINO_FORGE_TOKEN(faderThumbRim), RHINO_FORGE_TOKEN(knobCollar), RHINO_FORGE_TOKEN(knobRimLit),
        RHINO_FORGE_TOKEN(knobRimDark), RHINO_FORGE_TOKEN(knobDomeLit), RHINO_FORGE_TOKEN(knobDomeMid),
        RHINO_FORGE_TOKEN(knobDomeDark), RHINO_FORGE_TOKEN(tabOnTop), RHINO_FORGE_TOKEN(tabOffTop),
        RHINO_FORGE_TOKEN(tabFoot), RHINO_FORGE_TOKEN(railLit), RHINO_FORGE_TOKEN(railDark),
        RHINO_FORGE_TOKEN(railLine), RHINO_FORGE_TOKEN(badgeTop), RHINO_FORGE_TOKEN(badgeFoot),
        RHINO_FORGE_TOKEN(plateFaceMid), RHINO_FORGE_TOKEN(footerField), RHINO_FORGE_TOKEN(keyWhite),
        RHINO_FORGE_TOKEN(keyBlack), RHINO_FORGE_TOKEN(switchMetalLit), RHINO_FORGE_TOKEN(switchMetalDark),
        RHINO_FORGE_TOKEN(switchOffLit), RHINO_FORGE_TOKEN(switchOffDark), RHINO_FORGE_TOKEN(switchShadeTop),
        RHINO_FORGE_TOKEN(switchSlot), RHINO_FORGE_TOKEN(identityBar), RHINO_FORGE_TOKEN(identityBarLit),
        RHINO_FORGE_TOKEN(identityTitle), RHINO_FORGE_TOKEN(identityRule), RHINO_FORGE_TOKEN(identityHatch),
        RHINO_FORGE_TOKEN(presetOutline), RHINO_FORGE_TOKEN(tubeVignette), RHINO_FORGE_TOKEN(macroCellFloor),
        RHINO_FORGE_TOKEN(switchBezel), RHINO_FORGE_TOKEN(buttonWellDown), RHINO_FORGE_TOKEN(buttonWellFloor),
        RHINO_FORGE_TOKEN(guideFieldFloor), RHINO_FORGE_TOKEN(shelfFloor), RHINO_FORGE_TOKEN(plateWellFloor),
        RHINO_FORGE_TOKEN(chassisWellFloor), RHINO_FORGE_TOKEN(tabWellFloor), RHINO_FORGE_TOKEN(sideRailLit),
        RHINO_FORGE_TOKEN(sideRailDark), RHINO_FORGE_TOKEN(keyboardWellFloor), RHINO_FORGE_TOKEN(keyLabelOnBlack),
        RHINO_FORGE_TOKEN(keyLabelOnWhite), RHINO_FORGE_TOKEN(oscRed), RHINO_FORGE_TOKEN(oscOrange),
        RHINO_FORGE_TOKEN(oscGreen),

        RHINO_FORGE_TOKEN(valueText), RHINO_FORGE_TOKEN(knobPointer), RHINO_FORGE_TOKEN(knobArcDot),
        RHINO_FORGE_TOKEN(pawTint), RHINO_FORGE_TOKEN(pawHighlight), RHINO_FORGE_TOKEN(furRimLit),
        RHINO_FORGE_TOKEN(furLip), RHINO_FORGE_TOKEN(backdrop),
        RHINO_FORGE_TOKEN(meterFace), RHINO_FORGE_TOKEN(meterInk), RHINO_FORGE_TOKEN(meterRed),
    };
#undef RHINO_FORGE_TOKEN
    return bindings;
}

// Which theme the tokens currently hold.
inline ThemeId currentThemeId = ThemeId::forge;

// Writes one palette over every token. Called once, before any Editor exists:
// a component copies tokens as it is built, so a theme applied later would
// reach the chrome and miss the controls.
inline void applyTheme(ThemeId id)
{
    const auto& theme = themeFor(id);
    for (const auto& binding : tokenBindings())
        *binding.global = theme.*binding.member;
    currentThemeId = id;
}

// The question every Merli dispatch asks. Named isMerli rather than merli
// because `merli` is the namespace the Merli material lives in
// (ForgeMerliMaterial.h), and one scope cannot hold both.
inline bool isMerli() { return currentThemeId == ThemeId::merli; }

// --- Names ------------------------------------------------------------------

inline const char* themeName(ThemeId id) { return themeFor(id).name; }

// The id whose name this is, case-insensitively and trimmed, or -1.
inline int themeIndexOf(const juce::String& name)
{
    const auto wanted = name.trim();
    for (int i = 0; i < themeCount; ++i)
        if (wanted.equalsIgnoreCase(themeName(static_cast<ThemeId>(i)))) return i;
    return -1;
}

inline bool knownThemeName(const juce::String& name) { return themeIndexOf(name) >= 0; }

// Unknown names are Forge: a settings file from a build that knew another
// theme, or a hand-edited one, opens the panel rather than refusing to.
inline ThemeId themeFromName(const juce::String& name)
{
    const auto index = themeIndexOf(name);
    return index >= 0 ? static_cast<ThemeId>(index) : ThemeId::forge;
}

// --- Choosing ---------------------------------------------------------------

// Whether something in this process picked the theme on purpose — the snapshot
// tool, a test — which is what tells Processor::loadThemeOnce to leave the
// settings file alone.
inline bool themeChosenFlag = false;

inline void selectTheme(ThemeId id)
{
    applyTheme(id);
    themeChosenFlag = true;
}

inline bool themeChosen() { return themeChosenFlag; }

inline void forgetThemeChoiceForTests() { themeChosenFlag = false; }

// The menu the title plate opens. Built here rather than in the editor so a
// test can read back what it offers, as panelColourMenu is. Item ids are
// one-based in enum order, because a PopupMenu reports nothing chosen as zero.
inline juce::PopupMenu themeMenu(ThemeId current)
{
    juce::PopupMenu menu;
    menu.addSectionHeader("THEME");
    for (int choice = 0; choice < themeCount; ++choice)
    {
        const auto id = static_cast<ThemeId>(choice);
        juce::PopupMenu::Item item(themeName(id));
        item.itemID = choice + 1;
        item.isTicked = id == current;
        menu.addItem(item);
    }
    return menu;
}
}
