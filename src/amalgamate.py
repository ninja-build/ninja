#!/usr/bin/python

import sys
import re
import os

INCLUDE_RE = re.compile('^#include "([^"]*)"')

def parse_include(line):
  match = INCLUDE_RE.match(line)
  return match.groups()[0] if match else None

def exists(name):
  if os.path.isfile(name):
    return name
  else:
    return None

class Amalgamator:
  def __init__(self, output_file):
    self.included = set()
    self.output_c = open(output_file, "w")

  def add_src(self, infile_name):
    for line in open(infile_name):
      include = parse_include(line)
      if include is not None:
        if include not in self.included:
          self.included.add(include)
          self.add_src(exists(include) or exists(os.path.join("src", include)))
      else:
        self.output_c.write(line)

# ---- main ----

output_file = sys.argv[1]
amalgamator = Amalgamator(output_file)

for filename in sys.argv[2:]:
  amalgamator.add_src(filename.strip())
