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

#include "depfile_reader.h"
#include "test.h"
#include <gtest/gtest.h>

struct DepfileReaderTest : public testing::Test {
  void DepfileParser_EQ(DepfileParser& lhs, DepfileParser & rhs);

  VirtualFileSystem fs_;
  string out1, out2, out3, out4;
  DepfileParser sampleOut1, sampleOut2, sampleOut3, sampleOut4, sampleEmpty;

  virtual void SetUp() {
    string err;
    out1 = "out1: in1 in2\n";
    ASSERT_EQ(true, sampleOut1.Parse(&out1, &err));
    ASSERT_EQ("", err);
    
    out2 = "out2: in3 in4\n";
    ASSERT_EQ(true, sampleOut2.Parse(&out2, &err));
    ASSERT_EQ("", err);

    out3 = "out3: in5 in6\n";
    ASSERT_EQ(true, sampleOut3.Parse(&out3, &err));
    ASSERT_EQ("", err);

    out4 = "out4: in7 in8\n";
    ASSERT_EQ(true, sampleOut4.Parse(&out4, &err));
    ASSERT_EQ("", err);

    DepfileReader::cache.clear();
  }
};

void DepfileReaderTest::DepfileParser_EQ(DepfileParser& lhs, DepfileParser & rhs) {
  EXPECT_EQ(lhs.out().AsString(), rhs.out().AsString());
  EXPECT_EQ(lhs.ins().size(), rhs.ins().size());
  for (size_t i = 0; i < lhs.ins().size(); i++) {
    EXPECT_EQ(lhs.ins()[i].AsString(), rhs.ins()[i].AsString());
  }
}

TEST_F(DepfileReaderTest, VanillaDepfile) {
  fs_.Create("VanillaDepfile.d", 1, 
    "out1: \\\n"
    " in1 \\\n"
    " in2\n");

  DepfileReader reader;
  string err;
  EXPECT_TRUE(reader.Read("VanillaDepfile.d", "out1", &fs_, &err));
  EXPECT_EQ("", err);
  DepfileParser_EQ(sampleOut1, *reader.Parser());
}

TEST_F(DepfileReaderTest, OneDepfile) {
  fs_.Create("OneDepfile.D", 1, 
"out1: \\\n"
" in1 \\\n"
" in2\n");
  
  DepfileReader reader;
  string err;
  EXPECT_TRUE(reader.ReadGroup("OneDepfile.D", "out1", &fs_, &err));
  EXPECT_EQ("", err);
  DepfileParser_EQ(sampleOut1, *reader.Parser());
}

TEST_F(DepfileReaderTest, TwoDepfiles) {
  fs_.Create("TwoDepfiles.D", 1, 
"out1: \\\n"
" in1 \\\n"
" in2\n"
"out2:\\\n"
" in3 \\\n"
" in4 \\\n"
"\n");

  string err;
  DepfileReader reader1, reader2, reader3;

  EXPECT_TRUE(reader1.ReadGroup("TwoDepfiles.D", "out1", &fs_, &err));
  EXPECT_EQ("", err);
  DepfileParser_EQ(sampleOut1, *reader1.Parser());

  ASSERT_EQ(1, fs_.files_read_.size()); 
  EXPECT_EQ("TwoDepfiles.D", fs_.files_read_[0]); 

  EXPECT_TRUE(reader2.ReadGroup("TwoDepfiles.D", "out2", &fs_, &err));
  EXPECT_EQ("", err);
  EXPECT_EQ(1, fs_.files_read_.size()); // The .D file was not re-read
  DepfileParser_EQ(sampleOut2, *reader2.Parser());

  // Ask again - get nothing (but not an error)
  EXPECT_TRUE(reader3.ReadGroup("TwoDepfiles.D", "out2", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_FALSE(reader3.Parser());
}

TEST_F(DepfileReaderTest, TwoTimesTwoDepfiles) {
  fs_.Create("TwoDepfiles.D", 1, 
    "out1: in1 in2\n"
    "out2: in3 in4\n"
    "\n");

  fs_.Create("AnotherTwoDepfiles.D", 1, 
    "out3: in5 in6\n"
    "out4: in7  in8 \n");

  string err;
  DepfileReader reader1, reader2, reader3, reader4;

  // No files read so far
  ASSERT_EQ(0, fs_.files_read_.size()); 

  // Read out1, cache out2
  EXPECT_TRUE(reader1.ReadGroup("TwoDepfiles.D", "out1", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(reader1.Parser());
  DepfileParser_EQ(sampleOut1, *reader1.Parser());

  // Now the TwoDepfiles.D was read for the first time
  ASSERT_EQ(1, fs_.files_read_.size()); 
  EXPECT_EQ("TwoDepfiles.D", fs_.files_read_[0]); 

  // Read out4, cache out3
  EXPECT_TRUE(reader4.ReadGroup("AnotherTwoDepfiles.D", "out4", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(reader4.Parser());
  DepfileParser_EQ(sampleOut4, *reader4.Parser());

  // Now the AnotherTwoDepfiles.D was read for the first time
  ASSERT_EQ(2, fs_.files_read_.size()); 
  EXPECT_EQ("AnotherTwoDepfiles.D", fs_.files_read_[1]); 

  // Retrieve the remaining files from cache
  EXPECT_TRUE(reader2.ReadGroup("TwoDepfiles.D", "out2", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(reader2.Parser());
  DepfileParser_EQ(sampleOut2, *reader2.Parser());

  EXPECT_TRUE(reader3.ReadGroup("AnotherTwoDepfiles.D", "out3", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_TRUE(reader3.Parser());
  DepfileParser_EQ(sampleOut3, *reader3.Parser());

  // no new file reads 
  ASSERT_EQ(2, fs_.files_read_.size()); 
}

TEST_F(DepfileReaderTest, NewFileInProject) {
  fs_.Create("TwoDepfiles.D", 1, 
    "out1: in1 in2\n"
    "out2: in3 in4\n"
    "\n");

  string err;
  DepfileReader reader;

  // try to read a new file - no failure expected
  EXPECT_TRUE(reader.ReadGroup("TwoDepfiles.D", "out3", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_FALSE(reader.Parser());
}

TEST_F(DepfileReaderTest, NonExistentFile) {
  string err;
  DepfileReader reader;

  // try to read from a non-existent d file - no failure expected
  EXPECT_TRUE(reader.Read("NonExistent.d", "out", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_FALSE(reader.Parser());

  // try to read from a non-existent D file - no failure expected
  EXPECT_TRUE(reader.ReadGroup("NonExistent.D", "out", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_FALSE(reader.Parser()); 
}

TEST_F(DepfileReaderTest, EmptyFile) {
  string err;
  DepfileReader reader;

  fs_.Create("Empty.D", 1, "");
  fs_.Create("Empty.d", 1, "");

  // try to read from a non-existent d file - no failure expected
  EXPECT_TRUE(reader.Read("Empty.d", "out", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_FALSE(reader.Parser());

  // try to read from a non-existent D file - no failure expected
  EXPECT_TRUE(reader.ReadGroup("Empty.D", "out", &fs_, &err));
  EXPECT_EQ("", err);
  ASSERT_FALSE(reader.Parser());
}

