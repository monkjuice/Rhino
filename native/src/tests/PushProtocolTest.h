#pragma once

namespace rhino
{
// The Push's protocol and Rhino's arithmetic on it (app/PushProtocol.h):
// throws std::runtime_error naming the first thing that is wrong. Run by the
// geometry runner, which needs no Session and no audio device.
void checkPushProtocol();
}
