# Stable C ABI

`skilllib_c.h` is the language-neutral portability boundary for Skill Router 1.1.0.

## Design constraints

- No C++ exception crosses the ABI.
- Every operation returns an explicit `skilllib_status_t`.
- Variable output is returned in `skilllib_buffer_t`.
- The caller releases output with `skilllib_buffer_free`.
- Handles are opaque and closed with `skilllib_close`.
- The most recent handle-local diagnostic is available through `skilllib_last_error`.
- Payloads are UTF-8 JSON except `skilllib_catalog_generation`, which returns a UTF-8 `sha256:` string.
- Fetch requires non-empty `expected_revision` and `expected_catalog_generation` copied from the selected search result; empty values fail closed with `SKILLLIB_INVALID_ARGUMENT`.

The ABI deliberately does not return unmanaged `const char*` result buffers. Such an interface leaves ownership, lifetime, reentrancy, and error distinction undefined.

## Minimal C usage

```c
#include "skilllib_c.h"

skilllib_t* lib = NULL;
skilllib_buffer_t result = {0};

skilllib_status_t status = skilllib_open(
    "skill_index.db",
    "skill_telemetry.db",
    1,
    &lib);

if (status == SKILLLIB_OK) {
  status = skilllib_search(
      lib,
      "windows cpp build",
      8,
      "hybrid",
      0,
      &result);
}

if (status == SKILLIB_OK) {
  fwrite(result.data, 1, result.len, stdout);
  skilllib_buffer_free(&result);
} else if (lib) {
  fprintf(stderr, "%s\n", skilllib_last_error(lib));
}

skilllib_close(lib);
```

## Threading

Each handle owns a full-mutex SQLite connection pair. Do not call the same handle concurrently unless the host binding provides external serialization. Multiple handles may share the read-only catalog and writable telemetry database; SQLite WAL and busy timeout provide the current coordination layer.

## ABI compatibility

Within major ABI version 1:

- enum numeric values will not be reassigned;
- existing function signatures will not change;
- new status values or functions may be appended;
- JSON objects may gain fields but existing fields retain meaning;
- ranking behavior remains separately versioned by `skilllib_ranking_policy()`.

Bindings must expose both the engine version and ranking policy.
