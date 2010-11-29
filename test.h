#include <gtest/gtest.h>

struct StateTestWithBuiltinRules : public testing::Test {
  StateTestWithBuiltinRules();
  Node* GetNode(const string& path);

  State state_;
};
