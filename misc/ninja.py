#!/usr/bin/python

"""Python module for generating .ninja files.

Note that this is emphatically not a required piece of Ninja; it's
just a helpful utility for build-file-generation systems that already
use Python.
"""

import textwrap

class Writer(object):
    def __init__(self, output, width=78):
        self.output = output
        self.width = width

    def newline(self):
        self.output.write('\n')

    def comment(self, text):
        for line in textwrap.wrap(text, self.width - 2):
            self.output.write('# ' + line + '\n')

    def variable(self, key, value, indent=0):
        self._line('%s%s = %s' % ('  ' * indent, key, value), indent)

    def rule(self, name, command, description=None, depfile=None):
        self._line('rule %s' % name)
        self.variable('command', command, indent=1)
        if description:
            self.variable('description', description, indent=1)
        if depfile:
            self.variable('depfile', depfile, indent=1)

    def build(self, outputs, rule, inputs=None, implicit=None, order_only=None,
              variables=None):
        outputs = self._as_list(outputs)
        all_inputs = self._as_list(inputs)[:]

        if implicit:
            all_inputs.append('|')
            all_inputs.extend(self._as_list(implicit))
        if order_only:
            all_inputs.append('||')
            all_inputs.extend(self._as_list(order_only))

        self._line('build %s: %s %s' % (' '.join(outputs),
                                        rule,
                                        ' '.join(all_inputs)))

        if variables:
            for key, val in variables:
                self.variable(key, val, indent=1)

        return outputs

    # write 'text' word-wrapped at self.width characters
    def _line(self, text, indent=0):
        while len(text) > self.width:
            # we're searching for the last space in the
            # word-wraped area, and if non found we're looking
            # for the first space after the word-wrapped area.
            # for instance, if we want to wrap
            # "a b ghijkl mnopq" at 4 characters, wel'll first
            # the substring that fits the word wrapped area is
            # "a b ", so the last space (marked ~) is "a b~",
            # we'll print "a b ", next substring that fits
            # word wrapped area is "ghij", since there are no
            # spaces there, we'll look for the first space in
            # "kl mno" which is "kl~mno", we'll print
            # "ghijk " and continue to the next substring.
            # Now at "mnop" we have no space, and neither
            # at the next substring "q". In that case we'll
            # just print all current string, and stop.
            space = text.rfind(' ', 0, self.width - 4)
            if space < 0 or text[:space].strip() == "":
                space = text.find(' ',self.width -4)
            leading_space = '  ' * (indent+2)
            if space < 0 or text[:space].strip() == "":
                text = leading_space + text.lstrip()
                break
            self.output.write(text[0:space] + ' $\n')
            text = leading_space + text[space:].lstrip()
        self.output.write(text + '\n')

    def _as_list(self, input):
        if input is None:
            return []
        if isinstance(input, list):
            return input
        return [input]
