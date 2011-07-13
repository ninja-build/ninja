#!/usr/bin/env python
import unittest
from StringIO import StringIO
import ninja

class TestLineWordWrap(unittest.TestCase):
    def setUp(self):
        self.out = StringIO()
        self.n = ninja.Writer(self.out,5)

    def test_single_long_word(self):
        self.n._line('a'*10)
        self.assertEqual('a'*10,self.out.getvalue())
    def test_few_long_words(self):
        self.n._line('x '+'a'*10+' y')
        self.assertEqual('x \n'+'a'*10+'\ny',self.out.getvalue())
    def test_few_long_words_space(self):
        self.n._line('x '+'a'*10+' y',2)
        self.assertEqual('x \n  '+'a'*10+'\n  y',self.out.getvalue())

if __name__ == '__main__':
      unittest.main()
