# A Monoid Over Hashes: Closed-Form Skill Routing for Deterministic Agentic Systems

**Thomas Helm**  
**Torafirma Systems**  
**Version:** 1.1.0 formalization draft

## Abstract

We present a deterministic, content-addressed routing system for agent skills. Exact skill bodies are identified by SHA-256 revisions; a published catalog is committed by hashing an injective canonical serialization; routing is the exact argmax of an explicit bounded utility function combining lexical tiers, SQLite FTS5, bounded edit distance, lifecycle state, and observed usage telemetry. The system contains no vector database, learned classifier, embedding model, or hidden priority term.

The algebraic object is not the set of raw digest strings. It is the set of finite collections of immutable, content-addressed revision records under union. This forms a commutative idempotent monoid. A deterministic publication projection selects the active catalog, and SHA-256 supplies a computational commitment to its canonical byte representation. Equality of catalog generations therefore gives computational evidence of catalog equality under the collision-resistance assumption; it is not an information-theoretic proof.

The reference implementation is a local-first C++20 engine backed by SQLite. It provides explicit score decomposition, deterministic tie-breaking, revision-pinned fetch, catalog-generation pinning, append-only decision receipts, and separation between read-only consumers and privileged publishers.

## 1. Problem Statement

### 1.1 Version drift in agent tooling

A logical skill name is not an immutable instruction set. If an agent searches for a skill, another process changes the underlying file, and the agent later fetches by name alone, the loaded instructions may differ from the instructions that were ranked.

The router therefore distinguishes:

\[
\text{logical identity} \neq \text{byte identity}.
\]

A skill body \(b\) has revision

\[
r(b)=\texttt{"sha256:"}\,\|\,H(b),
\]

where \(H\) is SHA-256 and \(\|\) denotes concatenation. A routed candidate is identified by

\[
(\text{skill\_id},\text{skill\_version},\text{revision\_id},\text{catalog\_generation}).
\]

### 1.2 Semantic retrieval without embedding indirection

Embedding retrieval is useful, but it introduces a learned, model-dependent projection whose behavior may vary with model version, quantization, index construction, and training distribution. The present system instead uses a fully inspectable lexical utility:

1. exact token tiers;
2. Porter-stemmed FTS5/BM25 retrieval;
3. bounded Levenshtein correction;
4. explicit telemetry and lifecycle multipliers;
5. lexicographic tie-breaking.

Every contribution is emitted in the decision record.

### 1.3 Decentralized catalog agreement

Multiple consumers must be able to determine whether they ranked against the same published catalog. The router computes one generation identifier over a canonical serialization of publication-relevant catalog state. Replicas with different bytes overwhelmingly likely produce different generations; replicas with equal generations are treated as equivalent under the SHA-256 collision-resistance assumption.

## 2. Algebraic Foundation

### 2.1 Skill revisions

A runtime skill row is modeled as

\[
s=(i,v,d,k,p,r,\sigma,\tau),
\]

where:

- \(i\) is the logical skill identifier;
- \(v\) is the human-authored compatibility version;
- \(d\) is the description;
- \(k\) is the keyword set;
- \(p\) is the local path;
- \(r=H(b)\) is the revision of body \(b\);
- \(\sigma\) is lifecycle state;
- \(\tau\) is telemetry.

The path and telemetry do not participate in byte identity. The body revision does.

### 2.2 The revision-corpus monoid

Let \(\mathcal{U}\) be the universe of immutable revision records. Let

\[
\mathcal{R}=\{R\mid R\subseteq\mathcal{U},\;R\text{ finite}\}.
\]

Define composition by set union:

\[
R_A\oplus R_B = R_A\cup R_B.
\]

Then \((\mathcal{R},\oplus,\varnothing)\) is a commutative idempotent monoid:

**Closure**

\[
R_A,R_B\in\mathcal{R}\Rightarrow R_A\cup R_B\in\mathcal{R}.
\]

**Associativity**

\[
(R_A\cup R_B)\cup R_C=R_A\cup(R_B\cup R_C).
\]

**Identity**

\[
R\cup\varnothing=\varnothing\cup R=R.
\]

**Idempotence**

\[
R\cup R=R.
\]

The title “A Monoid Over Hashes” refers to this monoid of immutable content-addressed records. It does **not** claim that ordinary SHA-256 concatenation itself defines the monoid operation or that the catalog digest is a homomorphism.

### 2.3 Publication projection

A catalog is a deterministic projection

\[
P_\pi:\mathcal{R}\rightarrow\mathcal{C},
\]

parameterized by publication policy \(\pi\). The current implementation stores at most one admitted row per logical `skill_id`, normalizes `INDEXED` and `ACTIVE` to publication state `AVAILABLE`, and excludes `ARCHIVED` skills unless explicitly requested.

The projection is deliberately separate from revision storage: content addressability determines what a body is; publication policy determines whether it is eligible.

### 2.4 Canonical serialization

For a published catalog \(C\), rows are ordered by ascending `skill_id`. Each field is encoded as

