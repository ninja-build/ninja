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

"""measure the runtime of a command by repeatedly running it.
"""

import time
import subprocess
import sys
import math

devnull = open('/dev/null', 'w')

def run(cmd, repeat):
    print 'sampling:',
    sys.stdout.flush()

    samples = []
    for _ in range(repeat):
        start = time.time()
        subprocess.call(cmd, stdout=devnull, stderr=devnull)
        end = time.time()
        dt = (end - start) * 1000
        print '#%d:%dms' % (_, int(dt)),
        sys.stdout.flush()
        samples.append(dt)
    print

    # We're interested in the 'pure' runtime of the code, which is
    # conceptually the smallest time we'd see if we ran it enough times
    # such that it got the perfect time slices / disk cache hits.
    best = min(samples)
    print 'best: %dms' % (best)
    
    # Also print how varied the outputs were in an attempt to make it
    # more obvious if something has gone terribly wrong.
    N = float(len(samples))
    avg = float(sum(samples)) / N
    varsum = 0.0;
    # standard deviation
    for dt in samples:
        varsum = varsum + (float(dt) - avg)**2
    dev = math.sqrt(varsum / N)
    print 'mean: %dms +- %.1fms' % (avg, dev)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print 'usage: measure.py <number of runs> command args...'
        sys.exit(1)
    run(cmd=sys.argv[2:], repeat=int(sys.argv[1]))
