#pragma once

namespace rhino
{
// The note editor's velocity lane arithmetic (ui/notes/VelocityLaneGeometry.h):
// throws std::runtime_error naming the first thing that is wrong. Run by the
// geometry runner, which needs no Session and no audio device.
void checkVelocityLane();
}
