// Copyright 2019 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>
#include <set>
#include <memory>

#include "deps_log.h"
#include "graph.h"
#include "missing_deps.h"
#include "state.h"
#include "test.h"

const char kTestDepsLogFilename[] = "MissingDepTest-tempdepslog";

class MissingDependencyTestDelegate : public MissingDependencyScannerDelegate {
  void OnMissingDep(const Node* node, const std::string& path,
                    const Rule& generator) {}
};

struct MissingDependencyScannerTest : public testing::Test {
  MissingDependencyScannerTest()
      : generator_rule_("generator_rule"), compile_rule_("compile_rule"),
        scanner_(nullptr) {
    std::string err;
    deps_log_.OpenForWrite(kTestDepsLogFilename, &err);
    EXPECT_EQ("", err);
  }

  ~MissingDependencyScannerTest() {
    // Remove test file.
    deps_log_.Close();
  }

  MissingDependencyScanner* scanner() {
    assert(scanner_);
    return scanner_.get();
  }

  void RecordDepsLogDep(const std::string& from, const std::string& to) {
    Node* node_deps[] = { state_.LookupNode(to) };
    deps_log_.RecordDeps(state_.LookupNode(from), 0, 1, node_deps);
  }

  void ProcessAllNodes() {
    std::string err;
    std::vector<Node*> nodes = state_.RootNodes(&err);
    EXPECT_EQ("", err);
    scanner_ = std::make_unique<MissingDependencyScanner>(
        &delegate_, &deps_log_, &state_, &filesystem_, nodes);
  }

  void CreateInitialState() {
    EvalString deps_type;
    deps_type.AddText("gcc");
    compile_rule_.AddBinding("deps", deps_type);
    generator_rule_.AddBinding("deps", deps_type);
    Edge* header_edge = state_.AddEdge(&generator_rule_);
    state_.AddOut(header_edge, "generated_header", 0, nullptr);
    Edge* compile_edge = state_.AddEdge(&compile_rule_);
    state_.AddOut(compile_edge, "compiled_object", 0, nullptr);
  }

  void CreateGraphDependencyBetween(const char* from, const char* to) {
    Node* from_node = state_.LookupNode(from);
    Edge* from_edge = from_node->in_edge();
    state_.AddIn(from_edge, to, 0);
  }

  void AssertMissingDependencyBetween(const char* flaky, const char* generated,
                                      Rule* rule) {
    Node* flaky_node = state_.LookupNode(flaky);
    ASSERT_EQ(1u, scanner()->nodes_missing_deps_.count(flaky_node));
    Node* generated_node = state_.LookupNode(generated);
    ASSERT_EQ(1u, scanner()->generated_nodes_.count(generated_node));
    ASSERT_EQ(1u, scanner()->generator_rules_.count(rule));
  }

  ScopedFilePath scoped_file_path_ = kTestDepsLogFilename;
  MissingDependencyTestDelegate delegate_;
  Rule generator_rule_;
  Rule compile_rule_;
  DepsLog deps_log_;
  State state_;
  VirtualFileSystem filesystem_;
  std::unique_ptr<MissingDependencyScanner> scanner_;
};

TEST_F(MissingDependencyScannerTest, EmptyGraph) {
  ProcessAllNodes();
  ASSERT_FALSE(scanner()->HadMissingDeps());
}

TEST_F(MissingDependencyScannerTest, NoMissingDep) {
  CreateInitialState();
  ProcessAllNodes();
  ASSERT_FALSE(scanner()->HadMissingDeps());
}

TEST_F(MissingDependencyScannerTest, MissingDepPresent) {
  CreateInitialState();
  // compiled_object uses generated_header, without a proper dependency
  RecordDepsLogDep("compiled_object", "generated_header");
  ProcessAllNodes();
  ASSERT_TRUE(scanner()->HadMissingDeps());
  ASSERT_EQ(1u, scanner()->nodes_missing_deps_.size());
  ASSERT_EQ(1u, scanner()->missing_dep_path_count_);
  AssertMissingDependencyBetween("compiled_object", "generated_header",
                                 &generator_rule_);
}

TEST_F(MissingDependencyScannerTest, MissingDepFixedDirect) {
  CreateInitialState();
  // Adding the direct dependency fixes the missing dep
  CreateGraphDependencyBetween("compiled_object", "generated_header");
  RecordDepsLogDep("compiled_object", "generated_header");
  ProcessAllNodes();
  ASSERT_FALSE(scanner()->HadMissingDeps());
}

TEST_F(MissingDependencyScannerTest, MissingDepFixedIndirect) {
  CreateInitialState();
  // Adding an indirect dependency also fixes the issue
  Edge* intermediate_edge = state_.AddEdge(&generator_rule_);
  state_.AddOut(intermediate_edge, "intermediate", 0, nullptr);
  CreateGraphDependencyBetween("compiled_object", "intermediate");
  CreateGraphDependencyBetween("intermediate", "generated_header");
  RecordDepsLogDep("compiled_object", "generated_header");
  ProcessAllNodes();
  ASSERT_FALSE(scanner()->HadMissingDeps());
}

