// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "state.h"

#include <memory>

#include "graph.h"
#include "test.h"

using namespace std;

namespace {

TEST(State, Basic) {
  State state;

  EvalString command;
  command.AddText("cat ");
  command.AddSpecial("in");
  command.AddText(" > ");
  command.AddSpecial("out");

  Rule* rule = new Rule("cat");
  rule->AddBinding("command", command);
  state.bindings_.AddRule(rule);

  Edge* edge = state.AddEdge(rule);
  state.AddIn(edge, "in1", 0);
  state.AddIn(edge, "in2", 0);
  state.AddOut(edge, "out", 0);

  EXPECT_EQ("cat in1 in2 > out", edge->EvaluateCommand());

  EXPECT_FALSE(state.GetNode("in1", 0)->dirty());
  EXPECT_FALSE(state.GetNode("in2", 0)->dirty());
  EXPECT_FALSE(state.GetNode("out", 0)->dirty());
}

TEST(State, NonDelayingPool) {
  // A depth of 0 means that this pool will never delay edges.
  Pool pool(std::string("test_pool"), 0);
  EXPECT_TRUE(pool.is_valid());
  EXPECT_EQ(0, pool.depth());
  EXPECT_EQ(std::string("test_pool"), pool.name());
  EXPECT_EQ(0, pool.current_use());
  EXPECT_FALSE(pool.ShouldDelayEdge());

  Edge edge1, edge2, edge3;
  edge1.id_ = 1;
  edge2.id_ = 2;
  edge3.id_ = 3;

  pool.EdgeScheduled(edge1);
  EXPECT_EQ(0, pool.current_use());

  pool.EdgeScheduled(edge2);
  EXPECT_EQ(0, pool.current_use());

  pool.EdgeScheduled(edge3);
  EXPECT_EQ(0, pool.current_use());

  pool.EdgeFinished(edge1);
  EXPECT_EQ(0, pool.current_use());

  pool.EdgeFinished(edge2);
  EXPECT_EQ(0, pool.current_use());

  pool.EdgeFinished(edge3);
  EXPECT_EQ(0, pool.current_use());
}

TEST(State, DelayingPool) {
  Pool pool(std::string("delaying_pool"), 2);
  EXPECT_TRUE(pool.is_valid());
  EXPECT_EQ(2, pool.depth());
  EXPECT_EQ(std::string("delaying_pool"), pool.name());
  EXPECT_EQ(0, pool.current_use());
  EXPECT_TRUE(pool.ShouldDelayEdge());

  Edge edge1, edge2, edge3;
  edge1.id_ = 1;
  edge2.id_ = 2;
  edge3.id_ = 3;

  EdgeSet ready_edges;

  pool.DelayEdge(&edge1);
  pool.DelayEdge(&edge2);
  pool.DelayEdge(&edge3);
  EXPECT_EQ(0, pool.current_use());

  // Check that edge1 and edge2 are ready (WeightedEdgeCmp currently
  // ensures that edges with lower ids are scheduled first).
  pool.RetrieveReadyEdges(&ready_edges);
  EXPECT_EQ(2, pool.current_use());
  EXPECT_EQ(2, ready_edges.size());
  EXPECT_NE(ready_edges.find(&edge1), ready_edges.end());
  EXPECT_NE(ready_edges.find(&edge2), ready_edges.end());
  EXPECT_EQ(ready_edges.find(&edge3), ready_edges.end());

  // Finish edge2, verifies that this readies edge3.
  pool.EdgeFinished(edge2);
  EXPECT_EQ(1, pool.current_use());

  ready_edges.clear();
  pool.RetrieveReadyEdges(&ready_edges);
  EXPECT_EQ(2, pool.current_use());
  EXPECT_EQ(1, ready_edges.size());
  EXPECT_NE(ready_edges.find(&edge3), ready_edges.end());

  // Complete edge1 and edge3, verify there is no more work.
  pool.EdgeFinished(edge1);
  pool.EdgeFinished(edge3);
  EXPECT_EQ(0, pool.current_use());

  ready_edges.clear();
  pool.RetrieveReadyEdges(&ready_edges);
  EXPECT_TRUE(ready_edges.empty());
}

TEST(State, DelayingPoolWithDuplicateEdges) {
  Pool pool(std::string("delaying_pool"), 2);
  EXPECT_TRUE(pool.is_valid());
  EXPECT_EQ(2, pool.depth());
  EXPECT_EQ(std::string("delaying_pool"), pool.name());
  EXPECT_EQ(0, pool.current_use());
  EXPECT_TRUE(pool.ShouldDelayEdge());

  Edge edge1, edge2, edge3;
  edge1.id_ = 1;
  edge2.id_ = 2;
  edge3.id_ = 3;

  EdgeSet ready_edges;

  pool.DelayEdge(&edge1);
  pool.DelayEdge(&edge2);
  pool.DelayEdge(&edge3);
  // Insert edge duplicates!
  pool.DelayEdge(&edge1);
  pool.DelayEdge(&edge2);
  pool.DelayEdge(&edge3);
  EXPECT_EQ(0, pool.current_use());

  // Check that edge1 and edge2 are ready (WeightedEdgeCmp currently
  // ensures that edges with lower ids are scheduled first).
  pool.RetrieveReadyEdges(&ready_edges);
  EXPECT_EQ(2, pool.current_use());
  EXPECT_EQ(2, ready_edges.size());
  EXPECT_NE(ready_edges.find(&edge1), ready_edges.end());
  EXPECT_NE(ready_edges.find(&edge2), ready_edges.end());
  EXPECT_EQ(ready_edges.find(&edge3), ready_edges.end());

  // Finish edge2, verifies that this readies edge3.
  pool.EdgeFinished(edge2);
  EXPECT_EQ(1, pool.current_use());

  ready_edges.clear();
  pool.RetrieveReadyEdges(&ready_edges);
  EXPECT_EQ(2, pool.current_use());
  EXPECT_EQ(1, ready_edges.size());
  EXPECT_NE(ready_edges.find(&edge3), ready_edges.end());

  // Complete edge1 and edge3, verify there is no more work.
  pool.EdgeFinished(edge1);
  pool.EdgeFinished(edge3);
  EXPECT_EQ(0, pool.current_use());

  ready_edges.clear();
  pool.RetrieveReadyEdges(&ready_edges);
  EXPECT_TRUE(ready_edges.empty());
}

}  // namespace
