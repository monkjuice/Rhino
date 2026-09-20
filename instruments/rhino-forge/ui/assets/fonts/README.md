# Panel typefaces

Four faces, one per job on the panel:

| File | Used for |
| --- | --- |
| `ChakraPetchSemiBold.ttf` | Section headers and the page tabs |
| `RajdhaniMedium.ttf` | Control labels |
| `RajdhaniSemiBold.ttf` | Buttons and emphasised labels |
| `IBMPlexMonoMedium.ttf` | Numeric readouts |

All three families are under the SIL Open Font License 1.1, which permits
bundling them inside an application. The licences are here beside them:
`OFL-rajdhani.txt`, `OFL-chakrapetch.txt`, `OFL-ibmplexmono.txt`.

**These are subsets, not the upstream files.** Each was cut down with
`fontTools.subset` to Latin-1 plus the handful of symbols the panel actually
draws — degree, plus/minus, multiplication, arrows, the quotes and dashes —
keeping only the `kern`, `liga` and `tnum` features. That takes the four of
them from 988 KB to 79 KB, which matters because `juce_add_binary_data` expands
an asset to roughly three bytes of C++ per byte of data and rebuilds all of it
on a clean build.

To re-cut them after an upstream change, fetch from
`https://raw.githubusercontent.com/google/fonts/main/ofl/<family>/` and run:

```
python -m fontTools.subset <face>.ttf \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2013-2014,U+2018-201D,U+2022,U+2026,U+2032-2033,U+00B0,U+00B1,U+00D7,U+2212,U+00B5,U+2192,U+2190" \
  --layout-features="kern,liga,tnum" --no-hinting --desubroutinize \
  --output-file=<Face>.ttf
```

The subset deliberately has no CJK. The unit mark in the title bar is drawn in
the system's default face for exactly that reason — it is the one string on the
panel that relies on the platform's own font fallback.
