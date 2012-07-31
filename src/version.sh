#/bin/bash

# Copyright 2012 Google Inc. All Rights Reserved.
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

header="src/version.h"
version="V0.1.3" 

set -u
set -e
# set -x

which git || exit 0	# OK, noop

if [ -d .git ]; then
  branch=`git status -bsu no`
  revisioncount=`git log --oneline | wc -l`
  projectversion=`git describe --tags --long --always`
  # generate
  echo "const char* kVersion = \"ninja ${version} ${branch%%.*}-${projectversion%%-*}\";" \
    > "${header}.$$"
  # diff and mv if needed
  cmp -s "${header}.$$" "${header}" ||
    mv "${header}.$$" "${header}"
  # cleanup
  rm -f "${header}.$$"
fi

