#!/usr/bin/env python3

"""Integration test for 'ninja -t clean' with special target syntax."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

NINJA_PATH = os.path.abspath("./ninja")


class CleanTargetTest(unittest.TestCase):
    """Test that 'ninja -t clean' correctly resolves targets."""

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="ninja_clean_target_test_")
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

    def test_clean_caret_target(self):
        """Cleaning 'src^' should clean the first output of 'src'."""
        py = sys.executable.replace("\\", "/")
        with open("in.c", "w") as f:
            f.write("// source")
        with open("build.ninja", "w") as f:
            f.write(
                f"rule touch\n"
                f"  command = {py} -c \"import sys; open(sys.argv[1], 'w').close()\" $out\n"
                f"\n"
                f"build out.o: touch in.c\n"
            )

        # Build out.o
        result = self._run_ninja("out.o")
        self.assertEqual(result.returncode, 0, f"Build failed: {result.stderr}")
        self.assertTrue(os.path.exists("out.o"), "out.o was not created")

        # Clean using in.c^
        clean_result = self._run_ninja("-t", "clean", "in.c^")
        self.assertEqual(clean_result.returncode, 0, f"Clean failed: {clean_result.stderr}")
        self.assertFalse(os.path.exists("out.o"), "out.o should have been cleaned")
        self.assertIn("Cleaning...", clean_result.stdout)
        self.assertIn("1 file", clean_result.stdout)

    def test_clean_caret_target_no_out_edge(self):
        """Cleaning a target that has no out edges using '^' should report an error."""
        with open("in.c", "w") as f:
            f.write("// source")
        with open("build.ninja", "w") as f:
            f.write(
                "rule dummy\n"
                "  command = echo dummy\n"
                "\n"
                "build out.o: dummy in.c\n"
            )

        result = self._run_ninja("-t", "clean", "out.o^")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("'out.o' has no out edge", result.stderr)

    def test_clean_caret_target_unknown(self):
        """Cleaning a completely unknown target with '^' reports unknown target without '^'."""
        with open("build.ninja", "w") as f:
            f.write(
                "rule dummy\n"
                "  command = echo dummy\n"
                "\n"
                "build out.o: dummy in.c\n"
            )

        result = self._run_ninja("-t", "clean", "nonexistent.c^")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown target 'nonexistent.c'", result.stderr)

    def test_clean_mixed_targets(self):
        """Cleaning a mix of regular targets and '^' targets."""
        py = sys.executable.replace("\\", "/")
        with open("in1.c", "w") as f:
            f.write("// in1")
        with open("in2.c", "w") as f:
            f.write("// in2")
        with open("build.ninja", "w") as f:
            f.write(
                f"rule touch\n"
                f"  command = {py} -c \"import sys; open(sys.argv[1], 'w').close()\" $out\n"
                f"\n"
                f"build out1.o: touch in1.c\n"
                f"build out2.o: touch in2.c\n"
            )

        result = self._run_ninja("out1.o", "out2.o")
        self.assertEqual(result.returncode, 0, f"Build failed: {result.stderr}")
        self.assertTrue(os.path.exists("out1.o"))
        self.assertTrue(os.path.exists("out2.o"))

        clean_result = self._run_ninja("-t", "clean", "in1.c^", "out2.o")
        self.assertEqual(clean_result.returncode, 0, f"Clean failed: {clean_result.stderr}")
        self.assertFalse(os.path.exists("out1.o"), "out1.o should have been cleaned")
        self.assertFalse(os.path.exists("out2.o"), "out2.o should have been cleaned")
        self.assertIn("Cleaning...", clean_result.stdout)
        self.assertIn("2 files", clean_result.stdout)


if __name__ == "__main__":
    unittest.main()
