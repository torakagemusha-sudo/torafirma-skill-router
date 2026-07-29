# pi-skill-router — governed auto-routing for large skill libraries

Skill Router is a local-first C++20 router that lets an agent discover and load one specialized `SKILL.md` without ingesting the entire library. Version 1.1.0 adds explainable ranking, content-addressed skill revisions, fail-closed search-to-fetch identity, and enforceable consumer/operator separation.

## Deployment layout

```text
skill-router/
  skillrouter.exe
  skill_library/                  skill bodies
  skill_index.db                  catalogue
  skill_index.db.telemetry.db     decisions, counters and receipts
  skills/skill_router/            always-loaded interface skill
  INTEGRATION_MANUAL.md
```

Consumers open `skill_index.db` read-only. Runtime telemetry is written to the separate telemetry database. Operators alone index and publish catalogue changes.

## Build and test

```bat
build_windows_msvc.bat
build\test_library.exe
```

or:

```bash
make clean
make test
make
```

The current suite contains **61/61 passing deterministic tests** locally. CI runs the same source on Linux and Windows, including an end-to-end exact-fetch and eight-worker telemetry-concurrency smoke.

## Index the library

Each skill requires `name` and `description` frontmatter. `version` is optional and defaults to `1.0.0`.

```powershell
.\index-skills.ps1
```

The operator index computes a SHA-256 revision for every body and emits a deterministic catalogue generation.

When upgrading an existing 1.0.0 catalogue, stop consumers and run a complete operator re-index before starting 1.1.0. Old short hashes are deliberately not accepted as verified revisions.

## Automatic consumer flow

Search returns the ranking explanation and exact body identity:

```powershell
$hit = (.\skillrouter.exe search "authenticating users with oauth" --json | ConvertFrom-Json)[0]
```

Fetch requires the selected revision and generation:

```powershell
.\skillrouter.exe fetch $hit.skill_id `
  --revision $hit.revision_id `
  --catalog-generation $hit.catalog_generation
```

A changed file or catalogue generation is rejected; the router never silently substitutes a new body.

## Ranking policy

The policy identifier is:

```text
tf.skillrouter.hybrid-lexical.v1
```

```text
base = exact + 0.6 * fts_norm + 0.35 * fuzzy
score = base * telemetry_multiplier * state_multiplier
```

Exact matches score by tier: keywords `3.0`, name `2.0`, description `1.0`. FTS5 uses Porter stemming and prefix matching. Fuzzy matching uses bounded edit distance for exact-missed tokens. The telemetry multiplier is deterministic and capped; deprecated skills receive a `0.3` multiplier; ties resolve lexically by `skill_id`.

Search output includes all component scores, normalized query identity, catalogue generation, revision, historical counters, and tie-break key. The current policy uses no embeddings, learned model, capability-graph distance, or hidden static priority.

## Skill identity

```text
(skill_id, skill_version, revision_id, catalog_generation)
```

- `skill_id`: stable logical name;
- `skill_version`: human-authored compatibility version;
- `revision_id`: SHA-256 of exact body bytes;
- `catalog_generation`: SHA-256 identity of the ranked catalogue snapshot.

Loaded skills are pinned for the current task. There is no implicit hot reload.

## Interfaces

- **MCP stdio:** consumer search, exact fetch, stats and graveyard tools.
- **CLI:** machine-readable search/fetch and operator administration.
- **Loopback HTTP:** trusted local consumer integrations.
- **Operator shell:** interactive inspection and lifecycle administration.

MCP `skill_fetch` requires `skill_id`, `expected_revision`, and `catalog_generation`. MCP resources are revision-pinned as `skill://<skill_id>@sha256:<revision>`.

## Consumer versus operator

```powershell
# Consumer defaults for MCP/search/fetch/stats/serve
.\skillrouter.exe mcp --role consumer

# Explicit operator publication
.\skillrouter.exe index .\skill_library --role operator
```

Router guards and SQLite read-only mode enforce the logical boundary. Production deployments should also use filesystem ACLs or read-only mounts so consumer identities cannot write `SKILL.md` files or the catalogue directly.

## Full reference

See `INTEGRATION_MANUAL.md` for the complete ranking equation, data schemas, exact fetch protocol, CLI/MCP/HTTP reference, upgrade sequence, deployment controls, and limitations.

## License

MIT License. Copyright (c) 2026 Thomas Helm.
