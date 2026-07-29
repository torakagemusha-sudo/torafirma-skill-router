# Skill Router Architecture Measurement Ledger

This ledger records the remaining falsifiable boundaries in the multi-agent integration architecture. A status of **implemented / measurement active** means the mechanism exists and is covered by deterministic tests; it does not mean field evidence under arbitrary deployment load is complete.

## SR-RANK-001 — Versioned ranking and explanation contract

**Status:** Implemented / measurement active  
**Policy:** `tf.skillrouter.hybrid-lexical.v1`

**Claim.** Given an identical normalized query, ranking policy, catalogue generation, telemetry snapshot, search mode, and top-N bound, independent conforming implementations return the same ordered candidates and score decomposition.

**Implemented evidence.**

- Exact, FTS5, fuzzy, telemetry, lifecycle, and tie-break components are individually surfaced.
- Each candidate creates an append-only `routing_decisions` row.
- Query digest, normalized tokens, FTS normalization extrema, historical counters, and catalogue generation are retained.
- Golden tests cover exact tiering, Porter stemming, fuzzy typo recovery, exact dominance, telemetry reordering, top-N exposure, and deterministic lexical ties.

**Exit condition.**

A published conformance corpus can be replayed by at least two independent implementations with identical ordering and component values within a declared floating-point tolerance.

**Open field measurements.**

- Ambiguous-intent disagreement rate across heterogeneous agent hosts.
- Stability under catalogue growth where per-query FTS normalization changes.
- Whether the current telemetry formula saturates too rapidly for production fleets.

## SR-ID-001 — Load-time identity and update semantics

**Status:** Implemented / measurement active

**Claim.** Every consumer fetch returns the same immutable skill revision selected at search time, or fails closed. A task never changes skill revision without an explicit new search/fetch transition.

**Implemented evidence.**

- Search returns `skill_version`, `revision_id`, and `catalog_generation`.
- Consumer CLI and MCP fetch require the selected revision and catalogue generation.
- Fetch re-hashes bytes before return and records a receipt.
- Tests mutate a skill between search and fetch and verify rejection.
- Tests mutate catalogue membership and verify generation mismatch.
- `INDEXED -> ACTIVE` does not alter catalogue generation.
- MCP resources use revision-pinned URIs.

**Exit condition.**

For every successful load receipt:

```text
revision(search result) == revision(returned body) == revision(fetch receipt)
```

No adversarial interleaving of publication, search, and fetch can produce an unannounced body revision.

**Open field measurements.**

- Long-session stale-revision duration distribution.
- Frequency of explicit refresh requests after publication.
- Operational policy for emergency revocation of already-loaded instructions.

## SR-BOUNDARY-001 — Enforced consumer/publisher separation

**Status:** Router enforcement implemented; deployment evidence active

**Claim.** An agent-facing consumer can search, exact-fetch, and append telemetry but cannot change skill bodies, catalogue membership, or lifecycle state.

**Implemented evidence.**

- Consumer catalogue connections use SQLite read-only mode and `PRAGMA query_only`.
- Catalogue and runtime telemetry are separate databases.
- Publication and lifecycle methods reject read-only consumer instances.
- MCP exposes no register, index, deprecate, archive, or publication method.
- Tests verify consumer write denial and shared telemetry across isolated consumer processes.

**Exit condition.**

Under the deployed consumer operating-system identity:

- direct writes to all `SKILL.md` files fail;
- catalogue database mutation fails;
- registration, indexing, deprecation, and archival fail;
- telemetry append succeeds;
- search and exact-revision fetch succeed.

**Open field measurements.**

- Filesystem ACL or read-only mount evidence for each supported platform.
- SQLite write-contention behaviour in the separate telemetry database under fleet load.
- Atomic catalogue publication and consumer generation turnover under concurrent reads.

## Validation inventory

The 1.1.0 source suite currently contains 61 deterministic C++ tests. The high-signal contract cases include:

- standard SHA-256 vectors;
- version parsing and defaulting;
- score-component accounting;
- decision-receipt persistence;
- exact revision fetch success;
- wrong-revision rejection;
- on-disk drift rejection;
- catalogue-generation rejection;
- generation stability across first fetch;
- read-only consumer denial;
- separate and shared telemetry;
- consumer drift detection without catalogue mutation;
- revision-pinned MCP tools and resources.

CI is required to build and execute the suite plus the end-to-end contract smoke on Linux and Windows for every pull request and push to `main`. The smoke performs 40 searches across eight concurrent consumer workers and asserts exact agreement among counters, routing decisions, and suggestion events.
