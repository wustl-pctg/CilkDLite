"""Source-export boundary tests; no network, compiler build, or external Git state."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


exporter = load("snapshot_export_test", SCRIPTS / "export-snapshot.py")
preparer = load("snapshot_prepare_test", SCRIPTS / "prepare-snapshot.py")


class SnapshotFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "source"
        self.root.mkdir()
        self.names = {"sources.json", "snapshot-files.txt", "scripts/export-snapshot.py"}
        self.put("scripts/export-snapshot.py", (SCRIPTS / "export-snapshot.py").read_text())
        self.repositories = []
        for tree in ["."] + sorted(exporter.SOURCE_TREES):
            self.repositories.append({"path": tree, "name": tree, "commit": "1" * 40})
            if tree != ".":
                self.put(tree + "/source.c", "/* source */\n")
        self.write_sources()
        self.write_manifest()

    def put(self, path, contents, include=True):
        file = self.root / path
        file.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(contents, bytes):
            file.write_bytes(contents)
        else:
            file.write_text(contents, encoding="utf-8")
        if include:
            self.names.add(path)
        return file

    def write_sources(self):
        (self.root / "sources.json").write_text(json.dumps({"format": 1, "repositories": self.repositories}) + "\n")

    def write_manifest(self):
        (self.root / "snapshot-files.txt").write_text("\n".join(sorted(self.names)) + "\n")

    def export(self, name="export"):
        self.write_manifest()
        destination = self.base / name
        exporter.export_snapshot(self.root, destination)
        return destination


class SnapshotTests(SnapshotFixture):
    def test_git_metadata_is_not_copied_and_explicit_metadata_is_rejected(self):
        metadata = [".git/config", "src/cheetah/nested/.git", "src/opencilk/nested/.git/config", "src/cilktools/.gitmodules"]
        for path in metadata:
            self.put(path, "private metadata\n", include=False)
        output = self.export()
        self.assertFalse(any(p.name in {".git", ".gitmodules"} for p in output.rglob("*")))
        for index, path in enumerate(metadata):
            with self.subTest(path=path):
                self.names.add(path)
                self.write_manifest()
                with self.assertRaisesRegex(exporter.SnapshotError, "Git metadata"):
                    exporter.export_snapshot(self.root, self.base / ("rejected" + str(index)))
                self.names.remove(path)

    def test_only_allowlisted_dirty_files_are_exported(self):
        self.put("src/cheetah/source.c", "/* reviewed local edit */\n")
        self.put("src/cheetah/private-experiment.c", "/* unlisted */\n", include=False)
        output = self.export()
        self.assertIn("reviewed local edit", (output / "src/cheetah/source.c").read_text())
        self.assertFalse((output / "src/cheetah/private-experiment.c").exists())

    def test_compiler_ir_inputs_are_preserved_runtime_ir_is_rejected(self):
        self.put("src/opencilk/llvm/test/example.ll", "; regression input\n")
        self.put("src/opencilk/llvm/test/example.bc", b"BC\xc0\xde-test-fixture")
        output = self.export()
        self.assertTrue((output / "src/opencilk/llvm/test/example.ll").is_file())
        self.assertTrue((output / "src/opencilk/llvm/test/example.bc").is_file())
        for index, suffix in enumerate([".ll", ".bc", ".outputfile"]):
            path = "src/cheetah/runtime/generated" + suffix
            self.put(path, "output")
            self.write_manifest()
            with self.assertRaisesRegex(exporter.SnapshotError, "generated runtime"):
                exporter.export_snapshot(self.root, self.base / ("runtime-rejected" + str(index)))
            self.names.remove(path)

    def test_runtime_binary_without_extension_is_rejected(self):
        self.put("src/cheetah/cilkbench/accidental-binary", b"\x7fELFother bytes")
        self.write_manifest()
        with self.assertRaisesRegex(exporter.SnapshotError, "Compiled runtime artifact"):
            exporter.export_snapshot(self.root, self.base / "rejected")

    def test_build_download_and_container_state_are_rejected(self):
        for index, path in enumerate(["build/result", "downloads/archive.tar.gz", "logs/build.log",
                                      ".container-home/file", ".container-sys/passwd",
                                      "scripts/.container-home/file", "scripts/.container-sys/passwd"]):
            self.put(path, "local state")
            self.write_manifest()
            with self.subTest(path=path), self.assertRaisesRegex(exporter.SnapshotError, "local build, account, or container state"):
                exporter.export_snapshot(self.root, self.base / ("state-rejected" + str(index)))
            self.names.remove(path)

    def test_symlinks_preserve_internal_file_and_directory_targets(self):
        file = self.put("src/parlaylib/include/example.h", "header\n")
        file.chmod(0o751)
        for name, target in [("src/cheetah/include-link", "../parlaylib/include"),
                             ("src/cheetah/header-link", "../parlaylib/include/example.h")]:
            (self.root / name).symlink_to(target)
            self.names.add(name)
        output = self.export()
        self.assertEqual(os.readlink(output / "src/cheetah/include-link"), "../parlaylib/include")
        self.assertEqual((output / "src/cheetah/header-link").read_text(), "header\n")
        self.assertEqual((output / "src/parlaylib/include/example.h").stat().st_mode & 0o777, 0o751)
        # Conventional checksum verification must work despite symlink entries.
        result = subprocess.run(["sha256sum", "--check", "SHA256SUMS"], cwd=output, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_escape_absolute_missing_and_unlisted_symlink_targets_are_rejected(self):
        outside = self.base / "outside.txt"
        outside.write_text("outside")
        self.put("src/cheetah/unlisted.h", "not selected", include=False)
        cases = [("../../../outside.txt", "escapes"), (str(outside), "Absolute"),
                 ("missing", "Broken"), ("unlisted.h", "not included")]
        for index, (target, message) in enumerate(cases):
            link = self.root / "src/cheetah/link"
            link.symlink_to(target)
            self.names.add("src/cheetah/link")
            self.write_manifest()
            with self.subTest(target=target), self.assertRaisesRegex(exporter.SnapshotError, message):
                exporter.export_snapshot(self.root, self.base / ("bad-link" + str(index)))
            link.unlink()
            self.names.remove("src/cheetah/link")

    def test_symlink_parent_is_not_followed(self):
        outside = self.base / "outside"
        outside.mkdir()
        (outside / "file.c").write_text("external")
        (self.root / "src/cheetah/through").symlink_to(outside)
        self.names.add("src/cheetah/through/file.c")
        self.write_manifest()
        with self.assertRaisesRegex(exporter.SnapshotError, "symlink parent"):
            exporter.export_snapshot(self.root, self.base / "rejected")

    def test_destination_safety_and_manifest_traversal(self):
        existing = self.base / "existing"
        existing.mkdir()
        sentinel = existing / "keep"
        sentinel.write_text("untouched")
        for destination in [existing, self.root / "nested-export"]:
            with self.subTest(destination=destination), self.assertRaises(exporter.SnapshotError):
                exporter.export_snapshot(self.root, destination)
        self.assertEqual(sentinel.read_text(), "untouched")
        self.assertFalse((self.root / "nested-export").exists())
        self.names.add("../outside.txt")
        self.write_manifest()
        with self.assertRaisesRegex(exporter.SnapshotError, "Unsafe relative"):
            exporter.export_snapshot(self.root, self.base / "rejected")

    def test_symlink_manifest_is_rejected_without_touching_target(self):
        outside = self.base / "manifest-target"
        outside.write_text("untouched")
        manifest = self.root / "snapshot-files.txt"
        manifest.unlink()
        manifest.symlink_to(outside)
        with self.assertRaisesRegex(exporter.SnapshotError, "regular metadata file"):
            exporter.export_snapshot(self.root, self.base / "rejected")
        self.assertEqual(outside.read_text(), "untouched")

    def test_inventory_is_deterministic_and_snapshot_reexports_without_git(self):
        first = self.export("first")
        second = self.export("second")
        for name in [exporter.INVENTORY, exporter.CHECKSUMS]:
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())
            self.assertNotIn(str(self.root), (first / name).read_text())
        third = self.base / "third"
        result = subprocess.run([sys.executable, str(first / "scripts/export-snapshot.py"), str(third)],
                                env={**os.environ, "PATH": ""}, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((first / exporter.INVENTORY).read_bytes(), (third / exporter.INVENTORY).read_bytes())


@unittest.skipUnless(shutil.which("git"), "Git is required only for preparation tests")
class PreparationTests(SnapshotFixture):
    def setUp(self):
        super().setUp()
        for record in self.repositories:
            directory = self.root if record["path"] == "." else self.root / record["path"]
            self.run_git(directory, "init", "-q", "-b", "master")
            if record["path"] == ".":
                self.run_git(directory, "add", "scripts/export-snapshot.py")
            else:
                self.run_git(directory, "add", "source.c")
            self.run_git(directory, "commit", "-qm", "Fixture source")
            record["commit"] = self.run_git(directory, "rev-parse", "HEAD").strip()
            record["branch"] = "master"
        self.write_sources()

    def run_git(self, directory, *args):
        result = subprocess.run(["git", "-C", str(directory), "-c", "user.name=Snapshot Test", "-c", "user.email=snapshot@example.invalid", *args], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_preparation_requires_explicit_new_files_and_verifies_head_and_branch(self):
        self.put("src/cheetah/new-driver.c", "approved later", include=False)
        self.put("src/cheetah/source.c", "reviewed modification")
        names = preparer.prepare(self.root, [], {})
        self.assertNotIn("src/cheetah/new-driver.c", names)
        self.assertIn("src/cheetah/source.c", names)
        names = preparer.prepare(self.root, ["src/cheetah/new-driver.c"], {})
        self.assertIn("src/cheetah/new-driver.c", names)
        repo = self.root / "src/cheetah"
        original = self.run_git(repo, "rev-parse", "HEAD").strip()
        self.run_git(repo, "add", "source.c")
        self.run_git(repo, "commit", "-qm", "Unapproved branch change")
        with self.assertRaisesRegex(preparer.snapshot.SnapshotError, "HEAD differs"):
            preparer.prepare(self.root, [], {})
        self.run_git(repo, "checkout", "-q", "--detach", original)
        with self.assertRaisesRegex(preparer.snapshot.SnapshotError, "Selected branch differs"):
            preparer.prepare(self.root, [], {})

    def test_vendored_archive_uses_pinned_tree_and_never_includes_extra_files(self):
        path = "src/parlaylib"
        original = self.root / path
        upstream = self.base / "parlay-upstream"
        shutil.move(str(original), upstream)
        original.mkdir()
        shutil.copyfile(upstream / "source.c", original / "source.c")
        (original / "unlisted.c").write_text("not in archive")
        names = preparer.prepare(self.root, [], {path: upstream})
        self.assertIn(path + "/source.c", names)
        self.assertNotIn(path + "/unlisted.c", names)
        (original / "source.c").unlink()
        with self.assertRaisesRegex(preparer.snapshot.SnapshotError, "Missing vendored source"):
            preparer.prepare(self.root, [], {path: upstream})


if __name__ == "__main__":
    unittest.main()
