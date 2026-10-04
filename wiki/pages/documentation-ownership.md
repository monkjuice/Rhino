---
title: Documentation ownership
type: convention
summary: READMEs introduce and build the products; the wiki is the canonical home for engineering detail, decisions and gotchas.
tags: [docs, readme, wiki, convention]
sources: []
updated: 2026-10-03
---

# Documentation ownership

The root `README.md` and `instruments/rhino-forge/README.md` are concise newcomer entry points: what each product is,
what it can do, its mid-alpha limits, and copy-paste Windows source-build instructions. They are not architecture or
implementation references.

This wiki is the canonical home for architecture, component contracts, decisions, conventions, development guides and
gotchas. `AGENTS.md` carries the active working rules and links into the wiki. Detailed historical plans and handover
files were removed on 2026-10-03 after their useful content had been consolidated here; do not recreate a second
long-form documentation layer beside the wiki.

Focused documents remain beside the thing they describe when that proximity is useful: test-runner READMEs, asset and
font provenance, and the research record. Code remains the source of truth when any documentation disagrees with it.

## Release-stage wording

The products are in mid-alpha. Windows is the currently documented and validated source-build path; macOS portability
is preserved but remains unvalidated. Production installers for Windows and macOS are targeted for December
2026-January 2027, so setup docs must not imply that installers exist before then.

## Related

- [Overview](../overview.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Build and test Forge](build-and-test-forge.md)
- [Git workflow](git-workflow.md)
