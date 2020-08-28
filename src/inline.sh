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

# This quick script converts a text file into an #include-able header.
# It expects the name of the variable as its first argument, and reads
# stdin and writes stdout.

varname="$1"

# 'od' and 'sed' may not be available on all platforms, and may not support the
# flags used here. We must ensure that the script exits with a non-zero exit
# code in those cases.
byte_vals=$(od -t x1 -A n -v) || exit 1
escaped_byte_vals=$(echo "${byte_vals}" \
  | sed -e 's|^[\t ]\{0,\}$||g; s|[\t ]\{1,\}| |g; s| \{1,\}$||g; s| |\\x|g; s|^|"|; s|$|"|') \
  || exit 1

# Only write output once we have successfully generated the required data
printf "const char %s[] = \n%s;" "${varname}" "${escaped_byte_vals}"