\[
L(x)=\operatorname{decimal}(|x|)\,\|\,\texttt{:}\,\|\,x\,\|\,\texttt{;},
\]

where \(|x|\) is the byte length of the UTF-8 field. The current generation encoding is

\[
\operatorname{Canon}(C)=
\big\|_{s\in\operatorname{sort}_{i}(C)}
L(i_s)\|L(d_s)\|L(k_s)\|L(r_s)\|L(v_s)\|L(\operatorname{pub}(\sigma_s)).
\]

Because every field is length-prefixed and each record has a fixed arity, the encoding is uniquely parseable over byte strings. Thus:

\[
\operatorname{Canon}(C_A)=\operatorname{Canon}(C_B)\Rightarrow C_A=C_B.
\]

The catalog generation is

\[
G(C)=\texttt{"sha256:"}\,\|\,H(\operatorname{Canon}(C)).
\]

Under collision resistance:

\[
G(C_A)=G(C_B)
\Longrightarrow_{\text{computational}}
\operatorname{Canon}(C_A)=\operatorname{Canon}(C_B).
\]

This is a computational commitment, not a logical biconditional guaranteed against an adversary with a SHA-256 collision.

## 3. Ranking as Utility Maximization

### 3.1 Query normalization

A query \(q\) is lower-cased, tokenized over alphanumeric and underscore characters, deduplicated in first-occurrence order, filtered by the public stopword set, and stripped of one-character tokens. Let the resulting ordered set be \(T(q)\).

The query digest is

\[
Q(q)=\texttt{"sha256:"}\,\|\,H(\operatorname{join}(T(q))).
\]

### 3.2 Exact lexical utility

For token \(t\) and skill \(s\), only the highest matching tier contributes:

\[
e(t,s)=
\begin{cases}
3,&t\in K_s\\
2,&t\in N_s\land t\notin K_s\\
1,&t\in D_s\land t\notin(K_s\cup N_s)\\
0,&\text{otherwise}.
\end{cases}
\]

Then

\[
E(s,q)=\sum_{t\in T(q)} e(t,s).
\]

### 3.3 FTS utility

FTS5 indexes `skill_id`, description, and keywords with tokenizer `porter unicode61`. BM25 column weights are \(2,1,3\). Raw BM25 output is negated so that larger is better.

For the current FTS result population:

\[
F(s,q)=
\begin{cases}
0,&s\text{ has no FTS match}\\
1,&F_{\max}=F_{\min}\\
0.5+0.5\frac{F_{\mathrm{raw}}(s,q)-F_{\min}}
{F_{\max}-F_{\min}},&\text{otherwise}.
\end{cases}
\]

Therefore \(F(s,q)\in[0,1]\).

### 3.4 Bounded fuzzy utility

Fuzzy matching is applied only to query tokens that miss every exact tier. For token \(t\),

\[
\delta(t)=
\begin{cases}
1,&|t|\le4\\
2,&|t|>4.
\end{cases}
\]

Let \(d(t,s)\) be the minimum Levenshtein distance between \(t\) and any keyword, name, or description token, evaluated only up to \(\delta(t)\). The contribution is

\[
z(t,s)=
\begin{cases}
1-\frac{d(t,s)}{|t|},&d(t,s)\le\delta(t)\\
0,&\text{otherwise}.
\end{cases}
\]

Then

\[
Z(s,q)=\frac{1}{|T(q)|}\sum_{t\in T(q)}z(t,s),
\qquad Z(s,q)\in[0,1].
\]

### 3.5 Base utility

For hybrid mode:

\[
B(s,q)=E(s,q)+0.6F(s,q)+0.35Z(s,q).
\]

Since

\[
0.6F+0.35Z\le0.95<1,
\]

non-exact evidence cannot overturn a full one-point exact-tier advantage. It can add recall and resolve ties within an exact tier.

### 3.6 Telemetry as an explicit empirical prior

Let \(a_s\) be returned-search exposure count and \(f_s\) successful fetch count. Define

\[
c_s=
\begin{cases}
f_s/a_s,&a_s>0\\
0,&a_s=0.
\end{cases}
\]

The multiplier is

\[
T(s)=1+\min\left(
0.5,\;
0.5c_s+0.05\log(1+f_s)
\right).
\]

Thus \(T(s)\in[1,1.5]\). It is non-decreasing in both \(c_s\) and \(f_s\) before saturation and constant after saturation. Calling it a “Bayesian prior” is an interpretation, not a conjugate Bayesian derivation: it is an explicit history-dependent empirical prior whose sufficient statistics are retained in telemetry.

The exposure denominator counts only candidates returned in bounded top-\(N\). It is therefore selection-biased and must not be represented as an unbiased estimator of global relevance.

### 3.7 Lifecycle multiplier

\[
D(s)=
\begin{cases}
0.3,&\sigma_s=\texttt{DEPRECATED}\\
1,&\text{otherwise}.
\end{cases}
\]

Archived skills are removed from the admissible set by default.

### 3.8 Closed-form selection

Final utility is

\[
U(s\mid q)=B(s,q)T(s)D(s).
\]

