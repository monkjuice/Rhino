# Third-party notices

Rhino's own code, presets and documentation are licensed under the GNU Affero General Public License, version 3 or later ([LICENSE](LICENSE)). The work of others that Rhino builds on or ships keeps its own licence, listed here.

## Fetched at build time

`native/scripts/fetch-dependencies.py` downloads these into `native/.deps/`, which git does not track. Each checkout carries its full licence.

| Project | Revision | Licence | Used by |
| --- | --- | --- | --- |
| [JUCE](https://github.com/juce-framework/JUCE) | `37c894f` | AGPLv3, or the commercial JUCE 8 licence. JUCE bundles third-party code under its own licences, among them the VST3 SDK (MIT), listed in JUCE's `LICENSE.md` | Rhino, Rhino Forge, Rhino Duck |
| [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) | `4536d8a` | GPLv3 or later, or a commercial licence | Rhino |
| [SoundTouch](https://www.surina.net/soundtouch/), bundled with Tracktion Engine | | LGPL 2.1 | Rhino's time warp |
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch), bundled with Tracktion Engine | | MIT | Rhino's time warp |
| [zstd](https://github.com/facebook/zstd) | `f8745da` | BSD 3-Clause (dual-licensed with GPLv2) | Rhino Forge, to open Serum 2 presets |

## In this repository

### Fonts

All under the SIL Open Font License 1.1, with the licence beside the font files.

- Inter: `native/assets/fonts/OFL.txt`
- Rajdhani, Chakra Petch and IBM Plex Mono: `OFL-*.txt` in `instruments/rhino-forge/ui/assets/fonts/` and `effects/rhino-duck/ui/assets/fonts/`

### Samples

Each pack under `library/Samples/` records its source and licence beside it.

- `Cumbia/` and `HandClap/`: made for Rhino by Kmila (Camila Arenas) and licensed under the GNU AGPL, version 3 or later, like Rhino itself (`SOURCE.md`)
- `TR808/`: recordings of a Roland TR-808 from [tidalcycles/sounds-tr808-fischer](https://github.com/tidalcycles/sounds-tr808-fischer), CC0 1.0 (`LICENSE-CC0.txt`, `SOURCE.md`)
- `VinylDrums/`: the *Giant Vinyl Drums Pack* by Dmitriy "Cyberworm" Vasilyev, published royalty free at rhythm-lab.com (`SOURCE.md`)

The TR808 and VinylDrums packs are not covered by the AGPL.

### Research snapshots

`research/sources/` holds excerpts of other projects' source, kept as study material and neither compiled nor imported. Each file keeps its upstream notice, and [research/SOURCE_MAP.md](research/SOURCE_MAP.md) links the exact upstream commit it came from.

- Ardour: GPLv2 or later
- LMMS: GPLv2 or later
- Zrythm: AGPLv3 or later, with additional trademark terms
- Tracktion Engine: GPLv3 or later
