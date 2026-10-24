#!/usr/bin/env python3
"""Stage a MetaMake build tied to retained native acceptance evidence.

Never installs into an AROS runtime. Platform overrides are separate, explicit
integration inputs, not silently copied into the application's Libs directory.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess
import tempfile


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def snapshot_paths(snapshot):
    helper = pathlib.Path(__file__).resolve().parents[1] / "engine/rebuild-fat-snapshot.py"
    spec = importlib.util.spec_from_file_location("fat_snapshot", helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.verify_snapshot(snapshot)


def require_fat_snapshot(snapshot, binary):
    paths = snapshot_paths(snapshot)
    required = {pathlib.Path(name) for name in (
        "rebuild.json", "rebuild-fat-snapshot.py", "source-provenance.json",
        "Licenses/LICENSE", "build-options.txt", "fat.handler")}
    if not required.issubset(paths):
        raise ValueError("Incomplete FAT source snapshot")
    recipe = json.loads((snapshot / "rebuild.json").read_text())
    if recipe.get("format") != 1 or recipe.get("expected_binary") != digest(binary):
        raise ValueError("FAT source recipe does not bind the selected binary")
    if digest(snapshot / "fat.handler") != digest(binary):
        raise ValueError("FAT snapshot binary differs from selection")
    if "full_fat=1" not in (snapshot / "build-options.txt").read_text().splitlines():
        raise ValueError("FAT snapshot is not a full source rebuild")
    return paths


def require_dos_snapshot(snapshot, package):
    paths = snapshot_paths(snapshot)
    required = {pathlib.Path(name) for name in (
        "rebuild.json", "rebuild-fat-snapshot.py", "source/LICENSE",
        "source-revision.txt", "catalog-revision.txt", "build-options.txt",
        "dos-loader-execute.patch", "dos.library", "aros-base.pkg.xz")}
    if not required.issubset(paths):
        raise ValueError("Incomplete DOS source snapshot")
    recipe = json.loads((snapshot / "rebuild.json").read_text())
    if (recipe.get("format") != 1 or recipe.get("artifact") != "dos.library"
            or recipe.get("expected_binary") != digest(snapshot / "dos.library")):
        raise ValueError("DOS source recipe does not bind its module")
    if digest(snapshot / "aros-base.pkg.xz") != digest(package):
        raise ValueError("DOS snapshot boot package differs from selection")
    if "loader_policy=1" not in (snapshot / "build-options.txt").read_text().splitlines():
        raise ValueError("DOS snapshot lacks execute policy")
    return paths


def recorded_hash(run, name):
    values = set()
    for manifest in run.glob("*-input*.sha256"):
        for line in manifest.read_text().splitlines():
            fields = line.split(maxsplit=1)
            if len(fields) == 2 and pathlib.Path(fields[1].lstrip("*")).name == name:
                values.add(fields[0])
    manifest = run / "inputs.sha256"
    for line in manifest.read_text().splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) == 2 and pathlib.Path(fields[1].lstrip("*")).name == name:
            values.add(fields[0])
    if len(values) != 1:
        raise ValueError(f"Missing or ambiguous evidence for {name}: {run}")
    return values.pop()


def require_match(path, run, name=None):
    if digest(path) != recorded_hash(run, name or path.name):
        raise ValueError(f"Selected binary differs from tested input: {path}")


def member_hashes(path):
    values = {}
    for line in path.read_text().splitlines():
        hash_value, name = line.split(maxsplit=1)
        name = pathlib.Path(name).name
        if name in values:
            raise ValueError("Duplicate member in evidence")
        values[name] = hash_value
    if not values:
        raise ValueError("Empty member evidence")
    return values


def require_companions(run):
    writer = (run / "serial-results.log").read_text()
    cold = (run / "cold-results.log").read_text()
    for marker in ("Public companion tools PASS runtime-prerequisites=1",
                   "Public exec helper PASS real-zpool=1 captured-output=1 missing-executable=1",
                   "Public companion configuration PASS hostid-save=1",
                   "Public companion mount PASS guid-bound=1"):
        if marker not in writer:
            raise ValueError(f"Missing companion runtime evidence: {marker}")
    marker = "Public persistent enabled restore PASS trusted-envarc=1 imported=1 mounted=1 readonly=1"
    if cold.count(marker) != 3 or "FAIL" in cold:
        raise ValueError("Missing persistent enabled startup qualification")
    unchanged = (run / "archive-unchanged.log").read_text()
    if ": OK" not in unchanged or "FAILED" in unchanged:
        raise ValueError("Missing archive unchanged evidence")


def require_optional_release(run):
    if not (run / "cold-release.txt").exists():
        return
    required = {
        "release-results.log": (
            "RESULT public service storage PASS",
            "Public cold writable release PASS pending-before=1 recursive-release=1 parent-absent=1 child-absent=1 reimport-absent=1 exact-data=1 exported=1",
            "Public backing clean detach PASS"),
        "release-zdb-check.log": ("No leaks",),
        "release-zdb-unchanged.log": (": OK",),
    }
    if (run / "archive-before.sha256").exists():
        required["archive-after-release.log"] = (": OK",)
    for name, markers in required.items():
        path = run / name
        if not path.is_file():
            raise ValueError(f"Missing writable cold-release evidence: {name}")
        text = path.read_text()
        if "FAIL" in text or any(marker not in text for marker in markers):
            raise ValueError(f"Failed writable cold-release evidence: {name}")


def require_encrypted_root(run):
    lines = (run / "serial-results.log").read_text().splitlines()
    required = (
        "RESULT public service storage PASS",
        "Public native encrypted root PASS properties=6 locked-import=2 mount-denied=2 key-reload=2 exact-data=1 cleanup=1",
        "Public native discovery data PASS handler-write=1 readonly-import-read=1 readonly-write-denied=1 writable-import-read=1 cleanup=1",
        "Public file label denial PASS corrupt=4 conflicting-member=4 checksum=4 manager-denied=12 forced-cli-denied=12 whole-members-unchanged=12 restored=12",
    )
    if any(marker not in lines for marker in required) or any(
            line.startswith("ASSERT at ") or
            (line.startswith("RESULT ") and line.endswith(" FAIL"))
            for line in lines):
        raise ValueError("Missing or failed encrypted pool-root regression")


def require_changed_pool(run):
    writer = (run / "serial-results.log").read_text().splitlines()
    for marker in (
        "Public destroyed changes CLI PASS attach=1 replace=1 add=1 active-force-refused=3 exported-force-refused=3 exact-read=5 cleanup=1",
        "Public destroyed changes Manager PASS attach=1 replace=1 add=1 active-refused=3 exported-refused=3 exact-read=5 retained=1",
    ):
        if marker not in writer:
            raise ValueError("Missing changed-pool writer qualification")
    cold = (run / "cold-results.log").read_text().splitlines()
    for phase in range(3):
        marker = f"Public changed pool cold PASS phase={phase} exact-read=1 readonly-write-denied=1 exported=1"
        if cold.count(marker) != 1:
            raise ValueError("Missing changed-pool cold qualification")
    before = member_hashes(run / "reuse-before.sha256")
    after = member_hashes(run / "reuse-after.sha256")
    if set(before) != {f"disk{i}" for i in range(5)} or before != after:
        raise ValueError("Changed-pool member hashes differ or are incomplete")
    for name, marker in (("reuse-zdb-check.log", "No leaks"),
                         ("reuse-zdb-unchanged.log", ": OK")):
        text = (run / name).read_text()
        if marker not in text or "FAIL" in text:
            raise ValueError("Missing changed-pool independent integrity evidence")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("gui_run", type=pathlib.Path)
    parser.add_argument("cold_run", type=pathlib.Path)
    parser.add_argument("--output-parent", type=pathlib.Path, required=True)
    parser.add_argument("--encrypted-root-run", type=pathlib.Path, required=True)
    parser.add_argument("--fat-handler", type=pathlib.Path, required=True)
    parser.add_argument("--fat-source-snapshot", type=pathlib.Path, required=True)
    parser.add_argument("--dos-package", type=pathlib.Path, required=True)
    parser.add_argument("--dos-source-snapshot", type=pathlib.Path, required=True)
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    build, gui, cold = (p.resolve(strict=True) for p in
                        (args.build, args.gui_run, args.cold_run))
    stage = build / "stage"
    encrypted = args.encrypted_root_run.resolve(strict=True)
    require_encrypted_root(encrypted)
    # Require terminal guest acceptance, not merely an opened window or link.
    for run in (gui, cold):
        serial = (run / "serial.log").read_text()
        if "RESULT public service storage PASS" not in serial or any(
                "RESULT " in line and "FAIL" in line for line in serial.splitlines()):
            raise ValueError(f"Missing writer/GUI acceptance: {run}")
    if "Public desktop post-UI data PASS exact-read=1" not in (gui / "serial.log").read_text():
        raise ValueError("Missing post-UI filesystem verification")
    if "RESULT public service storage PASS" not in (cold / "cold-serial.log").read_text():
        raise ValueError("Missing cold import acceptance")
    if "No leaks" not in (cold / "zdb-check.log").read_text():
        raise ValueError("Missing independent zdb verification")
    if member_hashes(cold / "members-before.sha256") != member_hashes(cold / "members-after.sha256"):
        raise ValueError("Cold import changed member hashes")
    for filename in ("zdb-members-unchanged.log",):
        path = cold / filename
        if not path.exists() or not path.read_text().strip() or "FAILED" in path.read_text():
            raise ValueError(f"Missing or failed unchanged-member evidence: {path}")
    selected = {
        "C/zfs": stage / "C/zfs", "C/zpool": stage / "C/zpool",
        "C/OpenZFSServiceNative": build / "package/OpenZFSServiceNative",
        "C/OpenZFSDevice": stage / "C/OpenZFSDevice",
        "Libs/openzfs.library": build / "package/openzfs.library",
        "L/OpenZFS-Handler": stage / "L/OpenZFS-handler",
        "Tools/OpenZFSManagerNative": stage / "Tools/OpenZFSManagerNative",
        "Tools/OpenZFS/ExecHelper": stage / "Tools/OpenZFS/ExecHelper",
        "Platform/Libs/posixc.library": stage / "Libs/posixc.library",
    }
    for destination, source in selected.items():
        name = pathlib.Path(destination).name
        require_match(source, gui, name)
        if name != "OpenZFSManagerNative":
            require_match(source, cold, name)
            require_match(source, encrypted, name)
        description = subprocess.check_output(["file", "-b", str(source)], text=True)
        if "ELF 64-bit LSB relocatable, x86-64" not in description or "AROS" not in description:
            raise ValueError(f"Not a native AROS x86-64 module: {source}")
    symbols = subprocess.check_output(["nm", str(selected["Libs/openzfs.library"])], text=True)
    if any(marker in symbols for marker in ("__wrap_", "oz_test_admission_hold", "oz_dos_test_calloc")):
        raise ValueError("Diagnostic engine cannot be staged")
    for symbol in ("zfsdev_ioctl_common", "oz_service_main", "OpenZFS_5_OpenZFSRunService"):
        if not any(line.endswith(" T " + symbol) for line in symbols.splitlines()):
            raise ValueError(f"Missing full shared-engine symbol: {symbol}")
    options = (build / "package/service-build-options.txt").read_text().splitlines()
    for value in ("service_test_modes=0", "full_dispatcher=1", "shared_engine=1", "status_trace=0"):
        if value not in options:
            raise ValueError(f"Missing release provenance: {value}")
    require_companions(cold)
    require_optional_release(cold)
    require_changed_pool(cold)
    # These are explicit platform inputs, not application replacements. FAT
    # is exercised only in the persistent run; DOS policy is used in all runs.
    fat = args.fat_handler.resolve(strict=True)
    require_match(fat, cold, "fat-handler")
    fat_snapshot = args.fat_source_snapshot.resolve(strict=True)
    fat_sources = require_fat_snapshot(fat_snapshot, fat)
    dos = args.dos_package.resolve(strict=True)
    for run in (gui, cold, encrypted):
        require_match(dos, run, "aros-base.pkg.xz")
    dos_snapshot = args.dos_source_snapshot.resolve(strict=True)
    dos_sources = require_dos_snapshot(dos_snapshot, dos)
    selected["Platform/L/fat-handler"] = fat
    selected["Platform/Boot/aros-base.pkg.xz"] = dos
    companions = ("OpenZFSStartup", "OpenZFSStartupMount", "OpenZFSRuntimeCheck", "OpenZFSHostIdentity")
    for name in companions:
        source = stage / "C" / name
        if not source.is_file():
            raise ValueError(f"Missing companion utility: {source}")
        require_match(source, cold, name)
        selected["C/" + name] = source
    selected_hashes = {name: digest(path) for name, path in selected.items()}
    parent = args.output_parent.resolve(strict=True)
    output = pathlib.Path(tempfile.mkdtemp(prefix="mmake-bundle.", dir=parent))
    for destination, source in selected.items():
        target = output / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if digest(target) != selected_hashes[destination] or digest(source) != selected_hashes[destination]:
            raise ValueError(f"Copy verification failed: {destination}")
    metadata = output / "Metadata"
    metadata.mkdir()
    sources = output / "Sources/FAT"
    sources.mkdir(parents=True)
    for relative in [*fat_sources, pathlib.Path("retained.sha256")]:
        target = sources / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(fat_snapshot / relative, target)
    require_fat_snapshot(sources, output / "Platform/L/fat-handler")
    sources = output / "Sources/DOS"
    sources.mkdir(parents=True)
    for relative in [*dos_sources, pathlib.Path("retained.sha256")]:
        target = sources / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dos_snapshot / relative, target)
    require_dos_snapshot(sources, output / "Platform/Boot/aros-base.pkg.xz")
    record = {"packaging_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
              "gui_run": str(gui), "cold_run": str(cold), "encrypted_root_run": str(encrypted), "build": str(build),
              "build_only_utilities": [], "native_companion_utilities": list(companions),
              "inputs": {name: {"path": str(path), "sha256": selected_hashes[name]} for name, path in selected.items()}}
    (metadata / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")
    (metadata / "packaging.patch").write_bytes(subprocess.check_output(["git", "diff", "--binary"], cwd=repo))
    shutil.copy2(__file__, metadata / "stage-mmake.py")
    for label, run in (("gui", gui), ("cold", cold), ("encrypted-root", encrypted)):
        target = metadata / label
        target.mkdir()
        for path in run.iterdir():
            if path.suffix in (".log", ".sha256") or path.name in ("source.txt", "source.patch", "base-platform.txt"):
                shutil.copy2(path, target / path.name)
    docs = output / "Docs"
    docs.mkdir()
    for name in ("LICENSE", "COPYRIGHT", "NOTICE", "BUILDING.md", "PORT-STATUS.md"):
        shutil.copy2(repo / name, docs / name)
    shutil.copy2(repo / "aros/engine/EXEC-POLICY.md", docs / "EXEC-POLICY.md")
    shutil.copy2(repo / "aros/distribution/METAMAKE-BUNDLE.md", output / "README.md")
    files = sorted(p for p in output.rglob("*") if p.is_file())
    (output / "SHA256SUMS").write_text("".join(f"{digest(p)}  {p.relative_to(output)}\n" for p in files))
    print(output)


if __name__ == "__main__":
    main()
