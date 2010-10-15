#include "ninja.h"

#include <gtest/gtest.h>

struct NinjaTest : public testing::Test {
  NinjaTest() {
    rule_cat_ = state_.AddRule("cat", "cat @in > $out");
  }

  Rule* rule_cat_;
  State state_;
};

TEST_F(NinjaTest, Basic) {
  Edge* edge = state_.AddEdge(rule_cat_);
  state_.AddInOut(edge, Edge::IN, "in1");
  state_.AddInOut(edge, Edge::IN, "in2");
  state_.AddInOut(edge, Edge::OUT, "out");
  EXPECT_FALSE(state_.GetNode("in1")->dirty());
  EXPECT_FALSE(state_.GetNode("in2")->dirty());
  EXPECT_FALSE(state_.GetNode("out")->dirty());

  state_.stat_cache()->GetFile("in1")->Touch(1);
  EXPECT_TRUE(state_.GetNode("in1")->dirty());
  EXPECT_FALSE(state_.GetNode("in2")->dirty());
  EXPECT_TRUE(state_.GetNode("out")->dirty());

  Plan plan(&state_);
  plan.AddTarget("out");
  ASSERT_TRUE(plan.FindWork());
  ASSERT_FALSE(plan.FindWork());
}
