#pragma once
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace rhino
{
// A JUCE font height is ascent plus descent, which the face then divides down
// to an em size: ask for a height of 12 and Inter is rasterised at 9.9 pixels
// per em. A fractional em is what puts stems between pixel columns, and with
// greyscale antialiasing and no grid fitting there is nothing to pull them
// back onto it. These ask for the em size directly, in whole pixels.
inline juce::Font uiFont(float pixelsPerEm)
{
    return juce::Font(juce::FontOptions().withPointHeight(pixelsPerEm));
}

inline juce::Font uiFontBold(float pixelsPerEm)
{
    return juce::Font(juce::FontOptions().withPointHeight(pixelsPerEm).withStyle("SemiBold"));
}

// Shortens a line with an ellipsis when it will not fit the width given. JUCE's
// own drawText does this, so anything replacing a drawText call needs it too or
// a long name simply runs out of its box instead of ending in a dash.
inline juce::String elidedToWidth(const juce::Font& font, const juce::String& text, int width)
{
    if (width <= 0) return {};
    if (juce::GlyphArrangement::getStringWidthInt(font, text) <= width) return text;
    static const juce::String ellipsis {juce::CharPointer_UTF8("\xe2\x80\xa6")};
    const auto room = width - juce::GlyphArrangement::getStringWidthInt(font, ellipsis);
    if (room <= 0) return {};
    // Binary search rather than a character at a time: a name is measured once
    // per repaint, and measuring is the expensive half of drawing it.
    int low = 0, high = text.length();
    while (low < high)
    {
        const auto mid = (low + high + 1) / 2;
        if (juce::GlyphArrangement::getStringWidthInt(font, text.substring(0, mid)) <= room)
            low = mid;
        else
            high = mid - 1;
    }
    return text.substring(0, low).trimEnd() + ellipsis;
}

// The other half of it: JUCE centres a line by computing its baseline in
// floats, so text centred in a box of the wrong height is drawn half a pixel
// low however whole the em size is. This rounds the baseline to a pixel row.
//
// Every line of text in the interface goes through here. One path, one baseline
// rule: a glyph run drawn any other way lands on whatever fractional row JUCE
// computes for it, and at ten pixels per em that is the difference between a
// crisp stem and a grey one.
inline void drawSnappedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                            juce::Justification justification = juce::Justification::centredLeft,
                            bool elide = false)
{
    const auto font = g.getCurrentFont();
    const auto line = elide ? elidedToWidth(font, text, area.getWidth()) : text;
    if (line.isEmpty()) return;
    const auto baseline = area.getY()
        + juce::roundToInt((static_cast<float>(area.getHeight()) + font.getAscent() - font.getDescent()) * 0.5f);
    auto x = area.getX();
    if (justification.testFlags(juce::Justification::horizontallyCentred))
        x = area.getCentreX() - juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, line) * 0.5f);
    else if (justification.testFlags(juce::Justification::right))
        x = area.getRight() - juce::GlyphArrangement::getStringWidthInt(font, line);
    g.drawSingleLineText(line, x, baseline);
}

class Theme final : public juce::LookAndFeel_V4
{
public:
    // The app ships its own UI face rather than inheriting the Windows shell
    // font. JUCE renders text with greyscale antialiasing and no grid fitting,
    // so at the sizes this interface uses a face drawn for screen - large
    // x-height, flat terminals, sturdy stems - stays legible where the shell
    // font goes soft. Glyphs the Latin cut does not carry, such as the
    // transport symbols, still reach the system fallback.
    // Which cut a font asks for, read off its style name rather than through
    // juce::Font::isBold(). That looks for "Bold" as a *whole word*, and
    // "SemiBold" is one word - so it answers false for the style uiFontBold
    // sets, and this handed back the Regular cut for every bold label in the
    // interface. Nothing looked obviously wrong, because a heading in Regular
    // is still legible; it is why the track card's name was struck twice to
    // get its weight back, and why that one string then looked soft.
    static bool wantsHeavierCut(const juce::Font& font)
    {
        return font.getTypefaceStyle().containsIgnoreCase("bold");
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        const auto heavy = wantsHeavierCut(font);
        auto& cached = heavy ? semiBold : regular;
        if (cached == nullptr)
            cached = juce::Typeface::createSystemTypefaceFor(
                heavy ? BinaryData::InterSemiBold_ttf : BinaryData::InterRegular_ttf,
                heavy ? static_cast<size_t>(BinaryData::InterSemiBold_ttfSize)
                      : static_cast<size_t>(BinaryData::InterRegular_ttfSize));
        return cached != nullptr ? cached : juce::LookAndFeel_V4::getTypefaceForFont(font);
    }

