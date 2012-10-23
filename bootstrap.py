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

from __future__ import print_function

from optparse import OptionParser
import sys
import os
import glob
import errno
import shlex
import shutil
import subprocess

os.chdir(os.path.dirname(os.path.abspath(__file__)))

parser = OptionParser()
parser.add_option('--verbose', action='store_true',
                  help='enable verbose build',)
parser.add_option('--x64', action='store_true',
                  help='force 64-bit build (Windows)',)
(options, conf_args) = parser.parse_args()

def run(*args, **kwargs):
    returncode = subprocess.call(*args, **kwargs)
    if returncode != 0:
        sys.exit(returncode)

# Compute system-specific CFLAGS/LDFLAGS as used in both in the below
# g++ call as well as in the later configure.py.
cflags = os.environ.get('CFLAGS', '').split()
ldflags = os.environ.get('LDFLAGS', '').split()
if sys.platform.startswith('freebsd'):
    cflags.append('-I/usr/local/include')
    ldflags.append('-L/usr/local/lib')

print('Building ninja manually...')

try:
    os.mkdir('build')
except OSError:
    e = sys.exc_info()[1]
    if e.errno != errno.EEXIST:
        raise

sources = []
for src in glob.glob('src/*.cc'):
    if src.endswith('test.cc') or src.endswith('.in.cc'):
        continue
    if src.endswith('bench.cc'):
        continue

    filename = os.path.basename(src)
    if filename == 'browse.cc':  # Depends on generated header.
        continue

    if sys.platform.startswith('win32'):
        if src.endswith('-posix.cc'):
            continue
    else:
        if src.endswith('-win32.cc'):
            continue

    sources.append(src)

if sys.platform.startswith('win32'):
    sources.append('src/getopt.c')

vcdir = os.environ.get('VCINSTALLDIR')
if vcdir:
    if options.x64:
        cl = [os.path.join(vcdir, 'bin', 'amd64', 'cl.exe')]
    else:
        cl = [os.path.join(vcdir, 'bin', 'cl.exe')]
    args = cl + ['/nologo', '/EHsc', '/DNOMINMAX']
else:
    args = shlex.split(os.environ.get('CXX', 'g++'))
    cflags.extend(['-Wno-deprecated',
                   '-DNINJA_PYTHON="' + sys.executable + '"',
                   '-DNINJA_BOOTSTRAP'])
    if sys.platform.startswith('win32'):
        cflags.append('-D_WIN32_WINNT=0x0501')
    if options.x64:
        cflags.append('-m64')
args.extend(cflags)
args.extend(ldflags)
binary = 'ninja.bootstrap'
if sys.platform.startswith('win32'):
    binary = 'ninja.bootstrap.exe'
args.extend(sources)
if vcdir:
    args.extend(['/link', '/out:' + binary])
else:
    args.extend(['-o', binary])

if options.verbose:
    print(' '.join(args))

try:
    run(args)
except:
    print('Failure running:', args)
    raise

verbose = []
if options.verbose:
    verbose = ['-v']

if sys.platform.startswith('win32'):
    print('Building ninja using itself...')
    run([sys.executable, 'configure.py', '--with-ninja=%s' % binary] +
        conf_args)
    run(['./' + binary] + verbose)

    # Copy the new executable over the bootstrap one.
    shutil.copyfile('ninja.exe', binary)

    # Clean up.
    for obj in glob.glob('*.obj'):
        os.unlink(obj)

    print("""
Done!

Note: to work around Windows file locking, where you can't rebuild an
in-use binary, to run ninja after making any changes to build ninja itself
you should run ninja.bootstrap instead.  Your build is also configured to
use ninja.bootstrap.exe as the MSVC helper; see the --with-ninja flag of
the --help output of configure.py.""")
else:
    print('Building ninja using itself...')
    run([sys.executable, 'configure.py'] + conf_args)
    run(['./' + binary] + verbose)
    os.unlink(binary)
    print('Done!')
