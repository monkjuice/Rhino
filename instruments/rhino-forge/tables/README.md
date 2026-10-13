# Forge's factory wavetables

Ten tables, sixteen frames each. Pick one from the table field along the top of
an oscillator's display or on the `TABLE` tab: its arrows step through every
table here and a click opens them as a menu. Or load any file with **LOAD**, or
drop one on the editor.

The menu is this folder's tree. A subfolder becomes a submenu, as deep as the
folders go, so `Basic/basic-shapes.wav` is reached through **Basic**.

## Your own tables

**NEW** on the `TABLE` tab asks for a name, writes a blank one-frame table to
`user/`, and loads it to draw on. **SAVE** writes the table back to its file in
`user/`. A table that has no file there yet, such as a factory table, the
built-in ten or one drawn over, is saved as a new file instead, so the tables
here are never written over. `user/` is ignored by git, so what you make there
stays on your machine. A packaged build with no source tree keeps the same tree
in `Documents/Rhino Forge/Tables`.

They are ordinary `.wav` files: single-cycle frames of 2048 samples laid end to
end, 32-bit float, mono, each frame bipolar and scaled so its peak reaches full
scale. That is the format the wavetables people already own are written in, so
these open in Serum, Vital and Bitwig as readily as they open in Forge, and any
table written that way loads here.

The frame size is named in a `clm ` chunk, the way Serum names its own. Forge
reads that chunk; a file without one is assumed to be 2048, which is what every
file that carries no chunk is written to anyway. A file stored at some other
frame size is resampled onto Forge's 2048 on the way in.

| File | What it sweeps through |
| --- | --- |
| `basic-shapes` | sine → triangle → square → saw |
| `pulse-width` | a square closing to a sliver |
| `harmonic-stack` | a saw built up one harmonic at a time, 1 to 32 |
| `wavefolder` | a sine driven into more and more folds |
| `hard-sync` | an inner oscillator running up to 5× and cut off each cycle |
| `formant-sweep` | a vowel moving from back to front |
| `bell-partials` | partials sliding off the harmonic series |
| `bit-crush` | a sine quantised to fewer and fewer steps, 32 down to 3 |
| `comb-notch` | a saw against a delayed copy, the notches moving |
| `drawbars` | an organ with its upper bars pulled out |

## Regenerating them

[`make-tables.py`](make-tables.py) writes all ten. It needs nothing but a Python
interpreter, and the formulas in it are the authoritative description of what
each table is.

```powershell
python instruments/rhino-forge/tables/make-tables.py
```

Sixteen frames is a deliberate size: enough for POSITION to sweep through rather
than step across, few enough that every frame stays legible in the editor's
frame strip, and well under the sixty-four-frame ceiling a hand-edited table is
held to. A larger table loads, but anything past the ceiling is thinned evenly
rather than cut short, so it still sweeps from its first shape to its last.
