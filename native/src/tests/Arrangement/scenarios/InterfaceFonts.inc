// How text is rasterised, which is the one thing about this interface that
// cannot be checked by reading it.
//
// Two things make a line of text at ten pixels per em go soft, and both of
// them are invisible in the source. A JUCE font height is ascent plus descent,
// which the face divides down to an em size, so asking for a height of twelve
// rasterises Inter at 9.9 pixels per em and puts every stem between two pixel
// columns. And JUCE centres a line by computing its baseline in floats, so text
// in a box of the wrong height is drawn half a pixel low however whole the em
// size is. uiFont answers the first and drawSnappedText the second, and this is
// what holds both of them to it.
{
    Theme fontTheme;
    juce::LookAndFeel::setDefaultLookAndFeel(&fontTheme);

    // The shell font is whatever Windows hands over; the interface is drawn
    // with the face the app ships, so this checks the theme is serving it
    // rather than silently falling back.
    for (const auto& font : {uiFont(11.0f), uiFontBold(11.0f)})
        require(fontTheme.getTypefaceForFont(font) != nullptr
                    && fontTheme.getTypefaceForFont(font)->getName().containsIgnoreCase("Inter"),
                "The interface is drawn with the bundled face");

    // The em size is asked for directly, so what comes back is what was asked
    // for rather than a height divided down by whatever face is serving it.
    for (const auto em : {8.0f, 9.0f, 10.0f, 11.0f, 13.0f, 17.0f})
    {
        require(std::abs(uiFont(em).getHeightInPoints() - em) < 0.001f,
                "uiFont asks for a whole number of pixels per em");
        require(std::abs(uiFontBold(em).getHeightInPoints() - em) < 0.001f,
                "and so does uiFontBold");
        // Read off the style name, because juce::Font::isBold looks for "Bold"
        // as a whole word and answers false for "SemiBold" - which is exactly
        // how every bold label in this interface came to be drawn in Regular.
        require(fontTheme.getTypefaceForFont(uiFontBold(em))
                    != fontTheme.getTypefaceForFont(uiFont(em)),
                "Weight comes from a cut of the face rather than from a synthesised stroke");
        {
            const auto heavy = fontTheme.getTypefaceForFont(uiFontBold(em));
            const auto described = heavy->getName() + " " + heavy->getStyle();
            require(described.containsIgnoreCase("semi") || described.containsIgnoreCase("bold"),
                    described.toRawUTF8());
        }
    }

    // Every stock control asks its look and feel for a font, and the stock
    // answer is a height derived from the control's own size. These round it,
    // so a button label is as crisp as the text a panel draws for itself.
    {
        juce::TextButton button;
        juce::ComboBox box;
        for (const auto height : {18, 20, 22, 26, 30, 42})
        {
            button.setSize(80, height);
            box.setSize(80, height);
            for (const auto& font : {fontTheme.getTextButtonFont(button, height),
                                     fontTheme.getComboBoxFont(box),
                                     fontTheme.getPopupMenuFont()})
            {
                const auto em = font.getHeightInPoints();
                require(std::abs(em - std::round(em)) < 0.001f,
                        "A stock control's font is a whole number of pixels per em");
                require(em >= 7.0f, "and never shrunk past legibility");
            }
        }
    }

    // The baseline. Drawn into boxes one pixel apart in height, a snapped
    // baseline lands on a pixel row either way: the ink is the same, sitting
    // where it was or exactly one row lower. A float baseline moves half a row
    // instead, and half a row is a different antialiasing pattern - which is
    // the blur, stated as something that can be measured.
    {
        const juce::String sample {"Audio 2 MIDI 14"};
        const auto renderInBox = [&sample](int boxHeight)
        {
            juce::Image image(juce::Image::RGB, 160, 48, true);
            juce::Graphics g(image);
            g.fillAll(juce::Colours::black);
            g.setColour(juce::Colours::white);
            g.setFont(uiFontBold(10.0f));
            drawSnappedText(g, sample, {4, 4, 150, boxHeight}, juce::Justification::centredLeft);
            return image;
        };
        // Pixel for pixel, not a count per row: two different subpixel phases
        // can happen to light the same number of pixels in a row, and it is
        // the pattern that says whether the glyphs landed on the grid.
        const auto matchesUnderSomeWholeRowShift = [](const juce::Image& a, const juce::Image& b)
        {
            for (int shift = -4; shift <= 4; ++shift)
            {
                auto matches = true;
                for (int y = 0; y < a.getHeight() && matches; ++y)
                    for (int x = 0; x < a.getWidth(); ++x)
                    {
                        const auto row = y + shift;
                        const auto other = juce::isPositiveAndBelow(row, b.getHeight())
                            ? b.getPixelAt(x, row) : juce::Colours::black;
                        if (a.getPixelAt(x, y) != other) { matches = false; break; }
                    }
                if (matches) return true;
            }
            return false;
        };
        const auto reference = renderInBox(18);
        auto anyInk = false;
        for (int y = 0; y < reference.getHeight() && !anyInk; ++y)
            for (int x = 0; x < reference.getWidth(); ++x)
                if (reference.getPixelAt(x, y).getBrightness() > 0.5f) { anyInk = true; break; }
        require(anyInk, "The sample actually draws something to measure");
        for (const auto boxHeight : {19, 20, 21, 22, 23})
            require(matchesUnderSomeWholeRowShift(reference, renderInBox(boxHeight)),
                    "A taller box moves the same glyphs down whole rows, never half of one");
    }

    // The elision drawSnappedText does in place of drawText's, so a name too
    // long for its box ends in a dash rather than running out of it.
    {
        const auto font = uiFontBold(10.0f);
        const juce::String name {"A track name far too long for the card it is on"};
        const auto wide = juce::GlyphArrangement::getStringWidthInt(font, name);
        require(elidedToWidth(font, name, wide + 20) == name, "Text that fits is left alone");
        const auto elided = elidedToWidth(font, name, 80);
        require(elided != name && elided.isNotEmpty(), "and text that does not is shortened");
        require(juce::GlyphArrangement::getStringWidthInt(font, elided) <= 80, "to inside the width it was given");
        require(elided.endsWith(juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6"))), "and says so with an ellipsis");
        require(elidedToWidth(font, name, 0).isEmpty(), "A box with no width draws nothing rather than overflowing");
    }

    // The track card's name, which was the one string in the interface drawn
    // any other way: it used to be struck twice to make it heavier than the
    // rest of the card, and a second composite over the same glyphs darkens the
    // antialiasing fringe into the stem. This renders the card and compares it
    // against the same string drawn once, on the same ground, at the same
    // place - so a second strike coming back would fail here rather than being
    // noticed by eye months later.
    {
        view.sync();
        view.resized();
        // A selected card is washed a few percent lighter and an armed one has
        // a strip down its edge, so the comparison is made on a card that is
        // neither rather than reproducing either of them here.
        auto track = -1;
        for (int candidate = 0; candidate < session.trackCount() && track < 0; ++candidate)
            if (!view.isTrackSelected(candidate) && !view.isTrackArmed(candidate))
                track = candidate;
        require(track >= 0, "There is an unselected card to measure");
        const auto nameArea = view.trackNameBounds(track);
        require(!nameArea.isEmpty(), "The card has a name box");
        const auto cardColour = session.trackColour(track).isTransparent()
            ? palette::control.brighter(0.18f) : session.trackColour(track);
        const auto card = view.createComponentSnapshot(nameArea);

        juce::Image expected(juce::Image::RGB, nameArea.getWidth(), nameArea.getHeight(), true);
        {
            juce::Graphics g(expected);
            g.fillAll(cardColour);
            g.setColour(cardColour.contrasting(0.8f));
            g.setFont(uiFontBold(10.0f));
            drawSnappedText(g, juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track),
                            expected.getBounds(), juce::Justification::centredLeft, true);
        }
        int differences = 0;
        for (int y = 0; y < expected.getHeight(); ++y)
            for (int x = 0; x < expected.getWidth(); ++x)
                if (card.getPixelAt(x, y) != expected.getPixelAt(x, y)) ++differences;
        require(differences == 0, "A track card's name is one pass of the SemiBold cut, like every other label");
    }

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}
