#!/bin/bash

# This script sets up a git commit to push new documentation.

if [ ! -f doc/manual.html -o ! -f doc/doxygen/html/index.html ]; then
    echo "ERROR: run 'ninja manual doxygen' on the main branch first."
    exit 1
fi

mv doc/manual.html .
git add manual.html
git rm -r doxygen
mv doc/doxygen/html doxygen
git add doxygen
echo "done; run 'git commit' to commit new docs."
