#include "DuckSuites.h"
#include "DuckTestSupport.h"
#include "DuckTestTools.h"

#include <iostream>

namespace rhino::duck::tests
{
namespace
{
struct Area
{
    const char* name;
    void (*run)();
};

// The whole registry. The name is both the argument and the CTest case, so
// `--engine` and `ctest -R duck_engine` select the same file.
constexpr Area areas[] {
    { "curves", curveTests },
    { "engine", engineTests },
    { "processor", processorTests },
    { "layout", layoutTests },
};

void listAreas(std::ostream& out)
{
    for (const auto& area : areas) out << "  --" << area.name << '\n';
}
}
}

// One binary, one CTest case per area, selected by argument, as Forge's and
// Rhino's own test binaries are. No argument runs every area in order.
int main(int argc, char** argv)
{
    using namespace rhino::duck::tests;
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const juce::String first = argc > 1 ? argv[1] : "";

    if (first == "--snapshot") return runSnapshot(argc, argv);
    if (first == "--list")
    {
        listAreas(std::cout);
        return 0;
    }

    juce::StringArray ran;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String requested = juce::String(argv[i]).trimCharactersAtStart("-");
        const Area* found = nullptr;
        for (const auto& area : areas)
            if (requested == area.name) found = &area;
        if (found == nullptr)
        {
            std::cerr << "Unknown Rhino Duck test area \"" << argv[i] << "\". Known areas:\n";
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
        std::cerr << failures << " Rhino Duck check(s) failed\n";
        return 1;
    }
    std::cout << "Rhino Duck checks passed" << (ran.isEmpty() ? juce::String() : " (" + ran.joinIntoString(", ") + ")") << '\n';
    return 0;
}
