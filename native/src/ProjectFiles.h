#pragma once
#include "Session.h"

namespace theta
{
// File workers receive detached project snapshots, never the live engine/edit.
class ProjectFiles
{
public:
    explicit ProjectFiles(Session& s) : session(s) {}
    void save(bool saveAs = false, std::function<void(bool)> completion = {});
    void open();
    void openFile(const juce::File&);
    void exportWav();
    void confirmUnsaved(std::function<void()>);
    std::function<void(juce::String)> status;
    std::function<void(bool)> loadingChanged;
private:
    void write(const juce::File&, std::function<void(bool)>);
    void renderWav(const juce::File&);
    void chooseOpen();
    void load(const juce::File&);
    void report(const juce::String& text) { if (status) status(text); }
    Session& session;
    bool busy = false;
    // Held for the whole of an export: the edit stays detached from the audio
    // device until this is released back on the message thread.
    std::unique_ptr<te::Edit::ScopedRenderStatus> renderStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool workers {1};
    JUCE_DECLARE_WEAK_REFERENCEABLE(ProjectFiles)
};
}