    // Every stock control asks its look-and-feel for a font, and the stock
    // answer is a *height* derived from the control's own size - so a 30 pixel
    // button asks for 16 and Inter rasterises it at 13.2 pixels per em. These
    // three are the ones this interface actually uses; each rounds that height
    // to a whole number of pixels per em so a button label, a combo box and a
    // menu row are as crisp as the text the panels draw for themselves.
    static juce::Font snapped(float heightInPixels)
    {
        return uiFont(std::max(7.0f, std::round(heightInPixels * 0.825f)));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return snapped(std::min(16.0f, static_cast<float>(buttonHeight) * 0.6f));
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override
    {
        return snapped(std::min(16.0f, static_cast<float>(box.getHeight()) * 0.7f));
    }

    juce::Font getPopupMenuFont() override
    {
        return uiFont(11.0f);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto diameter = static_cast<float>(std::min(width, height)) - 5.0f;
        const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height))
                              .withSizeKeepingCentre(diameter, diameter);
        const auto radius = area.getWidth() * 0.5f;
        const auto centre = area.getCentre();
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto face = slider.findColour(juce::Slider::backgroundColourId);
        const auto accent = slider.findColour(juce::Slider::trackColourId);
        const auto marker = slider.findColour(juce::Slider::thumbColourId);

        g.setColour(juce::Colour(0x33000000));
        g.fillEllipse(area.translated(0.0f, 1.5f).expanded(1.0f));
        g.setColour(face.brighter(0.05f));
        g.fillEllipse(area);
        g.setColour(face.darker(0.62f));
        g.fillEllipse(area.reduced(radius * 0.18f));
        g.setColour(face.brighter(0.55f).withAlpha(0.28f));
        g.drawEllipse(area.reduced(1.0f), 1.0f);

        const auto arc = area.reduced(4.0f);
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(centre.x, centre.y, arc.getWidth() * 0.5f, arc.getHeight() * 0.5f,
                                    0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(face.brighter(0.32f).withAlpha(0.55f));
        g.strokePath(backgroundArc, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arc.getWidth() * 0.5f, arc.getHeight() * 0.5f,
                               0.0f, rotaryStartAngle, angle, true);
        g.setColour(accent);
        g.strokePath(valueArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto pointAt = [centre](float a, float r)
        {
            return centre + juce::Point<float>(std::sin(a), -std::cos(a)) * r;
        };
        for (int i = 0; i < 8; ++i)
        {
            const auto tickAngle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 8.0f;
            const auto major = i % 2 == 0;
            const auto inner = pointAt(tickAngle, radius * (major ? 0.31f : 0.35f));
            const auto outer = pointAt(tickAngle, radius * (major ? 0.49f : 0.45f));
            juce::Path tick;
            tick.startNewSubPath(inner);
            tick.lineTo(outer);
            g.setColour(juce::Colour(0xffb9c4cb).withAlpha(major ? 0.86f : 0.42f));
            g.strokePath(tick, juce::PathStrokeType(major ? 1.7f : 1.2f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        juce::Path pointer;
        pointer.startNewSubPath(0.0f, -radius * 0.08f);
        pointer.lineTo(0.0f, -radius * 0.39f);
        g.setColour(marker);
        g.strokePath(pointer, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
        g.setColour(juce::Colour(0xff151b20));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(centre));
        g.setColour(marker.withAlpha(0.68f));
        g.drawEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(centre), 1.0f);
    }

    // A bar slider draws its value inside the bar, where the default label
    // font is larger than the bar it sits in. Only bars are touched, so the
    // rotary and horizontal sliders elsewhere keep their own text.
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
        const auto style = slider.getSliderStyle();
        if (label != nullptr && (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical))
            label->setFont(uiFont(9.0f));
        return label;
    }


    // The stock bar keeps its fill a half pixel clear of its own frame and
    // then outlines it a whole pixel wide, which at this size reads as a box
    // with a bar inside it. This is the bar: an unfilled trough, the value
    // filled edge to edge, and a hairline to separate it from the card.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (!slider.isBar())
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                   minSliderPos, maxSliderPos, style, slider);
            return;
        }
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto trough = slider.findColour(juce::Slider::backgroundColourId);
        const auto fill = slider.findColour(juce::Slider::trackColourId);
        const auto filled = slider.isHorizontal() ? bounds.withRight(sliderPos) : bounds.withTop(sliderPos);
        const auto unfilled = slider.isHorizontal() ? bounds.withLeft(sliderPos) : bounds.withBottom(sliderPos);
        g.setColour(trough);
        g.fillRect(bounds);
        g.setColour(fill);
        g.fillRect(filled);
        g.setColour(slider.findColour(juce::Slider::textBoxOutlineColourId));
        g.drawRect(bounds, 0.6f);

