#!/usr/bin/env python
#
# Copyright 2001 Google Inc. All Rights Reserved.
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

"""Script that generates the build.ninja for ninja itself.

Projects that use ninja themselves should either write a similar script
or use a meta-build system that supports Ninja output."""

from optparse import OptionParser
import os
import sys
sys.path.insert(0, 'misc')

import ninja

parser = OptionParser()
platforms = ['linux', 'freebsd', 'mingw', 'windows']
parser.add_option('--platform',
                  help='target platform (' + '/'.join(platforms) + ')',
                  choices=platforms)
parser.add_option('--debug', action='store_true',
                  help='enable debugging flags',)
parser.add_option('--profile', action='store_true',
                  help='enable profiling',)
(options, args) = parser.parse_args()

platform = options.platform
if platform is None:
    platform = sys.platform
    if platform.startswith('linux'):
        platform = 'linux'
    elif platform.startswith('freebsd'):
        platform = 'freebsd'
    elif platform.startswith('mingw'):
        platform = 'mingw'
    elif platform.startswith('win'):
        platform = 'windows'

BUILD_FILENAME = 'build.ninja'
buildfile = open(BUILD_FILENAME, 'w')
n = ninja.Writer(buildfile)
n.comment('This file is used to build ninja itself.')
n.comment('It is generated by ' + os.path.basename(__file__) + '.')
n.newline()

objext = '.o'
if platform == 'windows':
    objext = '.obj'

def src(filename):
    return os.path.join('src', filename)
def built(filename):
    return os.path.join('$builddir', filename)
def doc(filename):
    return os.path.join('doc', filename)
def cxx(name, **kwargs):
    return n.build(built(name + objext), 'cxx', src(name + '.cc'), **kwargs)
def cc(name, **kwargs):
    return n.build(built(name + objext), 'cxx', src(name + '.c'), **kwargs)

n.variable('builddir', 'build')

if platform == 'windows':
    cflags = ['/nologo', '/Zi', '/W4', '/WX', '/wd4530', '/wd4512',
            '/wd4706', '/wd4100', '/D_CRT_SECURE_NO_WARNINGS']
    if not options.debug:
        cflags.append('/Ox')
else:
    cflags = ['-g', '-Wall', '-Wno-deprecated', '-fno-exceptions',
            '-fvisibility=hidden', '-pipe']
    if not options.debug:
        cflags.append('-O2')
ldflags = []
if platform == 'mingw':
    n.variable('cxx', 'i586-mingw32msvc-c++')
    # "warning: visibility attribute not supported in this
    # configuration; ignored"
    cflags.remove('-fvisibility=hidden')
    cflags.append('-Igtest-1.6.0/include')
    ldflags.append('-Lgtest-1.6.0/lib/.libs')
elif platform == 'windows':
    n.variable('cxx', 'cldeps')
    cflags.append('-Igtest-1.6.0/include')
else:
    n.variable('cxx', os.environ.get('CXX', 'g++'))
    if options.profile:
        cflags.append('-pg')
        ldflags.append('-pg')

if 'CFLAGS' in os.environ:
    cflags.append(os.environ['CFLAGS'])
n.variable('cflags', ' '.join(cflags))
if 'LDFLAGS' in os.environ:
    ldflags.append(os.environ['LDFLAGS'])
n.variable('ldflags', ' '.join(ldflags))
n.newline()

if platform == 'windows':
  n.rule('cxx',
        command='$cxx "$out.d" "$out" cl $cflags -c "$in" "/Fo$out"', # cldeps, assume cl in path
        depfile='$out.d',
        description='CXX $out')
else:
  n.rule('cxx',
        command='$cxx -MMD -MF $out.d $cflags -c $in -o $out',
        depfile='$out.d',
        description='CXX $out')
n.newline()

ar = 'ar'
if platform == 'windows':
    ar = 'lib'
    n.rule('ar',
            command=ar + ' /nologo /out:$out $in',
            description='AR $out')
else:
    if platform == 'mingw':
        ar = 'i586-mingw32msvc-ar'
    n.rule('ar',
            command=ar + ' crs $out $in',
            description='AR $out')
n.newline()

