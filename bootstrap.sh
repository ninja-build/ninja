#!/bin/bash

set -e

srcs=$(ls src/*.cc | grep -v test)
echo "Building stage 1..."
g++ -Wno-deprecated -o ninja.bootstrap $srcs
echo "Building final result..."
./ninja.bootstrap ninja
rm ninja.bootstrap
echo "Done!"
