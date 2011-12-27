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

import sys
import os
import glob
import errno
import subprocess

def run(*args, **kwargs):
    try:
        subprocess.check_call(*args, **kwargs)
    except subprocess.CalledProcessError, e:
        sys.exit(e.returncode)

# Compute system-specific CFLAGS/LDFLAGS as used in both in the below
# g++ call as well as in the later configure.py.
cflags = os.environ.get('CFLAGS', '').split()
ldflags = os.environ.get('LDFLAGS', '').split()
if sys.platform.startswith('freebsd'):
    cflags.append('-I/usr/local/include')
    ldflags.append('-L/usr/local/lib')

print 'Building ninja manually...'

try:
    os.mkdir('build')
except OSError, e:
    if e.errno != errno.EEXIST:
        raise

with open('src/browse.py') as browse_py:
    with open('build/browse_py.h', 'w') as browse_py_h:
        run(['./src/inline.sh', 'kBrowsePy'],
            stdin=browse_py, stdout=browse_py_h)

sources = []
for src in glob.glob('src/*.cc'):
    if src.endswith('test.cc') or src.endswith('.in.cc'):
        continue

    if sys.platform.startswith('win32'):
        if src.endswith('/browse.cc') or src.endswith('/subprocess.cc'):
            continue
    else:
        if src.endswith('-win32.cc'):
            continue

    sources.append(src)

args = [os.environ.get('CXX', 'g++'), '-Wno-deprecated',
        '-DNINJA_PYTHON="' + sys.executable + '"']
args.extend(cflags)
args.extend(ldflags)
args.extend(['-o', 'ninja.bootstrap'])
args.extend(sources)
run(args)

print 'Building ninja using itself...'
run([sys.executable, 'configure.py'] + sys.argv[1:])
run(['./ninja.bootstrap'])
os.unlink('ninja.bootstrap')

print 'Done!'
