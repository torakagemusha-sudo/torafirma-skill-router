# A Monoid Over Hashes: Closed-Form Skill Routing for Deterministic Agentic Systems

**Thomas Helm**  
**Torafirma Systems**  
**Version:** 1.1.0 formalization draft

## Abstract

We present a deterministic, content-addressed routing system for agent skills. Exact skill bodies are identified by SHA-256 revisions; a published catalog is committed by hashing an injective canonical serialization; routing is the exact argmax of an explicit bounded utility function combining lexical tiers, SQLite FTS5, bounded edit distance, lifecycle state, and observed usage telemetry. The system contains no vector database, learned classifier, embedding model, or hidden priority term.

The algebraic object is not the set of raw digest strings. It is the set of finite collections of immutable, content-addressed revision records under union. This forms a commutative idempotent monoid. A deterministic catalog projection defines the serialized catalog state, and SHA-256 supplies a computational commitment to its canonical byte representation. Equality of catalog generations therefore gives computational evidence of catalog equality under the collision-resistance assumption; it is not an information-theoretic proof.

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

The title â€œA Monoid Over Hashesâ€ refers to this monoid of immutable content-addressed records. It does **not** claim that ordinary SHA-256 concatenation itself defines the monoid operation or that the catalog digest is a homomorphism.

### 2.3 Publication projection

A catalog is a deterministic projection

\[
P_\pi:\mathcal{R}\rightarrow\mathcal{C},
\]

parameterized by publication policy \(\pi\). The current implementation stores at most one current row per logical `skill_id`. Catalog generation commits every stored row, normalizing `INDEXED` and `ACTIVE` to publication state `AVAILABLE`; `ARCHIVED` rows remain part of the generation commitment. Search admissibility is a separate projection that excludes `ARCHIVED` rows unless `include_archived` is explicitly enabled.

The projection is deliberately separate from revision storage: content addressability determines what a body is; publication policy determines whether it is eligible for a given search.

### 2.4 Canonical serialization

For a published catalog \(C\), rows are ordered by ascending `skill_id`. Each field is encoded as

\[
L(x)=\operatorname{decimal}(|x|)\,\|\,\texttt{:}\,\|\,x\,\|\,\texttt{;},
\]

where \(|x|\) is the byte length of the stored string; protocol-facing text is intended to be UTF-8, while the serialization itself is defined over bytes. The current generation encoding is

\[
\operatorname{Canon}(C)=
\big\|_{s\in\operatorname{sort}_{i}(C)}
L(i_s)\|L(d_s)\|L(k_s)\|L(r_s)\|L(v_s)\|L(\operatorname{pub}(\sigma_s)).
\]

Because every field is length-prefixed and each record has a fixed arity, the encoding is uniquely parseable over byte strings. Thus:

\[
\operatorname{Canon}(C_A)=\operatorname{Canon}(C_B)\Rightarrow C_A=C_B.
\]

The catallog generation is

