# Skill Router 1.1.0 — Formal Contract and Portable ABI

This release turns the existing deterministic router into a formally documented, language-neutral contract.

## Included

- public ranking equation and exact-tier dominance proof;
- SHA-256 skill revisions and canonical catalog generation;
- revision- and generation-pinned fetch;
- read-only consumer / privileged publisher separation;
- append-only routing decisions and fetch receipts;
- `WHITEPAPER.md`;
- `PROOF_SKETCH.md`;
- stable C ABI with explicit ownership and status codes;
- pybind11, N-API, and Rust bindgen scaffolds;
- Linux and Windows C ABI smoke tests;
- signed `SHA256SUMS`.

## Signature scope

The release workflow creates a temporary release-specific OpenPGP key, signs the final checksum manifest, exports the corresponding public key, and destroys the private key with the runner. This proves that the included manifest and signature were generated together. It does not establish long-term publisher identity or a web-of-trust chain.

For persistent publisher authentication, replace the ephemeral workflow key with a protected organization signing key stored in GitHub Actions secrets or an external signing service.

## Mathematical scope

The monoid is finite union over immutable content-addressed revision records. SHA-256 commits to the canonical published catalog; ordinary digest concatenation is not claimed to be the monoid operation.

The router computes the exact argmax of the declared bounded utility over the evaluated catalog. It does not claim universal semantic optimality.
