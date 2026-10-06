#include "DeviceEditorPanel.h"
#include "Theme.h"
#include <algorithm>
#include <map>

// The face a device on the SDK gets when it has none of its own, generated
// from what its controls declare (Session::DeviceParameter):
//
// - Controls group by section, in the order a section first appears, under
//   its title. A section a device leaves unnamed has no title.
// - Sections that name the same tab group share one place and show one at a
//   time, under a row of tabs, so four operators do not make a wall of knobs.
// - A continuous control is a knob, which steps and skews the way it declares
//   and goes back to its default on a double-click. A choice is a chooser, and
//   a toggle is a switch.
// - A group fills columns two controls tall when the face has several places
//   or a long group. Otherwise its controls stand in one row of larger knobs.
// - Whatever the device describes beside its controls -- a diagram, traces --
//   stands after the first group (DeviceEditorPanelDisplay.cpp).
//
// Nothing here knows a device by name. A new device gets a usable, consistent
// face from its declarations alone, and draws its own only when it has
// something true to show from its DSP.
namespace rhino
{
namespace
{
constexpr int padding = 6;
constexpr int sectionGap = 12;
constexpr int titleHeight = 13;
constexpr int tabPadding = 9;       // either side of a tab's name
constexpr int choiceWidth = 116;
constexpr int compactWidth = 54;    // a knob or a switch, two rows to a column
constexpr int roomyWidth = 66;      // the same, in a single row

struct Group
{
    juce::String title;
    juce::String tabGroup;
    std::vector<int> controls;
};

// What stands in one place on the face: a section, or every section of a tab
// group, of which one shows at a time.
struct Block
{
    std::vector<Group> sections;
    bool tabbed() const { return sections.size() > 1; }
};

std::vector<Block> blocksOf(const std::vector<Session::DeviceParameter>& parameters, int count)
{
    std::vector<Group> groups;
    for (int i = 0; i < count; ++i)
    {
        const auto& parameter = parameters[static_cast<size_t>(i)];
        auto found = std::find_if(groups.begin(), groups.end(),
                                  [&parameter] (const Group& group) { return group.title == parameter.section; });
        if (found == groups.end())
        {
            groups.push_back({ parameter.section, parameter.tabGroup, {} });
            found = groups.end() - 1;
        }
        found->controls.push_back(i);
    }
    std::vector<Block> blocks;
    for (auto& group : groups)
    {
        auto found = group.tabGroup.isEmpty() ? blocks.end()
            : std::find_if(blocks.begin(), blocks.end(),
                           [&group] (const Block& block) { return block.sections.front().tabGroup == group.tabGroup; });
        if (found != blocks.end())
            found->sections.push_back(std::move(group));
        else
            blocks.push_back({ { std::move(group) } });
    }
    return blocks;
}

int rowsFor(const Group& group, size_t blockCount)
{
    const auto controls = group.controls.size();
    return controls > 6 || (blockCount > 1 && controls > 3) ? 2 : 1;
}

int widthOf(const Session::DeviceParameter& parameter, int rows)
{
    if (!parameter.choices.isEmpty() && !parameter.toggle)
        return choiceWidth;
    return rows > 1 ? compactWidth : roomyWidth;
}

// Controls fill a group's columns top to bottom, and each column is as wide as
// the widest control in it.
std::vector<int> columnWidths(const Group& group, int rows, const std::vector<Session::DeviceParameter>& parameters)
{
    std::vector<int> widths;
    for (size_t k = 0; k < group.controls.size(); ++k)
    {
        const auto column = k / static_cast<size_t>(rows);
        if (widths.size() <= column)
            widths.push_back(0);
        widths[column] = std::max(widths[column], widthOf(parameters[static_cast<size_t>(group.controls[k])], rows));
    }
    return widths;
}

juce::Font tabFont()
{
    return uiFontBold(7.5f);
}

int tabWidth(const juce::String& section)
{
    return juce::GlyphArrangement::getStringWidthInt(tabFont(), section.toUpperCase()) + tabPadding * 2;
}

// As wide as its widest section, so changing tabs never moves the rest of the
// face, and never narrower than its row of tabs.
int blockWidth(const Block& block, size_t blockCount, const std::vector<Session::DeviceParameter>& parameters)
{
    auto width = 0, tabs = 0;
    for (const auto& section : block.sections)
    {
        auto columns = 0;
        for (const auto column : columnWidths(section, rowsFor(section, blockCount), parameters))
            columns += column;
        width = std::max(width, columns);
        tabs += tabWidth(section.title);
    }
    return block.tabbed() ? std::max(width, tabs) : width;
}

bool hasTitles(const std::vector<Block>& blocks)
{
    return std::any_of(blocks.begin(), blocks.end(),
                       [] (const Block& block) { return block.sections.front().title.isNotEmpty(); });
}

// Every tab choice made this run, by device and tab group. Kept for the life of
// the app, not saved: which operator is open is how the face was left, not
// part of the song.
std::map<juce::String, juce::String>& rememberedTabs()
{
    static std::map<juce::String, juce::String> tabs;
    return tabs;
}
}

bool DeviceEditorPanel::isKnob(int parameter) const
{
    if (!juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size())))
        return true;
    const auto& control = parameters[static_cast<size_t>(parameter)];
    return control.choices.isEmpty() && !control.toggle;
}

