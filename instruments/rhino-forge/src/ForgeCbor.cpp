#include "ForgeCbor.h"
#include <cmath>
#include <cstring>

namespace rhino::forge
{
namespace
{
class CborReader
{
public:
    CborReader(const juce::uint8* begin, size_t size) : at(begin), end(begin + size) {}

    bool read(juce::var& into, int depth = 0)
    {
        if (depth > 64 || at >= end) return false;
        const auto initial = *at++;
        const auto major = initial >> 5;
        const auto info = initial & 0x1f;

        if (major == 7) return simple(info, into);
        if (info == 31)
            return indefinite(major, into, depth);
        juce::uint64 argument = 0;
        if (!length(info, argument)) return false;

        switch (major)
        {
            case 0: into = static_cast<juce::int64>(argument); return argument <= 0x7fffffffffffffffull;
            case 1: into = -1 - static_cast<juce::int64>(argument); return argument <= 0x7fffffffffffffffull;
            case 2:
            {
                if (argument > static_cast<juce::uint64>(end - at)) return false;
                into = juce::var(juce::MemoryBlock(at, static_cast<size_t>(argument)));
                at += argument;
                return true;
            }
            case 3:
            {
                if (argument > static_cast<juce::uint64>(end - at)) return false;
                into = juce::String::fromUTF8(reinterpret_cast<const char*>(at), static_cast<int>(argument));
                at += argument;
                return true;
            }
            case 4:
            {
                if (argument > static_cast<juce::uint64>(end - at)) return false;
                juce::Array<juce::var> items;
                items.ensureStorageAllocated(static_cast<int>(argument));
                for (juce::uint64 i = 0; i < argument; ++i)
                {
                    juce::var item;
                    if (!read(item, depth + 1)) return false;
                    items.add(std::move(item));
                }
                into = items;
                return true;
            }
            case 5:
            {
                if (argument > static_cast<juce::uint64>(end - at)) return false;
                auto* object = new juce::DynamicObject();
                into = juce::var(object);
                for (juce::uint64 i = 0; i < argument; ++i)
                    if (!entry(*object, depth)) return false;
                return true;
            }
            case 6: return read(into, depth + 1);
            default: break;
        }
        return false;
    }

    bool finished() const { return at == end; }

private:
    const juce::uint8* at;
    const juce::uint8* end;

    bool bytes(int count, juce::uint64& value)
    {
        if (end - at < count) return false;
        value = 0;
        for (int i = 0; i < count; ++i) value = (value << 8) | *at++;
        return true;
    }

    bool length(int info, juce::uint64& value)
    {
        if (info < 24) { value = static_cast<juce::uint64>(info); return true; }
        if (info == 24) return bytes(1, value);
        if (info == 25) return bytes(2, value);
        if (info == 26) return bytes(4, value);
        if (info == 27) return bytes(8, value);
        return false;
    }

    bool entry(juce::DynamicObject& object, int depth)
    {
        juce::var key, value;
        if (!read(key, depth + 1) || !read(value, depth + 1)) return false;
        const auto name = key.isString() ? key.toString() : juce::String(static_cast<juce::int64>(key));
        if (name.isEmpty()) return false;
        object.setProperty(juce::Identifier(name), value);
        return true;
    }

    bool indefinite(int major, juce::var& into, int depth)
    {
        const auto isBreak = [this] { return at < end && *at == 0xff; };
        if (major == 2 || major == 3)
        {
            juce::MemoryBlock joined;
            while (!isBreak())
            {
                juce::var chunk;
                if (at >= end || (*at >> 5) != major || !read(chunk, depth + 1)) return false;
                if (major == 2) joined.append(chunk.getBinaryData()->getData(), chunk.getBinaryData()->getSize());
                else joined.append(chunk.toString().toRawUTF8(), chunk.toString().getNumBytesAsUTF8());
            }
            ++at;
            into = major == 2 ? juce::var(joined) : juce::var(joined.toString());
            return true;
        }
        if (major == 4)
        {
            juce::Array<juce::var> items;
            while (!isBreak())
            {
                juce::var item;
                if (!read(item, depth + 1)) return false;
                items.add(std::move(item));
            }
            ++at;
            into = items;
            return true;
        }
        if (major == 5)
        {
            auto* object = new juce::DynamicObject();
            into = juce::var(object);
            while (!isBreak())
                if (!entry(*object, depth)) return false;
            ++at;
            return true;
        }
        return false;
    }

    bool simple(int info, juce::var& into)
    {
        juce::uint64 raw = 0;
        switch (info)
        {
            case 20: into = false; return true;
            case 21: into = true; return true;
            case 22: case 23: into = juce::var(); return true;
            case 25:
            {
                if (!bytes(2, raw)) return false;
                into = halfToDouble(static_cast<juce::uint16>(raw));
                return true;
            }
            case 26:
            {
                if (!bytes(4, raw)) return false;
                const auto bits = static_cast<juce::uint32>(raw);
                float value;
                std::memcpy(&value, &bits, sizeof(value));
                into = static_cast<double>(value);
                return true;
            }
            case 27:
            {
                if (!bytes(8, raw)) return false;
                double value;
                std::memcpy(&value, &raw, sizeof(value));
                into = value;
                return true;
            }
            default: break;
        }
        // Other simple values carry nothing a patch uses.
        if (info < 24) { into = juce::var(); return true; }
        if (info == 24) { into = juce::var(); return bytes(1, raw); }
        return false;
    }

    static double halfToDouble(juce::uint16 half)
    {
        const auto exponent = (half >> 10) & 0x1f;
        const auto mantissa = half & 0x3ff;
        double value;
        if (exponent == 0) value = std::ldexp(mantissa, -24);
        else if (exponent != 31) value = std::ldexp(mantissa + 1024, exponent - 25);
        else value = mantissa == 0 ? std::numeric_limits<double>::infinity() : std::nan("");
        return (half & 0x8000) != 0 ? -value : value;
    }
};
}

juce::Result readCbor(const void* data, size_t size, juce::var& result)
{
    if (data == nullptr || size == 0) return juce::Result::fail("The patch is empty.");
    CborReader reader(static_cast<const juce::uint8*>(data), size);
    juce::var read;
    if (!reader.read(read)) return juce::Result::fail("The patch could not be read.");
    result = read;
    return juce::Result::ok();
}
}
