#!/usr/bin/env python3

"""Integration test: compdb should not include validation-only edges."""

import json
import os
import subprocess
import tempfile
import unittest

NINJA_PATH = os.path.abspath("./ninja")


class CompdbValidationTest(unittest.TestCase):
    """Test that 'ninja -t compdb' excludes edges whose outputs are only
    used as validation dependencies (|@)."""

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="ninja_compdb_test_")
        self.original_dir = os.getcwd()
        os.chdir(self.test_dir)

    def tearDown(self):
        os.chdir(self.original_dir)
        import shutil

        shutil.rmtree(self.test_dir, ignore_errors=True)

    def _run_compdb(self, build_ninja, *extra_args):
        with open("build.ninja", "w") as f:
            f.write(build_ninja)
        cmd = [NINJA_PATH, "-t", "compdb"] + list(extra_args)
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def _run_compdb_targets(self, build_ninja, *targets):
        with open("build.ninja", "w") as f:
            f.write(build_ninja)
        cmd = [NINJA_PATH, "-t", "compdb-targets"] + list(targets)
        result = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_compdb_excludes_validation_edge(self):
        """A pure validation edge (output only used via |@) must not appear."""
        plan = """\
rule cc
  command = gcc -c $in -o $out
rule validate
  command = python check.py $in

build foo.o : cc foo.c |@ check_foo
build check_foo : validate foo.c
"""
        entries = self._run_compdb(plan)
        commands = [e["command"] for e in entries]
        self.assertTrue(any("gcc" in c for c in commands), "cc edge should be present")
        self.assertFalse(
            any("check.py" in c for c in commands), "validation edge should be excluded"
        )

    def test_compdb_with_rule_filter_excludes_validation(self):
        """Filtering by rule name still excludes validation-only edges."""
        plan = """\
rule cc
  command = gcc -c $in -o $out
rule validate
  command = python check.py $in

build foo.o : cc foo.c |@ check_foo
build check_foo : validate foo.c
"""
        entries = self._run_compdb(plan, "validate")
        self.assertEqual(
            entries, [], "validation rule should produce no compdb entries"
        )

    def test_compdb_keeps_non_validation_edge(self):
        """Edges whose outputs are regular build inputs must still appear."""
        plan = """\
rule cc
  command = gcc -c $in -o $out
rule link
  command = gcc $in -o $out

build foo.o : cc foo.c
build bar.o : cc bar.c
build prog : link foo.o bar.o
"""
        entries = self._run_compdb(plan)
        outputs = {e["output"] for e in entries}
        self.assertIn("foo.o", outputs)
        self.assertIn("bar.o", outputs)
        self.assertIn("prog", outputs)

    def test_compdb_targets_excludes_validation_edge(self):
        """compdb-targets should also exclude validation-only edges."""
        plan = """\
rule cc
  command = gcc -c $in -o $out
rule validate
  command = python check.py $in

build foo.o : cc foo.c |@ check_foo
build check_foo : validate foo.c
"""
        entries = self._run_compdb_targets(plan, "foo.o")
        commands = [e["command"] for e in entries]
        self.assertTrue(any("gcc" in c for c in commands))
        self.assertFalse(any("check.py" in c for c in commands))

    def test_compdb_mixed_validation_and_input(self):
        """An edge whose output is used as both validation and regular input
        should still be included."""
        plan = """\
rule cc
  command = gcc -c $in -o $out
rule link
  command = gcc $in -o $out

build foo.o : cc foo.c |@ bar.o
build bar.o : cc bar.c
build prog : link foo.o bar.o
"""
        entries = self._run_compdb(plan)
        outputs = {e["output"] for e in entries}
        self.assertIn(
            "bar.o", outputs, "bar.o is also a regular input so it must be included"
        )


if __name__ == "__main__":
    unittest.main()
