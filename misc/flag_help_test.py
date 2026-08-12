#!/usr/bin/env python3

"""Verify that documented command-line flags appear in `ninja -h` output."""

import os
import subprocess
import sys
import unittest

NINJA_PATH = os.path.abspath('./ninja')


class FlagHelp(unittest.TestCase):
    def test_keep_going_and_load_average_in_help(self):
        output = subprocess.check_output(
            [NINJA_PATH, '-h'], stderr=subprocess.STDOUT, text=True
        )
        self.assertIn('-k N', output)
        self.assertIn('keep going', output.lower())
        self.assertIn('-l N', output)
        self.assertIn('load average', output.lower())


if __name__ == '__main__':
    unittest.main()
