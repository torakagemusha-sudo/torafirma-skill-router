# Skill Router 1.1.0 for Windows

This is a portable Windows x64 package containing the router executable, interface skill, indexing helper, and documentation. It does not contain a private skill library or prebuilt catalogue.

## Quick start

1. Extract the ZIP to a writable directory.
2. Add skills beneath `skill_library\`. Each needs `name` and `description` frontmatter; `version` is optional.
3. Run the operator index:

```powershell
powershell -ExecutionPolicy Bypass -File .\index-skills.ps1
```

4. Search and exact-fetch:

```powershell
$hit = (.\skillrouter.exe search "windows cpp build" --json | ConvertFrom-Json)[0]

.\skillrouter.exe fetch $hit.skill_id `
  --revision $hit.revision_id `
  --catalog-generation $hit.catalog_generation
```

The default files are:

```text
skill_index.db
skill_index.db.telemetry.db
```

Search/fetch/MCP/HTTP default to consumer mode and open the catalogue read-only. Indexing and lifecycle administration use operator mode.

## Upgrade from 1.0.0

Stop any running router consumers and execute a complete index pass before using the 1.1.0 binary. This replaces legacy short content hashes with SHA-256 revision identities. An old catalogue is rejected rather than silently treated as verified.

## MCP registration

```powershell
.\skillrouter.exe mcp `
  --db C:\absolute\skill_index.db `
  --telemetry-db C:\absolute\skill_telemetry.db `
  --role consumer
```

The agent performs `skill_search`, selects a candidate, then calls `skill_fetch` with the returned `revision_id` and `catalog_generation`.

## Integrity

`SHA256SUMS.txt` contains a digest for every shipped payload file.

```powershell
Get-FileHash .\skillrouter.exe -Algorithm SHA256
```

The executable is built for Windows x64 with SQLite FTS5 enabled and the MSVC runtime linked statically. Only standard Windows system DLLs are required.

## Security boundary

The router opens the catalogue read-only in consumer mode and uses a separate writable telemetry database. For production use, also deny the consumer operating-system identity write permission over `skill_library\` and `skill_index.db`.

## License

MIT License. Copyright (c) 2026 Thomas Helm.
