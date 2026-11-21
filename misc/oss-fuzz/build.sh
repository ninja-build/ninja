#!/bin/bash -eu
# Copyright 2020 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

cmake -Bbuild-cmake -H.
cmake --build build-cmake

cd $SRC/ninja/misc

$CXX $CXXFLAGS -fdiagnostics-color -I/src/ninja/src -o fuzzer.o -c manifest_fuzzer.cc
$CXX $CXXFLAGS -fdiagnostics-color -I/src/ninja/src -o deps_log_fuzzer.o -c oss-fuzz/deps_log_fuzzer.cc

# Archive only the library objects (never the harness objects), so each
# harness links deterministically against exactly one LLVMFuzzerTestOneInput.
find ../build-cmake -name "*.o" -exec ar rcs fuzz_lib.a {} \;

$CXX $CXXFLAGS $LIB_FUZZING_ENGINE fuzzer.o -o $OUT/fuzzer fuzz_lib.a
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE deps_log_fuzzer.o -o $OUT/deps_log_fuzzer fuzz_lib.a

# Structurally valid .ninja_deps seeds (magic + version + ~id checksums are
# mutation-resistant gates; see deps_log_fuzzer.cc).
python3 oss-fuzz/generate_deps_seeds.py $OUT/deps_log_fuzzer_seed_corpus
zip -rj $OUT/deps_log_fuzzer_seed_corpus.zip $OUT/deps_log_fuzzer_seed_corpus

zip $OUT/fuzzer_seed_corpus.zip $SRC/sample_ninja_build
