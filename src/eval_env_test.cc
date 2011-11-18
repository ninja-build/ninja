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

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "eval_env.h"

namespace {

struct TestEnv : public Env {
  virtual string LookupVariable(const string& var) {
    return vars[var];
  }
  map<string, string> vars;
};

TEST(EvalString, PlainText) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("plain text", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("plain text", str.Evaluate(NULL));
}

TEST(EvalString, OneVariable) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("hi $var", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("hi $var", str.unparsed());
  TestEnv env;
  EXPECT_EQ("hi ", str.Evaluate(&env));
  env.vars["var"] = "there";
  EXPECT_EQ("hi there", str.Evaluate(&env));
}

TEST(EvalString, OneVariableUpperCase) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("hi $VaR", &err));
  EXPECT_EQ("", err);
  EXPECT_EQ("hi $VaR", str.unparsed());
  TestEnv env;
  EXPECT_EQ("hi ", str.Evaluate(&env));
  env.vars["VaR"] = "there";
  EXPECT_EQ("hi there", str.Evaluate(&env));
}

TEST(EvalString, Error) {
  EvalString str;
  string err;
  size_t err_index;
  EXPECT_FALSE(str.Parse("bad $", &err, &err_index));
  EXPECT_EQ("expected variable after $", err);
  EXPECT_EQ(5u, err_index);
}
TEST(EvalString, CurlyError) {
  EvalString str;
  string err;
  size_t err_index;
  EXPECT_FALSE(str.Parse("bad ${bar", &err, &err_index));
  EXPECT_EQ("expected closing curly after ${", err);
  EXPECT_EQ(9u, err_index);
}

TEST(EvalString, Curlies) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("foo ${var}baz", &err));
  EXPECT_EQ("", err);
  TestEnv env;
  EXPECT_EQ("foo baz", str.Evaluate(&env));
  env.vars["var"] = "barbar";
  EXPECT_EQ("foo barbarbaz", str.Evaluate(&env));
}

TEST(EvalString, Dollars) {
  EvalString str;
  string err;
  EXPECT_TRUE(str.Parse("foo$$bar$bar", &err));
  ASSERT_EQ("", err);
  TestEnv env;
  env.vars["bar"] = "baz";
  EXPECT_EQ("foo$barbaz", str.Evaluate(&env));
}

}  // namespace
