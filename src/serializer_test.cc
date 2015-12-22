// Copyright 2016 Google Inc. All Rights Reserved.
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

#include "disk_interface.h"
#include "graph.h"
#include "serializer.h"
#include "state.h"
#include "test.h"

namespace {

void InitState(State* state) {
  Pool* pool = new Pool("pool_name", 42);
  state->AddPool(pool);

  EvalString command;
  command.AddText("cat ");
  command.AddSpecial("in");
  command.AddText(" > ");
  command.AddSpecial("out");

  Rule* rule = new Rule("cat");
  rule->AddBinding("command", command);
  state->bindings_.AddRule(rule);

  Edge* edge = state->AddEdge(rule);
  state->AddIn(edge, "in1", 0);
  state->AddIn(edge, "in2", 0);
  state->AddOut(edge, "out", 0);

  string err;
  EXPECT_TRUE(state->AddDefault("out", &err));

  state->bindings_.AddBinding("foo", "bar");
}

TEST(Serializer, Basic) {
  const string kTestFile = "serializer_test.ninja.bin";
  // Create a state and serialize it.
  {
    State state;
    InitState(&state);

    Serializer serializer(kTestFile.c_str());
    EXPECT_TRUE(serializer.SerializeState(state));
  }

  // Deserialize the state and check it.
  {
    State state;
    Deserializer deserializer(kTestFile.c_str());
    EXPECT_TRUE(deserializer.DeserializeState(&state));

    Pool* pool = state.LookupPool("pool_name");
    ASSERT_NE(NULL, pool);
    EXPECT_EQ(42, pool->depth());

    Node* in1 = state.LookupNode("in1");
    ASSERT_NE(NULL, in1);
    Node* in2 = state.LookupNode("in2");
    ASSERT_NE(NULL, in2);
    Node* out = state.LookupNode("out");
    ASSERT_NE(NULL, out);
    ASSERT_EQ(1, state.edges_.size());
    Edge* edge = state.edges_[0];

    EXPECT_EQ("in1", in1->path());
    EXPECT_EQ("in2", in2->path());
    EXPECT_EQ("out", out->path());

    EXPECT_EQ(NULL, in1->in_edge());
    EXPECT_EQ(NULL, in2->in_edge());
    EXPECT_EQ(edge, out->in_edge());
    ASSERT_EQ(1, in1->out_edges().size());
    EXPECT_EQ(edge, in1->out_edges()[0]);
    ASSERT_EQ(1, in2->out_edges().size());
    EXPECT_EQ(edge, in2->out_edges()[0]);
    EXPECT_EQ(0, out->out_edges().size());

    ASSERT_EQ(2, edge->inputs_.size());
    EXPECT_EQ(in1, edge->inputs_[0]);
    EXPECT_EQ(in2, edge->inputs_[1]);
    ASSERT_EQ(1, edge->outputs_.size());
    EXPECT_EQ(out, edge->outputs_[0]);

    EXPECT_EQ("bar", state.bindings_.LookupVariable("foo"));

    ASSERT_EQ(1, state.defaults_.size());
    ASSERT_EQ(out, state.defaults_[0]);
  }
}

}  // namespace
