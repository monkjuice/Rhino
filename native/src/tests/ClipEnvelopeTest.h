#pragma once

namespace rhino
{
// An audio clip's envelopes as dsp/ClipEnvelope.h states them: throws
// std::runtime_error naming the first thing that is wrong. Run by the
// geometry runner, which needs no Session and no audio device.
void checkClipEnvelopeRules();
}
