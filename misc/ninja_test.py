#!/usr/bin/env python

# Copyright 2011 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import unittest
from StringIO import StringIO

import ninja

LONGWORD = 'a' * 10
INDENT = '    '

class TestLineWordWrap(unittest.TestCase):
    def setUp(self):
        self.out = StringIO()
        self.n = ninja.Writer(self.out, width=8)

    def test_single_long_word(self):
        # We shouldn't wrap a single long word.
        self.n._line(LONGWORD)
        self.assertEqual(LONGWORD + '\n', self.out.getvalue())

    def test_few_long_words(self):
        # We should wrap a line where the second word is overlong.
        self.n._line(' '.join(['x', LONGWORD, 'y']))
        self.assertEqual(' $\n'.join(['x',
                                      INDENT + LONGWORD,
                                      INDENT + 'y']) + '\n',
                         self.out.getvalue())

    def test_few_long_words_indented(self):
        # Check wrapping in the presence of indenting.
        self.n._line(' '.join(['x', LONGWORD, 'y']), indent=1)
        self.assertEqual(' $\n'.join(['  ' + 'x',
                                      '  ' + INDENT + LONGWORD,
                                      '  ' + INDENT + 'y']) + '\n',
                         self.out.getvalue())

if __name__ == '__main__':
    unittest.main()
