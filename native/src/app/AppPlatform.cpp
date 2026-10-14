#include "AppPlatform.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shlobj.h>
#endif

namespace rhino
{

void keepWindowAbove(juce::Component& window, juce::Component* owner)
{
   #if JUCE_WINDOWS
    // JUCE has no owned window, and always-on-top cannot stand in for one: it
    // floats above every app, and switching it off puts the window above
    // whatever app is in front. Setting the owner of a top-level window is
    // what GWLP_HWNDPARENT does for one that is not a child.
    auto* handle = static_cast<HWND>(window.getWindowHandle());
    if (handle == nullptr)
        return;
    auto* ownerHandle = owner != nullptr && owner->getTopLevelComponent() != nullptr
                          ? static_cast<HWND>(owner->getTopLevelComponent()->getWindowHandle())
                          : nullptr;
    if (ownerHandle == handle)
        return;
    if (reinterpret_cast<HWND>(GetWindowLongPtrW(handle, GWLP_HWNDPARENT)) != ownerHandle)
        SetWindowLongPtrW(handle, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(ownerHandle));
   #else
    juce::ignoreUnused(window, owner);
   #endif
}

juce::File rhinoLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino").getChildFile("rhino.log");
}

void avoidLegacyDirectSound(te::Engine& engine)
{
   #if JUCE_WINDOWS
    auto& manager = engine.getDeviceManager().deviceManager;
    const auto currentType = manager.getCurrentAudioDeviceType();
    if (currentType.isNotEmpty() && currentType != "DirectSound")
        return;

    for (auto* type : manager.getAvailableDeviceTypes())
        if (type != nullptr && type->getTypeName() == "Windows Audio")
        {
            juce::Logger::writeToLog("Rhino: using Windows Audio instead of legacy DirectSound");
            manager.setCurrentAudioDeviceType("Windows Audio", true);
            return;
        }
   #else
    juce::ignoreUnused(engine);
   #endif
}

void registerRhinoProjectFileAssociation()
{
   #if JUCE_WINDOWS
    constexpr auto registryRoot = "HKEY_CURRENT_USER\\Software\\Classes\\";
    constexpr auto projectType = "Rhino.Project";
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().quoted();
    const auto typeKey = juce::String(registryRoot) + projectType;
    const auto extensionKey = juce::String(registryRoot) + ".rhinoedit\\";
    const auto associated = juce::WindowsRegistry::setValue(extensionKey, projectType)
                         && juce::WindowsRegistry::setValue(typeKey + "\\", "Rhino project")
                         && juce::WindowsRegistry::setValue(typeKey + "\\DefaultIcon\\", executable + ",0")
                         && juce::WindowsRegistry::setValue(typeKey + "\\shell\\open\\command\\", executable + " \"%1\"");
    if (associated)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    else
        juce::Logger::writeToLog("Rhino: could not register .rhinoedit file association");
   #endif
}

}
