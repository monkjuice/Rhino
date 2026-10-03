---
title: Git workflow
type: convention
summary: Focused, verified commits pushed at milestones; stage explicit paths since the user edits Forge in the same tree; never rewrite pushed history.
tags: [both, git, workflow]
sources: []
updated: 2026-10-03
---

# Git workflow

One repository holds both products, Rhino under `native/` and Forge under `instruments/rhino-forge/`, on the `main` branch of the `origin` remote. The rules come from `AGENTS.md`, *Commit and push workflow*; the rest were learned the hard way.

## The rules

- **Commit and push at each meaningful milestone.** Routine, focused commits and pushes are authorised without asking each time. Do not let finished work pile up locally.
- **Before committing,** read `git status` and the diff, run the checks that fit the change, and make sure the commit holds only what was intended. Never commit secrets, build output, caches or fetched dependencies.
- **Match the validation to the change.** Native behaviour needs the relevant checks ([Build and test Rhino](build-and-test-rhino.md), [Build and test Forge](build-and-test-forge.md)). A documentation-only edit needs a diff review, not a build. Do not add tests that merely mirror a low-impact change.
- **Write messages that state the result,** as the history does: "Give the spectral oscillator its loop modes and markers".
- **Confirm the remote and the branch before pushing.** If the remote has moved, inspect it and reconcile. Never force-push or rewrite shared history without explicit permission.
- **Report what was committed and pushed.** A local commit is not "pushed" until the push has succeeded.

## Stage explicit paths

The user edits Forge in the same working tree while an agent works elsewhere, committing mid-session and leaving edits uncommitted. On 2026-09-19, `git add -A` swept their in-progress `ForgeCore.h`, `ForgeFx.h` and `ForgeLayout.h` into a documentation commit, recoverable only because nothing had been pushed. So:

- stage named paths (`git add native/ AGENTS.md`), never `git add -A` or `git add .`;
- treat changes outside your own area in `git status` as somebody else's;
- run `git log` again before assuming the branch is where you left it, because the user pushes mid-session too.

## What never goes in

- Build trees and fetched code. `native/build/`, `native/build-*/`, `native/.deps/`, `native/.tools/`, `instruments/rhino-forge/build/`, `artifacts/` and `temp/` are all ignored. Generated plugin bundles are never committed, and a VST3 is installed only after it has been validated in a host.
- Agent tooling. Skills, capture scripts and harnesses live at user level, outside the repository. The `synth-ab` skill was committed once and taken out again by a revert (`719a4aa`).

## Content and line endings

`.gitattributes` normalises text to LF and keeps `research/sources/**` byte-for-byte so its recorded hashes still match. It marks Forge's `tables/*.wav` as binary and stores audio under `library/` with Git LFS. Set up LFS for a new content type *before* its first file is committed, because converting afterwards means rewriting history ([Content is files, never compiled in](content-is-files.md)).

## Undoing a pushed mistake

Revert it with a new commit. Do not force-push.

## Related

- [Build and test Rhino](build-and-test-rhino.md)
- [Development environment and reference material](development-environment.md)
- [Directories not to read](off-limits-directories.md)
- [Content library](content-library.md)
