# Hacker News Submission Draft

**Title**

Show HN: A deterministic skill algebra for AI agents – closed-form, no embeddings, single researcher

**Submission text**

I built a local-first skill router for AI agents that treats loaded instructions as versioned artifacts rather than name-addressed prompt fragments.

The routing path is deterministic:

```text
base = exact + 0.6 * fts_norm + 0.35 * fuzzy
score = base * telemetry_multiplier * state_multiplier
```

Exact matching has public 3/2/1 keyword/name/description tiers. FTS is SQLite FTS5 with Porter stemming. Fuzzy matching is bounded Levenshtein distance. Telemetry is an explicit capped multiplier. Exact ties resolve by `skill_id`. There are no vector embeddings, learned classifiers, hidden priorities, or random tie-breaks.

Search returns:

```text
(skill_id, skill_version, revision_id, catalog_generation)
```

Fetch must present the SHA-256 revision and catalog generation returned by search. The router re-hashes the file immediately before returning it and fails closed if the file or catalog changed.

The repo includes:

- the C++20 reference implementation;
- 61 deterministic contract tests, with additional C ABI tests in the 1.1.0 formalization branch;
- a ranking and load-time identity contract;
- the whitepaper, “A Monoid Over Hashes”;
- a proof sketch that states the cryptographic assumptions and non-claims;
- a stable C ABI plus Python, Node, and Rust binding scaffolds;
- Linux and Windows CI;
- a release workflow that emits checksums and a detached OpenPGP signature.

One mathematical clarification: the monoid is the finite union of immutable content-addressed revision records. SHA-256 is the commitment to the canonical published catalog; I am not claiming that ordinary hash concatenation is itself the monoid operation.

The compact falsification demo is:

1. register a skill;
2. search and capture revision plus generation;
3. fetch successfully;
4. mutate the file;
5. repeat the pinned fetch and observe `REVISION_MISMATCH`;
6. re-index and observe the new revision and generation.

Repository: https://github.com/torakagemusha-sudo/torafirma-skill-router

Whitepaper: https://github.com/torakagemusha-sudo/torafirma-skill-router/blob/main/WHITEPAPER.md

Proof sketch: https://github.com/torakagemusha-sudo/torafirma-skill-router/blob/main/PROOF_SKETCH.md

I am interested in criticism of the canonicalization contract, ranking invariants, telemetry bias, and cross-language conformance boundary more than general “embeddings versus no embeddings” arguments.
