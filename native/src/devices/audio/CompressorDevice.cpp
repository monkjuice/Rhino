#include "CompressorDevice.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace rhino
{
namespace
{
// The block is handed to the engine's compressor a piece at a time, with each
// piece's input kept on the stack to divide its output by. The compressor
// carries its detector from one call to the next and reads its parameters at
// the head of each, which do not change within a block, so the pieces come
// out bit for bit as one call would.
constexpr int pieceLength = 64;
}

void CompressorDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    const auto channels = context.destBuffer != nullptr ? std::min(2, context.destBuffer->getNumChannels()) : 0;
    if (channels == 0 || context.bufferNumSamples <= 0)
    {
        CompressorPlugin::applyToBuffer(context);
        return;
    }
    auto& audio = *context.destBuffer;
    // The smallest gain applied to any frame of the block, less the make-up.
    auto least = 1.0f;
    std::array<std::array<float, pieceLength>, 2> heard;
    te::PluginRenderContext piece(context);
    for (int done = 0; done < context.bufferNumSamples; done += pieceLength)
    {
        piece.bufferStartSample = context.bufferStartSample + done;
        piece.bufferNumSamples = std::min(pieceLength, context.bufferNumSamples - done);
        for (int channel = 0; channel < channels; ++channel)
            std::copy_n(audio.getReadPointer(channel, piece.bufferStartSample), piece.bufferNumSamples,
                        heard[static_cast<size_t>(channel)].begin());
        const auto makeUp = te::dbToGain(outputDb.getCurrentValue());
        CompressorPlugin::applyToBuffer(piece);
        for (int i = 0; i < piece.bufferNumSamples; ++i)
        {
            // Both channels take the same gain, so the louder one measures it
            // more exactly.
            const auto channel = channels > 1 && std::abs(heard[1][static_cast<size_t>(i)])
                                                     > std::abs(heard[0][static_cast<size_t>(i)]) ? 1 : 0;
            const auto in = heard[static_cast<size_t>(channel)][static_cast<size_t>(i)];
            if (std::abs(in) < quietest)
                continue;
            least = std::min(least, audio.getSample(channel, piece.bufferStartSample + i) / (in * makeUp));
        }
    }
    if (!(least > 0.0f))
        return;
    const auto reduction = -juce::Decibels::gainToDecibels(least);
    if (reduction < faintestDb)
        return;
    auto previous = deepest.load(std::memory_order_relaxed);
    while (reduction > previous && !deepest.compare_exchange_weak(previous, reduction, std::memory_order_relaxed))
    {
    }
}
}
