#!/bin/sh
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

# This quick script converts a text file into an #include-able header,
# by hex-string encoding it.
# It expects the name of a variable holding the file as its first argument,
# reads the file from stdin, and writes stdout.

varname="$1"
echo "const char $varname[] ="
od -t x1 -A n -v |
  awk '{printf "\""; for(i=1; i<=NF; i++){ printf "\x"$i}; print "\"";  }'
echo ";"


