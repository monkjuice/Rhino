// The panel's side of presets: LOAD and SAVE opening the browser, the plate
// naming what is loaded, the status line, and a preset dropped on the window.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
void Editor::openPresetBrowser(bool save)
{
    if (presetBrowser == nullptr)
    {
        presetBrowser = std::make_unique<PresetBrowser>(processor);
        presetBrowser->onClose = [this] { closePresetBrowser(); };
        presetBrowser->onPresetLoaded = [this] (const juce::Result& result, const juce::File& file)
        {
            showPresetResult(result, file);
        };
        presetBrowser->announce = [this] (const juce::String& message, bool failed) { showStatus(message, failed); };
        addChildComponent(*presetBrowser);
    }
    presetBrowser->setBounds(ui::contentBounds(getLocalBounds()));
    presetBrowser->setVisible(true);
    presetBrowser->toFront(false);
    if (save) presetBrowser->openToSave();
    else presetBrowser->openToBrowse();
    loadPreset.setToggleState(true, juce::dontSendNotification);
    savePreset.setToggleState(save, juce::dontSendNotification);
}

void Editor::closePresetBrowser()
{
    if (presetBrowser == nullptr) return;
    presetBrowser->setVisible(false);
    loadPreset.setToggleState(false, juce::dontSendNotification);
    savePreset.setToggleState(false, juce::dontSendNotification);
    // The keys play again from the typing keyboard once the list has let go.
    grabKeyboardFocus();
    repaint();
}

void Editor::refreshPresetPlate()
{
    const auto revision = processor.presetRevision();
    if (revision == presetRevisionShown) return;
    presetRevisionShown = revision;

    const auto info = processor.presetInfo();
    juce::StringArray byline;
    if (info.author.isNotEmpty()) byline.add("BY " + info.author.toUpperCase());
    if (info.style.isNotEmpty()) byline.add(info.style.toUpperCase());
    if (byline.isEmpty() && info.name == "INIT") byline.add("DEFAULT PATCH");

    // Where it came from: a conversion says what from and counts what it
    // lost; a preset of the bank names its folder.
    juce::String origin;
    const auto file = processor.presetFile();
    const auto bank = presetBank::root();
    if (info.source.isNotEmpty()) origin = "FROM " + info.source.toUpperCase();
    else if (file.isAChildOf(bank))
        origin = file.getParentDirectory() == bank
            ? juce::String("BANK")
            : "BANK / " + file.getParentDirectory().getRelativePathFrom(bank).replaceCharacter('\\', '/').toUpperCase();
    else if (file != juce::File {}) origin = file.getParentDirectory().getFileName().toUpperCase();
    if (!info.warnings.isEmpty())
        origin << (origin.isEmpty() ? "" : "  /  ") << info.warnings.size()
               << (info.warnings.size() == 1 ? " WARNING" : " WARNINGS");

    presetPlate.show(info.name, byline.joinIntoString("  /  "), origin, !info.warnings.isEmpty());
    presetPlateTooltip = info.warnings.isEmpty()
        ? juce::String("Click to browse presets")
        : "Converted from " + info.source + ". Not carried across:\n- " + info.warnings.joinIntoString("\n- ");
}

void Editor::showPresetResult(const juce::Result& result, const juce::File&)
{
    if (result.failed())
        showStatus("ERROR // " + result.getErrorMessage(), true);
    else
    {
        const auto warnings = processor.presetInfo().warnings.size();
        // A conversion that lost something says so where it will be seen; the
        // list of what is in the browser's card and the plate's tooltip.
        if (warnings > 0)
            showStatus(juce::String(warnings) + (warnings == 1 ? " THING" : " THINGS")
                           + " DID NOT CONVERT - SEE THE BROWSER", true);
        else
            showStatus({}, false);
    }
    refreshPresetPlate();
    // Loading a preset can switch modules on or off behind the enable LEDs.
    applyEnableStates();
    repaint();
}

void Editor::showStatus(const juce::String& message, bool failed)
{
    presetName.setText(message, juce::dontSendNotification);
    presetName.setColour(juce::Label::textColourId, failed ? ui::signalViolet : ui::mutedText);
}

bool Editor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isPresetFile(juce::File(path))) return true;
    return false;
}

void Editor::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (!isPresetFile(file)) continue;
        showPresetResult(processor.loadPreset(file), file);
        if (presetBrowserOpen()) presetBrowser->openToBrowse();
        return;
    }
}
}
