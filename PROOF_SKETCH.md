# Proof Sketch: Invariants of the Skill Router

**Target:** Skill Router 1.1.0  
**Ranking policy:** `tf.skillrouter.hybrid-lexical.v1`

This note states the invariants at the level implemented by the reference C++ engine. Cryptographic conclusions are conditional on SHA-256 collision resistance. Floating-point replay is conditional on the declared tolerance and equivalent IEEE-754 behavior.

## Definitions

For body bytes \(b\),

\[
\operatorname{rev}(b)=\texttt{"sha256:"}\,\|\,H(b).
\]

For a catalog \(C\), let \(\operatorname{Canon}(C)\) be the ascending-`skill_id`, fixed-arity, length-prefixed encoding of:

\[
(\text{skill\_id},\text{description},\text{keywords},
\text{revision},\text{version},\text{publication state}).
\]

Then

\[
G(C)=\texttt{"sha256:"}\,\|\,H(\operatorname{Canon}(C)).
\]

For query \(q\) and skill \(s\),

\[
B(s,q)=E(s,q)+0.6F(s,q)+0.35Z(s,q)
\]

and

\[
U(s,q)=B(s,q)T(s)D(s).
\]

## Invariant 1: Revision Integrity

For every successful pinned fetch of skill \(s\),

\[
r_{\mathrm{expected}}
=
r_{\mathrm{indexed}}
=
\operatorname{rev}(b_{\mathrm{observed}}).
\]

### Proof sketch

The fetch procedure:

1. obtains the indexed revision \(r_{\mathrm{indexed}}\);
2. verifies the presented catalog generation;
3. reads the current body bytes;
4. computes \(r_{\mathrm{observed}}=\operatorname{rev}(b)\);
5. rejects if \(r_{\mathrm{observed}}\ne r_{\mathrm{indexed}}\);
6. rejects if \(r_{\mathrm{expected}}\ne r_{\mathrm{observed}}\);
7. returns the body only after both equalities hold.

Therefore a successful pinned fetch implies the equality above. A mismatch emits a terminal receipt and does not increment the successful-fetch counter.

In operator mode, observed index drift can transition publication state to `STALE`. In consumer/read-only mode, the catalog is not mutated; the same mismatch fails closed and is recorded in telemetry.

## Invariant 2: Catalog Generation Completeness

If

\[
\operatorname{Canon}(C_A)=\operatorname{Canon}(C_B),
\]

then

\[
C_A=C_B.
\]

### Proof sketch

Each field is encoded as decimal byte length, delimiter, exact bytes, and terminator. Each record has six fields and records are ordered by a unique logical key. The stream is therefore uniquely parseable. Equal canonical byte streams decode to equal ordered records.

If

\[
G(C_A)=G(C_B),
\]

then, under the SHA-256 collision-resistance assumption, it is computationally infeasible for the canonical byte streams to differ. Therefore equal generations are treated as evidence of catalog equality, not as an unconditional mathematical identity theorem.

## Invariant 3: Exact-Tier Dominance

Suppose two candidates \(s,t\) have equal telemetry and lifecycle multipliers, and

\[
E(s,q)\ge E(t,q)+1.
\]

Then

\[
B(s,q)>B(t,q).
\]

### Proof

Because \(F,Z\in[0,1]\),

\[
0\le0.6F+0.35Z\le0.95.
\]

The worst case for \(s\) is zero non-exact evidence and the best case for \(t\) is \(0.95\). Hence

\[
B(s,q)-B(t,q)
\ge 1-0.95
=0.05>0.
\]

Thus FTS and fuzzy evidence alone cannot overturn a full one-point exact advantage. This theorem does not hold when telemetry or lifecycle multipliers differ; those are declared utility terms, not hidden effects.

## Invariant 4: Telemetry Monotonicity Before Saturation

Let

\[
T(c,f)=1+\min(0.5,0.5c+0.05\log(1+f)).
\]

For \(c_2\ge c_1\ge0\) and \(f_2\ge f_1\ge0\),

\[
T(c_2,f_2)\ge T(c_1,f_1).
\]

### Proof sketch

Both \(0.5c\) and \(0.05\log(1+f)\) are non-decreasing on their domains. Their sum is non-decreasing, and applying \(\min(0.5,\cdot)\) preserves non-decreasing order. Strict increase is not guaranteed after the cap is reached.

If two candidates have equal base score and lifecycle multiplier, and one has a strictly larger unsaturated telemetry multiplier, it ranks higher.

## Invariant 5: Deterministic Ordering

Given equal:

- canonical catalog generation;
- telemetry counters;
- query bytes;
- normalization policy;
- ranking mode;
- top-\(N\);
- floating-point implementation within declared tolerance;

independent executions return the same ordered candidate list.

### Proof sketch

Every score term is a deterministic function of the listed inputs. Sorting is by descending final score and then ascending `skill_id`. There is no random term, wall-clock term, model inference, hidden priority, or unordered tie-break.

## Invariant 6: Search-to-Fetch Snapshot Integrity

Let search at time \(t\) return candidate \(s\), revision \(r_t\), and generation \(G_t\). A pinned fetch succeeds only when the fetch-time catalog generation equals \(G_t\) and the observed body revision equals \(r_t\).

### Proof sketch

Generation mismatch is checked before body return. Body bytes are re-hashed immediately before return. Any mismatch terminates. Therefore a successful fetch is bound to both the selected body and the selected catalog snapshot.

## Invariant 7: Event-History Monotonicity

Let \(L_n\) be the append-only routing and fetch event history after \(n\) accepted events. Then

\[
L_{n+1}=L_n\mathbin{\|}e_{n+1}.
\]

Therefore \(L_n\) is a prefix of \(L_{n+1}\).

Operational state may cycle, for example `ACTIVE → STALE → INDEXED → ACTIVE`. The lifecycle graph is not acyclic. Monotonicity belongs to the event history and revision lineage, not to the projected state label.

## Invariant 8: Read-Only Publication Separation

A library opened with `CatalogAccess::ReadOnly` cannot perform catalog writes through the router API.

### Proof sketch

The SQLite catalog is opened with `SQLITE_OPEN_READONLY` and `PRAGMA query_only=ON`. Publication methods call a write-authority guard before mutation. Runtime telemetry uses a separate writable database. A consumer can therefore search, exact-fetch, and emit receipts without possessing catalog mutation authority.

Filesystem ACLs or read-only mounts remain required to prevent mutation outside the router process.

## Non-claims

This proof sketch does not claim:

- collision-free hashing in the information-theoretic sense;
- a Markov-chain model of lifecycle state;
- global semantic optimality;
- unbiased telemetry;
- sublinear worst-case hybrid search in the current reference implementation;
- protection against a process that can bypass filesystem or database permissions.