juce::Colour DeviceEditorPanel::faceAccent() const
{
    if (const auto* entry = DeviceCatalog::byId(deviceId); entry != nullptr && entry->colour != 0)
        return juce::Colour(entry->colour);
    return palette::deviceAccent;
}

// The section last chosen in a tab group, or else the group's first.
juce::String DeviceEditorPanel::selectedTab(const juce::String& tabGroup) const
{
    juce::String first;
    auto chosenExists = false;
    const auto remembered = rememberedTabs().find(deviceKey + "/" + tabGroup);
    for (const auto& parameter : parameters)
    {
        if (parameter.tabGroup != tabGroup)
            continue;
        if (first.isEmpty())
            first = parameter.section;
        if (remembered != rememberedTabs().end() && parameter.section == remembered->second)
            chosenExists = true;
    }
    return chosenExists ? remembered->second : first;
}

void DeviceEditorPanel::selectTab(const juce::String& tabGroup, const juce::String& section)
{
    if (tabGroup.isEmpty() || selectedTab(tabGroup) == section)
        return;
    rememberedTabs()[deviceKey + "/" + tabGroup] = section;
    styleControls();
    styleGeneratedControls();
    resized();
    repaint();
}

bool DeviceEditorPanel::onHiddenTab(int parameter) const
{
    if (!juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size())))
        return false;
    const auto& control = parameters[static_cast<size_t>(parameter)];
    return control.tabGroup.isNotEmpty() && control.section != selectedTab(control.tabGroup);
}

int DeviceEditorPanel::generatedWidth() const
{
    const auto blocks = blocksOf(parameters, visibleParameterCount());
    auto width = padding * 2;
    for (size_t g = 0; g < blocks.size(); ++g)
    {
        width += blockWidth(blocks[g], blocks.size(), parameters);
        if (g > 0)
            width += sectionGap;
    }
    if (!display.empty())
        width += displayWidth() + (blocks.empty() ? 0 : sectionGap);
    return std::max(190, width);
}

void DeviceEditorPanel::ensureGeneratedControls()
{
    while (generatedChoices.size() < visibleParameterCount())
    {
        const auto index = generatedChoices.size();
        auto* choice = generatedChoices.add(new juce::ComboBox());
        choice->setJustificationType(juce::Justification::centred);
        choice->onChange = [this, index, choice]
        {
            if (syncing || choice->getSelectedId() == 0)
                return;
            writeParameter(index, static_cast<float>(choice->getSelectedId() - 1));
        };
        addChildComponent(choice);

        auto* toggle = generatedToggles.add(new juce::TextButton());
        toggle->onClick = [this, index]
        {
            const auto on = juce::isPositiveAndBelow(index, static_cast<int>(parameters.size()))
                && parameters[static_cast<size_t>(index)].value >= 0.5f;
            writeParameter(index, on ? 0.0f : 1.0f);
        };
        addChildComponent(toggle);
    }
}

void DeviceEditorPanel::styleGeneratedControls()
{
    const auto count = visibleParameterCount();
    const auto accent = faceAccent();
    syncing = true;
    for (int i = 0; i < generatedChoices.size(); ++i)
    {
        auto* choice = generatedChoices[i];
        auto* toggle = generatedToggles[i];
        if (i >= count || onHiddenTab(i))
        {
            choice->setVisible(false);
            toggle->setVisible(false);
            continue;
        }
        const auto& parameter = parameters[static_cast<size_t>(i)];
        const auto isChoice = !parameter.choices.isEmpty() && !parameter.toggle;
        const auto isToggle = parameter.toggle;
        choice->setVisible(isChoice);
        toggle->setVisible(isToggle);
        const auto tooltip = parameter.name + ": " + parameter.valueText + ". Right-click for automation.";

        if (isChoice)
        {
            // A panel is reused for whatever device it is shown next, so a
            // chooser's list is rebuilt only when it no longer matches.
            auto matches = choice->getNumItems() == parameter.choices.size();
            for (int item = 0; matches && item < parameter.choices.size(); ++item)
                matches = choice->getItemText(item) == parameter.choices[item];
            if (!matches)
            {
                choice->clear(juce::dontSendNotification);
                for (int item = 0; item < parameter.choices.size(); ++item)
                    choice->addItem(parameter.choices[item], item + 1);
            }
            choice->setSelectedId(juce::roundToInt(parameter.value) + 1, juce::dontSendNotification);
            choice->setTooltip(tooltip);
            choice->setColour(juce::ComboBox::backgroundColourId, palette::control);
            choice->setColour(juce::ComboBox::outlineColourId, palette::border);
            choice->setColour(juce::ComboBox::textColourId, palette::text);
            choice->setColour(juce::ComboBox::arrowColourId, accent);
        }
        if (isToggle)
        {
            const auto on = parameter.value >= 0.5f;
            toggle->setButtonText(parameter.valueText);
            toggle->setToggleState(on, juce::dontSendNotification);
            toggle->setTooltip(tooltip);
            toggle->setColour(juce::TextButton::buttonColourId, palette::control);
            toggle->setColour(juce::TextButton::buttonOnColourId, accent.darker(0.62f));
            toggle->setColour(juce::TextButton::textColourOffId, palette::textDim);
            toggle->setColour(juce::TextButton::textColourOnId, palette::text);
        }
    }
    syncing = false;
}

