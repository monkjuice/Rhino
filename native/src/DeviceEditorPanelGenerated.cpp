#include "DeviceEditorPanel.h"
#include "Theme.h"
#include <algorithm>

// The face a device on the SDK gets when it has none of its own, generated
// from what its controls declare (Session::DeviceParameter):
//
// - Controls group by section, in the order a section first appears, under
//   its title. A section a device leaves unnamed has no title.
// - A continuous control is a knob, which steps and skews the way it declares
//   and goes back to its default on a double-click. A choice is a chooser, and
//   a toggle is a switch.
// - A group fills columns two controls tall when the face has several
//   sections or a long one. Otherwise its controls stand in one row of
//   larger knobs.
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
constexpr int choiceWidth = 116;
constexpr int compactWidth = 54;    // a knob or a switch, two rows to a column
constexpr int roomyWidth = 66;      // the same, in a single row

struct Group
{
    juce::String title;
    std::vector<int> controls;
};

std::vector<Group> groupsOf(const std::vector<Session::DeviceParameter>& parameters, int count)
{
    std::vector<Group> groups;
    for (int i = 0; i < count; ++i)
    {
        const auto& section = parameters[static_cast<size_t>(i)].section;
        auto found = std::find_if(groups.begin(), groups.end(), [&section] (const Group& group) { return group.title == section; });
        if (found == groups.end())
        {
            groups.push_back({ section, {} });
            found = groups.end() - 1;
        }
        found->controls.push_back(i);
    }
    return groups;
}

int rowsFor(const Group& group, size_t groupCount)
{
    const auto controls = group.controls.size();
    return controls > 6 || (groupCount > 1 && controls > 3) ? 2 : 1;
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

bool hasTitles(const std::vector<Group>& groups)
{
    return std::any_of(groups.begin(), groups.end(), [] (const Group& group) { return group.title.isNotEmpty(); });
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

int DeviceEditorPanel::generatedWidth() const
{
    const auto groups = groupsOf(parameters, visibleParameterCount());
    auto width = padding * 2;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const auto rows = rowsFor(groups[g], groups.size());
        for (const auto column : columnWidths(groups[g], rows, parameters))
            width += column;
        if (g > 0)
            width += sectionGap;
    }
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
        if (i >= count)
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
    const auto count = visibleParameterCount();
    const auto groups = groupsOf(parameters, count);
    const auto titled = hasTitles(groups);
    generatedSections.clear();
    auto x = contentArea.getX() + padding;
    const auto top = contentArea.getY() + (titled ? titleHeight : 0);
    const auto height = contentArea.getBottom() - top;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        if (g > 0)
            x += sectionGap;
        const auto& group = groups[g];
        const auto rows = rowsFor(group, groups.size());
        const auto widths = columnWidths(group, rows, parameters);
        const auto cellHeight = height / rows;
        auto sectionWidth = 0;
        for (const auto width : widths)
            sectionWidth += width;
        generatedSections.push_back({ group.title, { x, contentArea.getY(), sectionWidth, contentArea.getHeight() } });

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
                auto row = inner.withSizeKeepingCentre(inner.getWidth(), 22);
                parameterAutomation[i]->setBounds(row.removeFromRight(16).withSizeKeepingCentre(16, 14));
                row.removeFromRight(3);
                control->setBounds(row);
            }
        }
        x += sectionWidth;
    }
}

void DeviceEditorPanel::paintGenerated(juce::Graphics& g)
{
    for (size_t s = 0; s < generatedSections.size(); ++s)
    {
        const auto& section = generatedSections[s];
        if (s > 0)
        {
            g.setColour(palette::border);
            const auto x = section.area.getX() - sectionGap / 2;
            g.fillRect(x, section.area.getY() + 3, 1, section.area.getHeight() - 6);
        }
        if (section.title.isEmpty())
            continue;
        g.setColour(palette::textDim);
        g.setFont(uiFontBold(7.5f));
        drawSnappedText(g, section.title.toUpperCase(), section.area.withHeight(titleHeight),
                        juce::Justification::centredLeft, true);
    }
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
