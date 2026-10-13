#include "ForgeProcessor.h"

namespace rhino::forge
{
namespace
{
juce::ValueTree lfoNode(int index, const LfoTable& table, const juce::String& name)
{
    juce::ValueTree node("ForgeLfoTable");
    node.setProperty("index", index, nullptr);
    node.setProperty("name", name, nullptr);
    node.setProperty("custom", table.custom, nullptr);
    node.setProperty("columns", table.columns, nullptr);
    node.setProperty("rows", table.rows, nullptr);
    for (int i = 0; i < table.count; ++i)
    {
        const auto& source = table.points[static_cast<size_t>(i)];
        juce::ValueTree point("Point");
        point.setProperty("x", source.x, nullptr);
        point.setProperty("y", source.y, nullptr);
        // A straight segment writes nothing, so a table drawn before curves
        // existed and one drawn since with no bend are the same file, and a
        // table read here by eye still shows only what was actually bent.
        if (source.curve != 0.0f) point.setProperty("curve", source.curve, nullptr);
        if (source.curveAt != 0.5f) point.setProperty("curveAt", source.curveAt, nullptr);
        node.addChild(point, -1, nullptr);
    }
    return node;
}

bool readLfoNode(const juce::ValueTree& node, LfoTable& table)
{
    if (!node.hasType("ForgeLfoTable") || node.getNumChildren() > maxLfoPoints) return false;
    LfoTable next;
    next.custom = static_cast<bool>(node.getProperty("custom", true));
    next.count = node.getNumChildren();
    next.columns = static_cast<int>(node.getProperty("columns", 8));
    next.rows = static_cast<int>(node.getProperty("rows", 8));
    for (int i = 0; i < next.count; ++i)
    {
        const auto point = node.getChild(i);
        if (!point.hasType("Point") || !point.hasProperty("x") || !point.hasProperty("y")) return false;
        next.points[static_cast<size_t>(i)] = {static_cast<float>(point.getProperty("x")),
                                                static_cast<float>(point.getProperty("y")),
                                                static_cast<float>(point.getProperty("curve", 0.0f)),
                                                static_cast<float>(point.getProperty("curveAt", 0.5f))};
    }
    if (next.custom ? !next.valid() : next.count != 0 || next.columns < 2 || next.columns > 32
                                          || next.rows < 2 || next.rows > 32) return false;
    table = next;
    return true;
}

// A drawn path travels in the patch the same way a drawn table does: one node
// per LFO that has one, holding its points in order.
juce::ValueTree lfoPathNode(int index, const LfoPath& path)
{
    juce::ValueTree node("ForgeLfoPath");
    node.setProperty("index", index, nullptr);
    for (int i = 0; i < path.count; ++i)
    {
        juce::ValueTree point("Point");
        point.setProperty("x", path.points[static_cast<size_t>(i)].x, nullptr);
        point.setProperty("y", path.points[static_cast<size_t>(i)].y, nullptr);
        node.addChild(point, -1, nullptr);
    }
    return node;
}

bool readLfoPathNode(const juce::ValueTree& node, LfoPath& path)
{
    if (!node.hasType("ForgeLfoPath") || node.getNumChildren() > maxLfoPoints) return false;
    LfoPath next;
    next.custom = true;
    next.count = node.getNumChildren();
    for (int i = 0; i < next.count; ++i)
    {
        const auto point = node.getChild(i);
        if (!point.hasType("Point") || !point.hasProperty("x") || !point.hasProperty("y")) return false;
        next.points[static_cast<size_t>(i)] = {static_cast<float>(point.getProperty("x")),
                                               static_cast<float>(point.getProperty("y"))};
    }
    if (!next.valid()) return false;
    path = next;
    return true;
}
}

LfoSteps Processor::lfoSteps(int lfo) const
{
    LfoSteps result {};
    if (lfo < 0 || lfo >= lfoCount) return result;
    const auto& published = meterLfoStep[static_cast<size_t>(lfo)];
    for (size_t i = 0; i < result.size(); ++i) result[i] = published[i].load(std::memory_order_relaxed);
    return result;
}

LfoPath Processor::lfoPath(int lfo) const
{
    LfoPath result;
    if (lfo < 0 || lfo >= lfoCount) return result;
    const auto index = static_cast<size_t>(lfo);
    result.count = std::clamp(lfoPathCount[index].load(std::memory_order_relaxed), 0, maxLfoPoints);
    for (int i = 0; i < result.count; ++i)
        result.points[static_cast<size_t>(i)] = {
            lfoPathX[index][static_cast<size_t>(i)].load(std::memory_order_relaxed),
            lfoPathY[index][static_cast<size_t>(i)].load(std::memory_order_relaxed)};
    result.custom = lfoPathCustom[index].load(std::memory_order_relaxed);
    return result;
}

// The same hand-over the table uses: the points land first and the custom
// flag last, so the audio thread reads the default path until the whole of
// the drawn one is there.
void Processor::setLfoPath(int lfo, const LfoPath& path)
{
    if (lfo < 0 || lfo >= lfoCount || (path.custom && !path.valid())) return;
    const auto index = static_cast<size_t>(lfo);
    lfoPathCustom[index].store(false, std::memory_order_relaxed);
    for (int i = 0; i < path.count; ++i)
    {
        lfoPathX[index][static_cast<size_t>(i)].store(path.points[static_cast<size_t>(i)].x,
                                                      std::memory_order_relaxed);
        lfoPathY[index][static_cast<size_t>(i)].store(path.points[static_cast<size_t>(i)].y,
                                                      std::memory_order_relaxed);
    }
    lfoPathCount[index].store(path.count, std::memory_order_relaxed);
    lfoPathCustom[index].store(path.custom, std::memory_order_release);
}

LfoTable Processor::lfoTable(int lfo) const
{
    LfoTable result;
    if (lfo < 0 || lfo >= lfoCount) return result;
    const auto index = static_cast<size_t>(lfo);
    result.count = lfoPointCount[index].load(std::memory_order_relaxed);
    result.columns = lfoColumns[index].load(std::memory_order_relaxed);
    result.rows = lfoRows[index].load(std::memory_order_relaxed);
    if (result.columns == 0) result.columns = 8;
    if (result.rows == 0) result.rows = 8;
    result.count = std::clamp(result.count, 0, maxLfoPoints);
    for (int i = 0; i < result.count; ++i)
        result.points[static_cast<size_t>(i)] = {
            lfoPointX[index][static_cast<size_t>(i)].load(std::memory_order_relaxed),
            lfoPointY[index][static_cast<size_t>(i)].load(std::memory_order_relaxed),
            lfoPointCurve[index][static_cast<size_t>(i)].load(std::memory_order_relaxed),
            lfoPointCurveAt[index][static_cast<size_t>(i)].load(std::memory_order_relaxed)};
    result.custom = lfoCustom[index].load(std::memory_order_relaxed);
    return result;
}

void Processor::setLfoTable(int lfo, const LfoTable& table, const juce::String& name)
{
    if (lfo < 0 || lfo >= lfoCount || (table.custom && !table.valid())) return;
    const auto index = static_cast<size_t>(lfo);
    lfoCustom[index].store(false, std::memory_order_relaxed);
    for (int i = 0; i < table.count; ++i)
    {
        lfoPointX[index][static_cast<size_t>(i)].store(table.points[static_cast<size_t>(i)].x,
                                                       std::memory_order_relaxed);
        lfoPointY[index][static_cast<size_t>(i)].store(table.points[static_cast<size_t>(i)].y,
                                                       std::memory_order_relaxed);
        lfoPointCurve[index][static_cast<size_t>(i)].store(table.points[static_cast<size_t>(i)].curve,
                                                          std::memory_order_relaxed);
        lfoPointCurveAt[index][static_cast<size_t>(i)].store(table.points[static_cast<size_t>(i)].curveAt,
                                                            std::memory_order_relaxed);
    }
    lfoPointCount[index].store(table.count, std::memory_order_relaxed);
    lfoColumns[index].store(table.columns, std::memory_order_relaxed);
    lfoRows[index].store(table.rows, std::memory_order_relaxed);
    {
        const juce::ScopedLock lock(lfoNameLock);
        lfoNames[index] = table.custom ? name : "Default";
    }
    lfoCustom[index].store(table.custom, std::memory_order_release);
}

juce::String Processor::lfoTableName(int lfo) const
{
    if (lfo < 0 || lfo >= lfoCount || !lfoCustom[static_cast<size_t>(lfo)].load(std::memory_order_relaxed))
        return "Default";
    const juce::ScopedLock lock(lfoNameLock);
    return lfoNames[static_cast<size_t>(lfo)];
}

void Processor::appendLfoTables(juce::ValueTree& tree) const
{
    for (int i = 0; i < lfoCount; ++i)
    {
        const auto table = lfoTable(i);
        if ((table.custom && table.valid()) || table.columns != 8 || table.rows != 8)
            tree.addChild(lfoNode(i, table, lfoTableName(i)), -1, nullptr);
        // The default path is the same in every copy of Forge, so only a
        // drawn one is worth writing.
        const auto path = lfoPath(i);
        if (path.custom && path.valid()) tree.addChild(lfoPathNode(i, path), -1, nullptr);
    }
}

void Processor::applyLfoTables(const juce::ValueTree& tree)
{
    for (int i = 0; i < lfoCount; ++i)
    {
        LfoTable table;
        LfoPath path;
        juce::String name("Default");
        for (const auto child : tree)
        {
            const auto forThis = static_cast<int>(child.getProperty("index", -1)) == i;
            if (child.hasType("ForgeLfoTable") && forThis)
                if (readLfoNode(child, table)) name = child.getProperty("name", "Custom").toString();
            if (child.hasType("ForgeLfoPath") && forThis) readLfoPathNode(child, path);
        }
        setLfoTable(i, table, name);
        setLfoPath(i, path);
    }
}

juce::Result Processor::saveLfoTable(int lfo, const juce::File& destination) const
{
    if (lfo < 0 || lfo >= lfoCount || destination == juce::File {})
        return juce::Result::fail("Choose an LFO table file.");
    const auto table = lfoTable(lfo);
    if (!table.custom || !table.valid()) return juce::Result::fail("Draw a custom LFO first.");
    const auto xml = lfoNode(0, table, destination.getFileNameWithoutExtension()).createXml();
    juce::TemporaryFile temporary(destination);
    if (xml == nullptr || !xml->writeTo(temporary.getFile(), {})
        || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Forge could not save the LFO table.");
    return juce::Result::ok();
}

juce::Result Processor::loadLfoTable(int lfo, const juce::File& source)
{
    if (lfo < 0 || lfo >= lfoCount) return juce::Result::fail("Choose an LFO.");
    const auto xml = juce::XmlDocument::parse(source);
    if (xml == nullptr) return juce::Result::fail("That LFO table is not readable XML.");
    LfoTable table;
    if (!readLfoNode(juce::ValueTree::fromXml(*xml), table) || !table.custom)
        return juce::Result::fail("That file is not a valid Forge LFO table.");
    setLfoTable(lfo, table, source.getFileNameWithoutExtension());
    return juce::Result::ok();
}
}