void DeviceEditorPanel::layoutGenerated()
{
    visualArea = {};
    displayArea = {};
    const auto count = visibleParameterCount();
    const auto blocks = blocksOf(parameters, count);
    const auto titled = hasTitles(blocks);
    generatedSections.clear();
    auto x = contentArea.getX() + padding;
    const auto top = contentArea.getY() + (titled ? titleHeight : 0);
    const auto height = contentArea.getBottom() - top;
    const auto placeDisplay = [this, &x]
    {
        const auto width = displayWidth();
        layoutDisplay({ x, contentArea.getY(), width, contentArea.getHeight() }, titleHeight);
        x += width;
    };
    if (blocks.empty() && !display.empty())
        placeDisplay();
    for (size_t g = 0; g < blocks.size(); ++g)
    {
        if (g > 0)
            x += sectionGap;
        const auto& block = blocks[g];
        const auto chosen = block.tabbed() ? selectedTab(block.sections.front().tabGroup) : juce::String();
        const auto shown = std::find_if(block.sections.begin(), block.sections.end(),
                                        [&chosen] (const Group& section) { return section.title == chosen; });
        const auto& group = shown != block.sections.end() ? *shown : block.sections.front();
        const auto rows = rowsFor(group, blocks.size());
        const auto widths = columnWidths(group, rows, parameters);
        const auto cellHeight = height / rows;
        const auto width = blockWidth(block, blocks.size(), parameters);
        GeneratedSection placed { group.title, { x, contentArea.getY(), width, contentArea.getHeight() }, {}, {} };
        if (block.tabbed())
        {
            placed.tabGroup = group.tabGroup;
            auto tabX = x;
            for (const auto& section : block.sections)
            {
                const auto tab = tabWidth(section.title);
                placed.tabs.push_back({ section.title, { tabX, contentArea.getY(), tab, titleHeight } });
                tabX += tab;
            }
        }
        generatedSections.push_back(std::move(placed));

        auto columnX = x;
        for (size_t k = 0; k < group.controls.size(); ++k)
        {
            const auto column = k / static_cast<size_t>(rows);
            const auto row = static_cast<int>(k % static_cast<size_t>(rows));
            if (k > 0 && row == 0)
                columnX += widths[column - 1];
            const auto i = group.controls[k];
            const juce::Rectangle<int> cell(columnX, top + row * cellHeight, widths[column], cellHeight);
            auto inner = cell.reduced(2, 0);
            parameterLabels[i]->setBounds(inner.removeFromTop(14));
            if (isKnob(i))
            {
                parameterValues[i]->setBounds(inner.removeFromBottom(13));
                const auto size = juce::jlimit(26, 54, std::min(inner.getWidth() - 12, inner.getHeight() - 2));
                parameterSliders[i]->setBounds(inner.withSizeKeepingCentre(size, size));
                parameterAutomation[i]->setBounds(parameterSliders[i]->getRight() - 8, parameterSliders[i]->getY() - 2, 16, 14);
            }
            else
            {
                // The A button stands beside a chooser or a switch rather
                // than over its caption, which a short cell has no room for.
                auto* control = parameters[static_cast<size_t>(i)].toggle
                    ? static_cast<juce::Component*>(generatedToggles[i]) : generatedChoices[i];
                auto line = inner.withSizeKeepingCentre(inner.getWidth(), 22);
                parameterAutomation[i]->setBounds(line.removeFromRight(16).withSizeKeepingCentre(16, 14));
                line.removeFromRight(3);
                control->setBounds(line);
            }
        }
        x += width;
        if (g == 0 && !display.empty())
        {
            x += sectionGap;
            placeDisplay();
        }
    }
}

