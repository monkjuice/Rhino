#include "DeviceEditorPanel.h"
#include "ContentLibrary.h"
#include "DevicePreset.h"
#include <vector>

// A device's presets from its own panel: right-click the name bar for the
// presets there are to load, and to save the device as one. The browser
// offers the same presets as rows under the device, to drag.
namespace rhino
{
namespace
{
constexpr int saveItem = 100000;

bool hasPresets(const juce::String& deviceId)
{
    const auto* device = DeviceCatalog::byId(deviceId);
    return device != nullptr && !device->external && !device->infrastructure;
}
}

void DeviceEditorPanel::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(deviceName.toUpperCase() + " PRESETS");
    std::vector<juce::File> files;
    if (hasPresets(deviceId))
    {
        // Rhino's own first, then the person's, as the browser lists them.
        auto lastWasUser = false;
        for (const auto& preset : ContentLibrary::presets())
        {
            if (preset.deviceId != deviceId)
                continue;
            if (preset.user && !lastWasUser && !files.empty())
                menu.addSeparator();
            lastWasUser = preset.user;
            files.push_back(preset.file);
            menu.addItem(static_cast<int>(files.size()), preset.name);
        }
        if (files.empty())
            menu.addItem(saveItem + 1, "No presets yet", false);
        menu.addSeparator();
        menu.addItem(saveItem, "Save preset...");
    }
    else
    {
        menu.addItem(saveItem, "Only Rhino's own devices have presets", false);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&title),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), files,
         target = std::pair { track, pluginSlot }] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            if (result == saveItem)
            {
                safe->askToSavePreset();
                return;
            }
            if (result > static_cast<int>(files.size()))
                return;
            const auto& file = files[static_cast<size_t>(result - 1)];
            // Loading syncs the rack, which may rebuild this panel, so what
            // is said afterwards goes through a copy of the callback.
            const auto report = safe->status;
            const auto outcome = safe->session.loadDevicePreset(target.first, target.second, file);
            if (report)
                report(outcome.wasOk() ? "Loaded " + DevicePreset::nameOf(file) : outcome.getErrorMessage());
        });
}

void DeviceEditorPanel::askToSavePreset()
{
    auto* window = new juce::AlertWindow("Save preset", "A name for this " + deviceName + " preset:",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", deviceName);
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), window, id = deviceId] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;
            const auto name = juce::File::createLegalFileName(window->getTextEditorContents("name").trim());
            if (name.isEmpty())
            {
                if (safe->status) safe->status("A preset needs a name.");
                return;
            }
            const auto file = ContentLibrary::userPresets().getChildFile(id).getChildFile(name + DevicePreset::extension);
            if (!file.existsAsFile())
            {
                safe->savePreset(file);
                return;
            }
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                             .withTitle("Replace preset?")
                                             .withMessage("You already have a preset called " + name + ". Replace it?")
                                             .withButton("Replace")
                                             .withButton("Cancel")
                                             .withAssociatedComponent(safe.getComponent()),
                                         [safe, file] (int choice)
                                         {
                                             // The first button answers 1.
                                             if (choice == 1 && safe != nullptr)
                                                 safe->savePreset(file);
                                         });
        }), true);
}

void DeviceEditorPanel::savePreset(const juce::File& file)
{
    const auto result = session.saveDevicePreset(track, pluginSlot, file);
    if (status)
        status(result.wasOk() ? "Saved " + DevicePreset::nameOf(file) + " to your presets" : result.getErrorMessage());
    if (result.wasOk() && presetsChanged)
        presetsChanged();
}
}
