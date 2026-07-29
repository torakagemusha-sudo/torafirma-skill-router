# Skill Router Integration Manual

**Source version:** 1.1.0  
**Ranking policy:** `tf.skillrouter.hybrid-lexical.v1`  
**Status:** implementation complete; 61 deterministic C++ tests passing locally

Skill Router is a local-first C++20 engine for discovering and loading large agent skill libraries without placing the complete catalogue or every skill body into model context.

Version 1.1.0 formalizes three boundaries that were previously implicit:

1. the exact ranking function and its replay record;
2. the identity of a skill across catalogue updates;
3. the runtime separation between consumers and publishers.

## 1. System model

A deployment contains three distinct state surfaces:

```text
skill library/             SKILL.md bodies; read-only to consumers
skill_index.db             searchable catalogue; read-only to consumers
skill_index.db.telemetry.db routing decisions, events, counters, fetch receipts
```

The operator indexes and publishes skills. Consumer processes search the catalogue, fetch exact content revisions, and append telemetry. Skill bodies are never stored in SQLite.

```text
user task
   |
   v
agent -> skill_search(query)
   |         |
   |         +-> ranked candidates + score components
   |             + skill_id
   |             + skill_version
   |             + revision_id
   |             + catalog_generation
   |
   +-> skill_fetch(skill_id, revision_id, catalog_generation)
             |
             +-> recompute body SHA-256
             +-> fail closed on drift or generation change
             +-> return exactly one verified SKILL.md
```

## 2. Roles

### Consumer

Consumer mode is the default for `search`, `fetch`, `stats`, `graveyard`, `serve`, and `mcp`.

A consumer:

- opens the catalogue with `SQLITE_OPEN_READONLY`;
- enables `PRAGMA query_only`;
- writes only to the separate telemetry database;
- cannot register, index, deprecate, archive, or mutate catalogue state;
- must exact-fetch the revision and catalogue generation returned by search.

### Operator

Operator mode is used by `register`, `index`, the interactive administration shell, and lifecycle commands.

An operator may:

- validate and index a skill;
- publish a new revision;
- deprecate or archive a logical skill;
- inspect and administer lifecycle state.

Use `--role operator` explicitly in scripts when the distinction matters. Publication commands already open the catalogue read-write by design.

## 3. Build

### Linux or native POSIX

```bash
cd skill-router
make clean
make test
make
```

SQLite is vendored and compiled with:

```text
-DSQLITE_THREADSAFE=1
-DSQLITE_ENABLE_FTS5
```

### Windows with MSVC

From a Visual Studio 2022 x64 Developer Command Prompt:

```bat
cd skill-router
build_windows_msvc.bat
build\test_library.exe
```

### Windows with MinGW

```bash
make windows
```

The engine is C++20 plus vendored SQLite. MCP uses stdio and HTTP binds only to IPv4 loopback.

## 4. Frontmatter and skill identity

A canonical skill begins with:

```yaml
---
name: security-scan
version: 1.3.0
description: "Use for repository-wide security review and evidence-backed findings."
---
```

Required fields:

- `name`: stable logical `skill_id`;
- `description`: searchable routing description.

Optional field:

- `version`: human-authored compatibility version; defaults to `1.0.0`.

At registration, the complete file bytes receive:

```text
revision_id = sha256(SKILL.md bytes)
```

A loaded skill is identified by:

```text
(skill_id, skill_version, revision_id, catalog_generation)
```

The version and revision serve different purposes. The version communicates compatibility intent; the revision proves the exact bytes.

## 5. Catalogue schema

The catalogue stores compact metadata only:

| Column | Meaning |
|---|---|
| `skill_id` | Logical name; unique |
| `description` | Searchable frontmatter description |
| `keywords` | Curated or derived searchable terms |
| `path` | Filesystem path to the body |
| `content_hash` | Protocol-facing SHA-256 revision ID |
| `size_bytes` | Indexed byte size |
| `version` | Human-authored skill version |
| `state` | Lifecycle state |
| legacy telemetry fields | Retained for migration compatibility; new writes use telemetry DB |

The FTS5 table is external-content over `skill_id`, `description`, and `keywords`. Triggered updates keep it synchronized with catalogue metadata.

## 6. Telemetry schema

The separate telemetry database contains:

### `skill_telemetry`

Per-skill search and fetch counters plus timestamps.

### `search_log`

Append-only `SUGGESTED`, `FETCHED`, `USED`, and drift-detection events.

### `routing_decisions`

One row per returned candidate containing the complete score decomposition and replay identity.

### `fetch_receipts`

One row per exact-fetch attempt containing expected and observed revision/generation values and a terminal status.

Separation allows the catalogue and skill library to remain read-only while runtime observations remain writable.

## 7. Lifecycle state

```text
REGISTERED -> INDEXED -> ACTIVE
                    \-> STALE
INDEXED/ACTIVE -> DEPRECATED
INDEXED/ACTIVE -> ARCHIVED
```

