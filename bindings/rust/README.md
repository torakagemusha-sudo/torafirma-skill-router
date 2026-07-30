# Rust Binding

This crate uses bindgen for the raw C ABI and provides a small safe wrapper with deterministic ownership.

```rust
use torafirma_skillrouter::SkillLibrary;

let mut router = SkillLibrary::open(
    "skill_index.db",
    "skill_telemetry.db",
    true,
)?;

let hits = router.search("windows cpp build", 8, "hybrid", false)?;
let hit = &hits[0];

let loaded = router.fetch(
    hit["skill_id"].as_str().unwrap(),
    hit["revision_id"].as_str().unwrap(),
    hit["catalog_generation"].as_str().unwrap(),
    "",
)?;
```

The wrapper is intentionally not marked `Send` or `Sync`; create separate handles per worker or add an explicit synchronization layer.
