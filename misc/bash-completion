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

# Add the following to your .bashrc to tab-complete ninja targets
#   . path/to/ninja/misc/bash-completion

_ninja_target() {
    local cur prev targets dir line targets_command OPTIND

    # When available, use bash_completion to:
    #   1) Complete words when the cursor is in the middle of the word
    #   2) Complete paths with files or directories, as appropriate
    if _get_comp_words_by_ref cur prev &>/dev/null ; then
        case $prev in
            -f)
                _filedir
                return 0
                ;;
            -C)
                _filedir -d
                return 0
                ;;
        esac
    else
        cur="${COMP_WORDS[COMP_CWORD]}"
    fi

    if [[ "$cur" == "--"* ]]; then
        # there is currently only one argument that takes --
	COMPREPLY=($(compgen -P '--' -W 'version' -- "${cur:2}"))
    else
	dir="."
	line=$(echo ${COMP_LINE} | cut -d" " -f 2-)
        # filter out all non relevant arguments but keep C for dirs
	while getopts :C:f:j:l:k:nvd:t: opt $line; do
	    case $opt in
                # eval for tilde expansion
		C) eval dir="$OPTARG" ;;
	    esac
	done;
	targets_command="eval ninja -C \"${dir}\" -t targets all 2>/dev/null | cut -d: -f1"
	COMPREPLY=($(compgen -W '`${targets_command}`' -- "$cur"))
    fi
    return
}
complete -F _ninja_target ninja