- `INDEXED`: admitted and searchable.
- `ACTIVE`: successfully fetched at least once by an operator-mode instance.
- `STALE`: indexed revision no longer matches the file on disk.
- `DEPRECATED`: searchable but multiplied by `0.3`.
- `ARCHIVED`: excluded from search unless explicitly included.

For catalogue-generation identity, `INDEXED` and `ACTIVE` normalize to `AVAILABLE`. A first fetch therefore does not invalidate the generation.

## 8. Ranking function

The current ranker is deterministic and lexical. It does not use embeddings, a learned classifier, capability-graph distance, or a static manual-priority field.

### Query normalization

Queries are lower-cased, tokenized, stopword-filtered, and deduplicated. The router records the normalized tokens and their SHA-256 digest.

### Exact score

For each normalized query token, only the highest matching tier counts:

```text
keywords     3.0
skill name  2.0
description 1.0
```

### FTS5 score

FTS5 uses `porter unicode61`, prefix queries, and BM25 column weights:

```text
skill_id=2.0, description=1.0, keywords=3.0
```

Matched raw values are normalized per query to `[0.5,1.0]`.

### Fuzzy score

Only exact-missed tokens enter bounded Levenshtein matching:

```text
max distance 1 for token length <= 4
max distance 2 otherwise
```

The aggregate fuzzy score lies in `[0,1]`.

### Base and final score

```text
base = exact + 0.6 * fts_norm + 0.35 * fuzzy

conversion = fetch_count / search_count, or 0 when never searched
telemetry_multiplier = 1 + min(
    0.5,
    0.5 * conversion + 0.05 * log1p(fetch_count)
)

state_multiplier = 0.3 when DEPRECATED, otherwise 1.0
final_score = base * telemetry_multiplier * state_multiplier
```

FTS and fuzzy weights total `0.95`, below one exact-description tier. They can improve recall and break exact ties but cannot overturn a one-tier exact advantage.

Candidates sort by descending score, then ascending `skill_id` for an exact tie.

## 9. Search output

Machine-readable search output includes the complete selection identity and explanation:

```json
{
  "skill_id": "security-scan",
  "description": "...",
  "skill_version": "1.3.0",
  "revision_id": "sha256:...",
  "catalog_generation": "sha256:...",
  "ranking_policy": "tf.skillrouter.hybrid-lexical.v1",
  "query_digest": "sha256:...",
  "normalized_tokens": "repository security review",
  "search_mode": "hybrid",
  "state": "INDEXED",
  "score": 9.42,
  "score_components": {
    "exact_keyword": 6.0,
    "exact_name": 0.0,
    "exact_description": 1.0,
    "fts_raw": 0.01,
    "fts_normalized": 1.0,
    "fts_min": 0.002,
    "fts_max": 0.01,
    "fuzzy": 0.0,
    "base": 7.6,
    "search_count": 12,
    "fetch_count": 4,
    "telemetry_multiplier": 1.24,
    "state_multiplier": 1.0,
    "tie_break_key": "security-scan"
  }
}
```

## 10. Exact fetch and pinning

A consumer fetch requires:

```text
skill_id
expected_revision
catalog_generation
```

The router verifies the current generation, reads the body, computes its SHA-256, verifies the indexed revision, then verifies the expected revision.

Terminal outcomes include:

- `OK`;
- `CATALOG_GENERATION_MISMATCH`;
- `INDEX_DRIFT`;
- `EXPECTED_REVISION_MISMATCH`.

No mismatch returns a body. A successful body is pinned in the agent context for the current task. There is no implicit hot reload.

## 11. CLI reference

```text
skillrouter                                      operator shell
skillrouter register <SKILL.md>                  [--db PATH] [--telemetry-db PATH]
skillrouter index <root>                         [--db PATH] [--telemetry-db PATH]
skillrouter search "query"                       [--db PATH] [--telemetry-db PATH]
                                                  [--top N] [--json]
                                                  [--mode hybrid|exact|fts|fuzzy]
skillrouter fetch <skill_id>                     --revision SHA256
                                                  --catalog-generation SHA256
                                                  [--db PATH] [--telemetry-db PATH]
skillrouter use <skill_id>                       [--context "query"]
skillrouter stats                                [--db PATH] [--telemetry-db PATH]
skillrouter graveyard                            [--min-searches N]
skillrouter deprecate <skill_id>                 [--role operator]
skillrouter archive <skill_id>                   [--role operator]
skillrouter serve                                [--port 8090] [--role consumer|operator]
skillrouter mcp                                  [--role consumer|operator]
```

Defaults:

```text
catalog:   skill_index.db
telemetry: skill_index.db.telemetry.db
```

### Operator index

```powershell
.\skillrouter.exe index .\skill_library `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role operator
```

### Consumer search and exact fetch

```powershell
$hit = (.\skillrouter.exe search "windows cpp build" `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role consumer `
  --json | ConvertFrom-Json)[0]

