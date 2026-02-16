#!/usr/bin/env python3

"""Integration test for 'ninja -t restat' with builddir."""

import os
import subprocess
import tempfile
import time
import unittest

NINJA_PATH = os.path.abspath("./ninja")


class RestatBuildDirTest(unittest.TestCase):
    """Test that 'ninja -t restat' respects builddir."""

    def setUp(self):
        """Create a temporary directory for the test."""
        self.test_dir = tempfile.mkdtemp(prefix="ninja_restat_test_")
        self.original_dir = os.getcwd()
        os.chdir(self.test_dir)

    def tearDown(self):
        """Clean up the temporary directory."""
        os.chdir(self.original_dir)
        import shutil

        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_restat_with_builddir(self):
        """Test that ninja -t restat updates mtime in builddir/.ninja_log."""

        # Create a simple build.ninja file with builddir
        build_ninja = """
builddir = build

rule touch
  command = touch $out
  description = Creating $out

build output.txt: touch
"""
        with open("build.ninja", "w") as f:
            f.write(build_ninja)

        # Run ninja to build the output
        result = subprocess.run([NINJA_PATH], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, f"Initial build failed: {result.stderr}")
        self.assertTrue(os.path.exists("output.txt"), "output.txt was not created")
        self.assertTrue(
            os.path.exists("build/.ninja_log"), "build/.ninja_log was not created"
        )

        # Read the original .ninja_log to get the initial mtime
        with open("build/.ninja_log", "r") as f:
            log_lines = f.readlines()

        # Find the entry for output.txt
        output_entry = ""
        for line in log_lines:
            if "output.txt" in line:
                output_entry = line.strip()
                break

        self.assertNotEqual(
            output_entry, "", "output.txt not found in build/.ninja_log"
        )

        # Parse the log entry: start_time\tend_time\tmtime\toutput\tcommand_hash
        parts = output_entry.split("\t")
        self.assertEqual(len(parts), 5, f"Unexpected log format: {output_entry}")
        original_mtime = int(parts[2])

        # Wait a bit to ensure different mtime
        time.sleep(0.01)

        # Touch the output file to update its mtime using explicit time
        current_time = time.time() + 2  # Add 2 seconds to ensure different mtime
        os.utime("output.txt", (current_time, current_time))

        # Get the new actual file mtime
        new_file_mtime = int(
            os.path.getmtime("output.txt") * 1000000000
        )  # Convert to nanoseconds
        self.assertGreater(
            new_file_mtime,
            original_mtime,
            f"File mtime should have increased: {new_file_mtime} vs {original_mtime}",
        )

        # Run ninja -t restat
        result = subprocess.run(
            [NINJA_PATH, "-t", "restat"], capture_output=True, text=True
        )
        self.assertEqual(
            result.returncode, 0, f"ninja -t restat failed: {result.stderr}"
        )

        # Read the updated .ninja_log
        with open("build/.ninja_log", "r") as f:
            updated_log_lines = f.readlines()

        # Find the updated entry for output.txt
        updated_entry = ""
        for line in updated_log_lines:
            if "output.txt" in line:
                updated_entry = line.strip()
                break

        self.assertNotEqual(
            updated_entry, "", "output.txt not found in updated build/.ninja_log"
        )

        # Parse the updated log entry
        updated_parts = updated_entry.split("\t")
        self.assertEqual(
            len(updated_parts), 5, f"Unexpected updated log format: {updated_entry}"
        )
        updated_mtime = int(updated_parts[2])

        # Verify that the mtime was updated in the log
        self.assertGreater(
            updated_mtime,
            original_mtime,
            f"mtime in build/.ninja_log should have been updated. "
            f"Original: {original_mtime}, Updated: {updated_mtime}",
        )
