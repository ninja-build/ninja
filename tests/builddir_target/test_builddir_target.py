#!/usr/bin/env python3

"""Integration test for target lookup with builddir fallback."""

import os
import shutil
import subprocess
import tempfile
import unittest

NINJA_PATH = os.path.abspath("./ninja")


class BuilddirTargetTest(unittest.TestCase):
    """Test that targets can be found via $builddir fallback."""

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="ninja_builddir_target_test_")
        self.original_dir = os.getcwd()
        os.chdir(self.test_dir)

    def tearDown(self):
        os.chdir(self.original_dir)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def _run_ninja(self, *args):
        result = subprocess.run(
            [NINJA_PATH] + list(args), capture_output=True, text=True
        )
        return result

    def test_builddir_fallback(self):
        """Passing 'foo' should build '$builddir/foo' when no literal 'foo' target exists."""
        with open("build.ninja", "w") as f:
            f.write(
                "builddir = out\n"
                "\n"
                "rule touch\n"
                "  command = touch $out\n"
                "\n"
                "build $builddir/foo: touch\n"
            )

        result = self._run_ninja("foo")
        self.assertEqual(result.returncode, 0, f"Build failed: {result.stderr}")
        self.assertTrue(os.path.exists("out/foo"), "out/foo was not created")

    def test_exact_match_takes_priority(self):
        """A literal target 'foo' should be preferred over '$builddir/foo'."""
        with open("build.ninja", "w") as f:
            f.write(
                "builddir = out\n"
                "\n"
                "rule cp\n"
                "  command = cp $in $out\n"
                "\n"
                "rule touch\n"
                "  command = touch $out\n"
                "\n"
                "build foo: touch\n"
                "build $builddir/foo: cp foo\n"
            )

        result = self._run_ninja("foo")
        self.assertEqual(result.returncode, 0, f"Build failed: {result.stderr}")
        # Only the exact-match target should have been built; out/foo depends on
        # foo so if ninja built out/foo both would exist, but requesting just
        # "foo" should only build the literal target.
        self.assertTrue(os.path.exists("foo"), "foo was not created")
        self.assertFalse(
            os.path.exists("out/foo"),
            "out/foo should not have been built when exact 'foo' exists",
        )

    def test_no_builddir_no_fallback(self):
        """Without builddir, an unknown target should still fail."""
        with open("build.ninja", "w") as f:
            f.write(
                "rule touch\n"
                "  command = touch $out\n"
                "\n"
                "build bar: touch\n"
            )

        result = self._run_ninja("nonexistent")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown target", result.stderr)

    def test_fallback_with_subdirectory(self):
        """Fallback should work for paths with subdirectories."""
        with open("build.ninja", "w") as f:
            f.write(
                "builddir = out\n"
                "\n"
                "rule touch\n"
                "  command = mkdir -p `dirname $out` && touch $out\n"
                "\n"
                "build $builddir/sub/bar: touch\n"
            )

        result = self._run_ninja("sub/bar")
        self.assertEqual(result.returncode, 0, f"Build failed: {result.stderr}")
        self.assertTrue(os.path.exists("out/sub/bar"), "out/sub/bar was not created")

    def test_fallback_miss_still_errors(self):
        """When the target isn't found even under $builddir, an error is reported."""
        with open("build.ninja", "w") as f:
            f.write(
                "builddir = out\n"
                "\n"
                "rule touch\n"
                "  command = touch $out\n"
                "\n"
                "build $builddir/foo: touch\n"
            )

        result = self._run_ninja("nonexistent")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown target", result.stderr)
