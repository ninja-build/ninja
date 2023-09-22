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

#ifndef NINJA_TEST_H_
#define NINJA_TEST_H_

#include <stddef.h>

#include "disk_interface.h"
#include "manifest_parser.h"
#include "state.h"
#include "util.h"

// A tiny testing framework inspired by googletest, but much simpler and
// faster to compile. It supports most things commonly used from googltest. The
// most noticeable things missing: EXPECT_* and ASSERT_* don't support
// streaming notes to them with operator<<, and for failing tests the lhs and
// rhs are not printed. That's so that this header does not have to include
// sstream, which slows down building ninja_test almost 20%.
namespace testing {
class Test {
  bool failed_;
  int assertion_failures_;
 public:
  Test() : failed_(false), assertion_failures_(0) {}
  virtual ~Test() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
  virtual void Run() = 0;

  bool Failed() const { return failed_; }
  int AssertionFailures() const { return assertion_failures_; }
  void AddAssertionFailure() { assertion_failures_++; }
  bool NullFailure(bool expected, const char* file, int line, const char* error,
                   const char* value);
  bool BooleanFailure(bool expected, const char* file, int line,
                      const char* error, const char* value);
  bool BinopFailure(const char* file, int line, const char* error, const char* first_value, const char* second_value);
};

/// std::void_t<T> is only available since C++17, so use
/// ::testing::Void<T>::type for the same thing. Used by AsString<> below.
template <class ...>
struct Void {
  using type = void;
};

/// Expand to a type expression that is only valid if |expression| compiles
/// properly. Only useful for C++ SFINAE.
#define TESTING_ENABLE_IF_EXPRESSION_COMPILES(expression) \
  typename ::testing::Void<decltype(expression)>::type

/// Expand to a type expression that is only valid if type T provides
/// an AsString() method. Note: for simplicity, there is no check that
/// the result is convertible into an std::string here.
#define TESTING_ENABLE_IF_METHOD_AS_STRING_EXISTS_FOR_TYPE(T) \
  TESTING_ENABLE_IF_EXPRESSION_COMPILES(std::declval<T>().AsString())

/// Expand to a type expression that is only valid if type T is supported
/// as an std::to_string() argument type.
#define TESTING_ENABLE_IF_STD_TO_STRING_SUPPORTS_TYPE(T) \
  TESTING_ENABLE_IF_EXPRESSION_COMPILES(std::to_string(std::declval<T>()))

/// RemoveCVRef<T> remove references as well as const and volatile modifier.
/// Used by AsString<> below.
template <typename T>
struct RemoveCVRef {
  using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

/// Expand to the base type of expression value |v|, which means without
/// any reference, const or volatile qualifier. This can be used as a
/// type argument when defining template specializations.
#define TESTING_BASE_TYPE_FOR_VALUE(v) \
  typename ::testing::RemoveCVRef<decltype(v)>::type

/// ::testing::AsString<T> is a helper struct used to convert expression
/// in EXPECT_XXX and ASSERT_XXX macros into the string representation of
/// their value. This uses C++11 template metadata magic which can be easily
/// confusing, so as a short technical note:
///
/// - An AsString<T> value is an object that provides a `c_str()` method that
///   will be used to print it in case of test failure.
///
/// - By default, AsString<T>::c_str() will return `"<UNPRINTABLE>"`. This
///   would happen when T is an std::pair<> or something more complex.
///
/// - If type T has an AsString() method, it will be used automatically
///   to retrieve a printable version of the value. E.g. StringPiece!
///
/// - If type T is supported by std::to_string(), the latter is used
///   automatically as well (so all integral and floating point values).
///
/// - AsString<T*>::c_str() will return an hexadecimal address (just like %p)
///

/// The generic / fallback version.
template <typename T, typename Enable = void>
struct AsString {
  AsString(const T& value) {}
  const char* c_str() const { return "<UNPRINTABLE>"; }
};

/// Print booleans as either "true" or "false".
template <>
struct AsString<bool, void> {
  AsString(bool value) : value_(value) {}
  const char* c_str() const { return value_ ? "true" : "false"; }
  bool value_;
};

/// Print pointers with %p by default. See implementation in ninja_test.cc
template <>
struct AsString<void*, void> {
  AsString(const void* ptr);
  const char* c_str() const { return str_.c_str(); }
  std::string str_;
};

template <typename T>
struct AsString<T*, void> : public AsString<void*, void> {
  AsString(const T* ptr) : AsString<void*, void>(ptr) {}
};

/// Anything supported by std::to_string() is supported implicitly too.
template <typename T>
struct AsString<T, TESTING_ENABLE_IF_STD_TO_STRING_SUPPORTS_TYPE(T)> {
  AsString(const T value) : str_(std::to_string(value)) {}
  const char* c_str() const { return str_.c_str(); }
  std::string str_;
};

/// Anything that has an AsString() method is supported implicitly too.
/// For example, StringPiece!
template <typename T>
struct AsString<T, TESTING_ENABLE_IF_METHOD_AS_STRING_EXISTS_FOR_TYPE(T)> {
  AsString(const T& value) : str_(value.AsString()) {}
  const char* c_str() const { return str_.c_str(); }
  std::string str_;
};

/// Support for std::string, avoids a copy.
template <>
struct AsString<std::string> {
  AsString(const std::string& value) : str_(value) {}
  const char* c_str() const { return str_.c_str(); }
  const std::string& str_;
};

/// Support for literal C strings.
template <size_t N>
struct AsString<char [N], void> {
  AsString(const char* value) : value_(value) {}
  const char* c_str() const { return value_; }
  const char* value_;
};

/// Support for C string pointers.
template <>
struct AsString<const char*> {
  AsString(const char* value) : value_(value) {}
  const char* c_str() const { return value_; }
  const char* value_;
};

template <>
struct AsString<char*> : public AsString<const char*> {
  AsString(const char* value) : AsString<const char*>(value) {}
};

}

void RegisterTest(testing::Test* (*)(), const char*);

extern testing::Test* g_current_test;
#define TEST_F_(x, y, name)                                           \
  struct y : public x {                                               \
    static testing::Test* Create() { return g_current_test = new y; } \
    virtual void Run();                                               \
  };                                                                  \
  struct Register##y {                                                \
    Register##y() { RegisterTest(y::Create, name); }                  \
  };                                                                  \
  Register##y g_register_##y;                                         \
  void y::Run()

