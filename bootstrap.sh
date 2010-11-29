#!/bin/bash

set -e

srcs=$(ls *.cc | grep -v test)
echo "Building stage 1..."
g++ -o ninja.bootstrap $srcs
echo "Building final result..."
./ninja.bootstrap ninja
rm ninja.bootstrap
echo "Done!"