void DeviceEditorPanel::paintGenerated(juce::Graphics& g)
{
    const auto accent = faceAccent();
    const auto separator = [&g] (int left, const juce::Rectangle<int>& area)
    {
        g.setColour(palette::border);
        g.fillRect(left - sectionGap / 2, area.getY() + 3, 1, area.getHeight() - 6);
    };
    for (size_t s = 0; s < generatedSections.size(); ++s)
    {
        const auto& section = generatedSections[s];
        // A knob turning repaints only itself, and its section's titles and
        // tabs are not worth measuring and drawing again for it.
        if (!g.clipRegionIntersects(section.area.withTrimmedLeft(-sectionGap)))
            continue;
        if (s > 0)
            separator(section.area.getX(), section.area);
        g.setFont(tabFont());
        if (section.tabs.empty())
        {
            if (section.title.isEmpty())
                continue;
            g.setColour(palette::textDim);
            drawSnappedText(g, section.title.toUpperCase(), section.area.withHeight(titleHeight),
                            juce::Justification::centredLeft, true);
            continue;
        }
        // The tab showing is lit and underlined. A tab whose section the
        // display says is heard -- an FM carrier -- carries a dot.
        for (const auto& [name, area] : section.tabs)
        {
            const auto showing = name == section.title;
            g.setColour(showing ? palette::text : palette::textDim);
            drawSnappedText(g, name.toUpperCase(), area, juce::Justification::centred);
            if (showing)
            {
                g.setColour(accent);
                g.fillRect(area.getX() + 3, area.getBottom() - 1, area.getWidth() - 6, 2);
            }
            const auto heard = std::any_of(display.blocks.begin(), display.blocks.end(),
                                           [&name] (const DeviceDisplay::Block& block)
                                           { return block.output && block.section == name; });
            if (heard)
            {
                g.setColour(accent);
                g.fillEllipse(static_cast<float>(area.getX()) + 2.0f, static_cast<float>(area.getCentreY()) - 2.0f,
                              4.0f, 4.0f);
            }
        }
    }
    if (!displayArea.isEmpty())
    {
        if (!generatedSections.empty())
            separator(displayArea.getX(), displayArea);
        paintDisplay(g);
    }
}

// A click on a tab shows its section, and so does a click on a block of the
// display that names a section in a tab group.
bool DeviceEditorPanel::handleGeneratedClick(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();
    for (const auto& section : generatedSections)
        for (const auto& [name, area] : section.tabs)
            if (area.expanded(0, 2).contains(position))
            {
                selectTab(section.tabGroup, name);
                return true;
            }
    if (const auto block = displayBlockAt(position.toFloat()); block >= 0)
    {
        const auto& wanted = display.blocks[static_cast<size_t>(block)].section;
        for (const auto& parameter : parameters)
            if (parameter.section == wanted && parameter.tabGroup.isNotEmpty())
            {
                selectTab(parameter.tabGroup, wanted);
                return true;
            }
    }
    return false;
}

juce::String DeviceEditorPanel::roleOfSection(const juce::String& section) const
{
    for (const auto& block : display.blocks)
        if (block.section == section)
            return block.role;
    return {};
}

// The display's blocks and the tabs say what they are when pointed at, which
// is where a carrier is told from a modulator in words.
juce::String DeviceEditorPanel::getTooltip()
{
    if (face == Face::DrumRack)
        if (const auto tip = drumTooltip(getMouseXYRelative()); tip.isNotEmpty())
            return tip;
    if (face == Face::Generated)
    {
        const auto position = getMouseXYRelative();
        if (const auto block = displayBlockAt(position.toFloat()); block >= 0)
        {
            const auto& shown = display.blocks[static_cast<size_t>(block)];
            return shown.section + (shown.role.isNotEmpty() ? ": " + shown.role : juce::String())
                 + ". Click to show its controls.";
        }
        for (const auto& section : generatedSections)
            for (const auto& [name, area] : section.tabs)
                if (area.contains(position))
                {
                    const auto role = roleOfSection(name);
                    return name + (role.isNotEmpty() ? ": " + role : juce::String());
                }
    }
    return SettableTooltipClient::getTooltip();
}

int DeviceEditorPanel::generatedParameterForComponent(const juce::Component* component) const
{
    for (int i = 0; i < generatedChoices.size(); ++i)
    {
        if (generatedChoices[i]->isVisible()
            && (component == generatedChoices[i] || generatedChoices[i]->isParentOf(component)))
            return i;
        if (generatedToggles[i]->isVisible()
            && (component == generatedToggles[i] || generatedToggles[i]->isParentOf(component)))
            return i;
    }
    return -1;
}
}
