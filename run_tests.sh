#!/bin/sh

set -e
set -u
export LC_ALL=C

if [ `id -u` -ne 0 ]
then
  echo >&2 "You have to be root to run this script."
  exit 2
fi

ulimit -n 2048
./ninja_test
