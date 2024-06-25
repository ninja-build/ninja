#!/usr/bin/env bash
set -eu

cd $(dirname $0)

revision=d88c5e15079047777b418132ece5879e7c9aaa2b

for file in LICENSE \
              parallel_hashmap/phmap.h \
              parallel_hashmap/phmap_base.h \
              parallel_hashmap/phmap_bits.h \
              parallel_hashmap/phmap_config.h \
              parallel_hashmap/phmap_fwd_decl.h \
              parallel_hashmap/phmap_utils.h
do
  mkdir -p $(dirname $file)
  curl --silent https://raw.githubusercontent.com/greg7mdp/parallel-hashmap/$revision/$file > $file
done
