---
title: Forge's macOS test build is a rolling public pre-release
type: decision
summary: Since aac0b67 (2026-11-20) every green Forge macOS run from main replaces the public pre-release forge-macos-latest with its validated Rhino-Forge-macOS.pkg, because a run's artifact downloads only for someone signed in to GitHub. Publishing is a job of its own, the only one holding a write token. The release is deleted and recreated so its tag moves to the commit built while its addresses stay the same, and its notes link the licence and source at that commit. Verified the same day: the asset downloads with no GitHub account.
tags: [forge, macos, ci, release, packaging, licence]
sources: []
updated: 2026-11-20
---

# Forge's macOS test build is a rolling public pre-release

## Context

A run's artifact downloads only for someone signed in to GitHub, even on a public repository. Signed out, the artifact's name shows as plain text with no download button. That is what the first person to try the Mac build found, so the package needed an address anyone can download from ([Forge's macOS build and installer](forge-macos-build.md)). The user explicitly approved making the build public.

## The choice

Since `aac0b67` (2026-11-20), `.github/workflows/forge-macos.yml` has a second job, `publish`:

- It has `needs: build` and runs only when `github.ref` is `refs/heads/main`, on `ubuntu-latest`.
- It is the only job with `contents: write`. The workflow's default stays `contents: read`.
- It downloads the artifact and renames it `Rhino-Forge-macOS.pkg`, because GitHub turns a space in a release asset's name into a dot. `PACKAGE` (`Rhino Forge.pkg`) moved to the workflow-level `env` so that both jobs read it.
- If the release `forge-macos-latest` exists, it deletes it with `gh release delete --cleanup-tag`. It then recreates it with `gh release create ... --prerelease --latest=false --target $GITHUB_SHA`, titled "Rhino Forge for macOS (test build)", and emits a `::notice` with the download URL.

The addresses stay the same across builds:

- Release page: `https://github.com/monkjuice/Rhino/releases/tag/forge-macos-latest`
- Asset: `https://github.com/monkjuice/Rhino/releases/download/forge-macos-latest/Rhino-Forge-macOS.pkg`

The Forge README's *Get Forge on macOS* links both, and notes that run artifacts remain for signed-in users for 30 days.

## Why this shape

- **A job of its own.** The write token is never held by the build job, which runs compilers and third-party code.
- **After the build job.** The build job has already installed the package and required auval's success line, so only a validated package is published.
- **Replaced, not edited.** Editing the release in place would leave its tag on an older commit. Recreating it moves the tag to the commit built, so the release page always says which commit that was.
- **`--latest=false`, pre-release.** It never becomes the repository's Latest release.

## Consequences

- Between the delete and the create, the links return 404 for a moment.
- The notes say:
  - what the build is: unsigned, macOS 11 or later, universal, the short commit, replaced on every push that changes Forge
  - what it installs where, and the one-time Open Anyway
  - that Forge is AGPL-3.0-or-later, with links to `LICENSE` and `THIRD_PARTY_NOTICES.md` at the exact commit built

  That partly meets the AGPL's source requirement for this test build. The `.pkg` itself still carries no licence text ([Rhino is licensed under the AGPL v3 or later](agpl-licence.md)).
- Every push to `main` that touches Forge publishes a new public binary.

## Status

**Verified on 2026-11-20.** Run 44841968703 (`aac0b67`) passed:

- The build job took about 7 minutes.
- In the publish job, `download-artifact` took 2 s and "Replace the test build release" 6 s, and the job emitted the notice with the URL.

Checked afterwards with no authentication:

- The release API for `forge-macos-latest` reports the title above, `prerelease` true, `target_commitish` `aac0b67c8a2a...`, and one asset, `Rhino-Forge-macOS.pkg`, of 47.4 MB.
- The asset URL redirects to `release-assets.githubusercontent.com` and returns 200 with that file name, so anyone can download it without a GitHub account.

Since `4ce11ab` the build job also opens and quits the installed app. Since `792745a` it fails on a non-zero exit status as well as on a crash report, so a build that crashes on quit on the runner is not published. The first real Mac crashed on every quit, but the runner has not reproduced it. Run 44845567288 (`4ce11ab`) passed and replaced the release, which now carries the window fit from `251b554` ([Forge's macOS build and installer](forge-macos-build.md#status)). `actions/download-artifact@v4` carries the same Node 20 deprecation warning as the other actions ([Forge's macOS build and installer](forge-macos-build.md#gotchas)).

## Related

- [Forge's macOS build and installer](forge-macos-build.md)
- [Rhino is licensed under the AGPL v3 or later](agpl-licence.md)
- [Documentation ownership](documentation-ownership.md)
