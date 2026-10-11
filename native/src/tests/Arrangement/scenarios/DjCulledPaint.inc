        // The clips grid repaints the cells that changed: a hover two, a sync
        // that found the cells as they were none, a clip made in a cell that
        // cell. And a repaint of part of a console, of the mixer or of the
        // grid draws exactly what drawing everything would have under the
        // same clip, over a colour none of them uses, so a word the culling
        // forgets cannot pass for one it drew (as PartialRepaint.inc holds
        // the arrangement and the note editor).
        {
            auto& cells = djView->decks[1]->grid;
            require(cells.hasTrack() && cells.isVisible() && cells.rows() >= 4, "The lane's console shows its clips grid");
            auto& damage = recordDamage(cells);
            const auto cellCentre = [&cells](int scene) { return cells.cellBounds(scene).getCentre().toFloat(); };
            cells.mouseMove(mouseAt(cells, cellCentre(0), cellCentre(0), 30));
            damage.region.clear();
            cells.mouseMove(mouseAt(cells, cellCentre(2), cellCentre(2), 31));
            require(damage.region.containsRectangle(cells.cellBounds(0)) && damage.region.containsRectangle(cells.cellBounds(2))
                        && !damage.region.intersectsRectangle(cells.cellBounds(1)) && !damage.region.intersectsRectangle(cells.cellBounds(3)),
                    "A hover moving to another cell repaints the two cells and none between or beyond");
            cells.mouseExit(mouseAt(cells, cellCentre(2), cellCentre(2), 32));
            damage.region.clear();
            cells.sync();
            require(damage.region.isEmpty(), "A sync that finds the cells as they were repaints none");
            require(booth.createSlotClip(0, 3).wasOk(), "A clip made in the lane's fourth cell");
            djView->sync();
            require(!damage.region.isEmpty() && cells.cellBounds(3).contains(damage.region.getBounds()),
                    "repaints that cell alone");
            require(booth.deleteSlotClip(0, 3).wasOk(), "and the cell is cleared again");
            djView->sync();
            cells.setCachedComponentImage(nullptr);
        }
        {
            const auto paintUnder = [](juce::Component& panel, juce::Image& canvas, juce::Rectangle<int> area)
            {
                juce::Graphics g(canvas);
                g.reduceClipRegion(area);
                g.fillAll(juce::Colour(0xffff00ff));
                panel.paintEntireComponent(g, false);
            };
            const auto checkAreas = [&](juce::Component& panel, bool& cull, const char* name, std::vector<juce::Rectangle<int>> areas)
            {
                for (int x = 0; x < panel.getWidth(); x += 23)
                    areas.push_back({x, 0, 3, panel.getHeight()});
                for (int y = 0; y < panel.getHeight(); y += 17)
                    areas.push_back({0, y, panel.getWidth(), 2});
                juce::Image everything(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true, juce::SoftwareImageType());
                juce::Image culled(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true, juce::SoftwareImageType());
                for (const auto& area : areas)
                {
                    cull = false;
                    paintUnder(panel, everything, area);
                    cull = true;
                    paintUnder(panel, culled, area);
                    const juce::Image::BitmapData expected(everything, juce::Image::BitmapData::readOnly);
                    const juce::Image::BitmapData drawn(culled, juce::Image::BitmapData::readOnly);
                    for (int y = std::max(0, area.getY()); y < std::min(panel.getHeight(), area.getBottom()); ++y)
                        for (int x = std::max(0, area.getX()); x < std::min(panel.getWidth(), area.getRight()); ++x)
                            if (expected.getPixelColour(x, y) != drawn.getPixelColour(x, y))
                                require(false, (juce::String(name) + ": repainting " + area.toString() + " drew pixel "
                                                + juce::Point<int>(x, y).toString() + " as " + drawn.getPixelColour(x, y).toDisplayString(true)
                                                + " where drawing everything gives "
                                                + expected.getPixelColour(x, y).toDisplayString(true)).toRawUTF8());
                }
            };
            for (auto& console : djView->decks)
            {
                std::vector<juce::Rectangle<int>> seams {console->headerArea(), console->tempoCaption.box(), console->brakeCaption.box(),
                                                         console->tempo.getBounds(), console->jog.getBounds().removeFromTop(6)};
                checkAreas(*console, console->cullRepaints, "A console", seams);
            }
            auto& mixer = djView->mixer;
            std::vector<juce::Rectangle<int>> mixerSeams {mixer.bpmArea, mixer.beatArea, mixer.faderCurveCaption.box(),
                                                          mixer.aLine.box(), mixer.bLine.box(), mixer.masterTitle.box()};
            for (auto& strip : mixer.strips)
                for (const auto& caption : strip->captions)
                    mixerSeams.push_back(caption.box().expanded(1));
            checkAreas(mixer, mixer.cullRepaints, "The mixer", mixerSeams);
            auto& cells = djView->decks[1]->grid;
            checkAreas(cells, cells.cullRepaints, "The clips grid", {cells.cellBounds(DjClipGrid::songCell), cells.cellBounds(0).withHeight(3)});
        }
