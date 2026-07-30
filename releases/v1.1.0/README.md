# v1.1.0 Release Staging

The GitHub Actions release workflow is the authoritative asset generator.

It builds and tests Linux and Windows artifacts, adds the whitepaper and proof sketch, computes `SHA256SUMS`, creates a release-specific OpenPGP signature, exports the public key and fingerprint, and publishes all assets through `gh release create`.

The files committed in this directory are release instructions and notes, not substitutes for the final generated binary checksum manifest.
