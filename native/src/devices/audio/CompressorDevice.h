#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>

namespace rhino
{
namespace te = tracktion::engine;

// Tracktion's compressor, sounding exactly as it does, that also says how far
// it is turning the audio down, for the gain reduction meter on its face
// (DeviceEditorPanelCompressor.cpp).
//
// The engine keeps its detector private, so nothing here models it. The
// compressor multiplies each frame by one gain, its Output gain times what the
// detector takes away, so the reduction is read off the audio itself: what
// came out over what went in, less the make-up. The meter therefore shows the
// gain that was applied and cannot drift from it.
//
// It has a type name of its own because the engine registers its built-ins
// before Rhino's and refuses a second type under a name it already has. A
// document saved with the engine's own "compressor" still opens, as that
// plugin, with no meter.
class CompressorDevice final : public te::CompressorPlugin
{
public:
    inline static const char* xmlTypeName = "rhino.compressor.v1";
    explicit CompressorDevice(te::PluginCreationInfo info) : te::CompressorPlugin(std::move(info)) {}

    juce::String getPluginType() override { return xmlTypeName; }
    void applyToBuffer(const te::PluginRenderContext&) override;

    // The deepest gain reduction since the last call, in decibels above zero,
    // 0 for none. Any thread; the face is its one reader, so a call spends it.
    float takeGainReductionDb() noexcept { return deepest.exchange(0.0f, std::memory_order_relaxed); }

    // How much quieter than this a frame may be and still be measured. The
    // engine adds and takes away 1.0 on every stereo frame to flush denormals,
    // which rounds a frame this quiet by up to a part in 1700, 0.005 dB, and
    // anything quieter by more; silence measures nothing at all.
    static constexpr float quietest = 1.0e-4f;
    // Less reduction than this, in decibels, is that rounding, not the
    // compressor at work, and reads as none.
    static constexpr float faintestDb = 0.01f;

private:
    std::atomic<float> deepest {0.0f};
};
}
