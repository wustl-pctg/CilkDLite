#!/usr/bin/env python3
"""Prepare a reviewable source allowlist from pinned Git trees.

Tracked modifications are read from the working files; deleted tracked files
are omitted. New files require explicit --include/--include-file approval.
For vendored archives, --archive-repo TREE=LOCAL_REPOSITORY supplies the pinned
Git tree used to extract them. Local repository paths are never recorded.
"""

import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys


spec = importlib.util.spec_from_file_location("snapshot_export", Path(__file__).with_name("export-snapshot.py"))
snapshot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot)


def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True)
    if result.returncode:
        raise snapshot.SnapshotError("Git verification failed for " + repo.name + ": " + " ".join(args[:2]))
    return result.stdout


def tree_files(repo, commit):
    result = []
    for entry in git(repo, "ls-tree", "-r", "-z", commit).split(b"\0"):
        if not entry:
            continue
        metadata, name = entry.split(b"\t", 1)
        mode, kind, _ = metadata.split(b" ", 2)
        # Nested repositories must have their own explicit provenance entry.
        if mode == b"160000":
            continue
        if kind != b"blob" or mode not in {b"100644", b"100755", b"120000"}:
            raise snapshot.SnapshotError("Unsupported tracked source type")
        result.append(name.decode("utf-8"))
    return result


def verify_checkout(path, record):
    if git(path, "rev-parse", "HEAD").decode().strip() != record["commit"]:
        raise snapshot.SnapshotError("HEAD differs from sources.json for " + record["path"])
    branch = record.get("branch")
    if branch:
        # Release worktrees can have their own branch names. Verify the selected
        # upstream branch ref, without fetching or recording remote URLs.
        candidates = ["refs/remotes/origin/" + branch, "refs/heads/" + branch]
        for ref in candidates:
            result = subprocess.run(["git", "-C", str(path), "rev-parse", "--verify", ref], capture_output=True)
            if result.returncode == 0:
                if result.stdout.decode().strip() != record["commit"]:
                    raise snapshot.SnapshotError("Selected branch differs from sources.json for " + record["path"])
                break
        else:
            raise snapshot.SnapshotError("Cannot verify selected branch for " + record["path"])


def prepare(root, includes, archive_repos):
    root = Path(root).resolve(strict=True)
    sources = snapshot.load_sources(root)
    selected = {snapshot.MANIFEST, "sources.json"}
    for record in sources["repositories"]:
        prefix = record["path"]
        directory = root if prefix == "." else root / prefix
        if prefix in archive_repos:
            repository = Path(archive_repos[prefix]).resolve(strict=True)
            # The commit object, not that repository's current branch, is the
            # authority for an archive that has no local .git metadata.
            git(repository, "cat-file", "-e", record["commit"] + "^{commit}")
            archive = True
        else:
            if not (directory / ".git").exists():
                raise snapshot.SnapshotError("No Git checkout for " + prefix + "; supply --archive-repo")
            repository = directory
            verify_checkout(repository, record)
            archive = False
        for local in tree_files(repository, record["commit"]):
            name = local if prefix == "." else prefix + "/" + local
            if snapshot.exclusion_reason(name):
                continue
            file = root / name
            if not file.exists() and not file.is_symlink():
                if archive:
                    raise snapshot.SnapshotError("Missing vendored source: " + name)
                continue
            selected.add(name)
    for name in includes:
        reason = snapshot.exclusion_reason(name)
        if reason:
            raise snapshot.SnapshotError("Explicit addition is forbidden (" + reason + "): " + name)
        if not (root / name).exists() and not (root / name).is_symlink():
            raise snapshot.SnapshotError("Explicit addition does not exist: " + name)
        selected.add(name)
    # Validate a candidate manifest before replacing an existing reviewed one.
    # MANIFEST itself may not exist yet, so validate it after writing below.
    return sorted(selected)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--include", action="append", default=[], metavar="PATH")
    parser.add_argument("--include-file", action="append", default=[], type=Path,
                        help="Explicit list of approved new paths; blank/comment lines ignored")
    parser.add_argument("--archive-repo", action="append", default=[], metavar="TREE=LOCAL_REPOSITORY")
    parser.add_argument("--check", action="store_true", help="Require existing allowlist to match; do not write")
    args = parser.parse_args(argv)
    try:
        archives = {}
        for value in args.archive_repo:
            tree, separator, repository = value.partition("=")
            if not separator or tree not in snapshot.SOURCE_TREES or tree in archives:
                raise snapshot.SnapshotError("Invalid/duplicate --archive-repo mapping")
            archives[tree] = repository
        includes = list(args.include)
        for path in args.include_file:
            includes.extend(line for line in path.read_text(encoding="utf-8").splitlines()
                            if line and not line.startswith("#"))
        root = args.source_root.resolve(strict=True)
        names = prepare(root, includes, archives)
        payload = "# Explicit source snapshot allowlist; generated by scripts/prepare-snapshot.py.\n" + "\n".join(names) + "\n"
        manifest = root / snapshot.MANIFEST
        if manifest.is_symlink():
            raise snapshot.SnapshotError("Refusing to replace a symlink manifest")
        if args.check:
            if not manifest.exists() or manifest.read_text(encoding="utf-8") != payload:
                raise snapshot.SnapshotError("Allowlist differs; prepare and review the changes")
            snapshot.validate_files(root, names)
        else:
            previous = manifest.read_bytes() if manifest.exists() else None
            manifest.write_text(payload, encoding="utf-8")
            try:
                snapshot.validate_files(root, names)
            except Exception:
                if previous is None:
                    manifest.unlink()
                else:
                    manifest.write_bytes(previous)
                raise
        print("Prepared " + str(len(names)) + " allowlisted source files/links. Review " + snapshot.MANIFEST + ".")
    except (snapshot.SnapshotError, OSError, UnicodeError) as exc:
        print("Snapshot preparation failed: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
