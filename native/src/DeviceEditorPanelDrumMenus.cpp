#include "DeviceEditorPanelDrumsInternal.h"
#include "ContentLibrary.h"
#include "DrumKitFile.h"
#include "Theme.h"

// The Drum Rack face's menus and dialogs: a pad's menu and its choke group,
// choosing, renaming and saving a pad's sound, and the name bar's kits.
namespace rhino
{
namespace
{
// The kind of drum a sound is, for the folder its preset is saved in: the
// synth's own, or what the sound's names say, or Percussion when they say
// nothing.
juce::String drumTypeOfSound(const DrumSound& sound)
{
    if (sound.source == DrumRackEngine::Source::synth)
        return drumModelInfo(sound.model).type;
    auto unnamed = sound;
    unnamed.name.clear();
    for (const auto& words : {sound.displayName(), unnamed.displayName()})
        if (const auto type = ContentLibrary::drumTypeOf({}, words); type.isNotEmpty())
            return type;
    return "Percussion";
}
}

using namespace drumface;

// ---- menus and dialogs ------------------------------------------------------------

void DeviceEditorPanel::showDrumPadMenu(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr || !juce::isPositiveAndBelow(pad, DrumRackDevice::padCount))
        return;
    const auto view = device->pad(pad);
    const auto filled = view.sound.has_value();
    const auto playsSynth = filled && view.sound->source == DrumRackEngine::Source::synth;
    const auto playsSample = filled && !playsSynth && !view.unreadable;
    constexpr int playItem = 1, sampleItem = 2, renameItem = 3, saveItem = 4, clearItem = 5, sliceItem = 6;
    constexpr int synthItems = 100, chokeItems = 200;

    juce::PopupMenu synths;
    for (int model = 0; model < drumModelCount; ++model)
        synths.addItem(synthItems + model, drumModelInfo(static_cast<DrumModel>(model)).name, true,
                       playsSynth && static_cast<int>(view.sound->model) == model);
    juce::PopupMenu choke;
    for (int group = 0; group <= DrumRackEngine::chokeGroupCount; ++group)
        choke.addItem(chokeItems + group, group == 0 ? juce::String("None") : "Group " + juce::String(group), true,
                      filled && view.sound->choke == group);

    juce::PopupMenu menu;
    menu.addSectionHeader((padTitle(pad) + (filled ? "  " + view.sound->displayName() : juce::String())).toUpperCase());
    menu.addItem(playItem, "Play", filled);
    menu.addSeparator();
    menu.addItem(sampleItem, "Sample...");
    menu.addSubMenu("Synth", synths);
    menu.addSubMenu("Choke group", choke, filled);
    menu.addItem(sliceItem, "Slice to pads", playsSample);
    menu.addSeparator();
    menu.addItem(renameItem, "Rename...", filled);
    menu.addItem(saveItem, "Save as drum preset...", filled);
    menu.addItem(clearItem, "Clear pad", filled);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), pad] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            // The menu outlives the click, so the device is looked up again.
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            const auto name = padTitle(pad);
            const auto report = safe->status;
            if (result == playItem)
                target->previewPad(pad);
            else if (result == sampleItem)
                safe->chooseDrumSample(pad);
            else if (result == renameItem)
                safe->askToRenameDrumPad(pad);
            else if (result == saveItem)
                safe->askToSaveDrumPreset(pad);
            else if (result == sliceItem)
            {
                target->setSelectedPad(pad);
                safe->spreadDrumSlices();
            }
            else if (result == clearItem)
            {
                safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Clear " + name.toLowerCase(),
                                                 [target, pad] { target->clearPad(pad); });
                if (report) report(name + " cleared");
            }
            else if (result >= synthItems && result < synthItems + drumModelCount)
            {
                const auto model = static_cast<DrumModel>(result - synthItems);
                safe->session.editDeviceSettings(safe->track, safe->pluginSlot,
                                                 "Make " + name.toLowerCase() + " a " + drumModelInfo(model).name,
                                                 [target, pad, model] { target->setPadSynth(pad, model); });
                if (report) report(name + " plays a synthesised " + juce::String(drumModelInfo(model).name).toLowerCase());
            }
            else if (result >= chokeItems && result <= chokeItems + DrumRackEngine::chokeGroupCount)
            {
                const auto group = result - chokeItems;
                safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Choke " + name.toLowerCase(),
                                                 [target, pad, group] { target->setPadChoke(pad, group); });
                if (report) report(group == 0 ? name + " chokes nothing" : name + " is in choke group " + juce::String(group));
            }
        });
}

