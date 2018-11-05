#!/usr/bin/env python3

"""Runs ./ninja and checks if the output is correct.

In order to simulate a smart terminal it uses the 'script' command.
"""

import subprocess
import sys
import tempfile
import unittest

def run(build_ninja, flags='', pipe=False):
    with tempfile.NamedTemporaryFile('w') as f:
        f.write(build_ninja)
        f.flush()
        ninja_cmd = './ninja {} -f {}'.format(flags, f.name)
        try:
            if pipe:
                output = subprocess.check_output([ninja_cmd], shell=True)
            else:
                output = subprocess.check_output(['script', '-qfec', ninja_cmd, '/dev/null'])
        except subprocess.CalledProcessError as err:
            sys.stdout.buffer.write(err.output)
            raise err
    final_output = ''
    for line in output.decode('utf-8').splitlines(True):
        if len(line) > 0 and line[-1] == '\r':
            continue
        final_output += line.replace('\r', '')
    return final_output

class Output(unittest.TestCase):
    def test_issue_1418(self):
        self.assertEqual(run(
'''rule echo
  command = sleep 0.$delay && echo $out
  description = echo $out

build a: echo
  delay = 3
build b: echo
  delay = 2
build c: echo
  delay = 1
'''),
'''[1/3] echo c\x1b[K
c
[2/3] echo b\x1b[K
b
[3/3] echo a\x1b[K
a
''')

    def test_issue_1214(self):
        print_red = '''rule echo
  command = printf '\x1b[31mred\x1b[0m'
  description = echo $out

build a: echo
'''
        # Only strip color when ninja's output is piped.
        self.assertEqual(run(print_red),
'''[1/1] echo a\x1b[K
\x1b[31mred\x1b[0m
''')
        self.assertEqual(run(print_red, pipe=True),
'''[1/1] echo a
red
''')
        # Even in verbose mode, colors should still only be stripped when piped.
        self.assertEqual(run(print_red, flags='-v'),
'''[1/1] printf '\x1b[31mred\x1b[0m'
\x1b[31mred\x1b[0m
''')
        self.assertEqual(run(print_red, flags='-v', pipe=True),
'''[1/1] printf '\x1b[31mred\x1b[0m'
red
''')

if __name__ == '__main__':
    unittest.main()
