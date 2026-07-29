# Skill library

Place one skill directory per capability here. Each directory must contain a `SKILL.md` with YAML frontmatter containing at least:

```yaml
---
name: stable-skill-id
version: 1.0.0
description: "When this skill should be selected."
---
```

`version` is optional and defaults to `1.0.0`, but explicit semantic versions are recommended. The operator index computes a SHA-256 revision from the complete file bytes.

Run `..\index-skills.ps1` after adding or changing skills. Consumers must re-search after publication and exact-fetch the returned revision and catalogue generation.
