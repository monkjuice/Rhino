        // Every other bar group is washed a shade lighter so the beats read as
        // bars. Only the time signature knows how wide a bar is, so the test
        // that matters is that the stripe moves when the meter does: the same
        // zoom and the same tempo must give three-beat columns in 3/4 and
        // four-beat columns in 4/4.
        view.sync();
        {
            view.setSize(1000, 246);
            // A lane with nothing on it: a clip is drawn over the wash, so
            // sampling a track that carries one measures the clip. The
            // starter stack's second lane is empty and, unlike a track added
            // at the bottom, on screen at this panel height.
            const auto bandTrack = 1;
            require(te::getAudioTracks(*session.edit)[bandTrack]->getClips().isEmpty(),
                    "The lane the wash is read off carries no clips");
            view.sync();
            // A known zoom, so pixels per bar is arithmetic rather than
            // whatever the last scenario left the view fitted to.
            view.viewStart = 0.0;
            view.viewSpan = 8.0;
            const auto secondsPerBeat = 60.0 / session.tempo();
            // Just below the lane's top edge rather than down its middle: an
            // empty track prints "Drop audio here" across the centre, and an
            // antialiased glyph pixel is not the lane colour.
            const auto laneY = static_cast<int>(view.lane(bandTrack).getY()) + 8;
            // Sampled mid-beat, off the grid lines: a line is one pixel wide
            // and the wash is a property of the lane between them.
            const auto shadeAtBeat = [&](const juce::Image& picture, double beat)
            {
                return picture.getPixelAt(static_cast<int>(view.xFor((beat + 0.5) * secondsPerBeat)), laneY);
            };
            const auto bandsOf = [&](int numerator, int denominator)
            {
                require(session.setTimeSignature(numerator, denominator).wasOk(), "Set the meter under the bands");
                view.sync();
                view.viewStart = 0.0;
                view.viewSpan = 8.0;
                const auto picture = view.createComponentSnapshot(view.getLocalBounds());
                const auto base = shadeAtBeat(picture, 0.0);
                std::vector<bool> washed;
                for (int beat = 0; beat < 12; ++beat)
                {
                    const auto here = shadeAtBeat(picture, beat);
                    // Two shades and no others: a third would mean the sample
                    // had wandered onto a line, a clip or a glyph.
                    require(here == base || here.getBrightness() > base.getBrightness(),
                            "A lane is either the base shade or the wash, and the wash is the lighter one");
                    washed.push_back(here != base);
                }
                return washed;
            };
            // One bar to a band, which is what makes the columns readable as
            // bars at all; the grouping rule is exercised on its own below.
            require(view.xFor(4.0 * secondsPerBeat) - view.xFor(0.0) >= barBandMinimumPixels,
                    "At this zoom a 4/4 bar is wide enough to stand as its own band");
            require(view.xFor(3.0 * secondsPerBeat) - view.xFor(0.0) >= barBandMinimumPixels,
                    "And so is a 3/4 bar");
            // Bar one is the base shade and bar two is washed, counted from bar
            // zero rather than from the left edge, so scrolling cannot flip it.
            const std::vector<bool> expectedFour {false, false, false, false, true, true, true, true,
                                                  false, false, false, false};
            require(bandsOf(4, 4) == expectedFour, "In 4/4 the wash covers four beats and alternates every bar");
            const std::vector<bool> expectedThree {false, false, false, true, true, true,
                                                   false, false, false, true, true, true};
            require(bandsOf(3, 4) == expectedThree, "In 3/4 the same zoom gives three-beat columns");
            // One beat to the bar is too narrow to read alone, so bars are
            // grouped in powers of two until a band is wide enough - which is
            // what keeps a 1/4 project from strobing.
            require(session.setTimeSignature(1, 4).wasOk(), "One beat to the bar");
            view.sync();
            view.viewStart = 0.0;
            view.viewSpan = 8.0;
            const auto bar = view.xFor(secondsPerBeat) - view.xFor(0.0);
            require(barsPerBand(bar) > 1 && barsPerBand(bar) * bar >= barBandMinimumPixels,
                    "A bar too narrow to read is banded with its neighbours");
            require(barsPerBand(bar) / 2 * bar < barBandMinimumPixels, "And not grouped more than it has to be");
            const std::vector<bool> expectedOne {false, false, false, false, true, true, true, true,
                                                 false, false, false, false};
            require(bandsOf(1, 4) == expectedOne, "Four one-beat bars make the band a 4/4 bar would have had");
            require(session.setTimeSignature(4, 4).wasOk(), "Leave the document in four four");
            view.sync();
            view.fit();
        }
        {
            // The two grid readouts are one setting, so they can never disagree
            // about the division or about whether the grid is on at all.
            view.gridSettings.mode = GridMode::fixed;
            view.gridSettings.fixedDivision = GridDivision::eighth;
            view.updateGridControl();
            require(view.toolbarGrid.getButtonText().contains("1/8")
                        && view.gridControl.getButtonText().contains("1/8"),
                    "Both grid readouts show the division in force");
            view.setGridEnabled(false);
            require(!view.snap.getToggleState(), "Turning the grid off releases the snap toggle");
            require(view.toolbarGrid.getButtonText().contains("Off")
                        && view.gridControl.getButtonText().contains("Off"),
                    "Both readouts say the grid is off rather than naming a division");
            view.setGridEnabled(true);
            require(view.gridSettings.mode == GridMode::fixed
                        && view.gridSettings.fixedDivision == GridDivision::eighth,
                    "Turning it back on restores the division it was left on, not a default");
            require(view.snap.getToggleState(), "And the snap toggle comes back with it");
        }
