# Skill Router Integration Patterns

This document shows how `skillrouter` serves individual agents, concurrent agent fleets, human operators, scripts, and local services from one governed skill library.

The key properties are:

- skill bodies remain on disk;
- the catalogue contains compact searchable metadata and immutable SHA-256 revision identities;
- only the selected skill body is fetched;
- every routing decision is explainable and replayable against its recorded inputs;
- consumer processes open the catalogue read-only;
- runtime telemetry is written to a separate database;
- publication is an operator function, not an agent-consumer function;
- loaded skill revisions are pinned for the current task and never hot-reloaded implicitly.

The exact formula and identity semantics are defined in [Ranking and Load-Time Identity Contract](RANKING_AND_IDENTITY_CONTRACT.md). Open evidence points are tracked in [Architecture Measurement Ledger](ARCHITECTURE_MEASUREMENT_LEDGER.md).

## 1. Single-agent automatic routing

```mermaid
flowchart LR
    U[User task] --> A[Agent]
    A -->|skill_search| R[Consumer skillrouter]
    R -->|read-only| C[(Catalogue generation)]
    R -->|append| T[(Telemetry DB)]
    C --> R
    R -->|ranked candidate + revision + generation| A
    A -->|skill_fetch with exact revision| R
    R -->|verified SKILL.md only| A
    A --> O[Task output]
```

The user states the underlying task. The agent invokes the router internally, receives bounded ranked candidates, selects one, and fetches the exact revision returned by search. The user does not manually browse or choose a skill.

## 2. Centralized consumer service for multiple agents

```mermaid
flowchart TB
    subgraph Agents[Concurrent agents]
        A1[Agent A]
        A2[Agent B]
        A3[Agent C]
        A4[Agent D]
    end

    A1 -->|search / exact fetch| R[Central consumer router]
    A2 -->|search / exact fetch| R
    A3 -->|search / exact fetch| R
    A4 -->|search / exact fetch| R

    R -->|read-only| C[(Published catalogue)]
    R -->|read-only| L[(Skill library)]
    R -->|append decisions and receipts| T[(Shared telemetry)]

    R -->|Skill X @ revision a| A1
    R -->|Skill Y @ revision b| A2
    R -->|Skill Z @ revision c| A3
    R -->|Skill X @ revision a| A4
```

Agents share ranking telemetry and catalogue state without sharing active prompt context. The service can serve the same immutable skill revision to several agents concurrently.

## 3. One library, multiple consumer transports

```mermaid
flowchart LR
    subgraph Clients
        M[MCP-capable agent]
        S[Human inspector]
        C[CLI script or CI]
        H[Trusted local application]
    end

    M -->|stdio MCP| R[Consumer router engine]
    S -->|read-only commands| R
    C -->|CLI + JSON| R
    H -->|loopback HTTP| R

    R -->|read-only| I[(One published catalogue)]
    R -->|read-only| L[(One skill library)]
    R -->|append| T[(One telemetry stream)]
```

MCP, CLI, and loopback HTTP are transport projections over the same ranker and exact-fetch contract. Direct operator administration should run through a separately privileged process.

## 4. Per-agent process isolation with shared immutable catalogue

```mermaid
flowchart TB
    L[(Read-only skill library)]
    C[(Read-only catalogue generation)]
    T[(Shared writable telemetry DB)]

    A1[Agent A] --> R1[Consumer router A]
    A2[Agent B] --> R2[Consumer router B]
    A3[Agent C] --> R3[Consumer router C]

    R1 --> L
    R2 --> L
    R3 --> L
    R1 --> C
    R2 --> C
    R3 --> C
    R1 --> T
    R2 --> T
    R3 --> T
```

Use one consumer router per agent when process-level fault isolation matters. Each process opens the catalogue with SQLite read-only flags. The operating-system identity should also receive read-only permission over the catalogue and skill bodies; only the telemetry database is writable.

## 5. Governed publication and atomic generation turnover

