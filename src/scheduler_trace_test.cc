// Copyright 2026 Google Inc. All Rights Reserved.
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

#include "scheduler_trace.h"

#include <stdio.h>

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "build.h"
#include "build_result.h"
#include "graph.h"
#include "state.h"
#include "status_printer.h"
#include "test.h"

using namespace std;

namespace {

bool WriteTestFile(const string& path, const string& contents) {
  FILE* file = fopen(path.c_str(), "wb");
  if (!file)
    return false;
  bool result =
      fwrite(contents.data(), 1, contents.size(), file) == contents.size();
  result = fclose(file) == 0 && result;
  return result;
}

string ReadTestFile(const string& path) {
  ifstream input(path.c_str(), ios::in | ios::binary);
  return string((istreambuf_iterator<char>(input)),
                istreambuf_iterator<char>());
}

SchedulerTraceFields PlanFields(const Edge* edge) {
  SchedulerTraceFields fields;
  fields.push_back(make_pair("rule", edge->rule().name()));
  fields.push_back(make_pair(
      "pool", edge->pool()->name().empty() ? "default" : edge->pool()->name()));
  fields.push_back(make_pair("pool_depth", to_string(edge->pool()->depth())));
  fields.push_back(make_pair("weight", to_string(edge->weight())));
  return fields;
}

SchedulerTraceFields ReadyFields(const Edge* edge) {
  SchedulerTraceFields fields;
  fields.push_back(make_pair(
      "pool", edge->pool()->name().empty() ? "default" : edge->pool()->name()));
  fields.push_back(make_pair("pool_depth", to_string(edge->pool()->depth())));
  fields.push_back(make_pair("pool_use", "0"));
  return fields;
}

void RecordPlan(SchedulerTrace* trace, Edge* edge) {
  ASSERT_TRUE(trace->RecordEdgeEvent("plan", edge, PlanFields(edge)));
}

void RecordDependency(SchedulerTrace* trace, Edge* edge, Edge* dependency) {
  SchedulerTraceFields fields;
  fields.push_back(make_pair("dependency", SchedulerTrace::EdgeId(dependency)));
  fields.push_back(
      make_pair("dependency_label", SchedulerTrace::EdgeLabel(dependency)));
  ASSERT_TRUE(trace->RecordEdgeEvent("plan_dependency", edge, fields));
}

void RecordStart(SchedulerTrace* trace, Edge* edge,
                 const Edge* cause = nullptr) {
  ASSERT_TRUE(trace->RecordEdgeEvent("eligible", edge, cause));
  ASSERT_TRUE(trace->RecordEdgeEvent("ready", edge, ReadyFields(edge), cause));
  ASSERT_TRUE(trace->RecordEdgeEvent("selected", edge));
  ASSERT_TRUE(trace->RecordEdgeEvent("started", edge));
}

void RecordCompletion(SchedulerTrace* trace, Edge* edge, int status) {
  SchedulerTraceFields outcome;
  outcome.push_back(make_pair("status", to_string(status)));
  outcome.push_back(make_pair("source", "runner"));
  ASSERT_TRUE(trace->RecordEdgeEvent("outcome", edge, outcome));
  SchedulerTraceFields completion;
  completion.push_back(
      make_pair("result", status == 0 ? "success" : "failure"));
  ASSERT_TRUE(trace->RecordEdgeEvent("completed", edge, completion));
}

struct SchedulerTraceTest : StateTestWithBuiltinRules {
  void SetUp() override {
    StateTestWithBuiltinRules::SetUp();
    ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
                                        "pool serial\n"
                                        "  depth = 1\n"
                                        "build a: cat input\n"
                                        "build b: cat input\n"
                                        "build out: cat a b\n"
                                        "build unrelated: cat input\n"));
  }

  Edge* edge(const string& output) {
    return state_.LookupNode(output)->in_edge();
  }

  unique_ptr<SchedulerTrace> Open(const string& path, int parallelism = 2) {
    string err;
    unique_ptr<SchedulerTrace> trace = SchedulerTrace::Open(
        path, SchedulerTrace::GraphDigest(state_), parallelism, 1, 0,
        SchedulerTrace::kDefaultMaxEvents, SchedulerTrace::kDefaultMaxBytes,
        &err);
    EXPECT_EQ("", err);
    return trace;
  }
};

