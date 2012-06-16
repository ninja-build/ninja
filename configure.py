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

import ninja_syntax

parser = OptionParser()
platforms = ['linux', 'freebsd', 'mingw', 'windows']
profilers = ['gmon', 'pprof']
parser.add_option('--platform',
                  help='target platform (' + '/'.join(platforms) + ')',
                  choices=platforms)
parser.add_option('--host',
                  help='host platform (' + '/'.join(platforms) + ')',
                  choices=platforms)
parser.add_option('--debug', action='store_true',
                  help='enable debugging extras',)
parser.add_option('--profile', metavar='TYPE',
                  choices=profilers,
                  help='enable profiling (' + '/'.join(profilers) + ')',)
parser.add_option('--with-gtest', metavar='PATH',
                  help='use gtest unpacked in directory PATH')
parser.add_option('--with-python', metavar='EXE',
                  help='use EXE as the Python interpreter',
                  default=os.path.basename(sys.executable))
(options, args) = parser.parse_args()
if args:
    print 'ERROR: extra unparsed command-line arguments:', args
    sys.exit(1)

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
host = options.host or platform

BUILD_FILENAME = 'build.ninja'
buildfile = open(BUILD_FILENAME, 'w')
n = ninja_syntax.Writer(buildfile)
n.comment('This file is used to build ninja itself.')
n.comment('It is generated by ' + os.path.basename(__file__) + '.')
n.newline()

n.comment('The arguments passed to configure.py, for rerunning it.')
n.variable('configure_args', ' '.join(sys.argv[1:]))
env_keys = set(['CXX', 'AR', 'CPPFLAGS', 'CFLAGS', 'LDFLAGS'])
configure_env = dict((k, os.environ[k]) for k in os.environ if k in env_keys)
if configure_env:
    config_str = ' '.join([k + '=' + configure_env[k] for k in configure_env])
    n.variable('configure_env', config_str + '$ ')
n.newline()

CXX = configure_env.get('CXX', 'g++')
objext = '.o'
if platform == 'windows':
    CXX = 'cl'
    objext = '.obj'

def src(filename):
    return os.path.join('src', filename)
def built(filename):
    return os.path.join('$builddir', filename)
def doc(filename):
    return os.path.join('doc', filename)
def cc(name, **kwargs):
    return n.build(built(name + objext), 'cxx', src(name + '.c'), **kwargs)
def cxx(name, **kwargs):
    return n.build(built(name + objext), 'cxx', src(name + '.cc'), **kwargs)
def binary(name):
    if platform in ('mingw', 'windows'):
        return name + '.exe'
    return name

n.variable('builddir', 'build')
n.variable('cxx', CXX)
if platform == 'windows':
    n.variable('ar', 'link')
else:
    n.variable('ar', configure_env.get('AR', 'ar'))

if platform == 'windows':
    cflags = ['/nologo', '/Zi', '/W4', '/WX', '/wd4530', '/wd4100', '/wd4706',
              '/wd4512', '/wd4800', '/wd4702', '/wd4819', '/GR-',
              '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
              "/DNINJA_PYTHON=\"%s\"" % (options.with_python,)]
    ldflags = ['/DEBUG', '/libpath:$builddir']
    if not options.debug:
        cflags += ['/Ox', '/DNDEBUG', '/GL']
        ldflags += ['/LTCG', '/OPT:REF', '/OPT:ICF']
else:
    cflags = ['-g', '-Wall', '-Wextra',
              '-Wdeprecated',
              '-Wno-unused-parameter',
              '-fno-rtti',
              '-fno-exceptions',
              '-fvisibility=hidden', '-pipe',
              "'-DNINJA_PYTHON=\"%s\"'" % (options.with_python,)]
    if options.debug:
        cflags += ['-D_GLIBCXX_DEBUG', '-D_GLIBCXX_DEBUG_PEDANTIC']
    else:
        cflags += ['-O2', '-DNDEBUG']
    if 'clang' in os.path.basename(CXX):
        cflags += ['-fcolor-diagnostics']
    ldflags = ['-L$builddir']
libs = []

if platform == 'mingw':
    cflags.remove('-fvisibility=hidden');
    ldflags.append('-static')
elif platform == 'sunos5':
    cflags.remove('-fvisibility=hidden')
elif platform == 'windows':
    pass
else:
    if options.profile == 'gmon':
        cflags.append('-pg')
        ldflags.append('-pg')
    elif options.profile == 'pprof':
        libs.append('-lprofiler')

cppflags = []
if 'CPPFLAGS' in configure_env:
    cppflags.append(configure_env['CPPFLAGS'])
n.variable('cppflags', ' '.join(cppflags))
if 'CFLAGS' in configure_env:
    cflags.append(configure_env['CFLAGS'])
n.variable('cflags', ' '.join(cflags))
if 'LDFLAGS' in configure_env:
    ldflags.append(configure_env['LDFLAGS'])
n.variable('ldflags', ' '.join(ldflags))
n.newline()

if platform == 'windows':
    n.rule('cxx',
        command='$cxx $cppflags $cflags -c $in /Fo$out',
        depfile='$out.d',
        description='CXX $out')
else:
    n.rule('cxx',
        command='$cxx -MMD -MT $out -MF $out.d $cppflags $cflags -c $in -o $out',
        depfile='$out.d',
        description='CXX $out')
n.newline()

