#pragma once
#include "model/Session.h"

namespace rhino
{
// File workers receive detached project snapshots, never the live engine/edit.
class ProjectFiles
{
public:
    explicit ProjectFiles(Session& s) : session(s) {}
    void save(bool saveAs = false, std::function<void(bool)> completion = {});
    void newProject();
    void open();
    void openFile(const juce::File&);
    void exportWav();
    void confirmUnsaved(std::function<void()>);
    std::function<void(juce::String)> status;
    std::function<void(bool)> loadingChanged;
    // The shell renders a started export a slice per frame behind its loading
    // screen, then hands it back to exportFinished. Unset, the export renders
    // before exportWav's chooser returns.
    std::function<void(const juce::File&)> exportStarted;
    void exportFinished();
private:
    void write(const juce::File&, std::function<void(bool)>);
    void renderWav(const juce::File&);
    void chooseOpen();
    void load(const juce::File&);
    void report(const juce::String& text) { if (status) status(text); }
    Session& session;
    bool busy = false;
    juce::File exporting;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool workers {1};
    JUCE_DECLARE_WEAK_REFERENCEABLE(ProjectFiles)
};
}
