---
title: Development environment and reference material
type: guide
summary: What the Windows development machine offers (shells, CMake, Python, ffmpeg, PDF tools, the reference manuals) and the habits it demands.
tags: [both, environment, windows, tooling]
sources: []
updated: 2026-10-07
---

# Development environment and reference material

One Windows 10 machine hosts the developer and the coding agents, working in the same tree at once. What follows was
checked there on 2026-10-03; build prerequisites are in [Build and test Rhino](build-and-test-rhino.md).

## Tools

- **Shells:** Git Bash, and Windows PowerShell 5.1, which has no `&&` (chain with `; if ($?) { ... }`).
- **CMake** is installed system-wide and on `PATH`.
- **Python 3.13** with `numpy`, Pillow and `tkinter`: enough to decode a `.wav`, sample PNG pixels and run the
  `synth-ab` capture window.
- **ffmpeg and ffplay** from a build in the Downloads folder, on `PATH` under Git Bash; another copy ships inside an app
  under `%LOCALAPPDATA%\Programs`. Look in both before concluding ffmpeg is missing. It decodes what `wave` refuses,
  such as 32-bit float WAVs.
- **PDFs:** `pdftotext -layout -f N -l M` works (Git Bash's `/mingw64/bin`). `pdftoppm` is absent, so the Read tool
  cannot render a page: install PyMuPDF into the scratchpad (`python -m pip install --target <scratch>\pylibs pymupdf`,
  about two minutes), save `page.get_pixmap(dpi=140)` as a PNG and read that.
- **Git LFS** is installed; the audio under `library/` needs it.

## Reference material

Both manuals are in the Downloads folder, and in both the printed page number is the PDF page number.

- **Serum 2 User Guide** (`Serum 2 User Guide.pdf`), Forge's north star. Its page map is in
  [Comparing Forge with Serum](comparing-forge-with-serum.md).
- **Ableton Live 12 manual** (`live12-manual-en.pdf`, 1009 pages), the reference when a Rhino device is modelled on a
  Live one. Auto Shift is pp. 537-544.

Read the figures too: the screenshots carry layout the prose never states, such as a channel strip's control order.

## Habits the setup demands

- **Bash-tool heredocs collapse backslashes**, even with a quoted delimiter, and truncate long payloads. A patch script
  matching `"\n"` in C++ then fails silently or writes a broken literal (`error C2001`). Write anything longer than a
  few lines, or holding a backslash, to a file with the Write tool and run it by path.
- **Commit messages go through a file in PowerShell 5.1.** A `git commit -m @'...'@` message that contains double
  quotes is split into extra arguments, and git fails with "pathspec ... did not match". Write the message to a file in
  the scratchpad and run `git commit -F <file>`. Do not hide the commit's stderr with `2>$null`: on 2026-10-03 that hid
  the failure until `git push` reported "Everything up-to-date".
- **Anchor identifier renames at both ends.** A regex rename of `SpectralMarkers\b` to `SpectralMarkerPair` also
  rewrote `paintSpectralMarkers`, because a `\b` only at the end matches inside a longer name (2026-10-03). Use
  `\bName\b`.
- **Pillow's `getbbox()` on an RGBA image reads only the alpha channel**, so `ImageChops.difference` of two opaque
  snapshots always comes back "identical". Convert both to RGB first; that is how the Drum Rack face's pixel-identity
  across commit `94d4d9f` was checked ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).
- **Tooling stays out of the repository.** Capture scripts, monitors and skills go in the scratchpad or
  `%USERPROFILE%\.claude\skills\`, where `synth-ab` lives; ask before committing any to the repo
  ([Git workflow](git-workflow.md)).
- **An interactive tool leaves the verbs to the developer:** start, stop, name, save, discard. Record to a temporary
  file and write the real one only on an explicit save, so a bad take costs nothing.
- **Others are in the same tree.** The developer edits and commits `instruments/rhino-forge/` mid-session, and another
  agent may be building. Stage explicit paths and never `git add -A`, re-check `git log` before assuming where the
  branch is, and name the owner of a locked file before killing anything
  ([Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)).
- **Never read** `native/.deps/` or `research/sources/` ([Directories not to read](off-limits-directories.md)).

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Comparing Forge with Serum](comparing-forge-with-serum.md)
- [Git workflow](git-workflow.md)
- [Directories not to read](off-limits-directories.md)
- [Reading MIDI hardware on Windows](reading-midi-hardware-on-windows.md)