TEST_F(SchedulerTraceTest, StableIdentityAndGraphDigestIgnoreParseOrder) {
  State other;
  AddCatRule(&other);
  ASSERT_NO_FATAL_FAILURE(AssertParse(&other,
                                      "build unrelated: cat input\n"
                                      "build out: cat a b\n"
                                      "build b: cat input\n"
                                      "build a: cat input\n"
                                      "pool serial\n"
                                      "  depth = 1\n"));

  EXPECT_EQ(SchedulerTrace::EdgeId(edge("a")),
            SchedulerTrace::EdgeId(other.LookupNode("a")->in_edge()));
  EXPECT_EQ("ef7d3c6f1b072c74e", SchedulerTrace::EdgeId(edge("a")));
  EXPECT_EQ(SchedulerTrace::GraphDigest(state_),
            SchedulerTrace::GraphDigest(other));

  State changed_input_kind;
  AddCatRule(&changed_input_kind);
  ASSERT_NO_FATAL_FAILURE(AssertParse(&changed_input_kind,
                                      "pool serial\n"
                                      "  depth = 1\n"
                                      "build a: cat input\n"
                                      "build b: cat input\n"
                                      "build out: cat a | b\n"
                                      "build unrelated: cat input\n"));
  EXPECT_NE(SchedulerTrace::GraphDigest(state_),
            SchedulerTrace::GraphDigest(changed_input_kind));
}

TEST_F(SchedulerTraceTest, ReplayAllowsIndependentCompletionOrders) {
  ScopedFilePath first_path("SchedulerTraceTest-causal-first.trace");
  ScopedFilePath second_path("SchedulerTraceTest-causal-second.trace");
  Edge* a = edge("a");
  Edge* b = edge("b");
  Edge* out = edge("out");

  for (int order = 0; order < 2; ++order) {
    const string path = order == 0 ? first_path.path() : second_path.path();
    unique_ptr<SchedulerTrace> trace = Open(path);
    ASSERT_TRUE(trace);
    RecordPlan(trace.get(), a);
    RecordPlan(trace.get(), b);
    RecordPlan(trace.get(), out);
    RecordDependency(trace.get(), out, a);
    RecordDependency(trace.get(), out, b);
    RecordStart(trace.get(), a);
    RecordStart(trace.get(), b);
    RecordCompletion(trace.get(), order == 0 ? a : b, 0);
    RecordCompletion(trace.get(), order == 0 ? b : a, 0);
    RecordStart(trace.get(), out, order == 0 ? b : a);
    RecordCompletion(trace.get(), out, 0);
    string err;
    ASSERT_TRUE(trace->Finish("success", &err)) << err;
    EXPECT_TRUE(SchedulerTrace::Replay(path, state_,
                                       SchedulerTrace::GraphDigest(state_),
                                       SchedulerTrace::kDefaultMaxEvents,
                                       SchedulerTrace::kDefaultMaxBytes, &err))
        << err;
  }
}

TEST_F(SchedulerTraceTest, ReplayRejectsIllegalPoolReservation) {
  ScopedFilePath path("SchedulerTraceTest-pool.trace");
  Edge* a = edge("a");
  Edge* b = edge("b");
  a->pool_ = state_.LookupPool("serial");
  b->pool_ = state_.LookupPool("serial");

  unique_ptr<SchedulerTrace> trace = Open(path.path());
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), b);
  RecordStart(trace.get(), a);
  ASSERT_TRUE(trace->RecordEdgeEvent("eligible", b));
  ASSERT_TRUE(trace->RecordEdgeEvent("ready", b, ReadyFields(b)));
  string err;
  ASSERT_TRUE(trace->Finish("failure", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("pool serial had no capacity")) << err;
}

