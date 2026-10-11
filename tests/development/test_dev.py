# SPDX-License-Identifier: AGPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
from contextlib import redirect_stdout
from io import StringIO

spec = importlib.util.spec_from_file_location("dev", Path(__file__).parents[2] / "tools/dev.py")
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)


class WatcherChecks(unittest.TestCase):
    def test_source_changes_and_removal(self):
        self.assertEqual(dev.changed({"a": b"1", "b": b"1"}, {"a": b"2", "c": b"1"}), {"a", "b", "c"})
        self.assertEqual(dev.changed({"a": b"1"}, {"a": b"1"}), set())
        self.assertEqual(dev.requires_restart({"src/domain/Project.h", "src/ui/Editor.h"}), {"src/domain/Project.h"})

    def test_publication_replaces_token(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dev.publish(root)
            first = (root / "ui-ready").read_text()
            dev.publish(root)
            self.assertNotEqual(first, (root / "ui-ready").read_text())
            self.assertFalse((root / "ui-ready.tmp").exists())

    def test_failed_build_keeps_publication_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build/dev").mkdir(parents=True)
            process = Mock(returncode=0)
            process.poll.side_effect = [None, 0]
            before = {"src/ui/Editor.cpp": b"old"}
            after = {"src/ui/Editor.cpp": b"new"}
            with patch.object(dev, "ROOT", root), patch.object(dev, "sources", side_effect=[before, after, after]), \
                 patch.object(dev, "build", side_effect=[True, False]) as build, patch.object(dev, "publish") as publish, \
                 patch.object(dev.subprocess, "run"), patch.object(dev.subprocess, "Popen", return_value=process), \
                 patch.object(dev.time, "sleep"), redirect_stdout(StringIO()):
                self.assertEqual(dev.main(), 0)
                self.assertEqual(publish.call_count, 1)
                self.assertEqual(build.call_args_list[-1].args, ("OpentoneUI",))

    def test_core_change_requires_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build/dev").mkdir(parents=True)
            process = Mock()
            process.poll.return_value = None
            before = {"src/domain/Project.h": b"old"}
            after = {"src/domain/Project.h": b"new"}
            with patch.object(dev, "ROOT", root), patch.object(dev, "sources", side_effect=[before, after, after]), \
                 patch.object(dev, "build", return_value=True) as build, patch.object(dev, "publish") as publish, \
                 patch.object(dev.subprocess, "run"), patch.object(dev.subprocess, "Popen", return_value=process), \
                 patch.object(dev.time, "sleep"), redirect_stdout(StringIO()):
                self.assertEqual(dev.main(), 2)
                self.assertEqual(build.call_count, 1)
                self.assertEqual(publish.call_count, 1)

    def test_core_change_during_build_blocks_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build/dev").mkdir(parents=True)
            process = Mock()
            process.poll.return_value = None
            before = {"src/ui/Editor.cpp": b"old", "src/domain/Project.h": b"old"}
            edited = {**before, "src/ui/Editor.cpp": b"new"}
            during_build = {**edited, "src/domain/Project.h": b"new"}
            with patch.object(dev, "ROOT", root), \
                 patch.object(dev, "sources", side_effect=[before, edited, edited, during_build, during_build]), \
                 patch.object(dev, "build", return_value=True), patch.object(dev, "publish") as publish, \
                 patch.object(dev.subprocess, "run"), patch.object(dev.subprocess, "Popen", return_value=process), \
                 patch.object(dev.time, "sleep"), redirect_stdout(StringIO()):
                self.assertEqual(dev.main(), 2)
                self.assertEqual(publish.call_count, 1)


if __name__ == "__main__":
    unittest.main()
