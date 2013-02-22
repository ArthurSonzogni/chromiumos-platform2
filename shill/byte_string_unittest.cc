// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>

#include <gtest/gtest.h>

#include "shill/byte_string.h"

using testing::Test;

namespace shill {

namespace {
const unsigned char kTest1[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
const unsigned char kTest2[] = { 1, 2, 3, 0xa };
const char kTest2HexString[] = "0102030A";
const unsigned int kTest2Uint32 = 0x0102030a;
const unsigned char kTest3[] = { 0, 0, 0, 0 };
const char kTest4[] = "Hello world";
const char kTest5[] = { 0, 1, 2, 3 };
const size_t kExpectedLength = 4;
}  // namespace {}

class ByteStringTest : public Test {
};

TEST_F(ByteStringTest, Empty) {
  uint32 val;

  ByteString bs1(0);
  EXPECT_TRUE(bs1.IsEmpty());
  EXPECT_EQ(0, bs1.GetLength());
  EXPECT_TRUE(bs1.GetData() == NULL);
  EXPECT_FALSE(bs1.ConvertToNetUInt32(&val));
  EXPECT_TRUE(bs1.IsZero());
}

TEST_F(ByteStringTest, NonEmpty) {
  ByteString bs1(kTest1, sizeof(kTest1));
  uint32 val;

  EXPECT_FALSE(bs1.IsEmpty());

  EXPECT_EQ(sizeof(kTest1), bs1.GetLength());
  for (unsigned int i = 0; i < sizeof(kTest1); i++) {
    EXPECT_EQ(bs1.GetData()[i], kTest1[i]);
  }
  EXPECT_TRUE(bs1.GetData() != NULL);
  EXPECT_FALSE(bs1.ConvertToNetUInt32(&val));
  EXPECT_FALSE(bs1.IsZero());

  ByteString bs2(kTest2, sizeof(kTest2));
  EXPECT_EQ(sizeof(kTest2), bs2.GetLength());
  for (unsigned int i = 0; i < sizeof(kTest2); i++) {
    EXPECT_EQ(bs2.GetData()[i], kTest2[i]);
  }
  EXPECT_TRUE(bs2.GetData() != NULL);
  EXPECT_FALSE(bs2.IsZero());

  EXPECT_FALSE(bs2.Equals(bs1));

  ByteString bs3(kTest3, sizeof(kTest3));
  EXPECT_EQ(sizeof(kTest3), bs3.GetLength());
  for (unsigned int i = 0; i < sizeof(kTest3); i++) {
    EXPECT_EQ(bs3.GetData()[i], kTest3[i]);
  }
  EXPECT_TRUE(bs3.GetData() != NULL);
  EXPECT_TRUE(bs3.IsZero());
  EXPECT_FALSE(bs2.Equals(bs1));
  EXPECT_FALSE(bs3.Equals(bs1));

  ByteString bs4(std::string(kTest4), false);
  EXPECT_EQ(strlen(kTest4), bs4.GetLength());
  EXPECT_EQ(0, memcmp(kTest4, bs4.GetData(), bs4.GetLength()));

  ByteString bs5(std::string(kTest4), true);
  EXPECT_EQ(strlen(kTest4) + 1, bs5.GetLength());
  EXPECT_EQ(0, memcmp(kTest4, bs5.GetData(), bs5.GetLength()));

  ByteString bs6(kTest1, sizeof(kTest1));
  EXPECT_TRUE(bs6.Equals(bs1));

  ByteString bs7(kTest5, sizeof(kTest5));
  ASSERT_TRUE(bs7.GetData() != NULL);
  EXPECT_EQ(sizeof(kTest5), bs7.GetLength());
  for (unsigned int i = 0; i < sizeof(kTest5); i++) {
    EXPECT_EQ(bs7.GetData()[i], kTest5[i]);
  }
}

TEST_F(ByteStringTest, SubString) {
  ByteString bs1(kTest1, sizeof(kTest1));
  ByteString bs2(kTest1 + 3, 4);
  EXPECT_TRUE(bs2.Equals(bs1.GetSubstring(3, 4)));
  const int kMargin = sizeof(kTest1) - 3;
  ByteString bs3(kTest1 + kMargin, sizeof(kTest1) - kMargin);
  EXPECT_TRUE(bs3.Equals(bs1.GetSubstring(kMargin, sizeof(kTest1))));
  EXPECT_TRUE(bs1.GetSubstring(sizeof(kTest1), 10).IsEmpty());
}

TEST_F(ByteStringTest, UInt32) {
  ByteString bs1 = ByteString::CreateFromNetUInt32(kTest2Uint32);
  uint32 val;

  EXPECT_EQ(4, bs1.GetLength());
  EXPECT_TRUE(bs1.GetData() != NULL);
  EXPECT_TRUE(bs1.ConvertToNetUInt32(&val));
  EXPECT_EQ(kTest2Uint32, val);
  EXPECT_FALSE(bs1.IsZero());

  ByteString bs2(kTest2, sizeof(kTest2));
  EXPECT_TRUE(bs1.Equals(bs2));
  EXPECT_TRUE(bs2.ConvertToNetUInt32(&val));
  EXPECT_EQ(kTest2Uint32, val);

  ByteString bs3 = ByteString::CreateFromCPUUInt32(0x1020304);
  EXPECT_EQ(4, bs1.GetLength());
  EXPECT_TRUE(bs3.GetData() != NULL);
  EXPECT_TRUE(bs3.ConvertToCPUUInt32(&val));
  EXPECT_EQ(0x1020304, val);
  EXPECT_FALSE(bs3.IsZero());

#if __BYTE_ORDER == __LITTLE_ENDIAN
  EXPECT_FALSE(bs1.Equals(bs3));
#else
  EXPECT_TRUE(bs1.Equals(bs3));
#endif
}

TEST_F(ByteStringTest, Resize) {
  ByteString bs1(kTest2, sizeof(kTest2));

  bs1.Resize(sizeof(kTest2) + 10);
  EXPECT_EQ(sizeof(kTest2) + 10, bs1.GetLength());
  EXPECT_TRUE(bs1.GetData() != NULL);
  EXPECT_EQ(0, memcmp(bs1.GetData(), kTest2, sizeof(kTest2)));
  for (size_t i = sizeof(kTest2); i < sizeof(kTest2) + 10; ++i) {
    EXPECT_EQ(0, bs1.GetData()[i]);
  }

  bs1.Resize(sizeof(kTest2) - 2);
  EXPECT_EQ(sizeof(kTest2) - 2, bs1.GetLength());
  EXPECT_EQ(0, memcmp(bs1.GetData(), kTest2, sizeof(kTest2) - 2));
}

TEST_F(ByteStringTest, HexEncode) {
  ByteString bs(kTest2, sizeof(kTest2));
  EXPECT_EQ(kTest2HexString, bs.HexEncode());
}

TEST_F(ByteStringTest, BitwiseAnd) {
  ByteString bs(kTest1, sizeof(kTest1));
  ByteString mask;
  ByteString expected_result;
  for (size_t i = 0; i < sizeof(kTest1); i++) {
    EXPECT_FALSE(bs.BitwiseAnd(mask));
    unsigned char val = sizeof(kTest1) - i;
    mask.Append(ByteString(&val, 1));
    val &= bs.GetConstData()[i];
    expected_result.Append(ByteString(&val, 1));
  }
  EXPECT_TRUE(bs.BitwiseAnd(mask));
  EXPECT_TRUE(bs.Equals(expected_result));
  bs.Resize(sizeof(kTest1) - 1);
  EXPECT_FALSE(bs.BitwiseAnd(mask));
}

TEST_F(ByteStringTest, BitwiseOr) {
  ByteString bs(kTest1, sizeof(kTest1));
  ByteString merge;
  ByteString expected_result;
  for (size_t i = 0; i < sizeof(kTest1); i++) {
    EXPECT_FALSE(bs.BitwiseOr(merge));
    unsigned char val = sizeof(kTest1) - i;
    merge.Append(ByteString(&val, 1));
    val |= bs.GetConstData()[i];
    expected_result.Append(ByteString(&val, 1));
  }
  EXPECT_TRUE(bs.BitwiseOr(merge));
  EXPECT_TRUE(bs.Equals(expected_result));
  bs.Resize(sizeof(kTest1) - 1);
  EXPECT_FALSE(bs.BitwiseOr(merge));
}

TEST_F(ByteStringTest, BitwiseInvert) {
  ByteString bs(kTest1, sizeof(kTest1));
  ByteString invert;
  for (size_t i = 0; i < sizeof(kTest1); i++) {
    unsigned char val = kTest1[i] ^ 0xff;
    invert.Append(ByteString(&val, 1));
  }
  bs.BitwiseInvert();
  EXPECT_TRUE(bs.Equals(invert));
}

// Offset.

TEST_F(ByteStringTest, EmptyOffset) {
  uint32 val;

  ByteString bs1(kTest1, sizeof(kTest1));
  bs1.ChopBeginningBytes(sizeof(kTest1));
  EXPECT_TRUE(bs1.IsEmpty());
  EXPECT_EQ(0, bs1.GetLength());
  EXPECT_TRUE(bs1.GetData() == NULL);
  EXPECT_FALSE(bs1.ConvertToNetUInt32(&val));
  EXPECT_TRUE(bs1.IsZero());
}

TEST_F(ByteStringTest, NonEmptyOffset) {
  ByteString bs1(kTest1, sizeof(kTest1));
  size_t new_length_1 = 2;
  size_t offset_1 = sizeof(kTest1) - new_length_1;
  bs1.ChopBeginningBytes(offset_1);

  uint32 val;

  EXPECT_FALSE(bs1.IsEmpty());

  EXPECT_EQ(2, bs1.GetLength());
  for (unsigned int i = offset_1; i < sizeof(kTest1); i++) {
    EXPECT_EQ(bs1.GetData()[i - offset_1], kTest1[i]);
  }
  EXPECT_TRUE(bs1.GetData() != NULL);
  EXPECT_FALSE(bs1.ConvertToNetUInt32(&val));
  EXPECT_FALSE(bs1.IsZero());

  ByteString bs2(kTest2, sizeof(kTest2));
  size_t new_length_2 = 3;
  size_t offset_2 = sizeof(kTest2) - new_length_2;
  bs2.ChopBeginningBytes(offset_2);

  EXPECT_EQ(new_length_2, bs2.GetLength());
  for (unsigned int i = offset_2; i < sizeof(kTest2); i++) {
    EXPECT_EQ(bs2.GetData()[i - offset_2], kTest2[i]);
  }
  EXPECT_TRUE(bs2.GetData() != NULL);
  EXPECT_FALSE(bs2.IsZero());

  EXPECT_FALSE(bs2.Equals(bs1));

  ByteString bs3(kTest3, sizeof(kTest3));
  size_t new_length_3 = 3;
  size_t offset_3 = sizeof(kTest3) - new_length_3;
  bs3.ChopBeginningBytes(offset_3);

  EXPECT_EQ(new_length_3, bs3.GetLength());
  for (unsigned int i = offset_3; i < sizeof(kTest3); i++) {
    EXPECT_EQ(bs3.GetData()[i - offset_3], kTest3[i]);
  }
  EXPECT_TRUE(bs3.GetData() != NULL);
  EXPECT_TRUE(bs3.IsZero());
  EXPECT_FALSE(bs2.Equals(bs1));
  EXPECT_FALSE(bs3.Equals(bs1));

  ByteString bs4(std::string(kTest4), false);
  size_t offset_4 = 1;
  bs4.ChopBeginningBytes(offset_4);

  EXPECT_EQ(strlen(kTest4) - offset_4, bs4.GetLength());
  EXPECT_EQ(0, memcmp(kTest4 + offset_4, bs4.GetData(), bs4.GetLength()));

  ByteString bs5(std::string(kTest4), true);
  size_t offset_5 = 1;
  bs5.ChopBeginningBytes(offset_5);

  EXPECT_EQ(strlen(kTest4) + 1 - offset_5, bs5.GetLength());
  EXPECT_EQ(0, memcmp(kTest4 + offset_5, bs5.GetData(), bs5.GetLength()));

  ByteString bs6(kTest1, sizeof(kTest1));
  bs6.ChopBeginningBytes(offset_1);
  EXPECT_TRUE(bs6.Equals(bs1));

  ByteString bs7(kTest5, sizeof(kTest5));
  size_t offset_7 = 3;
  size_t new_length_7 = sizeof(kTest5) - offset_7;
  bs7.ChopBeginningBytes(offset_7);

  ASSERT_TRUE(bs7.GetData() != NULL);
  EXPECT_EQ(new_length_7, bs7.GetLength());
  for (unsigned int i = offset_7; i < sizeof(kTest5); i++) {
    EXPECT_EQ(bs7.GetData()[i - offset_7], kTest5[i]);
  }
}

TEST_F(ByteStringTest, SubStringOffset) {
  size_t offset = 3;
  ByteString bs1(kTest1, sizeof(kTest1));
  ByteString bs2(kTest1, offset + kExpectedLength);
  bs2.ChopBeginningBytes(offset);

  EXPECT_TRUE(bs2.Equals(bs1.GetSubstring(offset, kExpectedLength)));
  const int kMargin = sizeof(kTest1) - offset;
  ByteString bs3(kTest1 + kMargin, sizeof(kTest1) - kMargin);
  EXPECT_TRUE(bs3.Equals(bs1.GetSubstring(kMargin, sizeof(kTest1))));
  EXPECT_TRUE(bs1.GetSubstring(sizeof(kTest1), 10).IsEmpty());
}

TEST_F(ByteStringTest, ResizeOffset) {
  ByteString bs1(kTest2, sizeof(kTest2));
  size_t offset = 1;
  bs1.ChopBeginningBytes(offset);

  bs1.Resize(sizeof(kTest2) + 10);
  EXPECT_EQ(sizeof(kTest2) + 10, bs1.GetLength());
  EXPECT_TRUE(bs1.GetData() != NULL);
  EXPECT_EQ(0, memcmp(bs1.GetData(),
                      kTest2 + offset,
                      sizeof(kTest2) - offset));
  for (size_t i = sizeof(kTest2) - offset; i < sizeof(kTest2) + 10; ++i) {
    EXPECT_EQ(0, bs1.GetData()[i]);
  }

  bs1.Resize(sizeof(kTest2) - 2);
  EXPECT_EQ(sizeof(kTest2) - 2, bs1.GetLength());
  EXPECT_EQ(0, memcmp(bs1.GetData(), kTest2 + offset, sizeof(kTest2) - 2));
}

TEST_F(ByteStringTest, HexEncodeOffset) {
  ByteString bs(kTest2, sizeof(kTest2));
  size_t offset = 2;
  size_t bytes_per_hex_digit = 2;
  bs.ChopBeginningBytes(offset);

  EXPECT_EQ(kTest2HexString + offset * bytes_per_hex_digit, bs.HexEncode());
}

TEST_F(ByteStringTest, BitwiseAndOffset) {
  ByteString bs(kTest1, sizeof(kTest1));
  size_t offset = 2;
  bs.ChopBeginningBytes(offset);

  ByteString mask;
  ByteString expected_result;
  for (size_t i = 0; i < sizeof(kTest1) - offset; i++) {
    EXPECT_FALSE(bs.BitwiseAnd(mask));
    unsigned char val = sizeof(kTest1) - i;
    mask.Append(ByteString(&val, 1));
    val &= bs.GetConstData()[i];
    expected_result.Append(ByteString(&val, 1));
  }
  EXPECT_TRUE(bs.BitwiseAnd(mask));
  EXPECT_TRUE(bs.Equals(expected_result));

  bs.Resize(sizeof(kTest1) - 1);
  EXPECT_FALSE(bs.BitwiseAnd(mask));
}

TEST_F(ByteStringTest, BitwiseOrOffset) {
  ByteString bs(kTest1, sizeof(kTest1));
  size_t offset = 3;
  bs.ChopBeginningBytes(offset);
  ByteString merge;
  ByteString expected_result;
  for (size_t i = 0; i < sizeof(kTest1) - offset; i++) {
    EXPECT_FALSE(bs.BitwiseOr(merge));
    unsigned char val = sizeof(kTest1) - i;
    merge.Append(ByteString(&val, 1));
    val |= bs.GetConstData()[i];
    expected_result.Append(ByteString(&val, 1));
  }
  EXPECT_TRUE(bs.BitwiseOr(merge));
  EXPECT_TRUE(bs.Equals(expected_result));
  bs.Resize(sizeof(kTest1) - 1);
  EXPECT_FALSE(bs.BitwiseOr(merge));
}

TEST_F(ByteStringTest, BitwiseInvertOffset) {
  ByteString bs(kTest1, sizeof(kTest1));
  size_t offset = 4;
  bs.ChopBeginningBytes(offset);
  ByteString invert;
  for (size_t i = 0; i < sizeof(kTest1) - offset; i++) {
    unsigned char val = kTest1[i + offset] ^ 0xff;
    invert.Append(ByteString(&val, 1));
  }
  bs.BitwiseInvert();
  EXPECT_TRUE(bs.Equals(invert));
}

}  // namespace shill
