#!/bin/sh
# created with:
# ninja -t commands
#
set -e
set -x

crossbuild=${1-1}

mkdir -p build
if [ ${crossbuild} -eq "0" ]; then

src/inline.sh kBrowsePy < src/browse.py > build/browse_py.h
g++ -MMD -MT build/build.o -MF build/build.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/build.cc -o build/build.o
g++ -MMD -MT build/build_log.o -MF build/build_log.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/build_log.cc -o build/build_log.o
g++ -MMD -MT build/clean.o -MF build/clean.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/clean.cc -o build/clean.o
g++ -MMD -MT build/depfile_parser.o -MF build/depfile_parser.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/depfile_parser.cc -o build/depfile_parser.o
g++ -MMD -MT build/disk_interface.o -MF build/disk_interface.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/disk_interface.cc -o build/disk_interface.o
g++ -MMD -MT build/edit_distance.o -MF build/edit_distance.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/edit_distance.cc -o build/edit_distance.o
g++ -MMD -MT build/eval_env.o -MF build/eval_env.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/eval_env.cc -o build/eval_env.o
g++ -MMD -MT build/explain.o -MF build/explain.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/explain.cc -o build/explain.o
g++ -MMD -MT build/graph.o -MF build/graph.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/graph.cc -o build/graph.o
g++ -MMD -MT build/graphviz.o -MF build/graphviz.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/graphviz.cc -o build/graphviz.o
g++ -MMD -MT build/lexer.o -MF build/lexer.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/lexer.cc -o build/lexer.o
g++ -MMD -MT build/metrics.o -MF build/metrics.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/metrics.cc -o build/metrics.o
g++ -MMD -MT build/parsers.o -MF build/parsers.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/parsers.cc -o build/parsers.o
g++ -MMD -MT build/state.o -MF build/state.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/state.cc -o build/state.o
g++ -MMD -MT build/util.o -MF build/util.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/util.cc -o build/util.o
g++ -MMD -MT build/subprocess.o -MF build/subprocess.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/subprocess.cc -o build/subprocess.o
g++ -MMD -MT build/ninja.o -MF build/ninja.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/ninja.cc -o build/ninja.o
g++ -MMD -MT build/browse.o -MF build/browse.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -fvisibility=hidden -pipe '-DNINJA_PYTHON="python"' -O2 -DNDEBUG -c src/browse.cc -o build/browse.o
rm -f build/libninja.a && ar crs build/libninja.a build/browse.o build/build.o build/build_log.o build/clean.o build/depfile_parser.o build/disk_interface.o build/edit_distance.o build/eval_env.o build/explain.o build/graph.o build/graphviz.o build/lexer.o build/metrics.o build/parsers.o build/state.o build/util.o build/subprocess.o
g++ -Lbuild -o ninja build/ninja.o -lninja

else

i386-mingw32-c++ -MMD -MT build/ninja.o -MF build/ninja.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/ninja.cc -o build/ninja.o
i386-mingw32-c++ -MMD -MT build/build.o -MF build/build.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/build.cc -o build/build.o
i386-mingw32-c++ -MMD -MT build/build_log.o -MF build/build_log.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/build_log.cc -o build/build_log.o
i386-mingw32-c++ -MMD -MT build/clean.o -MF build/clean.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/clean.cc -o build/clean.o
re2c -b -i --no-generation-date -o src/depfile_parser.cc src/depfile_parser.in.cc
i386-mingw32-c++ -MMD -MT build/depfile_parser.o -MF build/depfile_parser.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/depfile_parser.cc -o build/depfile_parser.o
i386-mingw32-c++ -MMD -MT build/disk_interface.o -MF build/disk_interface.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/disk_interface.cc -o build/disk_interface.o
i386-mingw32-c++ -MMD -MT build/edit_distance.o -MF build/edit_distance.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/edit_distance.cc -o build/edit_distance.o
i386-mingw32-c++ -MMD -MT build/eval_env.o -MF build/eval_env.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/eval_env.cc -o build/eval_env.o
i386-mingw32-c++ -MMD -MT build/explain.o -MF build/explain.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/explain.cc -o build/explain.o
i386-mingw32-c++ -MMD -MT build/graph.o -MF build/graph.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/graph.cc -o build/graph.o
i386-mingw32-c++ -MMD -MT build/graphviz.o -MF build/graphviz.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/graphviz.cc -o build/graphviz.o
re2c -b -i --no-generation-date -o src/lexer.cc src/lexer.in.cc
i386-mingw32-c++ -MMD -MT build/lexer.o -MF build/lexer.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/lexer.cc -o build/lexer.o
i386-mingw32-c++ -MMD -MT build/metrics.o -MF build/metrics.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/metrics.cc -o build/metrics.o
i386-mingw32-c++ -MMD -MT build/parsers.o -MF build/parsers.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/parsers.cc -o build/parsers.o
i386-mingw32-c++ -MMD -MT build/state.o -MF build/state.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/state.cc -o build/state.o
i386-mingw32-c++ -MMD -MT build/util.o -MF build/util.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/util.cc -o build/util.o
i386-mingw32-c++ -MMD -MT build/subprocess-win32.o -MF build/subprocess-win32.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/subprocess-win32.cc -o build/subprocess-win32.o
i386-mingw32-c++ -MMD -MT build/getopt.o -MF build/getopt.o.d -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-rtti -fno-exceptions -pipe '-DNINJA_PYTHON="Python"' -O2 -DNDEBUG -c src/getopt.c -o build/getopt.o
rm -f build/libninja.a && i386-mingw32-ar crs build/libninja.a build/build.o build/build_log.o build/clean.o build/depfile_parser.o build/disk_interface.o build/edit_distance.o build/eval_env.o build/explain.o build/graph.o build/graphviz.o build/lexer.o build/metrics.o build/parsers.o build/state.o build/util.o build/subprocess-win32.o build/getopt.o
i386-mingw32-c++ -Lbuild -static -o ninja.exe build/ninja.o -lninja

fi

