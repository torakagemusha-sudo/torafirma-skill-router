#!/usr/bin/env python3
"""End-to-end Skill Router 1.1 contract smoke test."""
from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import json
import sqlite3
import subprocess
import tempfile
from pathlib import Path


def run(binary: Path, *args: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(binary), *args],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != expect:
        raise AssertionError(
            f"command returned {result.returncode}, expected {expect}: {args}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def write_skill(path: Path, marker: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "---\n"
        "name: contract-smoke\n"
        "version: 1.1.0\n"
        'description: "Use for exact revision and concurrent telemetry contract smoke testing"\n'
        "---\n"
        "# Contract Smoke\n\n"
        f"{marker}\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    ns = parser.parse_args()
    binary = ns.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="skillrouter-contract-") as temp:
        root = Path(temp)
        library = root / "library"
        skill = library / "contract-smoke" / "SKILL.md"
        catalog = root / "catalog.db"
        telemetry = root / "telemetry.db"
        common = ("--db", str(catalog), "--telemetry-db", str(telemetry))

        write_skill(skill, "REVISION_A")
        indexed = json.loads(run(binary, "index", str(library), *common, "--role", "operator").stdout)
        assert indexed["created"] == 1

        hit = json.loads(
            run(binary, "search", "revision telemetry", *common, "--role", "consumer", "--json").stdout
        )[0]
        assert hit["ranking_policy"] == "tf.skillrouter.hybrid-lexical.v1"
        assert hit["revision_id"].startswith("sha256:")
        assert hit["catalog_generation"] == indexed["catalog_generation"]
        assert "score_components" in hit

        exact_args = (
            "fetch", hit["skill_id"],
            "--revision", hit["revision_id"],
            "--catalog-generation", hit["catalog_generation"],
            *common, "--role", "consumer",
        )
        fetched = run(binary, *exact_args)
        assert "REVISION_A" in fetched.stdout

        # A body mutation between search and fetch must fail closed.
        write_skill(skill, "MUTATED_WITHOUT_REINDEX")
        drifted = run(binary, *exact_args, expect=1)
        assert "REVISION_MISMATCH" in drifted.stderr

        # Re-indexing creates a new catalogue generation; the old selection must fail.
        write_skill(skill, "REVISION_B")
        updated = json.loads(run(binary, "index", str(library), *common, "--role", "operator").stdout)
        assert updated["updated"] == 1
        stale_generation = run(binary, *exact_args, expect=1)
        assert "CATALOG_GENERATION_MISMATCH" in stale_generation.stderr

        # The consumer role may not publish lifecycle state.
        denied = run(binary, "deprecate", hit["skill_id"], *common, "--role", "consumer", expect=1)
        assert "unavailable in consumer role" in denied.stderr

        # Concurrent consumer processes must share telemetry without lost updates.
        def search_once(_: int) -> None:
            run(binary, "search", "revision telemetry", *common, "--role", "consumer", "--json")

        workers = 8
        searches = 40
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            list(pool.map(search_once, range(searches)))

        # sqlite3.Connection's context manager commits/rolls back but does not
        # close the handle. Explicit closing is required before TemporaryDirectory
        # cleanup on Windows, where an open SQLite file cannot be unlinked.
        with contextlib.closing(sqlite3.connect(telemetry)) as db:
            search_count = db.execute(
                "SELECT search_count FROM skill_telemetry WHERE skill_id=?", (hit["skill_id"],)
            ).fetchone()[0]
            decisions = db.execute(
                "SELECT COUNT(*) FROM routing_decisions WHERE skill_id=?", (hit["skill_id"],)
            ).fetchone()[0]
            suggestions = db.execute(
                "SELECT COUNT(*) FROM search_log WHERE skill_id=? AND event='SUGGESTED'",
                (hit["skill_id"],),
            ).fetchone()[0]
            ok_fetches = db.execute(
                "SELECT COUNT(*) FROM fetch_receipts WHERE status='OK'"
            ).fetchone()[0]

        # One initial search plus the concurrent sweep.
        assert search_count == searches + 1
        assert decisions == searches + 1
        assert suggestions == searches + 1
        assert ok_fetches == 1

        print(
            json.dumps(
                {
                    "ok": True,
                    "concurrent_searches": searches,
                    "workers": workers,
                    "recorded_searches": search_count,
                    "routing_decisions": decisions,
                    "suggestions": suggestions,
                    "verified_fetches": ok_fetches,
                },
                sort_keys=True,
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
