---
name: skill-router
version: 1.1.0
description: "Use first when a task may benefit from specialized capability in a large local skill library. Search indexed metadata, select an explainable ranked candidate, then fetch only the exact SHA-256 revision returned by search."
---
# skill-router

This is the single always-loaded interface skill for a large on-disk skill library. Do not enumerate or ingest the complete catalogue. Route internally from the user's ordinary task.

## Mandatory protocol

1. Translate the task into a concise capability query.
2. Call `skill_search`.
3. Inspect the bounded candidates and score explanations.
4. Select the best eligible skill for the task.
5. Call `skill_fetch` with all identity fields returned by that search result:

```json
{
  "skill_id": "selected-skill",
  "expected_revision": "sha256:...",
  "catalog_generation": "sha256:...",
  "context": "the capability query"
}
```

6. Load only the returned body and continue the original task.
7. Do not ask the user to choose a skill unless the underlying task itself is genuinely ambiguous and requires domain clarification.

## Identity rule

A bare `skill_id` is not an immutable body identity. Treat the selected skill as:

```text
(skill_id, skill_version, revision_id, catalog_generation)
```

Never omit `expected_revision` or `catalog_generation` in the consumer path. If fetch reports a revision or generation mismatch, repeat search against the current catalogue. Do not retry with an unpinned bare fetch.

## Temporal rule

Once fetched, keep that exact revision for the current task. Do not silently hot-reload a newly published revision into an in-flight context. Refresh only through a new search and exact fetch.

## Ranking interpretation

The active policy is `tf.skillrouter.hybrid-lexical.v1`. It combines:

- exact token tiers: keywords `3.0`, name `2.0`, description `1.0`;
- FTS5 Porter-stemmed/prefix relevance weighted `0.6`;
- bounded edit-distance fuzzy relevance weighted `0.35`;
- a capped historical suggestion-to-fetch multiplier;
- lifecycle penalties;
- lexical `skill_id` tie-breaking.

It is deterministic and explainable. It does not use embeddings or a learned classifier.

## Consumer boundary

Normal agent use is read-only with respect to the catalogue and skill files. Runtime telemetry is written separately. Registration, indexing, deprecation, archival, and publication are operator functions and must not be invoked from the normal per-task agent loop.

## Failure handling

- No candidates: continue without a specialist skill or refine the capability query once.
- `CATALOG_GENERATION_MISMATCH`: search again.
- `REVISION_MISMATCH` or `INDEX_DRIFT`: do not load the body; report or trigger operator re-indexing.
- Deprecated candidate: prefer an equally suitable active candidate unless the deprecated skill is explicitly required.

See `INTEGRATION_MANUAL.md` for the full contract.
