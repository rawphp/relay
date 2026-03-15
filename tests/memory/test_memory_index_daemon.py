"""
TDD tests for memory_index_daemon.py subprocess-isolation refactor.

Tests describe the desired behavior BEFORE implementation.
Run with: python3 -m unittest tests/test_memory_index_daemon.py -v
"""

import os
import sys
import time
import unittest
import tempfile
import subprocess
from pathlib import Path
from unittest.mock import patch, MagicMock, call

# Add relay root to path so we can import the daemon
RELAY_HOME = Path(__file__).parents[2]
sys.path.insert(0, str(RELAY_HOME))

# We import only after path setup — if this triggers numpy/faiss/torch,
# test_no_heavy_imports will catch it
from lib.memory.memory_index_daemon import needs_rebuild, run_build


class TestNeedsRebuild(unittest.TestCase):
    """Tests for needs_rebuild(timestamp_path, memory_dir, relay_home) function."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tmpdir = Path(self.tmp.name)
        self.timestamp_path = self.tmpdir / ".index" / "last_build.timestamp"
        self.memory_dir = self.tmpdir / "memory"
        self.relay_home = self.tmpdir
        self.memory_dir.mkdir(parents=True)
        (self.tmpdir / ".index").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def test_no_timestamp_file_means_rebuild_needed(self):
        """No timestamp → must rebuild (first run)."""
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertTrue(result)

    def test_corrupt_timestamp_means_rebuild_needed(self):
        """Unreadable/corrupt timestamp → must rebuild (safe default)."""
        self.timestamp_path.write_text("not-a-float")
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertTrue(result)

    def test_no_memory_files_means_no_rebuild(self):
        """No memory files to index → nothing to rebuild."""
        self.timestamp_path.write_text(str(time.time()))
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertFalse(result)

    def test_fresh_index_means_no_rebuild(self):
        """Index newer than all memory files → no rebuild needed."""
        # Create a memory file
        md_file = self.memory_dir / "notes.md"
        md_file.write_text("# Notes")
        # Write timestamp AFTER the file was created
        time.sleep(0.01)
        self.timestamp_path.write_text(str(time.time()))
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertFalse(result)

    def test_stale_index_means_rebuild_needed(self):
        """Memory file newer than index → rebuild needed."""
        # Write an old timestamp
        old_ts = time.time() - 10
        self.timestamp_path.write_text(str(old_ts))
        # Create a memory file that is newer
        md_file = self.memory_dir / "notes.md"
        md_file.write_text("# Notes")
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertTrue(result)

    def test_main_memory_md_is_checked(self):
        """MEMORY.md at relay_home root is also checked for staleness."""
        old_ts = time.time() - 10
        self.timestamp_path.write_text(str(old_ts))
        # Create MEMORY.md at root (not inside memory_dir)
        main_memory = self.relay_home / "MEMORY.md"
        main_memory.write_text("# Main Memory")
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertTrue(result)

    def test_only_md_files_are_checked(self):
        """Non-.md files (e.g. .index files) don't trigger rebuilds."""
        self.timestamp_path.write_text(str(time.time() - 10))
        # Create a non-md file (should be ignored)
        (self.memory_dir / "index.faiss").write_text("binary")
        result = needs_rebuild(self.timestamp_path, self.memory_dir, self.relay_home)
        self.assertFalse(result)


class TestRunBuild(unittest.TestCase):
    """Tests for run_build(relay_home) function — subprocess isolation."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.relay_home = Path(self.tmp.name)
        # Create the expected venv python path
        venv_python = self.relay_home / "lib" / "memory" / "venv" / "bin"
        venv_python.mkdir(parents=True)
        (venv_python / "python3").touch()
        # Create build_index.py script path
        build_dir = self.relay_home / "lib" / "memory"
        (build_dir / "build_index.py").touch()

    def tearDown(self):
        self.tmp.cleanup()

    @patch("lib.memory.memory_index_daemon.subprocess.run")
    def test_calls_subprocess_with_correct_args(self, mock_run):
        """run_build spawns venv python with build_index.py --force."""
        mock_run.return_value = MagicMock(returncode=0, stdout="", stderr="")
        run_build(self.relay_home)
        expected_venv_python = str(self.relay_home / "lib" / "memory" / "venv" / "bin" / "python3")
        expected_script = str(self.relay_home / "lib" / "memory" / "build_index.py")
        mock_run.assert_called_once()
        args = mock_run.call_args[0][0]
        self.assertEqual(args[0], expected_venv_python)
        self.assertEqual(args[1], expected_script)
        self.assertIn("--force", args)

    @patch("lib.memory.memory_index_daemon.subprocess.run")
    def test_returns_true_on_success(self, mock_run):
        """run_build returns True when subprocess exits 0."""
        mock_run.return_value = MagicMock(returncode=0, stdout="", stderr="")
        result = run_build(self.relay_home)
        self.assertTrue(result)

    @patch("lib.memory.memory_index_daemon.subprocess.run")
    def test_returns_false_on_failure(self, mock_run):
        """run_build returns False when subprocess exits non-zero."""
        mock_run.return_value = MagicMock(returncode=1, stdout="", stderr="ImportError: No module named torch")
        result = run_build(self.relay_home)
        self.assertFalse(result)

    @patch("lib.memory.memory_index_daemon.subprocess.run")
    def test_returns_false_on_timeout(self, mock_run):
        """run_build returns False when subprocess times out."""
        mock_run.side_effect = subprocess.TimeoutExpired(cmd="python3", timeout=300)
        result = run_build(self.relay_home)
        self.assertFalse(result)

    @patch("lib.memory.memory_index_daemon.subprocess.run")
    def test_subprocess_has_timeout(self, mock_run):
        """run_build passes a timeout to subprocess.run to prevent hangs."""
        mock_run.return_value = MagicMock(returncode=0, stdout="", stderr="")
        run_build(self.relay_home)
        kwargs = mock_run.call_args[1]
        self.assertIn("timeout", kwargs)
        self.assertGreater(kwargs["timeout"], 0)


class TestNoDaemonHeavyImports(unittest.TestCase):
    """Ensure the daemon module itself doesn't drag in torch/numpy/faiss."""

    def test_daemon_does_not_import_torch(self):
        """After importing memory_index_daemon, torch must NOT be in sys.modules."""
        self.assertNotIn("torch", sys.modules)

    def test_daemon_does_not_import_numpy(self):
        """After importing memory_index_daemon, numpy must NOT be in sys.modules."""
        self.assertNotIn("numpy", sys.modules)

    def test_daemon_does_not_import_faiss(self):
        """After importing memory_index_daemon, faiss must NOT be in sys.modules."""
        self.assertNotIn("faiss", sys.modules)

    def test_daemon_does_not_import_sentence_transformers(self):
        """sentence_transformers must NOT be in sys.modules after daemon import."""
        self.assertNotIn("sentence_transformers", sys.modules)


if __name__ == "__main__":
    unittest.main(verbosity=2)
