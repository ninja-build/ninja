#include <gtest/gtest.h>

#include "graph.h"
#include "ninja.h"

struct StateTestWithBuiltinRules : public testing::Test {
  StateTestWithBuiltinRules();
  Node* GetNode(const string& path);

  // Mark every non-leaf node dirty.
  void ResetDirty();

  State state_;
};

void AssertParse(State* state, const char* input);