        // A value that crosses the end of the fill would be half legible in
        // either single colour, so it is drawn once per ground with each half
        // clipped to the ground it sits on. The colours come from the grounds
        // themselves, so any fader colour stays readable without a second set
        // of constants to keep in step. A bar that kept its own text box is
        // left alone, or the two would be drawn over each other.
        if (slider.getTextBoxPosition() != juce::Slider::NoTextBox) return;
        const auto text = slider.getTextFromValue(slider.getValue());
        if (text.isEmpty()) return;
        const auto area = bounds.toNearestInt();
        g.setFont(uiFont(9.0f));
        for (const auto& ground : {std::make_pair(filled, fill), std::make_pair(unfilled, trough)})
        {
            if (ground.first.isEmpty()) continue;
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(ground.first.toNearestInt());
            g.setColour(ground.second.contrasting(0.92f));
            drawSnappedText(g, text, area, juce::Justification::centred);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& background, bool highlighted, bool pressed) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = background.withMultipliedSaturation(button.hasKeyboardFocus(true) ? 1.3f : 0.9f)
                              .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
        if (pressed || highlighted) fill = fill.contrasting(pressed ? 0.2f : 0.05f);

        juce::Path shape;
        shape.addRectangle(bounds);
        g.setColour(fill);
        g.fillPath(shape);
        g.setColour(button.findColour(juce::ComboBox::outlineColourId));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        auto fill = box.findColour(juce::ComboBox::backgroundColourId)
                       .withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f);
        if (isButtonDown) fill = fill.contrasting(0.12f);
        g.setColour(fill);
        g.fillRect(0, 0, width, height);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRect(0, 0, width, height);

        const auto arrow = juce::Rectangle<float>(static_cast<float>(buttonX), static_cast<float>(buttonY),
                                                 static_cast<float>(buttonW), static_cast<float>(buttonH)).reduced(8.0f, 9.0f);
        juce::Path path;
        path.startNewSubPath(arrow.getX(), arrow.getY());
        path.lineTo(arrow.getCentreX(), arrow.getBottom());
        path.lineTo(arrow.getRight(), arrow.getY());
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.strokePath(path, juce::PathStrokeType(1.2f));
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
    {
        g.setColour(textEditor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRect(0, 0, width, height);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
    {
        const auto colour = textEditor.hasKeyboardFocus(true)
            ? textEditor.findColour(juce::TextEditor::focusedOutlineColourId)
            : textEditor.findColour(juce::TextEditor::outlineColourId);
        g.setColour(colour);
        g.drawRect(0, 0, width, height);
    }

private:
    juce::Typeface::Ptr regular, semiBold;
};
}
