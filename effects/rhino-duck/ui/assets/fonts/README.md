# Panel typefaces

Three faces, one per job on the panel:

| File | Used for |
| --- | --- |
| `ChakraPetchSemiBold.ttf` | The name on the panel and the chips |
| `RajdhaniSemiBold.ttf` | Captions and readings' labels |
| `IBMPlexMonoMedium.ttf` | Numbers that change while you watch |

They are the very subsets Rhino Forge carries (`instruments/rhino-forge/ui/assets/fonts/`),
copied rather than referenced so this project stands on its own: Latin-1 plus a
few symbols, cut with `fontTools.subset` as that folder's README describes. All
three families are under the SIL Open Font License 1.1, which permits bundling
them inside an application; the licences are beside them.
