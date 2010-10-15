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
}

