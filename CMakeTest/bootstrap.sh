#!/bin/sh

set -e
set -x

#
# usage: $0 [delay]
#
delay=${1:-0.333} # FIXME default wait 1/3 sec if no ARG1 Given! ck

# cleanup
rm -rf build *.cpp *.orig *~

# create build env
mkdir -p build && cd build

#XXX gvim ../CMakeLists.txt build.ninja rules.ninja

"/usr/local/CMake 2.8-8.app/Contents/bin/cmake" -G Ninja ..
#FIXME cmake -G Ninja ..
ninja -d explain
#again
ninja -d stats

########################################################
# Now force an cmake error:
echo "FIXME: cmake must rerun again without this sleep:"
sleep ${delay} # FIXME!
########################################################
sed -i.orig -e '/test.cpp$/a\
    test_not_existing_file.cpp' ../CMakeLists.txt

# append a new line with sed
# sed '/patternstring/a\
# new line string' file1
#
# Please see a new line in the command between a\ and new string.

ls -lrt --full-time ../CMakeLists.txt ../*.cpp *.ninja test*

#
# The problem is the missing include statement!
# Now we fix it!
#
ninja -v -d explain || printf "\n ERROR ignored, coninue ...!\n"
if [ $(grep -sc '^include rules.ninja' build.ninja)  -eq 1 ]
then
    echo " OK"
else
    printf "\nTEST FAILED\n!"
    sed -i.orig -e '/compilation DAG.$/a\
include rules.ninja' build.ninja
fi

########################################################
echo "FIXME: cmake must rerun again without this sleep:"
sleep ${delay} # FIXME!
ls -lrt --full-time ../CMakeLists.txt ../*.cpp *.ninja test*
########################################################
sed -i.orig -e '/test_not_existing_file.cpp$/d' ../CMakeLists.txt
ninja -v -d explain

ninja -v -t clean
ninja -v -d explain
ninja -t query test || printf "\nTEST FAILED\n!"

#XXX cat ../CMakeLists.txt


# claus-kleins-macbook-pro:build clausklein$ ninja -t query build.ninja
# build.ninja:
# input: RERUN_CMAKE
#   | /Users/clausklein/Workspace/cpp/ninja/CMakeTest/CMakeLists.txt
#   | /Users/clausklein/Workspace/cpp/ninja/CMakeTest/build/CMakeFiles/CMakeCXXCompiler.cmake
#   | /Users/clausklein/Workspace/cpp/ninja/CMakeTest/build/CMakeFiles/CMakeSystem.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/CMakeCXXInformation.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/CMakeCommonLanguageInclude.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/CMakeGenericSystem.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/CMakeSystemSpecificInformation.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/Compiler/GNU-CXX.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/Compiler/GNU.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/Platform/Darwin-GNU-CXX.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/Platform/Darwin-GNU.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/Platform/Darwin.cmake
#   | /usr/local/CMake 2.8-8.app/Contents/share/cmake-2.8/Modules/Platform/UnixPaths.cmake
#   | CMakeCache.txt
# outputs:
# claus-kleins-macbook-pro:build clausklein$ 

