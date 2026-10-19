#include "ForgeSuites.h"
#include "ForgeTestSupport.h"
#include "../ui/ForgeTheme.h"
#include "../ui/ForgePlacement.h"
#include "../ui/ForgeControls.h"
#include "../ui/ForgePresetPlate.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

// The theme: its names, its palettes, the file that remembers it, its menu,
// and the panel drawn in Merli.
//
// Two things make this area different from the others. The tokens are process
// globals copied by components as they are built, so every editor here is
// opened under a theme chosen on purpose and painted before the next theme is
// selected. And the settings file is the machine's own, so the override is
// pointed at a scratch of this area's for the whole run and the real file is
// checked afterwards for not having moved.
namespace rhino::forge::tests
{
namespace
{
using ui::ThemeId;

// Where ForgeTestMain pointed the override before any area ran. The area ends
// by restoring it rather than clearing it: an empty override is the real file,
// and a later area's createEditor must never be able to reach that.
juce::File mainScratchSettings()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("RhinoForgeTests")
        .getChildFile("Settings.xml");
}

// This area's own scratch, beside main's, so it can be deleted and rewritten
// freely without touching what main set up.
juce::File themeScratchSettings()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("RhinoForgeTests")
        .getChildFile("ThemeSettings.xml");
}

// The file the plugin reads on this machine, spelled here rather than asked of
// settingsFile(): the override hides it, and a check that it was left alone
// cannot borrow the path from the code it is checking.
juce::File realSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino Forge")
        .getChildFile("Settings.xml");
}

struct FileState
{
    bool exists;
    juce::int64 modified;
};

FileState stateOf(const juce::File& file)
{
    return { file.existsAsFile(), file.getLastModificationTime().toMilliseconds() };
}

void writeScratch(const juce::String& contents)
{
    const auto file = themeScratchSettings();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(contents);
}

