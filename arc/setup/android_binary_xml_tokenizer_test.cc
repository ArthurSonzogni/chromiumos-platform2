// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/android_binary_xml_tokenizer.h"

#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace arc {

namespace {

using Token = AndroidBinaryXmlTokenizer::Token;
using Type = AndroidBinaryXmlTokenizer::Type;

class AndroidBinaryXmlTokenizerTest : public testing::Test {
 public:
  AndroidBinaryXmlTokenizerTest() = default;
  AndroidBinaryXmlTokenizerTest(const AndroidBinaryXmlTokenizerTest&) = delete;
  AndroidBinaryXmlTokenizerTest& operator=(
      const AndroidBinaryXmlTokenizerTest&) = delete;
  ~AndroidBinaryXmlTokenizerTest() override = default;

  // testing::Test:
  void SetUp() override {
    // Create the test file.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_file_path_ = temp_dir_.GetPath().AppendASCII("test.xml");
    file_.Initialize(test_file_path_,
                     base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(file_.IsValid());

    // Write the magic number to the test file.
    ASSERT_TRUE(WriteData(AndroidBinaryXmlTokenizer::kMagicNumber,
                          sizeof(AndroidBinaryXmlTokenizer::kMagicNumber)));
  }

  // Writes the specified data to the test file.
  bool WriteData(const void* buf, size_t size) {
    return file_.WriteAtCurrentPos(static_cast<const char*>(buf), size) == size;
  }

  // Writes a token byte to the test file.
  bool WriteToken(Token token, Type type) {
    const char buf = static_cast<int>(token) | (static_cast<int>(type) << 4);
    return WriteData(&buf, sizeof(buf));
  }

 protected:
  base::ScopedTempDir temp_dir_;

  // Test file.
  base::FilePath test_file_path_;
  base::File file_;
};

TEST_F(AndroidBinaryXmlTokenizerTest, Empty) {
  AndroidBinaryXmlTokenizer tokenizer;
  ASSERT_TRUE(tokenizer.Init(test_file_path_));

  EXPECT_FALSE(tokenizer.is_eof());
  EXPECT_FALSE(tokenizer.Next());
  EXPECT_TRUE(tokenizer.is_eof());
}

TEST_F(AndroidBinaryXmlTokenizerTest, StartAndEndDocument) {
  // Android's serializer usually puts these tokens at the beginning and the end
  // of an Android binary XML file.
  ASSERT_TRUE(WriteToken(Token::kStartDocument, Type::kNull));
  ASSERT_TRUE(WriteToken(Token::kEndDocument, Type::kNull));

  AndroidBinaryXmlTokenizer tokenizer;
  ASSERT_TRUE(tokenizer.Init(test_file_path_));

  ASSERT_TRUE(tokenizer.Next());
  EXPECT_EQ(tokenizer.token(), Token::kStartDocument);
  ASSERT_TRUE(tokenizer.Next());
  EXPECT_EQ(tokenizer.token(), Token::kEndDocument);
  EXPECT_FALSE(tokenizer.Next());
  EXPECT_TRUE(tokenizer.is_eof());
}

}  // namespace

}  // namespace arc
