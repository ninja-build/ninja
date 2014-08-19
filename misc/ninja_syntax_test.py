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

try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO

import ninja_syntax

LONGWORD = 'a' * 10
LONGWORDWITHSPACES = 'a'*5 + '$ ' + 'a'*5
INDENT = '    '

class TestLineWordWrap(unittest.TestCase):
    def setUp(self):
        self.out = StringIO()
        self.n = ninja_syntax.Writer(self.out, width=8)

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

    def test_short_words_indented(self):
        # Test that indent is taking into acount when breaking subsequent lines.
        # The second line should not be '    to tree', as that's longer than the
        # test layout width of 8.
        self.n._line('line_one to tree')
        self.assertEqual('''\
line_one $
    to $
    tree
''',
                         self.out.getvalue())

    def test_few_long_words_indented(self):
        # Check wrapping in the presence of indenting.
        self.n._line(' '.join(['x', LONGWORD, 'y']), indent=1)
        self.assertEqual(' $\n'.join(['  ' + 'x',
                                      '  ' + INDENT + LONGWORD,
                                      '  ' + INDENT + 'y']) + '\n',
                         self.out.getvalue())

    def test_escaped_spaces(self):
        self.n._line(' '.join(['x', LONGWORDWITHSPACES, 'y']))
        self.assertEqual(' $\n'.join(['x',
                                      INDENT + LONGWORDWITHSPACES,
                                      INDENT + 'y']) + '\n',
                         self.out.getvalue())

    def test_fit_many_words(self):
        self.n = ninja_syntax.Writer(self.out, width=78)
        self.n._line('command = cd ../../chrome; python ../tools/grit/grit/format/repack.py ../out/Debug/obj/chrome/chrome_dll.gen/repack/theme_resources_large.pak ../out/Debug/gen/chrome/theme_resources_large.pak', 1)
        self.assertEqual('''\
  command = cd ../../chrome; python ../tools/grit/grit/format/repack.py $
      ../out/Debug/obj/chrome/chrome_dll.gen/repack/theme_resources_large.pak $
      ../out/Debug/gen/chrome/theme_resources_large.pak
''',
                         self.out.getvalue())

    def test_leading_space(self):
        self.n = ninja_syntax.Writer(self.out, width=14)  # force wrapping
        self.n.variable('foo', ['', '-bar', '-somethinglong'], 0)
        self.assertEqual('''\
foo = -bar $
    -somethinglong
''',
                         self.out.getvalue())

    def test_embedded_dollar_dollar(self):
        self.n = ninja_syntax.Writer(self.out, width=15)  # force wrapping
        self.n.variable('foo', ['a$$b', '-somethinglong'], 0)
        self.assertEqual('''\
foo = a$$b $
    -somethinglong
''',
                         self.out.getvalue())

    def test_two_embedded_dollar_dollars(self):
        self.n = ninja_syntax.Writer(self.out, width=17)  # force wrapping
        self.n.variable('foo', ['a$$b', '-somethinglong'], 0)
        self.assertEqual('''\
foo = a$$b $
    -somethinglong
''',
                         self.out.getvalue())

    def test_leading_dollar_dollar(self):
        self.n = ninja_syntax.Writer(self.out, width=14)  # force wrapping
        self.n.variable('foo', ['$$b', '-somethinglong'], 0)
        self.assertEqual('''\
foo = $$b $
    -somethinglong
''',
                         self.out.getvalue())

    def test_trailing_dollar_dollar(self):
        self.n = ninja_syntax.Writer(self.out, width=14)  # force wrapping
        self.n.variable('foo', ['a$$', '-somethinglong'], 0)
        self.assertEqual('''\
foo = a$$ $
    -somethinglong
''',
                         self.out.getvalue())

class TestBuild(unittest.TestCase):
    def setUp(self):
        self.out = StringIO()
        self.n = ninja_syntax.Writer(self.out)

    def test_variables_dict(self):
        self.n.build('out', 'cc', 'in', variables={'name': 'value'})
        self.assertEqual('''\
build out: cc in
  name = value
''',
                         self.out.getvalue())

    def test_variables_list(self):
        self.n.build('out', 'cc', 'in', variables=[('name', 'value')])
        self.assertEqual('''\
build out: cc in
  name = value
''',
                         self.out.getvalue())

if __name__ == '__main__':
    unittest.main()
