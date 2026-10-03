# Wiki schema

This is the LLM wiki of **Rhino**: a persistent, interlinked knowledge base about this software project that
coding agents write and maintain, and people read. Raw sources are immutable; the pages are the knowledge compiled
from them and from the code; `index.md` is the catalog and `log.md` the history.

Every agent that reads or edits this wiki follows this file.

## Layout

| Path | Contents | Written by |
|---|---|---|
| `SCHEMA.md` | These conventions. | People (agents may propose changes). |
| `index.md` | Catalog of every page, grouped by type, one line each. | Agents, with every page change. |
| `log.md` | Append-only history of wiki operations. | Agents, one entry per operation. |
| `overview.md` | The project at a glance. | Agents. |
| `pages/` | Every other page, kebab-case file names (`pages/auth-service.md`). | Agents. |
| `raw/` | Source documents: specs, articles, meeting notes, transcripts. | People. **Immutable**: never edit, move or delete. |

## What belongs here

Knowledge about this project that the code does not make obvious, or that takes long to reconstruct. Page types:

- `component`: a module, service or subsystem: responsibility, key files, interfaces, dependencies, data flow.
- `concept`: a domain or technical concept the code relies on.
- `decision`: a design decision: context, the choice, the alternatives rejected, the consequences.
- `guide`: how to do something: build, run, test, release, debug, configure.
- `convention`: rules the code follows: structure, naming, error handling, testing, style.
- `gotcha`: a pitfall, constraint or workaround, and why it exists.
- `source`: the summary of one raw source and what the wiki took from it.
- `analysis`: a filed answer to a question that connects several pages.
- `overview`: `overview.md` only.

Not here: what the code already says plainly (signatures, line-by-line behaviour), notes on trivial edits, secrets,
credentials or personal data.

## Pages

Every page starts with this frontmatter:

```yaml
---
title: Auth service
type: component
summary: Issues and validates the session tokens of the API.
tags: [auth, api]
sources: [raw/auth-spec.pdf]
updated: 2026-04-02
---
```

- `type`: one of the types above. `summary`: one line, reused in `index.md`. `sources`: the raw sources the page
  draws on, relative to this folder (`[]` when none). `updated`: the date of the last meaningful change.
- Then a `# Title` heading and the content: short sections, facts before opinions, repository-relative file paths
  (`src/auth/token.ts`) and identifiers in backticks.
- One topic per page, a few hundred words at most; split a page that grows beyond that. When a statement may go
  stale, say what it is based on.

## Links

- Use relative markdown links: `[Auth service](pages/auth-service.md)` from this folder, `[Auth service](auth-service.md)`
  between pages, `[spec](../raw/auth-spec.pdf)` from a page to a source.
- Link generously: every page links to its related pages and is linked from at least one other page (no orphans).
- Refer to code by repository-relative path; do not copy code beyond short excerpts.

## index.md

One section per type (`## Components`, `## Decisions`, …), one line per page:

```
- [Auth service](pages/auth-service.md) — Issues and validates the session tokens of the API.
```

The text after the dash is the page's `summary`. Update the index whenever a page is created, renamed or deleted, or
its summary changes.

## log.md

Append-only, newest entry at the bottom. Every entry starts with a heading in exactly this format (tools parse it):

```
## [2026-04-02] ingest | Auth service spec v2
- source: raw/auth-spec.pdf
- updated: pages/auth-service.md, pages/session-tokens.md
- created: pages/token-rotation.md
```

Operations: `init`, `seed`, `ingest`, `query`, `lint`, and `update` (what the wiki keeper records after code
changes). Never rewrite past entries.

## Operations

- **Ingest** a raw source: read it in full; write a `source` page for it; update or create the pages it affects; state
  contradictions with existing pages explicitly; update cross-links and `index.md`; append an `ingest` entry with the
  source path.
- **Query**: read `index.md`, then the relevant pages; answer with links to them. File an answer worth keeping as an
  `analysis` page and append a `query` entry; otherwise leave the wiki unchanged.
- **Lint**: look for contradictions, stale claims, orphan pages, broken links, pages missing from the index, missing
  frontmatter and gaps. Fix what is mechanical, list the rest, append a `lint` entry.
- **Update** after code changes: record new components, decisions, conventions and gotchas, preferably by editing
  existing pages; append an `update` entry.

## Rules

- Never modify anything under `raw/`.
- The code is the source of truth: when the wiki disagrees with it, fix the wiki.
- Prefer updating an existing page to creating a near-duplicate.
- Be concise and factual: no filler, and no speculation presented as fact.
- No secrets, credentials or personal data.