TEST_F(SchedulerTraceTest, ReplayRejectsDynamicDependencyUsedTooEarly) {
  ScopedFilePath path("SchedulerTraceTest-dynamic.trace");
  Edge* a = edge("a");
  Edge* out = edge("out");
  unique_ptr<SchedulerTrace> trace = Open(path.path());
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), out);
  RecordStart(trace.get(), out);
  SchedulerTraceFields dynamic;
  dynamic.push_back(make_pair("path", "a"));
  dynamic.push_back(make_pair("dependency", SchedulerTrace::EdgeId(a)));
  ASSERT_TRUE(trace->RecordEdgeEvent("dynamic_input", out, dynamic, a));
  string err;
  ASSERT_TRUE(trace->Finish("failure", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("before its dynamic graph information"))
      << err;
}

TEST_F(SchedulerTraceTest, ReplayDerivesStaticDependenciesFromGraph) {
  ScopedFilePath missing_path("SchedulerTraceTest-missing-dependency.trace");
  Edge* a = edge("a");
  Edge* out = edge("out");
  unique_ptr<SchedulerTrace> trace = Open(missing_path.path());
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), out);
  RecordStart(trace.get(), out);
  string err;
  ASSERT_TRUE(trace->Finish("failure", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(missing_path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("prerequisite")) << err;

  ScopedFilePath fabricated_path(
      "SchedulerTraceTest-fabricated-dependency.trace");
  Edge* unrelated = edge("unrelated");
  trace = Open(fabricated_path.path());
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), unrelated);
  RecordDependency(trace.get(), a, unrelated);
  ASSERT_TRUE(trace->Finish("failure", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(fabricated_path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("not an input")) << err;
}

TEST_F(SchedulerTraceTest, ReplayAcceptsRestatSuppression) {
  ScopedFilePath path("SchedulerTraceTest-restat.trace");
  Edge* a = edge("a");
  Edge* out = edge("out");
  unique_ptr<SchedulerTrace> trace = Open(path.path(), 1);
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), out);
  RecordDependency(trace.get(), out, a);
  RecordStart(trace.get(), a);
  SchedulerTraceFields outcome;
  outcome.push_back(make_pair("status", "0"));
  outcome.push_back(make_pair("source", "runner"));
  ASSERT_TRUE(trace->RecordEdgeEvent("outcome", a, outcome));
  SchedulerTraceFields restat;
  restat.push_back(make_pair("output", "a"));
  restat.push_back(make_pair("changed", "0"));
  restat.push_back(make_pair("mode", "restat"));
  ASSERT_TRUE(trace->RecordEdgeEvent("restat", a, restat));
  SchedulerTraceFields suppressed;
  suppressed.push_back(make_pair("reason", "restat"));
  ASSERT_TRUE(trace->RecordEdgeEvent("suppressed", out, suppressed, a));
  SchedulerTraceFields completed;
  completed.push_back(make_pair("result", "success"));
  ASSERT_TRUE(trace->RecordEdgeEvent("completed", a, completed));
  string err;
  ASSERT_TRUE(trace->Finish("success", &err));
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     SchedulerTrace::GraphDigest(state_),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceTest, VersionOneCompatibility) {
  ScopedFilePath path("SchedulerTraceTest-v1.trace");
  Edge* a = edge("a");
  const string id = SchedulerTrace::EdgeId(a);
  const string label = SchedulerTrace::EdgeLabel(a);
  string contents =
      "ninja_scheduler_trace\tversion=1\trequires=global-v1\tgraph=" +
      SchedulerTrace::GraphDigest(state_) + "\n" +
      "event\tseq=1\ttype=eligible\tedge=" + id + "\tlabel=" + label + "\n" +
      "event\tseq=2\ttype=selected\tedge=" + id + "\tlabel=" + label + "\n" +
      "event\tseq=3\ttype=started\tedge=" + id + "\tlabel=" + label + "\n" +
      "event\tseq=4\ttype=outcome\tedge=" + id + "\tlabel=" + label +
      "\tstatus=0\tsource=runner\n" +
      "event\tseq=5\ttype=completed\tedge=" + id + "\tlabel=" + label +
      "\tresult=success\nend\tstatus=success\tevents=5\n";
  ASSERT_TRUE(WriteTestFile(path.path(), contents));
  string err;
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     SchedulerTrace::GraphDigest(state_),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceTest, MalformedTruncatedAndOptionalData) {
  ScopedFilePath truncated_path("SchedulerTraceTest-truncated.trace");
  ScopedFilePath optional_path("SchedulerTraceTest-optional.trace");
  ScopedFilePath optional_lane_path("SchedulerTraceTest-optional-lane.trace");
  ScopedFilePath malformed_path("SchedulerTraceTest-malformed.trace");
  string header =
      "ninja_scheduler_trace\tversion=2\trequires=causal-v2"
      "\tgraph=" +
      SchedulerTrace::GraphDigest(state_) + "\n";
  ASSERT_TRUE(WriteTestFile(truncated_path.path(), header));
  string err;
  EXPECT_FALSE(SchedulerTrace::Replay(truncated_path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("truncated")) << err;

  unique_ptr<SchedulerTrace> trace = Open(optional_path.path());
  ASSERT_TRUE(trace);
  SchedulerTraceFields fields;
  fields.push_back(make_pair("optional", "1"));
  fields.push_back(make_pair("future_field", "ignored"));
  ASSERT_TRUE(trace->RecordEvent("future_annotation", fields));
  ASSERT_TRUE(trace->Finish("success", &err));
  EXPECT_TRUE(SchedulerTrace::Replay(optional_path.path(), state_,
                                     SchedulerTrace::GraphDigest(state_),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;

  trace = Open(optional_lane_path.path());
  ASSERT_TRUE(trace);
  Edge* a = edge("a");
  RecordPlan(trace.get(), a);
  fields.clear();
  fields.push_back(make_pair("optional", "1"));
  ASSERT_TRUE(trace->RecordEdgeEvent("future_edge_state", a, fields));
  RecordStart(trace.get(), a);
  RecordCompletion(trace.get(), a, 0);
  ASSERT_TRUE(trace->Finish("success", &err));
  EXPECT_TRUE(SchedulerTrace::Replay(optional_lane_path.path(), state_,
                                     SchedulerTrace::GraphDigest(state_),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;

  string malformed = header +
                     "event\tseq=1\ttype=manifest_state\ttype=plan\n"
                     "end\tstatus=success\tevents=1\n";
  ASSERT_TRUE(WriteTestFile(malformed_path.path(), malformed));
  EXPECT_FALSE(SchedulerTrace::Replay(malformed_path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("duplicate")) << err;
}

TEST_F(SchedulerTraceTest, HeaderRecordsManifestGeneration) {
  ScopedFilePath path("SchedulerTraceTest-generation.trace");
  string err;
  unique_ptr<SchedulerTrace> trace =
      SchedulerTrace::Open(path.path(), SchedulerTrace::GraphDigest(state_), 2,
                           1, 7, SchedulerTrace::kDefaultMaxEvents,
                           SchedulerTrace::kDefaultMaxBytes, &err);
  ASSERT_TRUE(trace) << err;
  ASSERT_TRUE(trace->Finish("success", &err)) << err;
  EXPECT_NE(string::npos,
            ReadTestFile(path.path()).find("manifest_generation=7"));
}

TEST_F(SchedulerTraceTest, GraphMismatchHasCompatibilityDiagnostic) {
  ScopedFilePath path("SchedulerTraceTest-mismatch.trace");
  unique_ptr<SchedulerTrace> trace = Open(path.path());
  ASSERT_TRUE(trace);
  string err;
  ASSERT_TRUE(trace->Finish("success", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(path.path(), state_, "different-graph",
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("incompatible with current build graph"))
      << err;
}

TEST_F(SchedulerTraceTest, ReducedTraceKeepsPrerequisitesNotUnrelatedWork) {
  ScopedFilePath input_path("SchedulerTraceTest-reduce-input.trace");
  ScopedFilePath output_path("SchedulerTraceTest-reduce-output.trace");
  Edge* a = edge("a");
  Edge* b = edge("b");
  Edge* out = edge("out");
  Edge* unrelated = edge("unrelated");
  unique_ptr<SchedulerTrace> trace = Open(input_path.path(), 3);
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), b);
  RecordPlan(trace.get(), out);
  RecordPlan(trace.get(), unrelated);
  RecordDependency(trace.get(), out, a);
  RecordDependency(trace.get(), out, b);
  RecordStart(trace.get(), unrelated);
  RecordCompletion(trace.get(), unrelated, 0);
  RecordStart(trace.get(), a);
  RecordStart(trace.get(), b);
  RecordCompletion(trace.get(), a, 0);
  RecordCompletion(trace.get(), b, 0);
  RecordStart(trace.get(), out, b);
  RecordCompletion(trace.get(), out, 9);
  string err;
  ASSERT_TRUE(trace->Finish("failure", &err));

  ASSERT_TRUE(SchedulerTrace::Reduce(
      input_path.path(), SchedulerTrace::EdgeId(out), output_path.path(),
      SchedulerTrace::kDefaultMaxEvents, SchedulerTrace::kDefaultMaxBytes,
      &err))
      << err;
  EXPECT_TRUE(SchedulerTrace::Replay(output_path.path(), state_,
                                     SchedulerTrace::GraphDigest(state_),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
  string reduced = ReadTestFile(output_path.path());
  EXPECT_NE(string::npos, reduced.find(SchedulerTrace::EdgeId(a)));
  EXPECT_NE(string::npos, reduced.find(SchedulerTrace::EdgeId(b)));
  EXPECT_EQ(string::npos, reduced.find(SchedulerTrace::EdgeId(unrelated)));
}

TEST_F(SchedulerTraceTest, ReaderEnforcesConfiguredLimits) {
  ScopedFilePath path("SchedulerTraceTest-limit.trace");
  unique_ptr<SchedulerTrace> trace = Open(path.path());
  ASSERT_TRUE(trace);
  SchedulerTraceFields optional;
  optional.push_back(make_pair("optional", "1"));
  ASSERT_TRUE(trace->RecordEvent("annotation", optional));
  string err;
  ASSERT_TRUE(trace->Finish("success", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_), 0,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("event limit")) << err;
}

TEST_F(SchedulerTraceTest, RecorderReportsOpenAndEventLimitErrors) {
  ScopedFilePath path("SchedulerTraceTest-writer-limit.trace");
  string err;
  unique_ptr<SchedulerTrace> missing =
      SchedulerTrace::Open("SchedulerTraceTest-missing-dir/trace",
                           SchedulerTrace::GraphDigest(state_), 1, 1, 0,
                           SchedulerTrace::kDefaultMaxEvents,
                           SchedulerTrace::kDefaultMaxBytes, &err);
  EXPECT_FALSE(missing);
  EXPECT_NE(string::npos, err.find("opening scheduler trace")) << err;

  unique_ptr<SchedulerTrace> trace =
      SchedulerTrace::Open(path.path(), SchedulerTrace::GraphDigest(state_), 1,
                           1, 0, 0, SchedulerTrace::kDefaultMaxBytes, &err);
  ASSERT_TRUE(trace) << err;
  EXPECT_FALSE(trace->RecordEvent("annotation", SchedulerTraceFields()));
  EXPECT_NE(string::npos, trace->error().find("event limit"));
  EXPECT_FALSE(trace->Finish("success", &err));
  EXPECT_FALSE(SchedulerTrace::Replay(path.path(), state_,
                                      SchedulerTrace::GraphDigest(state_),
                                      SchedulerTrace::kDefaultMaxEvents,
                                      SchedulerTrace::kDefaultMaxBytes, &err));
  EXPECT_NE(string::npos, err.find("incomplete")) << err;
}

TEST_F(SchedulerTraceTest, ReducedTraceRetainsOverlappingPoolContender) {
  ScopedFilePath input_path("SchedulerTraceTest-pool-reduce-input.trace");
  ScopedFilePath output_path("SchedulerTraceTest-pool-reduce-output.trace");
  Edge* a = edge("a");
  Edge* b = edge("b");
  a->pool_ = state_.LookupPool("serial");
  b->pool_ = state_.LookupPool("serial");
  unique_ptr<SchedulerTrace> trace = Open(input_path.path(), 1);
  ASSERT_TRUE(trace);
  RecordPlan(trace.get(), a);
  RecordPlan(trace.get(), b);
  RecordStart(trace.get(), a);
  ASSERT_TRUE(trace->RecordEdgeEvent("eligible", b));
  SchedulerTraceFields delayed;
  delayed.push_back(make_pair("reason", "pool_capacity"));
  delayed.push_back(make_pair("pool", "serial"));
  delayed.push_back(make_pair("pool_depth", "1"));
  delayed.push_back(make_pair("pool_use", "1"));
  ASSERT_TRUE(trace->RecordEdgeEvent("delayed", b, delayed));
  RecordCompletion(trace.get(), a, 0);
  ASSERT_TRUE(trace->RecordEdgeEvent("ready", b, ReadyFields(b), a));
  ASSERT_TRUE(trace->RecordEdgeEvent("selected", b));
  ASSERT_TRUE(trace->RecordEdgeEvent("started", b));
  RecordCompletion(trace.get(), b, 8);
  string err;
  ASSERT_TRUE(trace->Finish("failure", &err));

  ASSERT_TRUE(SchedulerTrace::Reduce(
      input_path.path(), SchedulerTrace::EdgeId(b), output_path.path(),
      SchedulerTrace::kDefaultMaxEvents, SchedulerTrace::kDefaultMaxBytes,
      &err))
      << err;
  string reduced = ReadTestFile(output_path.path());
  EXPECT_NE(string::npos, reduced.find(SchedulerTrace::EdgeId(a)));
  EXPECT_TRUE(SchedulerTrace::Replay(output_path.path(), state_,
                                     SchedulerTrace::GraphDigest(state_),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

struct ImmediateCommandRunner : CommandRunner {
  explicit ImmediateCommandRunner(VirtualFileSystem* fs) : fs_(fs) {}

  size_t CanRunMore() const override {
    return active_.size() < max_active_ ? max_active_ - active_.size() : 0;
  }

  bool StartCommand(Edge* edge) override {
    started_.push_back(SchedulerTrace::EdgeId(edge));
    for (Node* output : edge->outputs_) {
      if (unchanged_outputs_.count(output->path()))
        continue;
      map<string, string>::const_iterator content =
          contents_.find(output->path());
      fs_->Create(output->path(),
                  content == contents_.end() ? string() : content->second);
    }
    active_.push_back(edge);
    return true;
  }

  BuildResult WaitForCommand() override {
    if (active_.empty())
      return BuildResult::Finished{};
    Edge* edge = active_.front();
    active_.erase(active_.begin());
    ExitStatus status = ExitSuccess;
    return BuildResult::CommandCompleted(edge, status);
  }

  vector<Edge*> GetActiveEdges() override { return active_; }
  void Abort() override { active_.clear(); }

  size_t max_active_ = 2;
  VirtualFileSystem* fs_;
  vector<Edge*> active_;
  vector<string> started_;
  map<string, string> contents_;
  set<string> unchanged_outputs_;
};

struct SchedulerTraceBuildTest : StateTestWithBuiltinRules {
  SchedulerTraceBuildTest() : runner_(&fs_) {}

  void SetUp() override {
    StateTestWithBuiltinRules::SetUp();
    ASSERT_NO_FATAL_FAILURE(
        AssertParse(&state_,
                    "pool serial\n"
                    "  depth = 1\n"
                    "build first: cat input\n"
                    "build failed: cat input\n"
                    "build downstream: cat failed\n"
                    "build pooled_first: cat input\n"
                    "  pool = serial\n"
                    "build pooled_failed: cat input\n"
                    "  pool = serial\n"
                    "build discovery.dd: cat input\n"
                    "build dynamic_input: cat input\n"
                    "build dynamic_out: cat input || discovery.dd\n"
                    "  dyndep = discovery.dd\n"
                    "build preloaded.dd: phony\n"
                    "build pre_dynamic_input: cat input\n"
                    "build pre_dynamic_out: cat input || preloaded.dd\n"
                    "  dyndep = preloaded.dd\n"
                    "build restat_source: cat input\n"
                    "  restat = 1\n"
                    "build restat_downstream: cat restat_source\n"));
    fs_.Create("input", "");
    config_.verbosity = BuildConfig::QUIET;
    config_.parallelism = 2;
    status_.reset(new StatusPrinter(config_));
    builder_.reset(new Builder(&state_, config_, nullptr, nullptr, &fs_,
                               status_.get(), 0));
    builder_->command_runner_.reset(&runner_);
  }

  void TearDown() override {
    if (builder_)
      builder_->command_runner_.release();
    builder_.reset();
    status_.reset();
    StateTestWithBuiltinRules::TearDown();
  }

  BuildConfig config_;
  VirtualFileSystem fs_;
  ImmediateCommandRunner runner_;
  unique_ptr<StatusPrinter> status_;
  unique_ptr<Builder> builder_;
};

TEST_F(SchedulerTraceBuildTest, DisabledPathPerformsNoTraceOperations) {
  SchedulerTrace::ResetOperationCounts();
  string err;
  ASSERT_TRUE(builder_->AddTarget("downstream", &err)) << err;
  ASSERT_EQ(ExitSuccess, builder_->Build(&err)) << err;
  SchedulerTraceOperationCounts counts = SchedulerTrace::GetOperationCounts();
  EXPECT_EQ(0u, counts.files_opened);
  EXPECT_EQ(0u, counts.events_serialized);
  EXPECT_EQ(0u, counts.bytes_written);
}

TEST_F(SchedulerTraceBuildTest,
       InjectedParallelFailurePreventsDownstreamAndReplays) {
  ScopedFilePath path("SchedulerTraceBuildTest-injected.trace");
  const string failed_id =
      SchedulerTrace::EdgeId(state_.LookupNode("failed")->in_edge());
  const string downstream_id =
      SchedulerTrace::EdgeId(state_.LookupNode("downstream")->in_edge());
  config_.scheduler_trace_path = path.c_str();
  config_.scheduler_failure_edge = failed_id;
  config_.scheduler_failure_status = 17;
  config_.scheduler_graph_digest = SchedulerTrace::GraphDigest(state_);

  string err;
  ASSERT_TRUE(builder_->AddTarget("first", &err)) << err;
  ASSERT_TRUE(builder_->AddTarget("downstream", &err)) << err;
  EXPECT_EQ(static_cast<ExitStatus>(17), builder_->Build(&err));
  EXPECT_NE(
      runner_.started_.end(),
      find(runner_.started_.begin(), runner_.started_.end(),
           SchedulerTrace::EdgeId(state_.LookupNode("first")->in_edge())));
  EXPECT_EQ(runner_.started_.end(),
            find(runner_.started_.begin(), runner_.started_.end(), failed_id));
  EXPECT_EQ(
      runner_.started_.end(),
      find(runner_.started_.begin(), runner_.started_.end(), downstream_id));
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     config_.scheduler_graph_digest,
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceBuildTest, InjectedFailureBehindPoolRecordsContention) {
  ScopedFilePath path("SchedulerTraceBuildTest-pooled.trace");
  const string failed_id =
      SchedulerTrace::EdgeId(state_.LookupNode("pooled_failed")->in_edge());
  config_.scheduler_trace_path = path.c_str();
  config_.scheduler_failure_edge = failed_id;
  config_.scheduler_failure_status = 23;
  config_.scheduler_graph_digest = SchedulerTrace::GraphDigest(state_);

  string err;
  ASSERT_TRUE(builder_->AddTarget("pooled_first", &err)) << err;
  ASSERT_TRUE(builder_->AddTarget("pooled_failed", &err)) << err;
  EXPECT_EQ(static_cast<ExitStatus>(23), builder_->Build(&err));
  string contents = ReadTestFile(path.path());
  EXPECT_NE(string::npos, contents.find("type=delayed\tedge=" + failed_id))
      << contents;
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     config_.scheduler_graph_digest,
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceBuildTest, InjectedStatus130IsARecordedCommandFailure) {
  ScopedFilePath path("SchedulerTraceBuildTest-injected-130.trace");
  ScopedFilePath reduced_path(
      "SchedulerTraceBuildTest-injected-130-reduced.trace");
  const string failed_id =
      SchedulerTrace::EdgeId(state_.LookupNode("failed")->in_edge());
  config_.scheduler_trace_path = path.c_str();
  config_.scheduler_failure_edge = failed_id;
  config_.scheduler_failure_status = static_cast<int>(ExitInterrupted);
  config_.scheduler_graph_digest = SchedulerTrace::GraphDigest(state_);

  string err;
  ASSERT_TRUE(builder_->AddTarget("downstream", &err)) << err;
  EXPECT_EQ(ExitInterrupted, builder_->Build(&err));
  string contents = ReadTestFile(path.path());
  EXPECT_NE(string::npos, contents.find("type=outcome")) << contents;
  EXPECT_NE(string::npos, contents.find("result=failure")) << contents;
  ASSERT_TRUE(SchedulerTrace::Reduce(path.path(), failed_id,
                                     reduced_path.path(),
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
  EXPECT_TRUE(SchedulerTrace::Replay(reduced_path.path(), state_,
                                     config_.scheduler_graph_digest,
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceBuildTest, RecordsAndReplaysDyndepAtDiscoveryPoint) {
  ScopedFilePath path("SchedulerTraceBuildTest-dyndep.trace");
  runner_.contents_["discovery.dd"] =
      "ninja_dyndep_version = 1\n"
      "build dynamic_out: dyndep | dynamic_input\n";
  config_.scheduler_trace_path = path.c_str();
  config_.scheduler_graph_digest = SchedulerTrace::GraphDigest(state_);

  string err;
  ASSERT_TRUE(builder_->AddTarget("dynamic_out", &err)) << err;
  ASSERT_EQ(ExitSuccess, builder_->Build(&err)) << err;
  string contents = ReadTestFile(path.path());
  EXPECT_NE(string::npos, contents.find("type=dynamic_input")) << contents;
  EXPECT_NE(string::npos, contents.find("path=dynamic_input")) << contents;
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     config_.scheduler_graph_digest,
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceBuildTest, RecordsDyndepLoadedDuringTargetScan) {
  ScopedFilePath path("SchedulerTraceBuildTest-preloaded-dyndep.trace");
  fs_.Create("preloaded.dd",
             "ninja_dyndep_version = 1\n"
             "build pre_dynamic_out: dyndep | pre_dynamic_input\n");
  config_.scheduler_trace_path = path.c_str();
  config_.scheduler_graph_digest = SchedulerTrace::GraphDigest(state_);

  string err;
  ASSERT_TRUE(builder_->StartSchedulerTrace(&err)) << err;
  ASSERT_TRUE(builder_->AddTarget("pre_dynamic_out", &err)) << err;
  ASSERT_EQ(ExitSuccess, builder_->Build(&err)) << err;
  string contents = ReadTestFile(path.path());
  EXPECT_NE(string::npos, contents.find("type=dynamic_input")) << contents;
  EXPECT_NE(string::npos, contents.find("visibility=initial_scan")) << contents;
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     config_.scheduler_graph_digest,
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

TEST_F(SchedulerTraceBuildTest, RecordsAndReplaysActualRestatSuppression) {
  ScopedFilePath path("SchedulerTraceBuildTest-restat.trace");
  fs_.Create("restat_source", "unchanged");
  fs_.Create("restat_downstream", "already current");
  fs_.Tick();
  fs_.Create("input", "newer input");
  runner_.unchanged_outputs_.insert("restat_source");
  config_.scheduler_trace_path = path.c_str();
  config_.scheduler_graph_digest = SchedulerTrace::GraphDigest(state_);

  string err;
  ASSERT_TRUE(builder_->AddTarget("restat_downstream", &err)) << err;
  ASSERT_EQ(ExitSuccess, builder_->Build(&err)) << err;
  string contents = ReadTestFile(path.path());
  EXPECT_NE(string::npos, contents.find("type=restat")) << contents;
  EXPECT_NE(string::npos, contents.find("type=suppressed")) << contents;
  EXPECT_EQ(runner_.started_.end(),
            find(runner_.started_.begin(), runner_.started_.end(),
                 SchedulerTrace::EdgeId(
                     state_.LookupNode("restat_downstream")->in_edge())));
  EXPECT_TRUE(SchedulerTrace::Replay(path.path(), state_,
                                     config_.scheduler_graph_digest,
                                     SchedulerTrace::kDefaultMaxEvents,
                                     SchedulerTrace::kDefaultMaxBytes, &err))
      << err;
}

}  // namespace
