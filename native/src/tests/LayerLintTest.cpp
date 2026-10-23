#include "tests/LayerLintTest.h"
#include <juce_core/juce_core.h>
#include <array>
#include <stdexcept>

// The dependency direction, held by the suite rather than by review
// (wiki/pages/dependency-direction.md): a view or the shell talks to Session
// and never opens the engine behind it. Session::edit and Session::engine are
// public for the model's own units and the test runners, so nothing stops a
// view from spelling `session.edit->getTransport()`; this reads every source
// under ui/ and app/ and fails on the spellings that do. The shell's start-up
// is the one place allowed the engine, to choose the machine's audio device
// before there is a document.
//
// On 2026-10-23, 65 such lines in 18 files were moved behind Session
// (isPlaying, playheadSeconds, locate, secondsToBeats, beatsToSeconds,
// songLengthSeconds, canUndo, canRedo, loopRange, restartPlaybackIfRolling,
// audioDeviceManager, audioDeviceSummary, ownsDevice, transportState and
// undoHistory). A new need is met the same way: add the question to Session.

namespace rhino
{
namespace
{
struct Forbidden
{
    const char* spelling;
    const char* why;
};

constexpr std::array<Forbidden, 9> forbidden {{
    {"session.edit", "the document is the model's; ask Session"},
    {"session->edit", "the document is the model's; ask Session"},
    {"session.engine", "the engine is the model's; ask Session (audioDeviceManager, audioDeviceSummary)"},
    {"session->engine", "the engine is the model's; ask Session (audioDeviceManager, audioDeviceSummary)"},
    {"->getTransport()", "the transport is read through Session::isPlaying, playheadSeconds, locate and loopRange"},
    {".getUndoManager()", "undo is Session::undo, redo, canUndo and canRedo"},
    {"->restartPlayback()", "a graph rebuild is Session::restartPlaybackIfRolling"},
    {"tempoSequence.", "the tempo map is Session::secondsToBeats and beatsToSeconds"},
    {"#include <tracktion_engine", "a view includes Session.h, never the engine"},
}};

// Where the engine is allowed, each spelling with the lines it may take.
struct Allowed
{
    const char* file;
    const char* spelling;
    int lines;
};

constexpr std::array<Allowed, 2> allowed {{
    // The shell opens the machine's audio device before there is a document
    // to ask, and steers Windows off DirectSound while it does.
    {"app/Main.cpp", "session->engine", 3},
    {"app/AppPlatform.cpp", "#include <tracktion_engine", 1},
}};

int allowance(const juce::String& relativePath, const char* spelling)
{
    for (const auto& entry : allowed)
        if (relativePath == entry.file && juce::String(spelling) == entry.spelling)
            return entry.lines;
    return 0;
}

bool identifierCharacter(juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetterOrDigit(c) || c == '_';
}

// Whether the line uses the spelling as a token: `session.edit` is one,
// `session.editClip` and `session.editorNotes` are not.
bool usesToken(const juce::String& line, const char* spelling)
{
    const juce::String token(spelling);
    for (auto at = line.indexOf(token); at >= 0; at = line.indexOf(at + 1, token))
    {
        const auto after = at + token.length();
        if (after >= line.length() || !identifierCharacter(line[after]))
            return true;
    }
    return false;
}
}

void runLayerLintTest()
{
    const auto source = juce::File(juce::String(RHINO_SOURCE_DIR)).getChildFile("src");
    if (!source.isDirectory())
        throw std::runtime_error("The layer lint cannot find native/src");
    juce::StringArray findings;
    int filesRead = 0;
    for (const auto* layer : {"ui", "app"})
    {
        for (const auto& file : source.getChildFile(layer).findChildFiles(juce::File::findFiles, true, "*.cpp;*.h"))
        {
            ++filesRead;
            const auto relative = file.getRelativePathFrom(source).replaceCharacter('\\', '/');
            juce::StringArray lines;
            lines.addLines(file.loadFileAsString());
            for (const auto& rule : forbidden)
            {
                int hits = 0;
                for (int i = 0; i < lines.size(); ++i)
                {
                    // A comment may name what it is explaining.
                    if (lines[i].trimStart().startsWith("//") || !usesToken(lines[i], rule.spelling))
                        continue;
                    if (++hits > allowance(relative, rule.spelling))
                        findings.add(relative + ":" + juce::String(i + 1) + " uses " + rule.spelling + " - " + rule.why);
                }
            }
        }
    }
    if (filesRead < 20)
        throw std::runtime_error("The layer lint read too few files to mean anything");
    if (!findings.isEmpty())
        throw std::runtime_error(("The UI reaches past Session into the engine:\n  "
                                  + findings.joinIntoString("\n  ")).toStdString());
}
}