```mermaid
flowchart LR
    D[Skill author] --> V[Validation and review]
    V -->|rejected| Q[Quarantine]
    V -->|admitted| P[Operator publisher]
    P --> N[(Next catalogue generation)]
    P --> O[(Content-addressed skill bodies)]
    N -->|validate + atomic publish| C[(Current catalogue generation)]
    C --> R1[Consumer router fleet]
    O --> R1
    R1 --> T[(Runtime telemetry)]
```

Publication is separated from consumption. The operator validates skills, computes revisions, indexes the complete admitted set, and publishes a new generation. Consumers do not modify the live generation. A search made against generation `g` must fetch against `g`; a generation change produces a fail-closed mismatch and requires a fresh search.

## 6. Mixed local development

```mermaid
flowchart TB
    Dev[Developer / operator]
    Shell[Operator shell]
    Tests[Contract tests]
    Publisher[Indexer / publisher]
    Agent[MCP consumer]
    App[HTTP consumer]

    Dev --> Shell
    Dev --> Tests
    Dev --> Publisher
    Publisher --> C[(Read-write development catalogue)]
    Agent --> R[Read-only consumer router]
    App --> R
    R --> C
    R --> T[(Telemetry DB)]
```

A development machine can host both roles, but they remain explicit. Operator commands open the catalogue read-write. Search, fetch, HTTP, and MCP default to consumer/read-only behaviour.

## Operational guidance

| Concern | Recommended pattern |
|---|---|
| Lowest safe integration complexity | One consumer MCP router per agent host, read-only catalogue, separate telemetry database |
| Centralized capability governance | Immutable published catalogue with a controlled operator publication path |
| Strong process isolation | Separate consumer router process per agent |
| Shared telemetry and ranking | Consumer routers append to one coordinated telemetry database |
| Human inspection | CLI or operator shell, with operator credentials only when administration is intended |
| CI and validation | Operator indexing plus deterministic contract tests and machine-readable output |
| Custom local tooling | Loopback HTTP consumer API |
| High-concurrency production | Immutable catalogue generations, OS-level read-only permissions, separate telemetry storage |

## Ranking boundary

The ranker is `tf.skillrouter.hybrid-lexical.v1`:

```text
base = exact + 0.6 * fts_norm + 0.35 * fuzzy
score = base * telemetry_multiplier * state_multiplier
```

Exact token matches remain dominant; FTS5 adds stemmed/prefix recall; bounded edit distance adds typo tolerance; historical suggestion-to-fetch conversion contributes a capped multiplier; deprecated skills receive a penalty; ties resolve by `skill_id`.

Search returns the full score decomposition and the inputs required for replay. It does not use embeddings or capability-graph distance in this policy version.

## Skill identity and temporal boundary

A search result identifies a selected body with:

```text
(skill_id, skill_version, revision_id, catalog_generation)
```

Consumer fetch must present `revision_id` and `catalog_generation`. The router recomputes the body revision before return. On mismatch it returns no body.

An agent that has already loaded a body keeps that exact copy for its current task. Catalogue publication does not silently hot-reload an in-flight context. Refresh requires a new search and exact fetch.

## Runtime enforcement boundary

Router-level enforcement includes:

- SQLite read-only catalogue connections in consumer mode;
- separate writable telemetry storage;
- no publication tools in the MCP surface;
- read-only guards on registration and lifecycle methods;
- fail-closed exact revision fetches.

Production deployments should add filesystem ACLs, container mounts, or equivalent controls so a compromised consumer process cannot bypass the router and write skill files directly.

## Conflict model

Skill Router reduces context-level conflict by loading selected revisions on demand. It does not claim to eliminate all distributed-systems conflict.

Remaining measurable surfaces include:

- telemetry write contention under unusually high concurrency;
- publication turnover while consumers hold an earlier generation;
- different agents choosing different valid candidates for ambiguous intent;
- emergency revocation of instructions already loaded into a context;
- downstream conflicts created by the skills themselves.

These are explicit system boundaries, not silent assumptions.