if host == 'windows':
    n.rule('ar',
           command='lib /nologo /ltcg /out:$out $in',
           description='LIB $out')
elif host == 'mingw':
    n.rule('ar',
           command='cmd /c $ar cqs $out.tmp $in && move /Y $out.tmp $out',
           description='AR $out')
else:
    n.rule('ar',
           command='rm -f $out && $ar crs $out $in',
           description='AR $out')
n.newline()

if platform == 'windows':
    n.rule('link',
        command='$cxx $in $libs /nologo /link $ldflags /out:$out',
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
           description='INLINE $out',
           generator=True)  #XXX prevent clean of generated files
    n.build(built('browse_py.h'), 'inline', src('browse.py'),
            implicit='src/inline.sh',
            variables=[('varname', 'kBrowsePy')])
    n.newline()

    objs += cxx('browse', order_only=built('browse_py.h'))
    n.newline()

n.comment('the depfile parser and ninja lexers are generated using re2c.')
n.rule('re2c',
       command='re2c -b -i --no-generation-date -o $out $in',
       description='RE2C $out',
       generator=True)  #XXX prevent clean of generated files
# Generate the .cc files in the source directory so we can check them in.
n.build(src('depfile_parser.cc'), 're2c', src('depfile_parser.in.cc'))
n.build(src('lexer.cc'), 're2c', src('lexer.in.cc'))
n.newline()

n.comment('Core source files all build into ninja library.')
for name in ['build',
             'build_log',
             'clean',
             'depfile_parser',
             'disk_interface',
             'edit_distance',
             'eval_env',
             'explain',
             'graph',
             'graphviz',
             'lexer',
             'metrics',
             'parsers',
             'state',
             'util']:
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

if platform == 'windows':
    libs.append('ninja.lib')
else:
    libs.append('-lninja')

all_targets = []

n.comment('Main executable is library plus main() function.')
objs = cxx('ninja')
ninja = n.build(binary('ninja'), 'link', objs, implicit=ninja_lib,
                variables=[('libs', libs)])
if 'ninja' not in ninja:
  n.build('ninja', 'phony', ninja)
n.newline()
all_targets += ninja

n.comment('Tests all build into ninja_test executable.')

variables = []
test_cflags = None
test_ldflags = None
test_libs = libs
objs = []
if options.with_gtest:
    path = options.with_gtest

    gtest_all_incs = '-I%s -I%s' % (path, os.path.join(path, 'include'))
    if platform == 'windows':
        gtest_cflags = '/nologo /EHsc ' + gtest_all_incs
    else:
        gtest_cflags = '-fvisibility=hidden ' + gtest_all_incs
    objs += n.build(built('gtest-all' + objext), 'cxx',
                    os.path.join(path, 'src/gtest-all.cc'),
                    variables=[('cflags', gtest_cflags)])
    objs += n.build(built('gtest_main' + objext), 'cxx',
                    os.path.join(path, 'src/gtest_main.cc'),
                    variables=[('cflags', gtest_cflags)])

    test_cflags = cflags + ['-DGTEST_HAS_RTTI=0',
                            '-I%s' % os.path.join(path, 'include')]
elif platform == 'windows':
    test_libs.extend(['gtest_main.lib', 'gtest.lib'])
else:
    test_libs.extend(['-lgtest_main', '-lgtest'])

for name in ['build_log_test',
             'build_test',
             'clean_test',
             'depfile_parser_test',
             'disk_interface_test',
             'edit_distance_test',
             'graph_test',
             'lexer_test',
             'parsers_test',
             'state_test',
             'subprocess_test',
             'test',
             'util_test']:
    objs += cxx(name, variables=[('cflags', test_cflags)])

if platform != 'mingw' and platform != 'windows':
    test_libs.append('-lpthread')
ninja_test = n.build(binary('ninja_test'), 'link', objs, implicit=ninja_lib,
                     variables=[('ldflags', test_ldflags),
                                ('libs', test_libs)])
if 'ninja_test' not in ninja_test:
  n.build('ninja_test', 'phony', ninja_test)
n.newline()
all_targets += ninja_test


n.comment('Ancilliary executables.')
objs = cxx('parser_perftest')
all_targets += n.build(binary('parser_perftest'), 'link', objs,
                       implicit=ninja_lib, variables=[('libs', libs)])
objs = cxx('build_log_perftest')
all_targets += n.build(binary('build_log_perftest'), 'link', objs,
                       implicit=ninja_lib, variables=[('libs', libs)])
objs = cxx('canon_perftest')
all_targets += n.build(binary('canon_perftest'), 'link', objs,
                       implicit=ninja_lib, variables=[('libs', libs)])
objs = cxx('hash_collision_bench')
all_targets += n.build(binary('hash_collision_bench'), 'link', objs,
                              implicit=ninja_lib, variables=[('libs', libs)])
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
       command='asciidoc -a toc -a max-width=45em -o $out $in',
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

if host != 'mingw':
    n.comment('Regenerate build files if build script changes.')
    n.rule('configure',
           command='${configure_env}%s configure.py $configure_args' %
               options.with_python,
           generator=True)
    n.build('build.ninja', 'configure',
            implicit=['configure.py', 'misc/ninja_syntax.py'])
    n.newline()

n.comment('Build only the main binary by default.')
n.default(ninja)
n.newline()

n.build('all', 'phony', all_targets)

print 'wrote %s.' % BUILD_FILENAME
