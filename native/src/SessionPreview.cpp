#include "SessionInternal.h"

// Auditioning a library sound.
//
// Clicking a sample in the browser plays it once, the way Live's browser does.
// The preview is a second audio callback on the engine's device rather than
// anything inside the edit: JUCE's device manager sums its callbacks, so the
// sound is simply mixed alongside whatever the transport is doing. Nothing
// about the project changes, no track is chosen, and no undo transaction is
// opened - which is the whole point of being able to click a sound before
// deciding where it goes.
//
// Everything here is built on first use. A session that never previews starts
// no thread and registers no callback, which is what keeps the test runs and
// the offline renders exactly as they were.

namespace rhino
{
namespace
{
const char* previewSettingKey = "browserPreview";
}

// Auditioning is on unless the user has turned it off, and that choice outlives
// the session: a preference that resets every launch is not a preference. The
// test runs never read or write it, so a developer's setting cannot decide
// whether the suite passes.
bool Session::readPreviewPreference()
{
    if (isCommandLineTestMode())
        return true;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    return properties.getBoolValue(previewSettingKey, true);
}

void Session::setPreviewEnabled(bool enabled)
{
    if (browserPreview == enabled)
        return;
    browserPreview = enabled;
    if (!enabled)
        stopPreview();
    if (!isCommandLineTestMode())
    {
        juce::PropertiesFile properties(rhinoSettingsOptions());
        properties.setValue(previewSettingKey, enabled);
        properties.saveIfNeeded();
    }
    sendSynchronousChangeMessage();
}

void Session::ensurePreviewAttached()
{
    if (previewAttached)
        return;
    previewFormats.registerBasicFormats();
    previewThread.startThread(juce::Thread::Priority::normal);
    previewPlayer.setSource(&previewTransport);
    engine.getDeviceManager().deviceManager.addAudioCallback(&previewPlayer);
    previewAttached = true;
}

juce::Result Session::previewSample(const juce::File& file)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!browserPreview)
        return juce::Result::ok();
    if (!file.existsAsFile())
        return juce::Result::fail(file.getFileName() + " is missing from the library.");
    ensurePreviewAttached();
    // The previous audition ends the moment another is asked for, so clicking
    // down a list of kicks does not stack them.
    stopPreview();
    std::unique_ptr<juce::AudioFormatReader> reader(previewFormats.createReaderFor(file));
    if (reader == nullptr)
        return juce::Result::fail("Rhino cannot read " + file.getFileName() + ".");
    const auto rate = reader->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    // Read ahead on the preview thread: the audio callback must never wait on
    // a disk seek, and a library sample is a file like any other.
    previewTransport.setSource(source.get(), 32768, &previewThread, rate);
    previewReader = std::move(source);
    previewTransport.setPosition(0.0);
    previewTransport.start();
    return juce::Result::ok();
}

bool Session::isPreviewing() const
{
    return previewTransport.isPlaying();
}

void Session::stopPreview()
{
    previewTransport.stop();
    previewTransport.setSource(nullptr);
    previewReader.reset();
}

// Called before the device closes and again from the destructor, because either
// can come first: the audio callback must be off the device manager while both
// it and the transport it reads are still alive.
void Session::releasePreview()
{
    if (!previewAttached)
        return;
    engine.getDeviceManager().deviceManager.removeAudioCallback(&previewPlayer);
    previewPlayer.setSource(nullptr);
    stopPreview();
    previewThread.stopThread(2000);
    previewAttached = false;
}

}
