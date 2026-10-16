#include "ForgeProcessor.h"

// The machine's own settings: the theme today, and whatever joins it. One file
// serves every instance and every project on this machine, beside the MIDI map
// and kept the same way, because a setting here describes the machine and not
// the sound — a preset or a project that carried the theme would repaint
// someone else's panel.
namespace rhino::forge
{
namespace
{
const juce::String settingsTag("RhinoForgeSettings");
const juce::String themeAttribute("theme");

// Empty means the ordinary place. A function-local static rather than a class
// member, as midiMapOverride is: one location per process, and the Processor
// carries no state for it.
juce::File& settingsOverride()
{
    static juce::File redirected;
    return redirected;
}

// Whether the theme has been read this process. A plain flag rather than a
// call_once so selectTheme can pre-empt the read and a test can reset it.
bool& themeLoaded()
{
    static bool loaded = false;
    return loaded;
}

// The file's root when it is ours, or null for a missing, unreadable or
// foreign file — every one of which reads as the defaults.
std::unique_ptr<juce::XmlElement> settingsRoot(const juce::File& file)
{
    auto root = file.existsAsFile() ? juce::parseXML(file) : nullptr;
    if (root != nullptr && !root->hasTagName(settingsTag)) root.reset();
    return root;
}
}

void Processor::setSettingsFile(const juce::File& file) { settingsOverride() = file; }

juce::File Processor::settingsFile()
{
    if (settingsOverride() != juce::File()) return settingsOverride();
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino Forge")
        .getChildFile("Settings.xml");
}

ui::ThemeId Processor::readThemeChoice()
{
    const auto root = settingsRoot(settingsFile());
    if (root == nullptr) return ui::ThemeId::forge;
    // A missing attribute is an empty string, and an unknown name is Forge.
    return ui::themeFromName(root->getStringAttribute(themeAttribute));
}

// Read, change, write: a setting added by a later build survives an older one
// saving the theme, instead of the file being rewritten from what this build
// knows about.
void Processor::saveThemeChoice(ui::ThemeId theme)
{
    const auto file = settingsFile();
    auto root = settingsRoot(file);
    if (root == nullptr) root = std::make_unique<juce::XmlElement>(settingsTag);
    root->setAttribute(themeAttribute, juce::String(ui::themeName(theme)).toLowerCase());
    file.getParentDirectory().createDirectory();
    root->writeTo(file);
}

ui::ThemeId Processor::loadThemeOnce()
{
    auto& done = themeLoaded();
    if (done) return ui::currentThemeId;
    done = true;
    // A theme something already chose on purpose — the snapshot tool, a test —
    // outranks the file, which is what lets one binary render either look.
    if (!ui::themeChosen()) ui::applyTheme(readThemeChoice());
    return ui::currentThemeId;
}

void Processor::forgetLoadedThemeForTests() { themeLoaded() = false; }
}