.\skillrouter.exe fetch $hit.skill_id `
  --revision $hit.revision_id `
  --catalog-generation $hit.catalog_generation `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role consumer
```

## 12. MCP interface

Start the stdio server:

```powershell
.\skillrouter.exe mcp `
  --db C:\absolute\skill_index.db `
  --telemetry-db C:\absolute\skill_telemetry.db `
  --role consumer
```

### `skill_search`

Input:

```json
{
  "query": "repository security review",
  "top": 8,
  "mode": "hybrid",
  "include_archived": false
}
```

Output is a JSON array encoded in the MCP text result. Each candidate includes revision, generation, policy, and score components.

### `skill_fetch`

Input:

```json
{
  "skill_id": "security-scan",
  "expected_revision": "sha256:...",
  "catalog_generation": "sha256:...",
  "context": "repository security review"
}
```

All three identity fields are mandatory.

### Other tools

- `skill_stats`
- `skill_graveyard`

The MCP surface intentionally contains no catalogue-publication operations.

### MCP resources

Resources are revision-pinned:

```text
skill://security-scan@sha256:<revision>
```

An unpinned resource URI is rejected. Resource reads still verify current catalogue generation indirectly through the indexed revision/body relationship, but agent routing should prefer `skill_search` followed by `skill_fetch`, because that path also pins the catalogue generation.

## 13. HTTP interface

Start the loopback service:

```powershell
.\skillrouter.exe serve --port 8090 --role consumer
```

Endpoints:

| Method | Path | Result |
|---|---|---|
| GET | `/health` | engine, policy, generation, role, telemetry summary |
| GET | `/stats` | same operational summary |
| GET | `/graveyard` | low-conversion candidates |
| GET | `/search?q=...&mode=hybrid` | detailed ranked candidates |
| GET | `/fetch?id=...&revision=...&catalog_generation=...` | exact body or fail-closed error |

HTTP binds only to `127.0.0.1`. It has no authentication and must not be exposed or forwarded.

## 14. Consumer/publisher deployment

Recommended production layout:

```text
publisher identity:
  skill library      read/write
  catalogue next     read/write
  published catalogue replace permission
  telemetry          optional read

consumer identity:
  skill library      read-only
  published catalogue read-only
  telemetry          read/write
```

The router enforces read-only SQLite access and method guards. The operating system should independently enforce the same boundary through ACLs, read-only mounts, containers, or service identities.

For a multi-agent host, either:

- run one central consumer service; or
- run one consumer process per agent and share the immutable catalogue plus telemetry database.

The second pattern gives stronger fault isolation. The first gives simpler operations.

## 15. Publication sequence

A safe publication sequence is:

1. stage skill changes outside the live library;
2. validate frontmatter and policy;
3. index the complete candidate library in operator mode;
4. run all contract tests and smoke tests;
5. record the new catalogue generation;
6. publish skill bodies and catalogue as one controlled generation;
7. allow existing in-flight tasks to retain pinned revisions;
8. require new searches after a generation mismatch.

For stronger atomicity, publish immutable generation directories and switch a stable pointer only after validation.

## 16. Upgrade from 1.0.0

Version 1.0.0 used a short internal content hash and allowed bare `skill_id` fetches. Version 1.1.0 uses SHA-256 revision identities and exact consumer fetches.

Before starting 1.1.0 consumers against an old catalogue:

```powershell
# Stop consumers first.
.\skillrouter.exe index .\skill_library `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role operator
```

This re-index is mandatory. It upgrades the indexed revision values and emits the new catalogue generation. A stale pre-1.1 catalogue fails verification rather than silently returning unverified content.

## 17. Validation

The 1.1.0 suite contains 61 deterministic C++ tests covering:

- frontmatter and version handling;
- SHA-256 vectors;
- catalogue and telemetry separation;
- ranking components and deterministic tie-breaks;
- full decision receipts;
- exact fetch success;
- search/fetch revision races;
- catalogue generation turnover;
- read-only consumer denial;
- shared telemetry;
- MCP tool and resource identity contracts;
- malformed protocol and error paths.

GitHub Actions builds the router, runs the 61-test C++ suite, and executes an end-to-end exact-fetch and eight-worker telemetry-concurrency smoke on Linux and Windows.

## 18. Honest scope boundary

Skill Router decides which instructions to surface; it does not prove those instructions are safe or correct. Third-party skills remain untrusted input to downstream agents.

The engine provides deterministic selection, exact load identity, and separation of publication from consumption. It does not eliminate:

- ambiguous-intent disagreement between valid candidates;
- downstream conflicts created by skill instructions;
- emergency revocation complexity after instructions have entered context;
- operating-system compromise outside the router process;
- telemetry database contention at arbitrary fleet scale.

Those boundaries are explicit and measurable rather than hidden.
