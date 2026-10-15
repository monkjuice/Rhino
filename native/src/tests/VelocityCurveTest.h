#pragma once

namespace rhino
{
// A keyboard's velocity curves (model/VelocityCurve.h): throws
// std::runtime_error naming the first thing that is wrong. Run by the
// geometry runner, which needs no Session and no audio device.
void checkVelocityCurves();
}