TEST_F(MissingDependencyScannerTest, CyclicMissingDep) {
  CreateInitialState();
  RecordDepsLogDep("generated_header", "compiled_object");
  RecordDepsLogDep("compiled_object", "generated_header");
  // In case of a cycle, both paths are reported (and there is
  // no way to fix the issue by adding deps).
  ProcessAllNodes();
  ASSERT_TRUE(scanner()->HadMissingDeps());
  ASSERT_EQ(2u, scanner()->nodes_missing_deps_.size());
  ASSERT_EQ(2u, scanner()->missing_dep_path_count_);
  AssertMissingDependencyBetween("compiled_object", "generated_header",
                                 &generator_rule_);
  AssertMissingDependencyBetween("generated_header", "compiled_object",
                                 &compile_rule_);
}

TEST_F(MissingDependencyScannerTest, CycleInGraph) {
  CreateInitialState();
  CreateGraphDependencyBetween("compiled_object", "generated_header");
  CreateGraphDependencyBetween("generated_header", "compiled_object");
  // The missing-deps tool doesn't deal with cycles in the graph, because
  // there will be an error loading the graph before we get to the tool.
  // This test is to illustrate that.
  std::string err;
  std::vector<Node*> nodes = state_.RootNodes(&err);
  ASSERT_NE("", err);
}

class MissingDependencyCheckDelegate : public MissingDependencyScannerDelegate {
  void OnMissingDep(const Node* node, const std::string& path,
                    const Rule& generator) {
    data_.insert({ node->path(), path, generator.name() });
  }

 public:
  using type = std::set<std::array<std::string, 3>>;
  type data_;
};

struct MissingDependencyScannerTestWithDepfile
    : public StateTestWithBuiltinRules {
  MissingDependencyScannerTestWithDepfile()
      : scan_(&state_, NULL, NULL, &fs_, NULL, NULL),
        scanner_(nullptr) {
    std::string err;
    deps_log_.OpenForWrite(kTestDepsLogFilename, &err);
    EXPECT_EQ("", err);
  }

  void ProcessAllNodes() {
    std::string err;
    std::vector<Node*> nodes = state_.RootNodes(&err);
    EXPECT_EQ("", err);
    scanner_ = std::make_unique<MissingDependencyScanner>(
        &delegate_, &deps_log_, &state_, &fs_, nodes);
  }

  MissingDependencyScanner* scanner() { return scanner_.get(); }

  VirtualFileSystem fs_;
  MissingDependencyCheckDelegate delegate_;
  DepsLog deps_log_;
  DependencyScan scan_;
  std::unique_ptr<MissingDependencyScanner> scanner_;
};

TEST_F(MissingDependencyScannerTestWithDepfile, MissingDepPresentFromDyndep) {
  // the depfile input 'generated.h' is generated by dyndep.
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_, R"ninja(
rule catdep
  depfile = a.d
  command = cat $in > $out && printf 'a.o: generated.h\n' > a.d
build a.o: catdep a.c

rule dd
  command = touch dyndep.dd && printf 'ninja_dyndep_version = 1\nbuild out | generated.h: dyndep\n' > dyndep.dd

rule dyn_touch
  command = touch generated.h out
  dyndep = dyndep.dd

build dyndep.dd: dd
build out: dyn_touch || dyndep.dd
)ninja"));

  fs_.Create("a.d", "a.o: generated.h\n");
  fs_.Create("dyndep.dd",
             "ninja_dyndep_version = 1\n"
             "build out | generated.h: dyndep\n");

  ProcessAllNodes();
  EXPECT_TRUE(scanner()->HadMissingDeps());
  EXPECT_EQ(1u, scanner()->nodes_missing_deps_.size());
  EXPECT_EQ(1u, scanner()->missing_dep_path_count_);
  EXPECT_EQ(delegate_.data_, MissingDependencyCheckDelegate::type(
                                 { { "a.o", "generated.h", "dyn_touch" } }));

  // dyndep generates different header, not required by output "a.o"
  delegate_.data_.clear();
  fs_.Create("a.d", "a.o: generated.h\n");
  fs_.Create("dyndep.dd",
             "ninja_dyndep_version = 1\n"
             "build out | generated_Other.h: dyndep\n");

  ProcessAllNodes();
  EXPECT_FALSE(scanner()->HadMissingDeps());
  EXPECT_EQ(delegate_.data_, MissingDependencyCheckDelegate::type());
}

TEST_F(MissingDependencyScannerTestWithDepfile, MissingDepPresentFromDyndepInvalidFix) {
  // 'generated_ManifestInput.h' is provided as a manifest input, while the corresponding
  // output edge is introduced via a dyndep file.
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_, R"ninja(
rule catdep
  depfile = a.d
  command = cat $in > $out && printf 'a.o: generated.h\n' > a.d
build a.o: catdep a.c generated_ManifestInput.h

rule dd
  command = touch $out && printf 'ninja_dyndep_version = 1\nbuild out | generated.h generated_ManifestInput.h: dyndep\n' > $out

rule dyn_touch
  command = touch generated.h generated_ManifestInput.h out
  dyndep = dyndep.dd

build dyndep.dd: dd
build out: dyn_touch || dyndep.dd
)ninja"));

  fs_.Create("a.d", "a.o: generated.h\n");
  fs_.Create("dyndep.dd",
             "ninja_dyndep_version = 1\n"
             "build out | generated.h generated_ManifestInput.h: dyndep\n");

  ProcessAllNodes();
  EXPECT_TRUE(scanner()->HadMissingDeps());
  EXPECT_EQ(delegate_.data_,
            MissingDependencyCheckDelegate::type(
                { { "a.o", "generated.h", "dyn_touch" },
                  { "a.o", "generated_ManifestInput.h", "dyn_touch" } }));
}
