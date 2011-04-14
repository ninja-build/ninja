#!/bin/sh

set -o errexit
set -o nounset

STATUS=0

# Print each of its arguments on stderr (one per line) prefixed by the
# basename of this script and 'error'.
error()
{
  local i
  for i
  do
    echo >&2 "error: $i"
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
  test -r "$file" || fatal "'$file' is not readable."
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
