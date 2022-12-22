#compdef ninja
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

# Add the following to your .zshrc to tab-complete ninja targets
#   fpath=(path/to/ninja/misc/zsh-completion $fpath)

(( $+functions[_ninja-get-targets] )) || _ninja-get-targets() {
  dir="."
  if [ -n "${opt_args[-C]}" ];
  then
    eval dir="${opt_args[-C]}"
  fi
  file="build.ninja"
  if [ -n "${opt_args[-f]}" ];
  then
    eval file="${opt_args[-f]}"
  fi
  targets_command="ninja -f \"${file}\" -C \"${dir}\" -t targets all"
  eval ${targets_command} 2>/dev/null | cut -d: -f1
}

(( $+functions[_ninja-get-tools] )) || _ninja-get-tools() {
  # remove the first line; remove the leading spaces; replace spaces with colon
  ninja -t list 2> /dev/null | sed -e '1d;s/^ *//;s/ \+/:/'
}

(( $+functions[_ninja-get-modes] )) || _ninja-get-modes() {
  # remove the first line; remove the last line; remove the leading spaces; replace spaces with colon
  ninja -d list 2> /dev/null | sed -e '1d;$d;s/^ *//;s/ \+/:/'
}

(( $+functions[_ninja-modes] )) || _ninja-modes() {
  local -a modes
  modes=(${(fo)"$(_ninja-get-modes)"})
  _describe 'modes' modes
}

(( $+functions[_ninja-tools] )) || _ninja-tools() {
  local -a tools
  tools=(${(fo)"$(_ninja-get-tools)"})
  _describe 'tools' tools
}

(( $+functions[_ninja-targets] )) || _ninja-targets() {
  local -a targets
  targets=(${(fo)"$(_ninja-get-targets)"})
  _describe 'targets' targets
}

_arguments \
  '(- *)'{-h,--help}'[Show help]' \
  '(- *)--version[Print ninja version]' \
  '-C+[Change to directory before doing anything else]:directories:_directories' \
  '-f+[Specify input build file (default=build.ninja)]:files:_files' \
  '-j+[Run N jobs in parallel (default=number of CPUs available)]:number of jobs' \
  '-l+[Do not start new jobs if the load average is greater than N]:number of jobs' \
  '-k+[Keep going until N jobs fail (default=1)]:number of jobs' \
  '-n[Dry run (do not run commands but act like they succeeded)]' \
  '(-v --verbose --quiet)'{-v,--verbose}'[Show all command lines while building]' \
  "(-v --verbose --quiet)--quiet[Don't show progress status, just command output]" \
  '-d+[Enable debugging (use -d list to list modes)]:modes:_ninja-modes' \
  '-t+[Run a subtool (use -t list to list subtools)]:tools:_ninja-tools' \
  '*::targets:_ninja-targets'