// A Processor and its panel, declared so the panel is destroyed before the
// Processor it watches.
struct Panel
{
    std::unique_ptr<Processor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

// An editor built under a theme. selectTheme rather than applyTheme, because
// createEditor's loadThemeOnce reads the settings file on the first call in
// the process unless the theme was chosen on purpose, and which area made that
// first call depends on the command line.
std::unique_ptr<Panel> openPanel(ThemeId theme, int width, int height)
{
    ui::selectTheme(theme);
    auto opened = std::make_unique<Panel>();
    opened->processor = std::make_unique<Processor>();
    opened->editor.reset(opened->processor->createEditor());
    opened->editor->setSize(width, height);
    return opened;
}

void pressTab(juce::AudioProcessorEditor& editor, const char* page)
{
    for (auto* child : editor.getChildren())
        if (auto* tab = dynamic_cast<ui::PageTab*>(child))
            if (tab->getButtonText() == juce::String(page) && tab->onClick) tab->onClick();
}

// Painted the way --snapshot paints, so what the hash protocol says about a
// page holds for what these suites compare.
juce::Image paintPanel(juce::AudioProcessorEditor& editor)
{
    return editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
}

// Pixels that differ within the first `rows` rows, or everywhere when rows is
// negative. Images of unequal size differ in every pixel.
int differingPixels(const juce::Image& a, const juce::Image& b, int rows = -1)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return a.getWidth() * a.getHeight() + b.getWidth() * b.getHeight();
    const juce::Image::BitmapData left(a, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData right(b, juce::Image::BitmapData::readOnly);
    const auto height = rows < 0 ? a.getHeight() : juce::jmin(rows, a.getHeight());
    int differing = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (left.getPixelColour(x, y) != right.getPixelColour(x, y)) ++differing;
    return differing;
}

// --------------------------------------------------------------- the names ---

void nameSuite()
{
    for (int i = 0; i < ui::themeCount; ++i)
    {
        const auto id = static_cast<ThemeId>(i);
        require(ui::themeFromName(ui::themeName(id)) == id, "a theme's name reads back as its id");
        require(ui::knownThemeName(ui::themeName(id)), "a theme's own name is a known one");
    }
    requireText(ui::themeName(ThemeId::forge), "Forge", "the default theme is called Forge");
    requireText(ui::themeName(ThemeId::merli), "Merli", "the second theme is called Merli");

    require(ui::themeFromName("MERLI") == ThemeId::merli, "a name reads in any case");
    require(ui::themeFromName("Merli") == ThemeId::merli, "a name reads as it is spelled");
    require(ui::themeFromName(" merli ") == ThemeId::merli, "a name reads with space around it");

    // Unknown is Forge rather than a refusal: a settings file from a build
    // that knew another theme still opens the panel.
    for (const auto* unknown : {"", "tabby", "merl", "Merli2"})
    {
        const auto fallback = ui::themeFromName(unknown);
        requireText(ui::themeName(fallback), "Forge", "an unknown name is Forge");
    }
    require(ui::knownThemeName("Forge "), "a known name is known with space around it");
    require(!ui::knownThemeName("tabby"), "a name no theme has is not known");
    require(!ui::knownThemeName(""), "an empty name is not known");
}

// ------------------------------------------------------------ the palettes ---

// Whether every token holds the palette's value, through the one table
// applyTheme walks. Named in the report so a stale row can be found.
int tokensAstray(const ui::Theme& palette)
{
    int astray = 0;
    for (const auto& binding : ui::tokenBindings())
        if (*binding.global != palette.*binding.member)
        {
            std::cerr << "  token " << binding.name << " does not hold " << palette.name << "'s value\n";
            ++astray;
        }
    return astray;
}

void paletteSuite()
{
    // The table itself: one row per token, no row twice, nothing dangling.
    {
        const auto& bindings = ui::tokenBindings();
        require(!bindings.empty(), "the token table has rows");
        int duplicates = 0;
        for (size_t i = 0; i < bindings.size(); ++i)
        {
            require(bindings[i].global != nullptr && bindings[i].member != nullptr && bindings[i].name != nullptr,
                    "every token row names a global and a palette member");
            for (size_t j = i + 1; j < bindings.size(); ++j)
                if (bindings[i].global == bindings[j].global || bindings[i].member == bindings[j].member
                    || juce::String(bindings[i].name) == bindings[j].name)
                    ++duplicates;
        }
        require(duplicates == 0, "no token is bound twice");
    }

    ui::applyTheme(ThemeId::merli);
    require(ui::currentThemeId == ThemeId::merli, "applying Merli makes Merli the current theme");
    require(ui::isMerli(), "the dispatch question answers yes under Merli");
    require(tokensAstray(ui::merliTheme()) == 0, "applying Merli writes every token from its palette");

    // The sixteen the panel always had, by name rather than through the table,
    // so one of them dropped from the table is noticed here and not only in a
    // snapshot.
    const auto& chosen = ui::merliTheme();
    require(ui::electricBlue == chosen.electricBlue && ui::signalViolet == chosen.signalViolet
                && ui::text == chosen.text && ui::mutedText == chosen.mutedText
                && ui::labelText == chosen.labelText && ui::legendText == chosen.legendText
                && ui::chassis == chosen.chassis && ui::chassisLit == chosen.chassisLit
                && ui::plateFaceTop == chosen.plateFaceTop && ui::plateFaceFoot == chosen.plateFaceFoot
                && ui::plateEdgeLit == chosen.plateEdgeLit && ui::plateEdgeDark == chosen.plateEdgeDark
                && ui::panel == chosen.panel && ui::panelRaised == chosen.panelRaised
                && ui::line == chosen.line && ui::markRed == chosen.markRed,
            "the sixteen original tokens take Merli's values by name");

    ui::applyTheme(ThemeId::forge);
    require(ui::currentThemeId == ThemeId::forge, "applying Forge makes Forge the current theme");
    require(!ui::isMerli(), "the dispatch question answers no under Forge");
    require(tokensAstray(ui::forgeTheme()) == 0, "applying Forge writes every token from its palette");

    // The one place a fixed colour belongs in a test: these bytes are what the
    // baseline snapshots are hashed against, and the palette must give them
    // back exactly after Merli has been and gone.
    struct OriginalByte
    {
        const juce::Colour* global;
        juce::uint32 argb;
        const char* name;
    };
    const OriginalByte originals[] {
        {&ui::electricBlue,  0xff45a8ff, "electricBlue"},
        {&ui::signalViolet,  0xff9a6cff, "signalViolet"},
        {&ui::text,          0xffe8eaff, "text"},
        {&ui::mutedText,     0xff8f95ad, "mutedText"},
        {&ui::labelText,     0xffc0d5eb, "labelText"},
        {&ui::legendText,    0xff6c7386, "legendText"},
        {&ui::chassis,       0xff020304, "chassis"},
        {&ui::chassisLit,    0xff05060a, "chassisLit"},
        {&ui::plateFaceTop,  0xff191b1e, "plateFaceTop"},
        {&ui::plateFaceFoot, 0xff0b0d10, "plateFaceFoot"},
        {&ui::plateEdgeLit,  0xff868d9c, "plateEdgeLit"},
        {&ui::plateEdgeDark, 0xff020407, "plateEdgeDark"},
        {&ui::panel,         0xff111522, "panel"},
        {&ui::panelRaised,   0xff191d2d, "panelRaised"},
        {&ui::line,          0xff34394e, "line"},
        {&ui::markRed,       0xffdf503b, "markRed"},
    };
    int changed = 0;
    for (const auto& original : originals)
        if (original.global->getARGB() != original.argb)
        {
            std::cerr << "  token " << original.name << " came back as "
                      << original.global->toDisplayString(true) << '\n';
            ++changed;
        }
    require(changed == 0, "Forge's sixteen original bytes come back exactly");
}

// ------------------------------------------------------- the settings file ---

void settingsSuite()
{
    const auto scratch = themeScratchSettings();
    Processor::setSettingsFile(scratch);
    require(Processor::settingsFile() == scratch, "the override is where the settings are read");

    scratch.deleteFile();
    require(Processor::readThemeChoice() == ThemeId::forge, "no settings file means Forge");

    Processor::saveThemeChoice(ThemeId::merli);
    require(scratch.existsAsFile(), "saving a choice writes the settings file");
    if (const auto root = juce::parseXML(scratch))
    {
        require(root->hasTagName("RhinoForgeSettings"), "the settings file's root is RhinoForgeSettings");
        requireText(root->getStringAttribute("theme"), "merli", "the theme is kept as its lower-case name");
    }
    else
        require(false, "the settings file parses as XML");
    require(Processor::readThemeChoice() == ThemeId::merli, "a saved Merli reads back as Merli");

    Processor::saveThemeChoice(ThemeId::forge);
    if (const auto root = juce::parseXML(scratch))
        requireText(root->getStringAttribute("theme"), "forge", "saving Forge writes forge");
    require(Processor::readThemeChoice() == ThemeId::forge, "a saved Forge reads back as Forge");

    // Everything that is not a settings file reads as the default, quietly.
    writeScratch("<RhinoForgeSettings theme=\"nonsense\"/>");
    require(Processor::readThemeChoice() == ThemeId::forge, "an unknown theme name in the file is Forge");
    writeScratch("not xml at all");
    require(Processor::readThemeChoice() == ThemeId::forge, "a file that is not XML is Forge");
    writeScratch("<RhinoForgeMidiMap theme=\"merli\"/>");
    require(Processor::readThemeChoice() == ThemeId::forge, "a file with another root is Forge");
    writeScratch("<RhinoForgeSettings/>");
    require(Processor::readThemeChoice() == ThemeId::forge, "a settings file with no theme is Forge");

    // A setting a later build adds has to survive this build saving the theme,
    // which is why the save is read-change-write rather than a rewrite.
    writeScratch("<RhinoForgeSettings theme=\"forge\" later=\"kept\"/>");
    Processor::saveThemeChoice(ThemeId::merli);
    if (const auto root = juce::parseXML(scratch))
    {
        requireText(root->getStringAttribute("later"), "kept", "an attribute this build does not know survives a save");
        requireText(root->getStringAttribute("theme"), "merli", "and the theme is still written beside it");
    }
    else
        require(false, "the settings file still parses after a save over a foreign attribute");
}

// --------------------------------------------------------------- read once ---

void readOnceSuite()
{
    Processor::setSettingsFile(themeScratchSettings());

    // A fresh process whose settings say Merli: the first read applies it.
    Processor::saveThemeChoice(ThemeId::merli);
    ui::applyTheme(ThemeId::forge);
    Processor::forgetLoadedThemeForTests();
    ui::forgetThemeChoiceForTests();
    require(Processor::loadThemeOnce() == ThemeId::merli, "the first load reads the file");
    require(ui::currentThemeId == ThemeId::merli, "and applies what it read");
    require(ui::electricBlue == ui::merliTheme().electricBlue, "down to the tokens");

    // The file changing afterwards changes nothing until the next start.
    Processor::saveThemeChoice(ThemeId::forge);
    require(Processor::readThemeChoice() == ThemeId::forge, "the file now says Forge");
    require(Processor::loadThemeOnce() == ThemeId::merli, "a later load does not read it again");
    require(ui::currentThemeId == ThemeId::merli, "so the applied theme stays");
    require(ui::electricBlue == ui::merliTheme().electricBlue, "and so do the tokens");

    // A theme chosen on purpose before the first load outranks the file: the
    // snapshot tool renders either look from one machine this way.
    Processor::saveThemeChoice(ThemeId::merli);
    Processor::forgetLoadedThemeForTests();
    ui::forgetThemeChoiceForTests();
    ui::selectTheme(ThemeId::forge);
    require(ui::themeChosen(), "selecting a theme marks it chosen");
    require(Processor::loadThemeOnce() == ThemeId::forge, "a chosen theme is not overruled by the file");
    require(ui::currentThemeId == ThemeId::forge, "and stays applied");
    require(ui::electricBlue == ui::forgeTheme().electricBlue, "down to the tokens");

    // The hook itself: opening a panel is what performs the first read.
    Processor::forgetLoadedThemeForTests();
    ui::forgetThemeChoiceForTests();
    ui::applyTheme(ThemeId::forge);
    {
        auto processor = std::make_unique<Processor>();
        require(ui::currentThemeId == ThemeId::forge, "making a Processor reads no theme");
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        require(ui::currentThemeId == ThemeId::merli, "opening the panel reads the theme");
    }

    ui::applyTheme(ThemeId::forge);
    ui::forgetThemeChoiceForTests();
}

// ---------------------------------------------------------------- the menu ---

void menuSuite()
{
    for (int i = 0; i < ui::themeCount; ++i)
    {
        const auto current = static_cast<ThemeId>(i);
        auto menu = ui::themeMenu(current);
        int rows = 0, ticked = 0;
        juce::PopupMenu::MenuItemIterator walk(menu);
        while (walk.next())
        {
            const auto& item = walk.getItem();
            if (item.itemID == 0) continue;   // the section header
            ++rows;
            require(item.itemID == rows, "the menu offers the themes in enum order with one-based ids");
            requireText(item.text, ui::themeName(static_cast<ThemeId>(rows - 1)),
                        "each row of the menu is named for the theme it sets");
            if (item.isTicked)
            {
                ++ticked;
                require(static_cast<ThemeId>(item.itemID - 1) == current, "the tick is on the current theme");
            }
        }
        require(rows == ui::themeCount, "the menu offers every theme and no others");
        require(ticked == 1, "the menu ticks exactly one theme");
    }
}

// ----------------------------------------------------------- Merli renders ---

void merliRenderSuite()
{
    const auto before = failures;
    for (const auto [width, height] : {std::pair {ui::defaultPanelWidth, ui::defaultPanelHeight},
                                       std::pair {ui::minPanelWidth, ui::minPanelHeight}})
    {
        auto opened = openPanel(ThemeId::merli, width, height);
        require(opened->editor != nullptr, "the panel opens in Merli");
        if (opened->editor == nullptr) continue;
        for (const auto page : ui::tabPages)
        {
            pressTab(*opened->editor, ui::pageName(page));
            const auto picture = paintPanel(*opened->editor);
            require(picture.getWidth() == width && picture.getHeight() == height,
                    "a Merli page paints at the size it was asked for");
        }
    }
    require(failures == before, "every Merli page paints at both sizes without a check failing");

    // The textures are tiled from fixed origins and nothing in the chassis
    // reads a clock, so two fresh panels have to agree to the byte. That is
    // what lets the snapshot hash protocol cover Merli as it covers Forge.
    auto first = openPanel(ThemeId::merli, ui::defaultPanelWidth, ui::defaultPanelHeight);
    pressTab(*first->editor, "OSC");
    const auto one = paintPanel(*first->editor);
    first.reset();
    auto second = openPanel(ThemeId::merli, ui::defaultPanelWidth, ui::defaultPanelHeight);
    pressTab(*second->editor, "OSC");
    const auto two = paintPanel(*second->editor);
    const auto differing = differingPixels(one, two);
    if (differing != 0) std::cerr << "  two Merli panels differ in " << differing << " pixels\n";
    require(differing == 0, "two fresh Merli panels paint the OSC page byte for byte alike");
}

// -------------------------------------------------- Merli against Forge ---

void merliDiffersSuite()
{
    // The Forge panel is painted before Merli is selected: its components
    // copied Forge's colours as they were built, but what it draws at paint
    // time is read from the globals.
    auto forgePanel = openPanel(ThemeId::forge, ui::defaultPanelWidth, ui::defaultPanelHeight);
    pressTab(*forgePanel->editor, "OSC");
    const auto forgePicture = paintPanel(*forgePanel->editor);

    auto merliPanel = openPanel(ThemeId::merli, ui::defaultPanelWidth, ui::defaultPanelHeight);
    pressTab(*merliPanel->editor, "OSC");
    const auto merliPicture = paintPanel(*merliPanel->editor);

    // The header is where Merli is most itself: ears, fur and plates over
    // what Forge leaves as dark chassis.
    const auto differing = differingPixels(forgePicture, merliPicture, 100);
    if (differing <= 20000) std::cerr << "  the two themes differ in only " << differing << " header pixels\n";
    require(differing > 20000, "Merli's header differs from Forge's across most of its pixels");
    require(merliPicture.getPixelAt(2, 2) != ui::forgeTheme().chassis,
            "Merli's top-left corner is not Forge's chassis");

    // Decision 2, held structurally: a theme changes paint and colour and
    // nothing about where anything is.
    const auto& forgeChildren = forgePanel->editor->getChildren();
    const auto& merliChildren = merliPanel->editor->getChildren();
    require(forgeChildren.size() > 0, "the panel has components to compare");
    require(forgeChildren.size() == merliChildren.size(), "both themes build the same components");
    // The one exception is the preset's name, which goes on a different plate in
    // each theme at the user's request: Forge's maker plate, Merli's beside the
    // cat. Its words move; nothing that takes a click does.
    int moved = 0;
    for (int i = 0; i < juce::jmin(forgeChildren.size(), merliChildren.size()); ++i)
        if (forgeChildren[i]->getBounds() != merliChildren[i]->getBounds()
            && dynamic_cast<ui::PresetPlate*>(forgeChildren[i]) == nullptr)
            ++moved;
    if (moved != 0) std::cerr << "  " << moved << " components sit elsewhere under Merli\n";
    require(moved == 0, "every component keeps its rectangle under Merli");
}

// --------------------------------------------------------- Forge is stable ---

// The in-process half of the hash protocol: two fresh Forge panels agree to
// the byte, so a Forge pixel that moves between builds moved because of the
// build and not because of the paint.
void forgeStableSuite()
{
    auto first = openPanel(ThemeId::forge, ui::defaultPanelWidth, ui::defaultPanelHeight);
    pressTab(*first->editor, "OSC");
    const auto one = paintPanel(*first->editor);
    first.reset();
    auto second = openPanel(ThemeId::forge, ui::defaultPanelWidth, ui::defaultPanelHeight);
    pressTab(*second->editor, "OSC");
    const auto two = paintPanel(*second->editor);
    const auto differing = differingPixels(one, two);
    if (differing != 0) std::cerr << "  two Forge panels differ in " << differing << " pixels\n";
    require(differing == 0, "two fresh Forge panels paint the OSC page byte for byte alike");
}
}

void themeTests()
{
    std::cout << "Theme\n";
    // Everything below reads and writes the settings through the override, and
    // the one way to be sure is to look at the real file before and after.
    const auto realBefore = stateOf(realSettingsFile());
    Processor::setSettingsFile(themeScratchSettings());
    themeScratchSettings().deleteFile();

    nameSuite();
    paletteSuite();
    settingsSuite();
    readOnceSuite();
    menuSuite();
    forgeStableSuite();
    merliRenderSuite();
    merliDiffersSuite();

    // A no-argument run executes every area in one process, so the tokens go
    // back to Forge, the choice is forgotten, and the override returns to
    // main's scratch — never to the real file.
    ui::applyTheme(ThemeId::forge);
    ui::forgetThemeChoiceForTests();
    themeScratchSettings().deleteFile();
    Processor::setSettingsFile(mainScratchSettings());

    const auto realAfter = stateOf(realSettingsFile());
    require(realAfter.exists == realBefore.exists && realAfter.modified == realBefore.modified,
            "the machine's own settings file was never touched");
}
}
