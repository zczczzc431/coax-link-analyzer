#!/usr/bin/env python3
"""Offline release hygiene: banned artifacts, secret patterns, source integrity."""
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = {
    "private-key": r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----",
    "github-token": r"\b(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{30,})\b",
    "cloud-access-key": r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b",
    "credential-url": r"https?://[^\s/:]+:[^\s/@]+@[^\s/]+",
    "literal-credential": r'''(?im)^\s*(?:export\s+)?(?:password|passwd|sudo_password|ssh_password|api_key|access_token|secret_key)\s*[:=]\s*["']?[^\s"']{6,}''',
    "personal-home": r"(?i)(?:[a-z]:[\\/]+Users[\\/]+(?:Administrator|[^\\/\s]+)|/home/(?!root/|user/|runner/)[A-Za-z0-9_.-]+/)",
}
BANNED = {".bit", ".dcp", ".xsa", ".elf", ".arm", ".pem", ".key", ".docx", ".pdf", ".mp4", ".tar", ".gz", ".zip"}


def audit():
    failures = []
    count = 0
    for p in sorted(ROOT.rglob("*")):
        rel = p.relative_to(ROOT)
        if set(rel.parts) & {".git", "build", "__pycache__"} or not p.is_file():
            continue
        count += 1
        if p.suffix.lower() in BANNED or p.stat().st_size > 8 * 1024 * 1024:
            failures.append({"path": rel.as_posix(), "rule": "artifact-or-size"})
        if p.suffix.lower() in {".jpg", ".png"}:
            continue
        try:
            text = p.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            failures.append({"path": rel.as_posix(), "rule": "unknown-binary"})
            continue
        for name, pattern in PATTERNS.items():
            for match in re.finditer(pattern, text):
                failures.append({"path": rel.as_posix(), "rule": name,
                                 "line": text[:match.start()].count("\n") + 1})
    manifest = json.loads((ROOT / "docs/SOURCE_MANIFEST.json").read_text())
    for item in manifest["files"]:
        p = ROOT / item["path"]
        # Git normalizes CRLF source lines to LF; provenance records raw import bytes.
        b = p.read_bytes()
        digest = hashlib.sha256(b).hexdigest()
        normalized = hashlib.sha256(b.replace(b"\r\n", b"\n")).hexdigest()
        if digest != item["sha256"] and normalized != item.get("sha256_lf"):
            failures.append({"path": item["path"], "rule": "source-integrity"})
    print(json.dumps({"files_checked": count, "imported_files": len(manifest["files"]),
                      "failures": failures}, ensure_ascii=False, indent=2))
    return bool(failures)


if __name__ == "__main__":
    sys.exit(audit())
