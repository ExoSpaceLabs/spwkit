#!/usr/bin/env python3
"""Reject stale active documentation and integration-baseline drift."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

version_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
version_match = re.search(r"\bVERSION\s+(\d+\.\d+\.\d+)\b", version_text)
if not version_match:
    raise SystemExit("cannot determine SpWKit VERSION from CMakeLists.txt")
VERSION = version_match.group(1)
MINOR = VERSION.rsplit(".", 1)[0]

roots = ["docs", "examples", "integrations", "simulator", "src/backends", "tests", "tools"]
paths = [ROOT / "README.md", ROOT / "CONTRIBUTING.md", ROOT / "benchmarks/README.md"]
for name in roots:
    paths.extend((ROOT / name).rglob("*.md"))

historical = {
    "docs/cuse-feasibility.md",
    "docs/v0.6-scope.md",
}
errors: list[str] = []

stale_phrases = [
    "Stable v0.5",
    "stable v0.5",
    "v0.6 development work",
    "being consolidated for v0.6.1",
    "planned virtual SpaceWire service",
    "Planned command-line tools",
    "runtime evidence remains pending",
    "will begin only after the board/test architecture",
    "provisional CCSDSPack",
    "release acceptance: pending",
]

package_re = re.compile(r"find_package\(SpWKit\s+(\d+\.\d+)\s+CONFIG\s+REQUIRED\)")
for path in sorted(set(paths)):
    if not path.is_file():
        continue
    rel = path.relative_to(ROOT).as_posix()
    if rel.startswith("docs/releases/") or rel in historical:
        continue
    text = path.read_text(encoding="utf-8")
    for match in package_re.finditer(text):
        if match.group(1) != MINOR:
            errors.append(f"{rel}: SpWKit package example requests {match.group(1)}, expected {MINOR}")
    for phrase in stale_phrases:
        if phrase in text:
            errors.append(f"{rel}: stale active-document phrase: {phrase!r}")

for rel in ["README.md", "docs/current-status.md", "docs/roadmap.md"]:
    text = (ROOT / rel).read_text(encoding="utf-8")
    if f"v{VERSION}" not in text:
        errors.append(f"{rel}: does not mention current project version v{VERSION}")

release_note = ROOT / "docs" / "releases" / f"v{VERSION}.md"
if not release_note.is_file():
    errors.append(f"missing release note: {release_note.relative_to(ROOT)}")

ci_text = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
tag_match = re.search(r"CCSDSPACK_TAG:\s*([^\s]+)", ci_text)
sha_match = re.search(r"CCSDSPACK_SHA:\s*([0-9a-f]{40})", ci_text)
if not tag_match or not sha_match:
    errors.append("ci.yml: cannot determine immutable CCSDSPack baseline")
else:
    tag, sha = tag_match.group(1), sha_match.group(1)
    docker = (ROOT / "integrations/ccsdspack_v2/docker/Dockerfile").read_text(encoding="utf-8")
    compose = (ROOT / "integrations/ccsdspack_v2/compose.yml").read_text(encoding="utf-8")
    expected = [
        ("Dockerfile CCSDSPACK_REF", f"ARG CCSDSPACK_REF={tag}", docker),
        ("Dockerfile CCSDSPACK_SHA", f"ARG CCSDSPACK_SHA={sha}", docker),
        ("compose CCSDSPACK_REF", f"${{CCSDSPACK_REF:-{tag}}}", compose),
        ("compose CCSDSPACK_SHA", f"${{CCSDSPACK_SHA:-{sha}}}", compose),
    ]
    for label, needle, text in expected:
        if needle not in text:
            errors.append(f"{label}: does not match CI baseline {tag}@{sha}")

if errors:
    print("Documentation consistency check failed:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"Documentation consistency OK: SpWKit v{VERSION}, package line {MINOR}")