if platform == 'windows':
    n.rule('link',
        command='$cxx $ldflags $in $libs /nologo /link /out:$out',
        description='LINK $out')
else:
    n.rule('link',
        command='$cxx $ldflags -o $out $in $libs',
        description='LINK $out')
n.newline()

objs = []

if platform not in ('mingw', 'windows'):
    n.comment('browse_py.h is used to inline browse.py.')
    n.rule('inline',
           command='src/inline.sh $varname < $in > $out',
           description='INLINE $out')
    n.build(built('browse_py.h'), 'inline', src('browse.py'),
            variables=[('varname', 'kBrowsePy')])
    n.newline()

    n.comment("TODO: this shouldn't need to depend on inline.sh.")
    objs += cxx('browse',
                implicit='src/inline.sh',
                order_only=built('browse_py.h'))
    n.newline()

n.comment('Core source files all build into ninja library.')
for name in ['build', 'build_log', 'clean', 'eval_env', 'graph', 'graphviz',
             'parsers', 'util', 'stat_cache',
             'ninja_jumble']:
    objs += cxx(name)
if platform == 'mingw' or platform == 'windows':
    objs += cxx('subprocess-win32')
    objs += cc('getopt')
else:
    objs += cxx('subprocess')
if platform == 'windows':
    ninja_lib = n.build(built('ninja.lib'), 'ar', objs)
else:
    ninja_lib = n.build(built('libninja.a'), 'ar', objs)
n.newline()

n.comment('Main executable is library plus main() function.')
objs = cxx('ninja')
if platform == 'windows':
    n.build('ninja.exe', 'link', objs, implicit=ninja_lib,
            variables=[('libs', '$builddir\\ninja.lib')])
else:
    n.build('ninja', 'link', objs, implicit=ninja_lib,
            variables=[('libs', '-L$builddir -lninja')])
n.newline()

n.comment('Tests all build into ninja_test executable.')
objs = []
for name in ['build_test', 'build_log_test', 'graph_test', 'ninja_test',
             'parsers_test', 'subprocess_test', 'util_test', 'clean_test',
             'test']:
    objs += cxx(name)
ldflags.append('-lgtest_main -lgtest')
if platform != 'mingw':
    ldflags.append('-lpthread')
n.build('ninja_test', 'link', objs, implicit=ninja_lib,
        variables=[('libs', '-L$builddir -lninja'),
                   ('ldflags', ' '.join(ldflags))])
n.newline()

n.comment('Perftest executable.')
objs = cxx('parser_perftest')
n.build('parser_perftest', 'link', objs, implicit=ninja_lib,
        variables=[('libs', '-L$builddir -lninja')])
n.newline()

n.comment('Generate a graph using the "graph" tool.')
n.rule('gendot',
       command='./ninja -t graph > $out')
n.rule('gengraph',
       command='dot -Tpng $in > $out')
dot = n.build(built('graph.dot'), 'gendot', ['ninja', 'build.ninja'])
n.build('graph.png', 'gengraph', dot)
n.newline()

n.comment('Generate the manual using asciidoc.')
n.rule('asciidoc',
       command='asciidoc -a toc -o $out $in',
       description='ASCIIDOC $in')
manual = n.build(doc('manual.html'), 'asciidoc', doc('manual.asciidoc'))
n.build('manual', 'phony',
        order_only=manual)
n.newline()

n.comment('Generate Doxygen.')
n.rule('doxygen',
       command='doxygen $in',
       description='DOXYGEN $in')
n.variable('doxygen_mainpage_generator',
           src('gen_doxygen_mainpage.sh'))
n.rule('doxygen_mainpage',
       command='$doxygen_mainpage_generator $in > $out',
       description='DOXYGEN_MAINPAGE $out')
mainpage = n.build(built('doxygen_mainpage'), 'doxygen_mainpage',
                   ['README', 'HACKING', 'COPYING'],
                   implicit=['$doxygen_mainpage_generator'])
n.build('doxygen', 'doxygen', doc('doxygen.config'),
        implicit=mainpage)
n.newline()

n.comment('Regenerate build files if build script changes.')
n.rule('configure',
       command='./configure.py')
n.build('build.ninja', 'configure',
        implicit='configure.py')

print 'wrote %s.' % BUILD_FILENAME
