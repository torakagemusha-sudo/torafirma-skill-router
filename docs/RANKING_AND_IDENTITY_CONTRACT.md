# Skill Router Ranking and Load-Time Identity Contract

**Contract version:** `tf.skillrouter.contract.v1`  
**Ranking policy:** `tf.skillrouter.hybrid-lexical.v1`  
**Engine target:** Skill Router `1.1.0`

This document defines the observable contract between a Skill Router consumer, the catalogue used to rank skills, and the exact skill body ultimately loaded into an agent context. It is intended to make ambiguous selection and concurrent catalogue updates reproducible, explainable, and falsifiable.

## 1. Non-goals

The current ranker is not an embedding model, a learned classifier, a capability-graph traversal, or a manually assigned priority queue. It is a deterministic hybrid lexical ranker whose only mutable input is an explicit telemetry snapshot.

No consumer may assume that a bare `skill_id` identifies immutable instructions. A `skill_id` names a logical skill; the bytes loaded for a task are identified by a revision.

## 2. Normalized query

A query is lower-cased, split into alphanumeric or underscore tokens, deduplicated in first-occurrence order, and filtered by the router stopword set. One-character tokens are removed.

For query `q`, let the resulting ordered token set be `T(q)`. The router records:

- `normalized_tokens`: the space-joined normalized tokens;
- `query_digest = sha256(normalized_tokens)`.

These values are part of every routing decision receipt.

## 3. Exact lexical score

For each token `t` and skill `s`, only the highest matching tier contributes:

```text
3.0  if t occurs in skill keywords
2.0  else if t occurs in the skill_id/name
1.0  else if t occurs in the description
0.0  otherwise
```

Therefore:

```text
exact(s,q) = exact_keyword + exact_name + exact_description
```

Default keywords are derived from the first 20 normalized tokens of the skill name and description unless an operator supplies curated keywords.

## 4. FTS5 score

When SQLite FTS5 is available, query tokens are converted to an OR of quoted prefix expressions and evaluated against an external-content FTS5 table over:

1. `skill_id`;
2. `description`;
3. `keywords`.

The tokenizer is `porter unicode61`. BM25 column weights are `2.0`, `1.0`, and `3.0` respectively. Raw BM25 values are negated so larger values are better, then normalized across the current query result population:

```text
fts_norm = 0.5 + 0.5 * (fts_raw - fts_min) / (fts_max - fts_min)
```

When all returned raw FTS values are equal, a matched skill receives `fts_norm = 1.0`. A skill with no FTS match receives `0.0`.

The recorded `fts_min` and `fts_max` make the catalogue-relative normalization replayable.

## 5. Fuzzy score

Fuzzy matching is applied only to normalized query tokens that missed all exact tiers. Each missed token is compared with the union of keyword, name, and description tokens using bounded Levenshtein distance:

```text
max distance = 1  for tokens of length <= 4
max distance = 2  otherwise
contribution = 1 - distance / token_length
```

The accumulated contribution is divided by the total number of normalized query tokens. The result is in `[0,1]`.

## 6. Base score and invariant

For the default hybrid mode:

```text
base = exact + 0.6 * fts_norm + 0.35 * fuzzy
```

The non-exact weights sum to `0.95`, which is strictly less than the lowest exact tier (`1.0`). FTS and fuzzy matching can add recall or break an exact-score tie, but cannot overturn a one-tier exact advantage.

The `exact`, `fts`, and `fuzzy` modes isolate their respective components while preserving the same telemetry, state, and tie-break stages.

## 7. Telemetry multiplier

For skill `s` at the decision snapshot:

```text
conversion = fetch_count / search_count, or 0 when search_count is 0
telemetry_multiplier = 1 + min(
    0.5,
    0.5 * conversion + 0.05 * log1p(fetch_count)
)
```

This multiplier is bounded to `[1.0,1.5]`. It is deterministic but history-dependent. Consequently, a routing decision is replayable only when the telemetry counters used by that decision are retained.

`search_count` increments only for candidates returned inside the bounded top-N result. This exposure effect is intentional and recorded; it must not be confused with an unbiased estimate over all catalogue entries.

## 8. Lifecycle multiplier and tie-break