void DeviceEditorPanel::showDrumChokeMenu(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(pad);
    juce::PopupMenu menu;
    menu.addSectionHeader("CHOKE GROUP");
    for (int group = 0; group <= DrumRackEngine::chokeGroupCount; ++group)
        menu.addItem(group + 1, group == 0 ? juce::String("None") : "Group " + juce::String(group), true,
                     view.sound.has_value() && view.sound->choke == group);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), pad] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            const auto group = result - 1;
            safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Choke " + padTitle(pad).toLowerCase(),
                                             [target, pad, group] { target->setPadChoke(pad, group); });
        });
}

void DeviceEditorPanel::chooseDrumSample(int pad)
{
    const auto start = ContentLibrary::file("Samples");
    drumFileChooser = std::make_unique<juce::FileChooser>("A sample for " + padTitle(pad).toLowerCase(),
                                                          start.isDirectory() ? start : juce::File(),
                                                          "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    drumFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), pad] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (safe == nullptr || file == juce::File())
                return;
            const auto report = safe->status;
            const auto result = safe->session.loadDrumPadSample(safe->track, safe->pluginSlot, pad, file);
            if (report)
                report(result.wasOk() ? "Loaded " + file.getFileNameWithoutExtension() + " on " + padTitle(pad).toLowerCase()
                                      : result.getErrorMessage());
        });
}

void DeviceEditorPanel::askToRenameDrumPad(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    auto* window = new juce::AlertWindow("Rename " + padTitle(pad).toLowerCase(), "A name for this pad:",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", device->padName(pad));
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), window, pad] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            // An empty name gives the pad back its sound's own.
            const auto name = window->getTextEditorContents("name").trim();
            safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Rename " + padTitle(pad).toLowerCase(),
                                             [target, pad, name] { target->setPadName(pad, name); });
        }), true);
}

void DeviceEditorPanel::askToSaveDrumPreset(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(pad);
    if (!view.sound.has_value())
        return;
    // Filed by the kind of drum it is, so it lands in the browser beside
    // the samples of its kind.
    const auto folder = ContentLibrary::userDrums().getChildFile("Presets").getChildFile(drumTypeOfSound(*view.sound));
    askToSaveFile("drum preset", "A name for this drum preset:", view.sound->displayName(), folder,
                  DrumFiles::soundExtension,
                  [this, pad] (const juce::File& file)
                  {
                      const auto result = session.saveDrumPadPreset(track, pluginSlot, pad, file);
                      if (status)
                          status(result.wasOk() ? "Saved " + DrumFiles::nameOf(file) + " to your drum presets"
                                                : result.getErrorMessage());
                      if (result.wasOk() && presetsChanged)
                          presetsChanged();
                  });
}

void DeviceEditorPanel::showDrumKitMenu()
{
    constexpr int saveItem = 100000;
    juce::PopupMenu menu;
    menu.addSectionHeader("DRUM RACK KITS");
    std::vector<juce::File> files;
    // Rhino's own first, then the person's, as the browser lists them.
    auto lastWasUser = false;
    for (const auto& kit : ContentLibrary::drumKits())
    {
        if (kit.user && !lastWasUser && !files.empty())
            menu.addSeparator();
        lastWasUser = kit.user;
        files.push_back(kit.file);
        menu.addItem(static_cast<int>(files.size()), kit.name);
    }
    if (files.empty())
        menu.addItem(saveItem + 1, "No kits yet", false);
    menu.addSeparator();
    menu.addItem(saveItem, "Save kit...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&title),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), files] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            if (result == saveItem)
            {
                safe->askToSaveFile("kit", "A name for this kit:", safe->deviceName,
                                    ContentLibrary::userDrums().getChildFile("Kits"), DrumFiles::kitExtension,
                                    [panel = safe.getComponent()] (const juce::File& file)
                                    {
                                        const auto saved = panel->session.saveDrumKit(panel->track, panel->pluginSlot, file);
                                        if (panel->status)
                                            panel->status(saved.wasOk() ? "Saved " + DrumFiles::nameOf(file) + " to your kits"
                                                                        : saved.getErrorMessage());
                                        if (saved.wasOk() && panel->presetsChanged)
                                            panel->presetsChanged();
                                    });
                return;
            }
            if (result > static_cast<int>(files.size()))
                return;
            const auto& file = files[static_cast<size_t>(result - 1)];
            // Loading syncs the rack, which may rebuild this panel, so what is
            // said afterwards goes through a copy of the callback.
            const auto report = safe->status;
            const auto outcome = safe->session.loadDrumKit(safe->track, safe->pluginSlot, file);
            if (report)
                report(outcome.wasOk() ? "Loaded " + DrumFiles::nameOf(file) : outcome.getErrorMessage());
        });
}
}