#define TEST_F(x, y) TEST_F_(x, x##y, #x "." #y)
#define TEST(x, y) TEST_F_(testing::Test, x##y, #x "." #y)

/// Generic EXPECT macro for binary operations. The use of a lambda here
/// is necessary to ensure that the expressions in (a) and (b) are only
/// evaluated once at runtime in case of failure.
#define EXPECT_BINARY_OP(a, b, op)                                             \
  ([](const decltype(a)& va, const decltype(b)& vb) -> bool {                  \
    return ((va op vb)                                                         \
                ? true                                                         \
                : g_current_test->BinopFailure(                                \
                      __FILE__, __LINE__, #a " " #op " " #b,                   \
                      ::testing::AsString<TESTING_BASE_TYPE_FOR_VALUE(va)>(va) \
                          .c_str(),                                            \
                      ::testing::AsString<TESTING_BASE_TYPE_FOR_VALUE(vb)>(vb) \
                          .c_str()));                                          \
  }(a, b))

#define EXPECT_EQ(a, b) EXPECT_BINARY_OP(a, b, ==)
#define EXPECT_NE(a, b) EXPECT_BINARY_OP(a, b, !=)
#define EXPECT_GT(a, b) EXPECT_BINARY_OP(a, b, >)
#define EXPECT_LT(a, b) EXPECT_BINARY_OP(a, b, <)
#define EXPECT_GE(a, b) EXPECT_BINARY_OP(a, b, >=)
#define EXPECT_LE(a, b) EXPECT_BINARY_OP(a, b, <=)

/// Generic EXPECT macro for boolean comparisons.
#define EXPECT_BOOLEAN_OP(a, expected)                                        \
  ([](const decltype(a)& va) -> bool {                                        \
    return (static_cast<bool>(va) == expected)                                \
               ? true                                                         \
               : g_current_test->BooleanFailure(                              \
                     expected, __FILE__, __LINE__, #a,                        \
                     ::testing::AsString<TESTING_BASE_TYPE_FOR_VALUE(va)>(va) \
                         .c_str());                                           \
  }(a))

#define EXPECT_TRUE(a) EXPECT_BOOLEAN_OP(a, true)
#define EXPECT_FALSE(a) EXPECT_BOOLEAN_OP(a, false)

