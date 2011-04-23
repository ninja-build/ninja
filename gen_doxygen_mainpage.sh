#!/bin/sh

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

set -o errexit
set -o nounset

STATUS=0

# Print each of its arguments on stderr (one per line) prefixed by the
# basename of this script.
stderr()
{
  local me=$(basename "$0")
  local i
  for i
  do
    echo >&2 "$me: $i"
  done
}

# Print each of its arguments on stderr (one per line) prefixed by the
# basename of this script and 'error'.
error()
{
  local i
  for i
  do
    stderr "error: $i"
  done
  STATUS=1
}

generate_header()
{
  cat <<EOF
/**
 * \\mainpage
EOF
}

generate_footer()
{
  cat <<EOF
 */
EOF
}

include_file()
{
  local file="$1"
  if ! [ -r "$file" ]
  then
    error "'$file' is not readable."
    return
  fi
  cat <<EOF
 * \\section $file
 * \\verbatim
EOF
  cat < "$file"
  cat <<EOF
 \\endverbatim
EOF
}

if [ $# -eq 0 ]
then
  echo >&2 "usage: $0 inputs..."
  exit 1
fi

generate_header
for i in "$@"
do
  include_file "$i"
done
generate_footer

exit $STATUS
