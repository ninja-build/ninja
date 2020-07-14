// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License")
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

#if NINJA_ENABLE_CL_SOURCE_DEPENDENCIES

#include "clsourcedependencies_parser.h"

#include "test.h"

TEST(ParseCLSourceDependencies, ParseInvalidJSON) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(StringPiece("this is not JSON"), &err,
                                         includes));
  ASSERT_EQ("sourceDependencies parse error", err);
}

TEST(ParseCLSourceDependencies, ParseMissingVersion) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(StringPiece("{}"), &err, includes));
  ASSERT_EQ("sourceDependencies missing version", err);
}

TEST(ParseCLSourceDependencies, ParseVersionWrongType) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(StringPiece("{\"Version\":1.0}"), &err,
                                         includes));
  ASSERT_EQ("sourceDependencies version must be a string", err);
}

TEST(ParseCLSourceDependencies, ParseWrongVersion) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(StringPiece("{\"Version\":\"2.0\"}"),
                                         &err, includes));
  ASSERT_EQ("expected sourceDependencies version 1.x but found 2.0", err);
}

TEST(ParseCLSourceDependencies, ParseMissingData) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(StringPiece("{\"Version\":\"1.0\"}"),
                                         &err, includes));
  ASSERT_EQ("sourceDependencies missing data", err);
}

TEST(ParseCLSourceDependencies, ParseDataWrongType) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(
      StringPiece("{\"Version\":\"1.0\",\"Data\":true}"), &err, includes));
  ASSERT_EQ("sourceDependencies data must be an object", err);
}

TEST(ParseCLSourceDependencies, ParseMissingIncludes) {
  std::string err;
  std::set<std::string> includes;

  ASSERT_FALSE(ParseCLSourceDependencies(
      StringPiece("{\"Version\":\"1.0\",\"Data\":{}}"), &err, includes));
  ASSERT_EQ("sourceDependencies data missing includes", err);
}

TEST(ParseCLSourceDependencies, ParseIncludesWrongType) {
  std::string err;
  std::set<std::string> includes;

  StringPiece content("{\"Version\":\"1.0\",\"Data\":{\"Includes\":{}}}");
  ASSERT_FALSE(ParseCLSourceDependencies(content, &err, includes));
  ASSERT_EQ("sourceDependencies includes must be an array", err);
}

TEST(ParseCLSourceDependencies, ParseBadSingleInclude) {
  std::string err;
  std::set<std::string> includes;

  StringPiece content("{\"Version\":\"1.0\",\"Data\":{\"Includes\":[23]}}");
  ASSERT_FALSE(ParseCLSourceDependencies(content, &err, includes));
  ASSERT_EQ("sourceDependencies include path must be a string", err);
}

TEST(ParseCLSourceDependencies, ParseSimple) {
  std::string err;
  std::set<std::string> includes;

  StringPiece content(
      "{\"Version\":\"1.0\",\"Data\":{\"Includes\":[\"c:\\\\test.cpp\"]}}");
  ASSERT_TRUE(ParseCLSourceDependencies(content, &err, includes));
  ASSERT_EQ(1u, includes.size());
  ASSERT_EQ("c:\\test.cpp", *includes.begin());
}

TEST(ParseCLSourceDependencies, ParseReal) {
  StringPiece content(
      "{    \"Version\": \"1.0\",    \"Data\": {        \"Source\": "
      "\"c:\\\\test.cpp\",        \"Includes\": [            \"c:\\\\program "
      "files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\iostream\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\yvals_core.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vcruntime.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\sal.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\concurrencysal.h\",            \"c:\\\\program "
      "files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vadefs.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xkeycheck.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\istream\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\ostream\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\ios\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xlocnum\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\climits\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\limits.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cmath\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\yvals.h\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\crtdbg.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt.h\",           "
      " \"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vcruntime_new_debug.h\",            \"c:\\\\program "
      "files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vcruntime_new.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\crtdefs.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\use_ansi.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cstdlib\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\math.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_math.h\",      "
      "      \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\stdlib.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_malloc.h\",    "
      "        \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_search.h\",    "
      "        \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\stddef.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wstdlib.h\",   "
      "         \"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xtr1common\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cstdio\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\stdio.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wstdio.h\",    "
      "        \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_stdio_config."
      "h\",            \"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\iterator\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\iosfwd\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cstring\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\string.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_memory.h\",    "
      "        \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_memcpy_s.h\",  "
      "          \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\errno.h\",            "
      "\"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vcruntime_string.h\",            \"c:\\\\program "
      "files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wstring.h\",   "
      "         \"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cwchar\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\wchar.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wconio.h\",    "
      "        \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wctype.h\",    "
      "        \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wdirect.h\",   "
      "         \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wio.h\",       "
      "     \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_share.h\",     "
      "       \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wprocess.h\",  "
      "          \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_wtime.h\",     "
      "       \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\sys\\\\stat.h\",       "
      "     \"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\sys\\\\types.h\",      "
      "      \"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xstddef\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cstddef\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\initializer_list\",            \"c:\\\\program "
      "files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xutility\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\utility\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\type_traits\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\streambuf\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xiosbase\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\share.h\",            "
      "\"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\system_error\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\__msvc_system_error_abi.hpp\",            "
      "\"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cerrno\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\stdexcept\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\exception\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\malloc.h\",            "
      "\"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vcruntime_exception.h\",            \"c:\\\\program "
      "files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\eh.h\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\corecrt_terminate.h\", "
      "           \"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xstring\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xmemory\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cstdint\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\stdint.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\limits\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\cfloat\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\float.h\",            "
      "\"c:\\\\program files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\new\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xatomic.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\intrin0.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xcall_once.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xerrc.h\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\atomic\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xlocale\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\memory\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\typeinfo\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\vcruntime_typeinfo.h\",            \"c:\\\\program "
      "files (x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xfacet\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xlocinfo\",            \"c:\\\\program files "
      "(x86)\\\\microsoft visual "
      "studio\\\\2019\\\\preview\\\\vc\\\\tools\\\\msvc\\\\14.27."
      "29009\\\\include\\\\xlocinfo.h\",            \"c:\\\\program files "
      "(x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\ctype.h\",            "
      "\"c:\\\\program files (x86)\\\\windows "
      "kits\\\\10\\\\include\\\\10.0.18362.0\\\\ucrt\\\\locale.h\",            "
      "\"c:\\\\constants.h\",            \"c:\\\\test.h\"        ],        "
      "\"Modules\": []    }}");

  std::string err;
  std::set<std::string> includes;
  ASSERT_TRUE(ParseCLSourceDependencies(content, &err, includes));

  ASSERT_EQ(2u, includes.size());
  ASSERT_TRUE(includes.find("c:\\constants.h") != includes.end());
  ASSERT_TRUE(includes.find("c:\\test.h") != includes.end());
}

#endif // NINJA_ENABLE_CL_SOURCE_DEPENDENCIES