/// Generic EXPECT macro for pointer nullptr comparisons.
/// Note that boolean comparisons should work too, but the message in case
/// of failure is slightly clearer when using this macro.
#define EXPECT_NULL_OP(a, expected)                                           \
  ([](const decltype(a)& va) -> bool {                                        \
    return (expected == ((va) == nullptr))                                    \
               ? true                                                         \
               : g_current_test->NullFailure(                                 \
                     expected, __FILE__, __LINE__, #a,                        \
                     ::testing::AsString<TESTING_BASE_TYPE_FOR_VALUE(va)>(va) \
                         .c_str());                                           \
  }(a))

#define EXPECT_NULL(a) EXPECT_NULL_OP(a, true)
#define EXPECT_NOT_NULL(a) EXPECT_NULL_OP(a, false)

#define ASSERT_EQ(a, b) \
  if (!EXPECT_EQ(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_NE(a, b) \
  if (!EXPECT_NE(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_GT(a, b) \
  if (!EXPECT_GT(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_LT(a, b) \
  if (!EXPECT_LT(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_GE(a, b) \
  if (!EXPECT_GE(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_LE(a, b) \
  if (!EXPECT_LE(a, b)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_TRUE(a)  \
  if (!EXPECT_TRUE(a))  { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_FALSE(a) \
  if (!EXPECT_FALSE(a)) { g_current_test->AddAssertionFailure(); return; }
#define ASSERT_NULL(a)                     \
  if (!EXPECT_NULL(a)) {                   \
    g_current_test->AddAssertionFailure(); \
    return;                                \
  }
#define ASSERT_NOT_NULL(a)                 \
  if (!EXPECT_NOT_NULL(a)) {               \
    g_current_test->AddAssertionFailure(); \
    return;                                \
  }
#define ASSERT_NO_FATAL_FAILURE(a)                           \
  {                                                          \
    int fail_count = g_current_test->AssertionFailures();    \
    a;                                                       \
    if (fail_count != g_current_test->AssertionFailures()) { \
      g_current_test->AddAssertionFailure();                 \
      return;                                                \
    }                                                        \
  }

// Support utilities for tests.

struct Node;

/// A base test fixture that includes a State object with a
/// builtin "cat" rule.
struct StateTestWithBuiltinRules : public testing::Test {
  StateTestWithBuiltinRules();

  /// Add a "cat" rule to \a state.  Used by some tests; it's
  /// otherwise done by the ctor to state_.
  void AddCatRule(State* state);

  /// Short way to get a Node by its path from state_.
  Node* GetNode(const std::string& path);

  State state_;
};

void AssertParse(State* state, const char* input,
                 ManifestParserOptions = ManifestParserOptions());
void AssertHash(const char* expected, uint64_t actual);
void VerifyGraph(const State& state);

/// An implementation of DiskInterface that uses an in-memory representation
/// of disk state.  It also logs file accesses and directory creations
/// so it can be used by tests to verify disk access patterns.
struct VirtualFileSystem : public DiskInterface {
  VirtualFileSystem() : now_(1) {}

  /// "Create" a file with contents.
  void Create(const std::string& path, const std::string& contents);

  /// Tick "time" forwards; subsequent file operations will be newer than
  /// previous ones.
  int Tick() {
    return ++now_;
  }

  // DiskInterface
  virtual TimeStamp Stat(const std::string& path, std::string* err) const;
  virtual bool WriteFile(const std::string& path, const std::string& contents);
  virtual bool MakeDir(const std::string& path);
  virtual Status ReadFile(const std::string& path, std::string* contents,
                          std::string* err);
  virtual int RemoveFile(const std::string& path);

  /// An entry for a single in-memory file.
  struct Entry {
    int mtime;
    std::string stat_error;  // If mtime is -1.
    std::string contents;
  };

  std::vector<std::string> directories_made_;
  std::vector<std::string> files_read_;
  typedef std::map<std::string, Entry> FileMap;
  FileMap files_;
  std::set<std::string> files_removed_;
  std::set<std::string> files_created_;

  /// A simple fake timestamp for file operations.
  int now_;
};

struct ScopedTempDir {
  /// Create a temporary directory and chdir into it.
  void CreateAndEnter(const std::string& name);

  /// Clean up the temporary directory.
  void Cleanup();

  /// The temp directory containing our dir.
  std::string start_dir_;
  /// The subdirectory name for our dir, or empty if it hasn't been set up.
  std::string temp_dir_name_;
};

#endif // NINJA_TEST_H_
