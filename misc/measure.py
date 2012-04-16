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

devnull = open('/dev/null', 'w')

def run(cmd, repeat=10):
    print 'sampling:',
    sys.stdout.flush()

    samples = []
    for _ in range(repeat):
        start = time.time()
        subprocess.call(cmd, stdout=devnull, stderr=devnull)
        end = time.time()
        dt = (end - start) * 1000
        print '%dms' % int(dt),
        sys.stdout.flush()
        samples.append(dt)
    print

    # We're interested in the 'pure' runtime of the code, which is
    # conceptually the smallest time we'd see if we ran it enough times
    # such that it got the perfect time slices / disk cache hits.
    best = min(samples)
    # Also print how varied the outputs were in an attempt to make it
    # more obvious if something has gone terribly wrong.
    err = sum(s - best for s in samples) / float(len(samples))
    print 'estimate: %dms (mean err %.1fms)' % (best, err)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print 'usage: measure.py command args...'
        sys.exit(1)
    run(cmd=sys.argv[1:])
