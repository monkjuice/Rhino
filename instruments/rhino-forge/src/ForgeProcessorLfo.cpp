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
        juce::ValueTree point("Point");
        point.setProperty("x", table.points[static_cast<size_t>(i)].x, nullptr);
        point.setProperty("y", table.points[static_cast<size_t>(i)].y, nullptr);
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
                                                static_cast<float>(point.getProperty("y"))};
    }
    if (next.custom ? !next.valid() : next.count != 0 || next.columns < 2 || next.columns > 32
                                          || next.rows < 2 || next.rows > 32) return false;
    table = next;
    return true;
}
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
            lfoPointY[index][static_cast<size_t>(i)].load(std::memory_order_relaxed)};
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
    }
}

void Processor::applyLfoTables(const juce::ValueTree& tree)
{
    for (int i = 0; i < lfoCount; ++i)
    {
        LfoTable table;
        juce::String name("Default");
        for (const auto child : tree)
            if (child.hasType("ForgeLfoTable") && static_cast<int>(child.getProperty("index", -1)) == i)
                if (readLfoNode(child, table)) name = child.getProperty("name", "Custom").toString();
        setLfoTable(i, table, name);
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
