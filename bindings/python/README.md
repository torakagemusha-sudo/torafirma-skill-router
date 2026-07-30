# Python Binding

This directory provides a direct pybind11 convenience binding to the C++ engine.

The stable cross-language contract remains `skill-router/skilllib_c.h`. pybind11 is used here for Python ergonomics, not as the ABI boundary.

## Build

```bash
python -m pip install .
```

## Example

```python
from skillrouter_native import SkillLibrary, SearchMode

router = SkillLibrary(
    "skill_index.db",
    "skill_telemetry.db",
    read_only=True,
)

hits = router.search("windows cpp build", mode=SearchMode.HYBRID)
hit = hits[0]

loaded = router.fetch(
    hit["skill_id"],
    hit["revision_id"],
    hit["catalog_generation"],
)
print(loaded["body"])
```
