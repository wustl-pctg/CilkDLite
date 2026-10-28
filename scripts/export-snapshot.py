#!/usr/bin/env python3
"""Copy an explicit source allowlist into a new, history-free directory.

This exporter intentionally does not invoke Git. The same snapshot can therefore
be exported again after publication, using its included snapshot-files.txt.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys


MANIFEST = "snapshot-files.txt"
INVENTORY = "snapshot-inventory.json"
CHECKSUMS = "SHA256SUMS"
SOURCE_TREES = {
    "src/opencilk", "src/cheetah", "src/cilktools",
    "src/parlaylib", "src/gperftools",
}
GENERATED_METADATA = {INVENTORY, CHECKSUMS}
ROOT_STATE = {
    "build", "downloads", "logs", "dist", "container-state", "container-home",
    ".container-home", ".container-sys", ".venv",
    ".cache", ".ssh", ".aws", ".codex", ".agents", ".docker",
}
RUNTIME_SUFFIXES = {
    ".ll", ".bc", ".o", ".a", ".so", ".d", ".outputfile", ".debug",
    ".png", ".svg", ".dot", ".S", ".A", ".syms", ".log", ".csv",
    ".swp", ".swo", ".bak", ".tapir",
}
RUNTIME_RAW_FILES = {
    "compiled_test/debug.txt", "compiled_test/cilksort_autopsy",
    "compiled_test/fib_adams_low", "compiled_test/fib_adams_zero",
}


class SnapshotError(Exception):
    pass


def relative_path(value):
    """Accept one unambiguous, portable POSIX path from an allowlist."""
    if not isinstance(value, str) or not value or value.startswith("#"):
        raise SnapshotError("Invalid empty/comment path in allowlist")
    p = PurePosixPath(value)
    if (not p.parts or p.is_absolute() or p.as_posix() != value or ".." in p.parts
            or "\\" in value or any(ord(c) < 32 or ord(c) == 127 for c in value)):
        raise SnapshotError("Unsafe relative path: " + repr(value))
    return p


def exclusion_reason(name):
    """Scope generated-file filtering narrowly: LLVM .ll/.bc are source tests."""
    p = relative_path(name)
    if any(part in {".git", ".gitmodules"} for part in p.parts):
        return "Git metadata"
    if name in GENERATED_METADATA:
        return "generated snapshot metadata"
    if p.parts[0] != "src" and any(part in ROOT_STATE for part in p.parts):
        return "local build, account, or container state"
    if p.parts[0] == "src":
        tree = "/".join(p.parts[:2])
        if tree not in SOURCE_TREES:
            return "source tree not declared for this release"
    if name.startswith("src/cheetah/"):
        local = PurePosixPath(name).relative_to("src/cheetah")
        if (local.suffix in RUNTIME_SUFFIXES or ".ll." in local.name
                or ".so." in local.name or local.name.endswith("~")
                or local.name == "core" or local.name.startswith("core.")
                or "figures" in local.parts or "__pycache__" in local.parts
                or local.as_posix() in RUNTIME_RAW_FILES
                or local.parts[0] in {"build", "lib"}
                or local.parts[:2] == ("runtime", "build")):
            return "generated runtime/benchmark artifact"
    elif p.parts[0] != "src":
        if (p.suffix in {".pyc", ".swp", ".swo", ".bak", ".log"}
                or "__pycache__" in p.parts or p.name.endswith("~")):
            return "generated wrapper artifact"
    return None


def load_sources(root):
    if (root / "sources.json").is_symlink() or not (root / "sources.json").is_file():
        raise SnapshotError("sources.json must be a regular metadata file")
    try:
        data = json.loads((root / "sources.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SnapshotError("Cannot read sources.json: " + str(exc)) from exc
    if not isinstance(data, dict) or data.get("format") != 1 or not isinstance(data.get("repositories"), list):
        raise SnapshotError("sources.json must contain format 1 and repositories")
    seen = set()
    for repo in data["repositories"]:
        if not isinstance(repo, dict):
            raise SnapshotError("Invalid repository record in sources.json")
        path, commit = repo.get("path"), repo.get("commit", "")
        if path not in SOURCE_TREES | {"."} or path in seen:
            raise SnapshotError("Invalid/duplicate repository path in sources.json")
        if not isinstance(commit, str) or len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit):
            raise SnapshotError("Invalid source commit for " + path)
        seen.add(path)
    if seen != SOURCE_TREES | {"."}:
        raise SnapshotError("sources.json must identify the wrapper and all five source trees")
    return data


def read_manifest(root):
    if (root / MANIFEST).is_symlink() or not (root / MANIFEST).is_file():
        raise SnapshotError(MANIFEST + " must be a regular metadata file")
    try:
        lines = (root / MANIFEST).read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise SnapshotError("Cannot read " + MANIFEST + ": " + str(exc)) from exc
    paths = []
    for line in lines:
        if not line or line.startswith("#"):
            continue
        relative_path(line)
        paths.append(line)
    if len(set(paths)) != len(paths):
        raise SnapshotError("Duplicate path in " + MANIFEST)
    return sorted(paths)


def within(path, root):
    return path == root or root in path.parents


def check_parents(root, path):
    for parent in path.parents:
        if parent == root:
            return
        if parent.is_symlink():
            raise SnapshotError("Allowlisted file has a symlink parent: " + path.relative_to(root).as_posix())


def validate_files(root, names):
    names = sorted(names)
    selected = set(names)
    required = {MANIFEST, "sources.json", "scripts/export-snapshot.py"}
    if not required <= selected:
        raise SnapshotError("Allowlist must include " + ", ".join(sorted(required)))
    sources = load_sources(root)
    for repo in sources["repositories"]:
        for patch in repo.get("patches", []):
            if patch not in selected:
                raise SnapshotError("Provenance patch missing from allowlist: " + patch)
    directories = {"."}
    for name in names:
        directories.update(p.as_posix() for p in PurePosixPath(name).parents)
    records = []
    for name in names:
        reason = exclusion_reason(name)
        if reason:
            raise SnapshotError("Refusing " + reason + ": " + name)
        path = root / name
        check_parents(root, path)
        try:
            info = path.lstat()
        except OSError as exc:
            raise SnapshotError("Missing/unreadable allowlisted file: " + name) from exc
        mode = stat.S_IMODE(info.st_mode)
        if stat.S_ISLNK(info.st_mode):
            target = os.readlink(path)
            if Path(target).is_absolute():
                raise SnapshotError("Absolute symlink target: " + name)
            # Check both the immediate target (needed in the exported tree) and
            # its resolved endpoint; this also detects escaping symlink chains.
            immediate = Path(os.path.abspath(path.parent / target))
            try:
                resolved = path.resolve(strict=True)
            except (OSError, RuntimeError) as exc:
                raise SnapshotError("Broken/cyclic symlink: " + name) from exc
            for endpoint in (immediate, resolved):
                if not within(endpoint, root):
                    raise SnapshotError("Symlink escapes source tree: " + name)
                endpoint_name = endpoint.relative_to(root).as_posix()
                if any(p in {".git", ".gitmodules"} for p in endpoint.parts):
                    raise SnapshotError("Symlink points at Git metadata: " + name)
                if endpoint_name not in selected and endpoint_name not in directories:
                    raise SnapshotError("Symlink target is not included: " + name)
            records.append({"path": name, "type": "symlink", "mode": format(mode, "04o"), "target": target})
        elif stat.S_ISREG(info.st_mode):
            if mode & 0o7000:
                raise SnapshotError("Special permission bits on source file: " + name)
            if name.startswith("src/cheetah/"):
                with path.open("rb") as stream:
                    magic = stream.read(4)
                if magic in {b"\x7fELF", b"BC\xc0\xde", b"\xde\xc0\x17\x0b"}:
                    raise SnapshotError("Compiled runtime artifact: " + name)
            records.append({"path": name, "type": "file", "mode": format(mode, "04o")})
        else:
            raise SnapshotError("Allowlist entry is not a regular file or symlink: " + name)
    for tree in SOURCE_TREES:
        if not any(name.startswith(tree + "/") for name in selected):
            raise SnapshotError("No files selected for " + tree)
    return records


def digest_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def export_snapshot(source_root, destination):
    root = Path(source_root).resolve(strict=True)
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise SnapshotError("Destination already exists; nothing was overwritten")
    dest = destination.resolve(strict=False)
    if within(dest, root):
        raise SnapshotError("Destination must be outside the source tree")
    if not dest.parent.is_dir():
        raise SnapshotError("Destination parent must already exist")
    records = validate_files(root, read_manifest(root))
    # Reserve the destination exclusively, after all source paths pass checks.
    # On an I/O failure leave it for inspection; never remove a user's directory.
    dest.mkdir(mode=0o755, exist_ok=False)
    for record in records:
        name = record["path"]
        source, target = root / name, dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if record["type"] == "symlink":
            link = record["target"]
            target.symlink_to(link)
            data = os.fsencode(link)
            record["sha256"] = hashlib.sha256(data).hexdigest()
            record["size"] = len(data)
        else:
            # Recheck immediately before opening: a file replaced with a link
            # during export must not pull data from outside the reviewed tree.
            if source.is_symlink() or not source.is_file():
                raise SnapshotError("Source file changed type during export: " + name)
            check_parents(root, source)
            descriptor = os.open(source, os.O_RDONLY | os.O_NOFOLLOW)
            with os.fdopen(descriptor, "rb") as incoming, target.open("xb") as outgoing:
                if not stat.S_ISREG(os.fstat(incoming.fileno()).st_mode):
                    raise SnapshotError("Source file changed type during export: " + name)
                shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)
            target.chmod(int(record["mode"], 8))
            record["sha256"] = digest_file(target)
            record["size"] = target.stat().st_size
    inventory = {
        "format": 1,
        "hash_algorithm": "sha256",
        "symlink_hash": "SHA-256 of link target bytes; links are not followed",
        "files": records,
    }
    payload = (json.dumps(inventory, indent=2, sort_keys=True) + "\n").encode("utf-8")
    (dest / INVENTORY).write_bytes(payload)
    inventory_hash = hashlib.sha256(payload).hexdigest()
    # sha256sum follows symlinks, so link-target hashes live only in the JSON
    # inventory. The inventory's own checksum protects those link records.
    sums = [(r["path"], r["sha256"]) for r in records if r["type"] == "file"]
    sums.append((INVENTORY, inventory_hash))
    (dest / CHECKSUMS).write_text(
        "".join(digest + "  " + name + "\n" for name, digest in sorted(sums)),
        encoding="utf-8",
    )
    return len(records), inventory_hash


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", nargs="?", help="New directory outside the source tree")
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true", help="Validate allowlist without copying")
    args = parser.parse_args(argv)
    try:
        if args.check:
            root = args.source_root.resolve(strict=True)
            records = validate_files(root, read_manifest(root))
            print("Validated " + str(len(records)) + " allowlisted source files/links.")
        else:
            if args.destination is None:
                parser.error("destination is required unless --check is used")
            count, checksum = export_snapshot(args.source_root, args.destination)
            print("Exported " + str(count) + " source files/links.")
            print("Inventory SHA-256: " + checksum)
    except (SnapshotError, OSError) as exc:
        print("Snapshot export failed: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
