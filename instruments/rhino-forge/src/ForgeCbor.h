#pragma once

#include <juce_core/juce_core.h>

// Reading CBOR (RFC 8949) into a juce::var, which is what a Serum 2 preset's
// patch is once its zstd frame is opened.
//
// Maps become DynamicObjects keyed by their keys as text (an integer key is
// written out in decimal), arrays become arrays, byte strings MemoryBlocks,
// integers int64 and every float a double. Tags are read through: the item a
// tag wraps is kept and the tag dropped. Indefinite lengths are accepted.
// Anything malformed or nested deeper than any patch needs fails the whole
// read rather than returning half of it.
namespace rhino::forge
{
juce::Result readCbor(const void* data, size_t size, juce::var& result);
}