```text
state_multiplier = 0.3  when state == DEPRECATED
state_multiplier = 1.0  otherwise
final_score = base * telemetry_multiplier * state_multiplier
```

Archived skills are excluded unless the caller explicitly requests them.

Candidates are ordered by descending `final_score`. Exact numerical ties are resolved by ascending `skill_id`. There is no random tie-break.

## 9. Routing decision receipt

Every returned candidate generates an append-only decision row containing at least:

```text
ranking_policy
query
query_digest
normalized_tokens
catalog_generation
search_mode
rank
skill_id
skill_version
skill_revision
exact_keyword
exact_name
exact_description
fts_raw
fts_normalized
fts_min
fts_max
fuzzy
base_score
search_count
fetch_count
telemetry_multiplier
state_multiplier
final_score
tie_break_key
```

Given the same catalogue generation, telemetry snapshot, query normalization, ranking policy, mode, and top-N bound, independent implementations must return the same ordered candidates and component scores within declared floating-point tolerance.

## 10. Skill identity

A loaded skill is identified by the tuple:

```text
(skill_id, skill_version, revision_id, catalog_generation)
```

The fields have distinct meanings:

- `skill_id`: stable logical identity from frontmatter `name`;
- `skill_version`: human-authored compatibility version from frontmatter `version`, default `1.0.0`;
- `revision_id`: immutable `sha256:` identity of the exact `SKILL.md` bytes;
- `catalog_generation`: deterministic `sha256:` identity of the admitted catalogue metadata and revisions used for ranking.

`skill_version` is not a substitute for `revision_id`. Two byte-distinct bodies can share a version accidentally; the revision still distinguishes them.

## 11. Search-to-fetch contract

Consumer search returns `revision_id` and `catalog_generation` for every candidate. Consumer fetch must supply both:

```text
skill_fetch(
  skill_id,
  expected_revision,
  catalog_generation
)
```

The router then:

1. verifies that the current catalogue generation equals the expected generation;
2. reads the current file bytes;
3. computes their SHA-256 revision;
4. verifies that the observed revision equals the indexed revision;
5. verifies that it equals the expected revision;
6. returns the body only after all checks pass;
7. records a fetch receipt.

A mismatch fails closed. No mismatched body is returned and no successful-fetch counter is incremented.

Defined statuses include:

- `OK`;
- `CATALOG_GENERATION_MISMATCH`;
- `INDEX_DRIFT`;
- `EXPECTED_REVISION_MISMATCH`.

## 12. Session semantics

The default policy is **pin on fetch; no implicit hot reload**.

Once an agent loads a skill revision for a task, that exact revision governs the task until one of the following explicit transitions occurs:

- the task ends;
- the agent performs a new search and exact fetch;
- a higher-level policy invalidates the task and requires re-admission.

Publication of a new skill revision does not mutate instructions already present in an agent context. A long-running agent therefore holds a stale but identified copy rather than silently switching policy mid-task.

## 13. Catalogue generation semantics

The catalogue generation hashes each skill's logical identity, searchable metadata, version, revision, and publication-relevant lifecycle state in canonical `skill_id` order.

`INDEXED` and `ACTIVE` normalize to the same publication state, `AVAILABLE`, so a first successful fetch does not invalidate a generation. Content changes, metadata changes, version changes, deprecation, archival, or admission changes do invalidate it.

## 14. Consumer and operator roles

The consumer role opens the catalogue SQLite database read-only and writes runtime telemetry to a separate telemetry database. Consumer-facing MCP exposes search, exact fetch, statistics, and graveyard inspection only.

The operator role may register, index, deprecate, archive, and otherwise publish catalogue state.

Router enforcement is one layer. Production deployments should also grant the consumer process identity read-only filesystem access to skill bodies and the catalogue, while granting write access only to the telemetry database and its containing directory.

## 15. Upgrade rule from 1.0.0

Version 1.0.0 stored a non-protocol FNV-derived content hash and allowed bare fetches. Before starting 1.1.0 consumers against an existing catalogue:

1. stop consumer router processes;
2. run one operator `index` pass over the complete skill library;
3. capture the emitted catalogue generation;
4. start consumers with the catalogue read-only and a separate telemetry database.

A pre-1.1 catalogue that has not been re-indexed will fail revision verification rather than silently returning unverified content.
