# Node Binding

This directory provides a synchronous N-API binding over the stable C ABI.

```bash
npm install
npm run build
```

```js
const router = require("./");
const handle = router.open(
  "skill_index.db",
  "skill_telemetry.db",
  true
);

const hits = router.search(handle, "windows cpp build", 8, "hybrid", false);
const selected = hits[0];

const loaded = router.fetch(
  handle,
  selected.skill_id,
  selected.revision_id,
  selected.catalog_generation
);

console.log(loaded.body);
router.close(handle);
```

The external handle is finalized automatically, but explicit `close` is recommended.