Let \(\mathcal{A}(q,C)\) be the admissible catalog candidates with \(B(s,q)>0\). The selected skill is

\[
s^\star=
\arg\max_{s\in\mathcal{A}(q,C)}
\left(U(s\mid q),-\operatorname{lex}(i_s)\right),
\]

equivalently: descending final score, then ascending `skill_id`.

The claim is precise: the router computes the exact argmax of this declared utility over the evaluated candidate set. It is not claimed to be a universal optimum over every possible semantic representation.

## 4. State and Revision Invariants

### 4.1 Operational lifecycle

The normal lifecycle is

\[
\texttt{REGISTERED}\rightarrow
\texttt{INDEXED}\rightarrow
\texttt{ACTIVE}
\leftrightarrow
\texttt{STALE}\rightarrow
\texttt{DEPRECATED}\rightarrow
\texttt{ARCHIVED}.
\]

This is a deterministic finite-state transition system, not a Markov chain. Administrative restoration or re-indexing can introduce additional explicit transitions. The transition relation contains cycles, while the associated event history remains append-only.

### 4.2 Revision integrity

For a successful pinned fetch of skill \(s\):

\[
r_{\mathrm{expected}}
=
r_{\mathrm{indexed}}
=
H(b_{\mathrm{observed}}).
\]

The router checks catalog generation first, hashes the file immediately before return, compares observed and indexed revisions, compares observed and expected revisions, and only then returns the body and increments successful-fetch telemetry.

### 4.3 Consumer and operator behavior

An operator may mark a drifted row `STALE`. A read-only consumer cannot mutate the catalog; it fails closed and emits a drift receipt into separate writable telemetry. This separation is deliberate: consumer processes should not possess publication authority.

### 4.4 Generation pinning

A search returns \(G(C_t)\). Fetch must present the same generation. If publication changes between search and fetch,

\[
G(C_t)\ne G(C_{t+1}),
\]

the fetch terminates with `CATALOG_GENERATION_MISMATCH`, even when the selected skill body itself is unchanged. This prevents a decision made against one admitted catalog from being silently completed against another.

## 5. Performance and Correctness

### 5.1 Current complexity

The reference implementation currently scans admitted catalog rows to combine exact, fuzzy, lifecycle, and telemetry components. Let:

- \(n\) be catalog size;
- \(m=|T(q)|\);
- \(p_s\) be the searchable token pool for skill \(s\);
- \(k\) be the number of FTS matches.

The current upper-bound structure is approximately

\[
O\left(
T_{\mathrm{FTS}}(q)+
\sum_{s=1}^{n}m\,p_s\,\delta^2+
n\log n
\right),
\]

with edit-distance work bounded by maximum distance \(1\) or \(2\), early row termination, query-size limits, and skill-size limits. The implementation is therefore deterministic and bounded, but not yet asymptotically optimized for very large catalogs.

A future candidate-first implementation can reduce fuzzy rescoring to a bounded candidate set \(k\), yielding approximately

\[
T_{\mathrm{FTS}}(q)+O(km\bar p)+O(k\log k).
\]

No unmeasured throughput claim is made here. Benchmarks must report corpus construction, CPU, SQLite build, FTS availability, warm/cold cache, query distribution, top-\(N\), and telemetry state.

### 5.2 Correctness surfaces

The 1.1.0 contract is falsified if any of the following occurs:

1. two byte-distinct skill bodies receive the same revision without a SHA-256 collision;
2. a pinned fetch returns a body after revision or generation mismatch;
3. equal inputs and telemetry snapshots produce a different ordered result;
4. an undeclared score component changes rank;
5. a read-only consumer mutates publication state;
6. catalog serialization is ambiguous;
7. an exact one-tier advantage is overturned by FTS plus fuzzy evidence alone.

These properties are suitable for deterministic regression, cross-language conformance, and property-based testing.

## 6. Multi-Language Contract

The canonical portability boundary is a C ABI with:

- opaque handles;
- explicit status codes;
- caller-visible error retrieval;
- allocated output buffers with a matching free function;
- UTF-8 JSON payloads;
- no exception propagation across the ABI;
- explicit revision and catalog-generation parameters.

Python may additionally bind C++ directly through pybind11 for ergonomics. Node uses N-API. Rust generates the raw ABI with bindgen and wraps ownership in a safe `Drop` type. All bindings must preserve the same ranking policy, identity tuple, and failure statuses.

## 7. Conclusion

The router is deterministic because every routing input and every ranking term is explicit. It is content-addressed because loaded instructions are pinned to SHA-256 body revisions. It is consensus-capable because a canonical catalog projection has one generation commitment. It is explainable because every score component and tie-break is emitted. It is local-first because SQLite and the filesystem are sufficient.

The defensible claim is not that every routing problem has been solved. The claim is narrower and stronger:

> For a declared lexical utility, a declared catalog snapshot, and a declared telemetry snapshot, Skill Router computes a reproducible exact selection and refuses to load bytes that are not the bytes that were selected.

No black box is required for that guarantee.
