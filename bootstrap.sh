#!/bin/bash

set -e

srcs=$(ls *.cc | grep -v test)
echo "Building stage 1..."
g++ -o ninja $srcs
echo "Building final result..."
./ninja ninja
echo "Done!"
