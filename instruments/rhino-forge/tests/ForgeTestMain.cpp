#include "ForgeSuites.h"
#include "ForgeTestSupport.h"
#include "ForgeTestTools.h"

#include <cstring>
#include <iostream>

namespace rhino::forge::tests
{
namespace
{
struct Area
{
    const char* name;
    void (*run)();
};

// The whole registry, in the order a full run executes it: the panel first
// because it is the cheapest way to find a control that has been declared in
// one place and not the other, then everything that renders audio.
//
// The name is both the argument and the CTest case, so `--fx` and
// `ctest -R forge_fx` select the same file.
constexpr Area areas[] {
    {"layout", layoutTests},
    {"displays", displayTests},
    {"presets", presetTests},
    {"engine", engineTests},
    {"oscillator", oscillatorTests},
    {"warp", warpTests},
    {"filter", filterTests},
    {"mixer", mixerTests},
    {"fx", fxTests},
    {"modulation", modulationTests},
    {"envelope", envelopeTests},
    {"lfo", lfoTests},
    {"table", tableTests},
    {"voicing", voicingTests},
    {"arp", arpTests},
};

void listAreas(std::ostream& out)
{
    for (const auto& area : areas) out << "  --" << area.name << '\n';
}
}
}

// One binary, one CTest case per area, selected by argument, matching Rhino's
// own test convention. Run with no argument to execute every area in order.
int main(int argc, char** argv)
{
    using namespace rhino::forge::tests;

    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const juce::String first = argc > 1 ? argv[1] : "";

    // The development tools, which render something rather than check anything.
    // None is a CTest case; all are documented in README.md.
    if (first == "--snapshot" && argc > 2) return runSnapshot(argc, argv);
    if (first == "--profile") return runProfile(argc, argv);
    if (first == "--render") return runRender(argc, argv);

    if (first == "--list")
    {
        listAreas(std::cout);
        return 0;
    }

    // Named areas run in the order they were asked for, so a single command can
    // put the one being worked on first and the slow ones after it.
    juce::StringArray ran;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String requested = juce::String(argv[i]).trimCharactersAtStart("-");
        const Area* found = nullptr;
        for (const auto& area : areas)
            if (requested == area.name) found = &area;

        if (found == nullptr)
        {
            std::cerr << "Unknown Forge test area \"" << argv[i] << "\". Known areas:\n";
            listAreas(std::cerr);
            return 2;
        }
        found->run();
        ran.add(found->name);
    }

    if (ran.isEmpty())
        for (const auto& area : areas) area.run();

    if (failures > 0)
    {
        std::cerr << failures << " Forge check(s) failed\n";
        return 1;
    }
    std::cout << "Rhino Forge checks passed"
              << (ran.isEmpty() ? juce::String() : " (" + ran.joinIntoString(", ") + ")") << '\n';
    return 0;
}