\[)¡¤õqÑ•áÑÑÑì‰Í¡„ÈÔØè‰õp±qñp± ¡q½Á•É…Ñ½É¹…µ•í…¹½¹ô¡¤¤¸)qt()U¹‘•È½±±¥Í¥½¸É•Í¥ÍÑ…¹”è()ql)¡}¤õ¡}¤)q1½¹É¥¡Ñ…ÉÉ½İ}íqÑ•áÑí½µÁÕÑ…Ñ¥½¹…±õô)q½Á•É…Ñ½É¹…µ•í…¹½¹ô¡}¤õq½Á•É…Ñ½É¹…µ•í…¹½¹ô¡}¤¸)qt()Q¡¥Ì¥Ì„½µÁÕÑ…Ñ¥½¹…°½µµ¥Ñµ•¹Ğ°¹½Ğ„±½¥…°‰¥½¹‘¥Ñ¥½¹…°Õ…É…¹Ñ••……¥¹ÍĞ…¸…‘Ù•ÉÍ…Éäİ¥Ñ „M!´ÈÔØ½±±¥Í¥½¸¸((ŒŒ€Ì¸I…¹­¥¹œ…ÌUÑ¥±¥Ñä5…á¥µ¥é…Ñ¥½¸((ŒŒŒ€Ì¸ÄEÕ•Éä¹½Éµ…±¥é…Ñ¥½¸()ÅÕ•Éäp¡Åp¤¥Ì±½İ•Èµ…Í•°Ñ½­•¹¥é•½Ù•È…±Á¡…¹Õµ•É¥Œ…¹Õ¹‘•ÉÍ½É”¡…É…Ñ•ÉÌ°‘•‘ÕÁ±¥…Ñ•¥¸™¥ÉÍĞµ½ÕÉÉ•¹”½É‘•È°™¥±Ñ•É•‰äÑ¡”ÁÕ‰±¥ŒÍÑ½Áİ½ÉÍ•Ğ°…¹ÍÑÉ¥ÁÁ•½˜½¹”µ¡…É…Ñ•ÈÑ½­•¹Ì¸1•ĞÑ¡”É•ÍÕ±Ñ¥¹œ½É‘•É•Í•Ğ‰”p¡P¡Ä¥p¤¸()Q¡”ÅÕ•Éä‘¥•ÍĞ¥Ì()ql)D¡Ä¤õqÑ•áÑÑÑì‰Í¡„ÈÔØè‰õp±qñp± ¡q½Á•É…Ñ½É¹…µ•í©½¥¹ô¡P¡Ä¤¤¤¸)qt((ŒŒŒ€Ì¸Èá…Ğ±•á¥…°ÕÑ¥±¥Ñä()½ÈÑ½­•¸p¡Ñp¤…¹Í­¥±°p¡Íp¤°½¹±äÑ¡”¡¥¡•ÍĞµ…Ñ¡¥¹œÑ¥•È½¹ÑÉ¥‰ÕÑ•Ìè()ql)”¡Ğ±Ì¤ô)q‰•¥¹í…Í•Íô(Ì°™Ñq¥¸-}Íqp(È°™Ñq¥¸9}Íq±…¹Ñq¹½Ñ¥¸-}Íqp(Ä°™Ñq¥¸}Íq±…¹Ñq¹½Ñ¥¸¡-}ÍqÕÀ9}Ì¥qp(À°™qÑ•áÑí½Ñ¡•Éİ¥Í•ô¸)q•¹‘í…Í•Íô)qt()Q¡•¸()ql)¡Ì±Ä¤õqÍÕµ}íÑq¥¸P¡Ä¥ô”¡Ğ±Ì¤¸)qt((ŒŒŒ€Ì¸ÌQLÕÑ¥±¥Ñä()QLÔ¥¹‘•á•ÌÍ­¥±±}¥‘€°‘•ÍÉ¥ÁÑ¥½¸°…¹­•åİ½É‘Ìİ¥Ñ Ñ½­•¹¥é•ÈÁ½ÉÑ•ÈÕ¹¥½‘”ØÅ€¸	4ÈÔ½±Õµ¸İ•¥¡ÑÌ…É”p È°Ä°Íp¤¸I…Ü	4ÈÔ½ÕÑÁÕĞ¥Ì¹•…Ñ•Í¼Ñ¡…Ğ±…É•È¥Ì‰•ÑÑ•È¸()½ÈÑ¡”ÕÉÉ•¹ĞQLÉ•ÍÕ±ĞÁ½ÁÕ±…Ñ¥½¸è()ql)¡Ì±Ä¤ô)q‰•¥¹í…Í•Íô(À°™ÍqÑ•áÑì¡…Ì¹¼QLµ…Ñ¡õqp(Ä°™}íqµ…áôõ}íqµ¥¹õqp(À¸Ô¬À¸Õq™É…í}íqµ…Ñ¡ÉµíÉ…İõô¡Ì±Ä¤µ}íqµ¥¹õô)í}íqµ…áôµ}íqµ¥¹õô°™qÑ•áÑí½Ñ¡•Éİ¥Í•ô¸)q•¹‘í…Í•Íô)qt()Q¡•É•™½É”p¡¡Ì±Ä¥q¥¹lÀ°Åup¤¸(ŒŒŒ€Ì¸Ğ	½Õ¹‘•™ÕééäÕÑ¥±¥Ñä()Õééäµ…Ñ¡¥¹œ¥Ì…ÁÁ±¥•½¹±äÑ¼ÅÕ•ÉäÑ½­•¹ÌÑ¡…Ğµ¥ÍÌ•Ù•Éä•á…ĞÑ¥•È¸½ÈÑ½­•¸p¡Ñp¤°()ql)q‘•±Ñ„¡Ğ¤ô)q‰•¥¹í…Í•Íô(Ä°™ñÑñq±”Ñqp(È°™ÑğøĞ¸)q•¹‘í…Í•Íô)qt()1•Ğp¡¡Ğ±Ì¥p¤‰”Ñ¡”µ¥¹¥µÕ´1•Ù•¹Í¡Ñ•¥¸‘¥ÍÑ…¹”‰•Ñİ••¸p¡Ñp¤…¹…¹ä­•åİ½É°¹…µ”°½È‘•ÍÉ¥ÁÑ¥½¸Ñ½­•¸°•Ù…±Õ…Ñ•½¹±äÕÀÑ¼p¡q‘•±Ñ„¡Ğ¥p¤¸Q¡”½¹ÑÉ¥‰ÕÑ¥½¸¥Ì()ql)è¡Ğ±Ì¤ô)q‰•¥¹í…Í•Íô(Äµq™É…í¡Ğ±Ì¥õíñÑñô°™¡Ğ±Ì¥q±•q‘•±Ñ„¡Ğ¥qp(À°™qÑ•áÑí½Ñ¡•Éİ¥Í•ô¸)q•¹‘í…Í•Íô)qt()Q¡•¸()ql)h ¡Ì±Ä¤õq™É…ìÅõíñP¡Ä¥ñõqÍÕµ}íÑq¥¸P¡Ä¥õè¡Ğ±Ì¤°)qÅÅÕ…h¡Ì±Ä¥q¥¹lÀ°Åt¸)qt((ŒŒŒ€Ì¸Ô	…Í”ÕÑ¥±¥Ñä()½È¡å‰É¥µ½‘”è()ql)¡Ì±Ä¤õ¡Ì±Ä¤¬À¸Ù¡Ì±Ä¤¬À¸ÌÕh¡Ì±Ä¤¸)qt()M¥¹”()ql(À¸Ù¬À¸ÌÕiq±”À¸äÔğÄ°)qt()¹½¸µ•á…Ğ•Ù¥‘•¹”…¹¹½Ğ½Ù•ÉÑÕÉ¸„™Õ±°½¹”µÁ½¥¹Ğ•á…ĞµÑ¥•È…‘Ù…¹Ñ…”¸%Ğ…¸…‘É•…±°…¹É•Í½±Ù”Ñ¥•Ìİ¥Ñ¡¥¸…¸•á…ĞÑ¥•È¸((ŒŒŒ€Ì¸ØQ•±•µ•ÑÉä…Ì…¸•áÁ±¥¥Ğ•µÁ¥É¥…°ÁÉ¥½È()1•Ğp¡…}Íp¤‰”É•ÑÕÉ¹•µÍ•…É •áÁ½ÍÕÉ”½Õ¹Ğ…¹p¡™}Íp¤ÍÕ•ÍÍ™Õ°™•Ñ ½Õ¹Ğ¸•™¥¹”()ql)}Ìô)q‰•¥¹í…Í•Íô)™}Ì½…}Ì°™…}ÌøÁqp(À°™…}ÌôÀ¸)q•¹‘í…Í•Íô)qt()Q¡”µÕ±Ñ¥Á±¥•È¥Ì()ql)P¡Ì¤ôÄ­qµ¥¹q±•™Ğ (À¸Ô±pì