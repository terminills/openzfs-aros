#!/usr/bin/env python3
"""Prune only disposable images from verified, inactive public test runs.

Dry run by default. Logs, source snapshots, hashes, screenshots and binaries
remain. Failed/incomplete runs are deliberately ineligible.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2] / "build-aros"


def eligible(run):
    if run.is_symlink() or run.parent != ROOT or not re.fullmatch(
            r"(?:public-service|native-gui|preferences)-run\.[A-Za-z0-9]+", run.name):
        return False
    if (run / "Startup-Native-Cold").exists():
        for name, required in (("native-cold-results.log", "RESULT public service storage PASS"),
                               ("native-cold-unchanged.log", ": OK")):
            path = run / name
            if not path.is_file():
                return False
            text = path.read_text(errors="replace")
            if required not in text or re.search(r"\bFAIL(?:ED)?\b|ASSERT at", text):
                return False
        changed = run / "native-changes.txt"
        all_native = changed.is_file() and changed.read_text().strip() in ("5", "6")
        marker = ("Public all-native changed reader PASS online=1 exact-read=1 readonly-write-denied=1 exported=1" if all_native else
                  "Public native cold reader PASS locked=1 mount-denied=1 key-loaded=1 exact-read=1 readonly-write-denied=1 exported=1")
        if marker not in (run / "native-cold-results.log").read_text():
            return False
        if all_native:
            verified = {Path(line[:-4]).name for line in
                        (run / "native-cold-unchanged.log").read_text().splitlines()
                        if line.endswith(": OK")}
            if verified != {"partition.raw", "changes.raw"}:
                return False
            if (run / "native-recovery.txt").exists():
                for phase, health in (("native-degraded", "degraded"),
                                      ("native-restored", "online")):
                    result = run / f"{phase}-results.log"
                    hashes = run / f"{phase}-unchanged.log"
                    if not result.is_file() or not hashes.is_file():
                        return False
                    text = result.read_text()
                    marker = f"Public all-native changed reader PASS {health}=1 exact-read=1 readonly-write-denied=1 exported=1"
                    if (marker not in text or "RESULT public service storage PASS" not in text
                            or re.search(r"\bFAIL(?:ED)?\b|ASSERT at", text)):
                        return False
                    checks = hashes.read_text()
                    verified = {Path(line[:-4]).name for line in checks.splitlines()
                                if line.endswith(": OK")}
                    if "FAILED" in checks or verified != {"partition.raw", "changes.raw"}:
                        return False
    if (run / "cold-release.txt").exists():
        for name, required in (("release-results.log", "RESULT public service storage PASS"),
                               ("release-zdb-check.log", "No leaks"),
                               ("release-zdb-unchanged.log", ": OK")):
            path = run / name
            if not path.is_file():
                return False
            text = path.read_text(errors="replace")
            if required not in text or re.search(r"\bFAIL(?:ED)?\b", text):
                return False
    if run.name.startswith("preferences-run."):
        path = run / "serial-results.log"
        if not path.is_file() or (run / "backing.raw").exists():
            return False
        text = path.read_text(errors="replace")
        return "RESULT native IFF preferences PASS" in text and not re.search(r"\bFAIL\b", text)
    ram_mode = run / "persistent.txt"
    ram_only = (ram_mode.is_file() and ram_mode.read_text().strip() == "0"
                and not (run / "cold-results.log").exists())
    if run.name.startswith("native-gui-run.") or ram_only:
        # GUI suites use RAM-backed pools, never persistent image chains.
        path = run / "serial-results.log"
        if not path.is_file() or (run / "backing.raw").exists():
            return False
        text = path.read_text(errors="replace")
        return "RESULT public service storage PASS" in text and not re.search(r"\bFAIL\b", text)
    for name in ("serial-results.log", "cold-results.log"):
        path = run / name
        if not path.is_file():
            return False
        text = path.read_text(errors="replace")
        if "RESULT public service storage PASS" not in text or re.search(r"\bFAIL\b", text):
            return False
    check = run / "zdb-check.log"
    hashes = run / "zdb-members-unchanged.log"
    return (check.is_file() and "No leaks" in check.read_text(errors="replace")
            and hashes.is_file() and ": OK" in hashes.read_text()
            and "FAILED" not in hashes.read_text())


def candidates(run):
    result = [run / name for name in ("test.iso", "cold.iso", "native-cold.iso", "native-degraded.iso", "release.iso", "backing.raw", "retired-disk0")]
    archive = run / "startup-archive.raw"
    unchanged = run / "archive-unchanged.log"
    if (unchanged.is_file() and f"{archive}: OK" in unchanged.read_text()
            and "FAILED" not in unchanged.read_text()
            and "Public persistent settings PASS write=1" in (run / "serial-results.log").read_text()
            and (run / "cold-results.log").is_file()
            and "Public persistent settings PASS write=0" in (run / "cold-results.log").read_text()):
        result.append(archive)
    for name in ("members-before", "members-after", "members-release"):
        folder = run / name
        if folder.is_dir() and not folder.is_symlink():
            result.extend(p for p in folder.iterdir() if re.fullmatch(r"disk[0-9]+", p.name))
    return [p for p in result if p.is_file() and not p.is_symlink()]


def main():
    global ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--run", help="one basename under this repository's build-aros")
    parser.add_argument("--artifact-root", type=Path,
                        help="explicit private artifact directory on a separate filesystem")
    args = parser.parse_args()
    if args.artifact_root:
        external = args.artifact_root.resolve(strict=True)
        if not args.run or not external.is_dir() or external.stat().st_dev == ROOT.stat().st_dev:
            parser.error("external artifact root requires one run and a separate mounted filesystem")
        ROOT = external
    if args.run and not re.fullmatch(r"(?:public-service|native-gui|preferences)-run\.[A-Za-z0-9]+", args.run):
        parser.error("--run must be a public-service-run, native-gui-run or preferences-run basename")
    runs = [ROOT / args.run] if args.run else sorted(ROOT.glob("public-service-run.*"))
    total = 0
    for run in runs:
        if not eligible(run):
            continue
        files = candidates(run)
        if not files:
            continue
        # Inspect actual open files, not stale PID/monitor files. Any open image
        # excludes this entire run. Missing lsof or an unexpected error aborts.
        opened = subprocess.run(["lsof", "-t", "--", *map(str, files)],
                                capture_output=True, text=True)
        if opened.returncode not in (0, 1):
            raise RuntimeError(opened.stderr)
        if opened.stdout.strip():
            print(f"SKIP active {run.name}", flush=True)
            continue
        size = sum(p.stat().st_blocks * 512 for p in files)
        total += size
        print(f"{'PRUNE' if args.apply else 'WOULD PRUNE'} {run.name}: "
              f"{len(files)} files, {size / 2**30:.2f} GiB allocated", flush=True)
        if not args.apply:
            continue
        manifest = run / "pruned-images.jsonl"
        with manifest.open("a") as log:
            for path in files:
                before = path.stat()
                with path.open("rb") as source:
                    digest = hashlib.file_digest(source, "sha256").hexdigest()
                after = path.stat()
                if (before.st_ino, before.st_size, before.st_mtime_ns) != (
                        after.st_ino, after.st_size, after.st_mtime_ns):
                    raise RuntimeError(f"image changed while hashing: {path}")
                log.write(json.dumps({"path": str(path), "bytes": before.st_size,
                    "allocated_bytes": before.st_blocks * 512, "sha256": digest,
                    "action": "remove disposable verified test image"}) + "\n")
                log.flush()
                os.fsync(log.fileno())
                path.unlink()
    print(f"{'Reclaimed' if args.apply else 'Eligible'}: {total / 2**30:.2f} GiB", flush=True)


if __name__ == "__main__":
    main()
