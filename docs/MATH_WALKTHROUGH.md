# Fifteen-Minute Math Walkthrough

## 0:00–2:00 — Three failure modes

Open with one concrete sequence:

1. Search chooses revision \(r_1\).
2. The file changes.
3. A name-only fetch loads revision \(r_2\).

State the three problems:

- **Version drift:** logical name is not byte identity.
- **Opaque semantic routing:** embedding scores cannot be reconstructed from the repository alone.
- **Catalog disagreement:** two agents may rank against different admitted skill sets.

Board equation:

\[
\text{selection identity} =
(\text{skill\_id},\text{version},\text{revision},\text{generation}).
\]

## 2:00–6:00 — Lifecycle and monotonic history

Draw:

```text
REGISTERED -> INDEXED -> ACTIVE <-> STALE -> DEPRECATED -> ARCHIVED
```

Then draw a second line beneath it:

```text
e0 -> e1 -> e2 -> e3 -> ...
```

Explain the distinction:

- lifecycle state can move backward after explicit repair;
- the event history only appends;
- therefore the system is a deterministic transition system with a monotonic history, not a monotonic Markov chain.

Demonstrate:

```text
index(body_1)  -> revision r1
fetch(r1)      -> ACTIVE
mutate file    -> observed r2
fetch(r1)      -> REVISION_MISMATCH / STALE observation
re-index       -> published revision r2
```

## 6:00–10:00 — Derive the ranking function

Start with exact tiers:

\[
E=\sum_t
\begin{cases}
3&\text{keyword}\\
2&\text{name}\\
1&\text{description}\\
0&\text{miss}
\end{cases}
\]

Add normalized FTS and bounded fuzzy evidence:

\[
B=E+0.6F+0.35Z.
\]

Circle:

\[
0.6+0.35=0.95<1.
\]

Explain: FTS plus fuzzy cannot overturn a full one-point exact advantage when the multipliers are equal.

Add telemetry:

\[
T=1+\min(0.5,0.5c+0.05\log(1+f)).
\]

Add lifecycle:

\[
D=
\begin{cases}
0.3&\text{deprecated}\\
1&\text{otherwise}.
\end{cases}
\]

Final utility:

\[
U=B\,T\,D,\qquad
s^\star=\arg\max_s(U(s,q),-\operatorname{lex}(skill\_id)).
\]

Say precisely: “This is the exact argmax of the declared utility over the evaluated catalog. It is not a claim of universal semantic optimality.”

## 10:00–13:00 — Catalog generation

Write the length-prefix function:

\[
L(x)=|x|:x;
\]

Then:

\[
\operatorname{Canon}(C)=
\big\|_{\text{skill\_id order}}
L(id)L(desc)L(keywords)L(rev)L(version)L(state).
\]

Finally:

\[
G(C)=\texttt{sha256:}H(\operatorname{Canon}(C)).
\]

Explain:

- deterministic order;
- fixed field arity;
- byte lengths remove delimiter ambiguity;
- equal generations are computational evidence of equal catalogs under SHA-256 collision resistance.

Do not call SHA-256 itself the monoid operation. The monoid is finite union of immutable revision records; the digest commits to the published projection.

## 13:00–15:00 — Live CLI falsification demo

```powershell
# 1. Register
.\skillrouter.exe register .\demo\SKILL.md `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role operator

# 2. Search
$hit = (.\skillrouter.exe search "deterministic build" `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role consumer `
  --json | ConvertFrom-Json)[0]

# 3. Exact fetch succeeds
.\skillrouter.exe fetch $hit.skill_id `
  --revision $hit.revision_id `
  --catalog-generation $hit.catalog_generation `
  --db .\skill_index.db `
  --telemetry-db .\skill_telemetry.db `
  --role consumer

# 4. Mutate SKILL.md, then repeat the exact fetch
# Expected: REVISION_MISMATCH and no body returned.

# 5. Re-index, search again, and show a new revision/generation.
```

Close with the falsifiable guarantee:

> Same declared inputs produce the same rank; different bytes cannot pass the selected revision check.
