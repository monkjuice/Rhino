#include "VelocityCurveTest.h"
#include "model/VelocityCurve.h"
#include <set>
#include <stdexcept>
#include <string>

namespace rhino
{
namespace
{
void require(bool condition, const std::string& what)
{
    if (!condition)
        throw std::runtime_error("Velocity curves: " + what);
}
}

void checkVelocityCurves()
{
    for (int velocity = 0; velocity <= 127; ++velocity)
        require(applyVelocityCurve(VelocityCurve::linear, velocity) == velocity, "Linear plays every key as struck");

    std::set<std::string_view> ids;
    for (const auto& info : velocityCurves)
    {
        const auto name = std::string(info.name);
        require(ids.insert(info.id).second, name + " has an id of its own");
        require(velocityCurveFromId(info.id) == info.curve, name + " is read back from what it writes");
        require(applyVelocityCurve(info.curve, 0) == 0, name + " keeps a note-off a note-off");
        for (int velocity = 1; velocity <= 127; ++velocity)
        {
            const auto played = applyVelocityCurve(info.curve, velocity);
            require(played >= 1 && played <= 127, name + " never turns a struck key into a note-off");
            require(velocity == 1 || played >= applyVelocityCurve(info.curve, velocity - 1),
                    name + " never plays a harder strike quieter");
        }
        require(applyVelocityCurve(info.curve, 127) == 127, name + " still reaches full velocity");
        require(applyVelocityCurve(info.curve, 200) == 127, name + " takes a velocity past the range as the top");
    }
    require(velocityCurveFromId("") == VelocityCurve::linear && velocityCurveFromId("nonsense") == VelocityCurve::linear,
            "anything unrecognised reads as Linear");

    // The order the chooser lists them in is the order of touch: at every
    // point inside the range a softer curve plays at least as loud as the one
    // after it, and somewhere strictly louder.
    for (size_t index = 0; index + 2 < velocityCurves.size(); ++index)
    {
        const auto softer = velocityCurves[index].curve, harder = velocityCurves[index + 1].curve;
        auto louder = false;
        for (int velocity = 1; velocity <= 127; ++velocity)
        {
            require(applyVelocityCurve(softer, velocity) >= applyVelocityCurve(harder, velocity),
                    std::string(velocityCurves[index].name) + " plays at least as loud as "
                        + std::string(velocityCurves[index + 1].name));
            louder = louder || applyVelocityCurve(softer, velocity) > applyVelocityCurve(harder, velocity);
        }
        require(louder, std::string(velocityCurves[index].name) + " differs from the next curve");
    }

    // What the curves are for: a keyboard that needs a hard strike to reach
    // the top. Struck at half force, Soft is clearly louder and Softest loud.
    require(applyVelocityCurve(VelocityCurve::soft, 64) >= 75, "Soft lifts a half-force strike");
    require(applyVelocityCurve(VelocityCurve::softest, 64) >= 95, "Softest lifts it most");
    require(applyVelocityCurve(VelocityCurve::hard, 64) <= 50, "Hard asks for more force");
    for (int velocity = 1; velocity <= 127; ++velocity)
        require(applyVelocityCurve(VelocityCurve::fixed, velocity) == 127, "Fixed plays every note at full velocity");
}

}
