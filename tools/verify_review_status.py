#!/usr/bin/env python3
"""Consistency check for docs/REVIEW_STATUS.md against the three review reports.

Why this exists
---------------
Status used to live in eight different sections spread over three review reports, each
of which was correct on the day it was written and wrong a round later. Three separate
occasions (P1-3/P1-6/P1-8, P2-2/P2-3, R3-9) an item was fixed in some other round while
the reports still said "not fixed". That wastes work in both directions: re-doing
something already done, or believing work is finished when it is not.

The fix is to keep status in exactly one place (docs/REVIEW_STATUS.md) and to have a
program -- not discipline -- enforce that the reports and the index agree. This is that
program.

What it checks
--------------
  1. the index actually parsed (a broken table must not read as "no problems")
  2. item ids are unique
  3. every status comes from the closed vocabulary
  4. every "fixed" row names at least one file that exists on disk
  5. every item id that heads a section in a review report is registered in the index
  6. every id in the index appears in some review report (no orphan rows)
  7. every review report points at the index
  8. every status-bearing section in a review report carries that pointer
  9. the summary table agrees with the rows above it

Run with --selftest to prove each check can fail. A check that cannot fail is worse than
no check: it reports green and nobody looks again.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INDEX_PATH = ROOT / "docs" / "REVIEW_STATUS.md"
REVIEW_DOCS = [
    ROOT / "docs" / "CODE_REVIEW.md",
    ROOT / "docs" / "CODE_REVIEW_ROUND2.md",
    ROOT / "docs" / "CODE_REVIEW_ROUND3.md",
]

INDEX_MARK = "REVIEW_STATUS.md"

# The index is itself one of the documents the checks read (the summary check needs it),
# so it is keyed under a name that cannot collide with a real path.
INDEX_KEY = "<index>"

# P0-1..P0-7 / P1-x / P2-x / P3-x, N-x, R3-x, U-x. Kept explicit rather than a generic
# `[A-Z]+-\d+` so that a stray "UTF-8" or "SHA-1" in prose cannot masquerade as an item.
#
# Two forms, and the difference matters. Scanning the reports uses the *strict* form: their
# section headings include category titles like "## P0 必须修复" and "## P2 性能", and a
# lenient pattern would treat those as item ids and demand they be indexed.
ID_RE = re.compile(r"\b(?:P[0-3]-\d+|N-\d+|R[34]-\d+|U-\d+)\b")

# The index's own id column, where a bare `P3` is legitimate: round 1's P3 section is one
# section with no sub-numbers -- its items are a four-row table of prose, and inventing
# `P3-1..P3-4` would make the index disagree with the report about what the ids are.
INDEX_ID_RE = re.compile(r"\b(?:P[0-3](?:-\d+)?|N-\d+|R[34]-\d+|U-\d+)\b")

STATUSES = ("已修", "部分已修", "未修", "不做", "待人工确认")
FIXED_STATUSES = ("已修", "部分已修")

# A section is "status-bearing" if its heading is about what was or was not fixed. These
# are the sections that silently rot, so each one has to carry a pointer to the index.
#
# Deliberately *not* including a bare 状态 or 未做: "Idle 状态每次鼠标移动都全屏重绘" is an
# item heading that happens to contain the word 状态, and flagging every such heading would
# bury the real signal under noise. Match the section shapes that actually carry verdicts.
STATUS_SECTION_RE = re.compile(
    r"(未处理的|仍未处理|未修复|遗留项|修复核对|修复顺序|已落地的修复|已修复)"
)

# Minimum plausible row count. The index covers 41 items; anything much below that means
# the table failed to parse (a renamed column, a stray blank line) and every check below
# would pass vacuously against an empty list.
MIN_ROWS = 30


class Row:
    __slots__ = ("id", "title", "severity", "status", "paths_raw", "note", "line")

    def __init__(self, id_, title, severity, status, paths_raw, note, line):
        self.id = id_
        self.title = title
        self.severity = severity
        self.status = status
        self.paths_raw = paths_raw
        self.note = note
        self.line = line

    def paths(self):
        """Candidate file paths from the 落点 column."""
        out = []
        for token in re.split(r"[、,，\s]+", self.paths_raw):
            token = token.strip().strip("`")
            if not token:
                continue
            if "/" in token or token.endswith((".py", ".txt", ".iss", ".sh")):
                out.append(token)
        return out


def leading_int(cell: str):
    """First integer in the cell, or None. Tolerates `3（P1-2、P3、N-9）` and `**41**`."""
    m = re.search(r"\d+", cell)
    return int(m.group()) if m else None


def unemphasise(cell: str) -> str:
    """Strip the markdown emphasis people add to make a verdict stand out.

    `**部分已修**` and `部分已修` are the same verdict; a parser that only accepts one of
    them turns a cosmetic edit into a false failure, and a check that cries wolf gets
    ignored -- which is how the eight stale status sections happened in the first place.
    """
    return cell.strip().strip("*`_ ").strip()


def parse_index(text: str):
    """Returns (rows, parse_errors)."""
    rows, errors = [], []
    for lineno, line in enumerate(text.splitlines(), 1):
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 6:
            continue
        id_cell = unemphasise(cells[0])
        if not INDEX_ID_RE.fullmatch(id_cell):
            continue
        rows.append(
            Row(id_cell, cells[1], unemphasise(cells[2]), unemphasise(cells[3]),
                cells[4], cells[5], lineno)
        )
    if len(rows) < MIN_ROWS:
        errors.append(
            f"index parsed only {len(rows)} rows (expected >= {MIN_ROWS}); "
            "the table shape probably changed, and every other check would pass vacuously"
        )
    return rows, errors


def parse_docs(docs):
    return {str(p): p.read_text(encoding="utf-8") for p in docs}


# --------------------------------------------------------------------------- checks


def check_unique_ids(rows, docs):
    seen, errors = {}, []
    for r in rows:
        if r.id in seen:
            errors.append(f"{r.id}: duplicate index row (line {seen[r.id]} and {r.line})")
        seen[r.id] = r.line
    return errors


def check_status_vocabulary(rows, docs):
    return [
        f"{r.id}: status {r.status!r} is not one of {', '.join(STATUSES)}"
        for r in rows
        if r.status not in STATUSES
    ]


def check_paths_exist(rows, docs):
    errors = []
    for r in rows:
        if r.status not in FIXED_STATUSES:
            continue
        candidates = r.paths()
        if not candidates:
            errors.append(f"{r.id}: status is {r.status!r} but the 落点 column names no file")
            continue
        missing = [p for p in candidates if not (ROOT / p).exists()]
        if missing:
            errors.append(
                f"{r.id}: 落点 names {', '.join(missing)}, which do not exist "
                "(a fix that was reverted, or a typo)"
            )
    return errors


def check_doc_ids_registered(rows, docs):
    """Every id heading a section in a review report must be in the index."""
    known = {r.id for r in rows}
    errors = []
    for path, text in docs.items():
        for lineno, line in enumerate(text.splitlines(), 1):
            if not line.startswith("#"):
                continue
            for id_ in ID_RE.findall(line):
                if id_ not in known:
                    errors.append(
                        f"{Path(path).name}:{lineno}: {id_} heads a section but is not "
                        "registered in REVIEW_STATUS.md"
                    )
    return errors


def check_index_ids_present_in_docs(rows, docs):
    """No orphan index rows: every id must be traceable to a finding."""
    blob = "\n".join(text for path, text in docs.items() if path != INDEX_KEY)
    return [
        f"{r.id}: in the index but mentioned in no review report"
        for r in rows
        if r.id not in blob
    ]


def check_banner(rows, docs):
    return [
        f"{Path(path).name}: does not point at {INDEX_MARK}; a reader landing here has "
        "no way to learn the current status"
        for path, text in docs.items()
        if path != INDEX_KEY and INDEX_MARK not in text
    ]


def check_status_sections_carry_pointer(rows, docs):
    """Status-bearing sections must redirect to the index within a few lines."""
    errors = []
    for path, text in docs.items():
        if path == INDEX_KEY:
            continue
        lines = text.splitlines()
        for i, line in enumerate(lines):
            if not line.startswith("#") or not STATUS_SECTION_RE.search(line):
                continue
            # A few lines before as well as after: a `### 已修复` nested four lines under
            # its parent's pointer is already covered, and demanding a second pointer
            # there would be noise rather than clarity.
            window = "\n".join(lines[max(0, i - 6) : i + 12])
            if INDEX_MARK not in window:
                errors.append(
                    f"{Path(path).name}:{i + 1}: section {line.strip()!r} carries status "
                    f"but does not point at {INDEX_MARK} nearby"
                )
    return errors


def check_summary_counts(rows, docs):
    """The 汇总 table must agree with the rows above it. A forgotten summary line is
    exactly the kind of quiet drift this file exists to prevent."""
    text = docs[INDEX_KEY]
    actual = {s: 0 for s in STATUSES}
    for r in rows:
        if r.status in actual:
            actual[r.status] += 1

    errors = []
    for line in text.splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 2 or cells[0] not in STATUSES:
            continue
        claimed = leading_int(cells[1])
        if claimed is None:
            errors.append(f"summary row {cells[0]!r} does not start with a number: {cells[1]!r}")
            continue
        if claimed != actual[cells[0]]:
            errors.append(
                f"summary says {cells[0]} = {claimed}, but the table has {actual[cells[0]]}"
            )

    total = sum(actual.values())
    for line in text.splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 2 or cells[0] != "**合计**":
            continue
        claimed = leading_int(cells[1])
        if claimed is None:
            errors.append(f"summary total does not start with a number: {cells[1]!r}")
            continue
        if claimed != total:
            errors.append(f"summary says 合计 = {claimed}, but the table has {total} rows")
    return errors


CHECKS = [
    ("unique ids", check_unique_ids),
    ("status vocabulary", check_status_vocabulary),
    ("落点 paths exist", check_paths_exist),
    ("report ids registered in the index", check_doc_ids_registered),
    ("index ids traceable to a report", check_index_ids_present_in_docs),
    ("every report points at the index", check_banner),
    ("status sections carry the pointer", check_status_sections_carry_pointer),
    ("summary agrees with the table", check_summary_counts),
]


# --------------------------------------------------------------------------- selftest


def selftest() -> int:
    """Feed each check data that must fail it. A check that stays green here is broken."""
    good_row = Row("P0-1", "t", "P0", "已修", "CMakeLists.txt", "n", 1)
    bad_rows = {
        "unique ids": [good_row, Row("P0-1", "t", "P0", "已修", "CMakeLists.txt", "n", 2)],
        "status vocabulary": [Row("P0-1", "t", "P0", "已修好", "CMakeLists.txt", "n", 1)],
        "落点 paths exist": [Row("P0-1", "t", "P0", "已修", "src/does_not_exist.cpp", "n", 1)],
        "report ids registered in the index": [],
        "index ids traceable to a report": [Row("P0-9", "t", "P0", "已修", "CMakeLists.txt", "n", 1)],
        "every report points at the index": [good_row],
        "status sections carry the pointer": [good_row],
        "summary agrees with the table": [good_row],
    }
    bad_docs = {
        "unique ids": {},
        "status vocabulary": {},
        "落点 paths exist": {},
        "report ids registered in the index": {"r.md": "### P0-99 something new\n"},
        "index ids traceable to a report": {"r.md": "no ids here\n"},
        "every report points at the index": {"r.md": "no pointer\n"},
        "status sections carry the pointer": {"r.md": "### 仍未处理\nnothing follows\n"},
        # The table has one 已修 row; the summary claims 999.
        "summary agrees with the table": {INDEX_KEY: "| 已修 | 999 |\n| **合计** | 999 |\n"},
    }

    failures = 0
    print("selftest: each check is fed data it must reject")
    for name, fn in CHECKS:
        errors = fn(bad_rows[name], bad_docs[name])
        if errors:
            print(f"  ok    {name} -> {len(errors)} error(s)")
        else:
            print(f"  FAIL  {name} -> stayed green on input it must reject")
            failures += 1

    # And the guard itself: a table that failed to parse must be an error, not silence.
    _, errors = parse_index("no table here at all\n")
    if errors:
        print("  ok    index parse guard -> rejects an unparsable table")
    else:
        print("  FAIL  index parse guard -> stayed green on an empty parse")
        failures += 1

    print(f"\nselftest: {failures} failure(s)")
    return 1 if failures else 0


# --------------------------------------------------------------------------- main


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true", help="prove every check can fail")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not INDEX_PATH.exists():
        print(f"FAIL: {INDEX_PATH.relative_to(ROOT)} does not exist", file=sys.stderr)
        return 1

    index_text = INDEX_PATH.read_text(encoding="utf-8")
    rows, errors = parse_index(index_text)

    docs = parse_docs(REVIEW_DOCS)
    docs[INDEX_KEY] = index_text

    for name, fn in CHECKS:
        for message in fn(rows, docs):
            errors.append(f"[{name}] {message}")

    if errors:
        print(f"{len(errors)} problem(s) in {INDEX_PATH.relative_to(ROOT)}:\n")
        for message in errors:
            print(f"  - {message}")
        return 1

    print(
        f"ok: {len(rows)} item(s) indexed, "
        f"{len(REVIEW_DOCS)} review report(s) consistent with the index"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
