// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/byte_utils.h"

#include <base/logging.h>
#include <gtest/gtest.h>

namespace net_base::byte_utils {
namespace {

TEST(Bytes, ConvertBetweenUInt32) {
  uint32_t val = 0x11223344;
  const auto bytes = ToBytes<uint32_t>(val);

  EXPECT_EQ(bytes.size(), sizeof(uint32_t));
  EXPECT_EQ(FromBytes<uint32_t>(bytes), val);
}

TEST(Bytes, ConvertBetweenStruct) {
  struct Foo {
    int x;
    char y;
  };

  const Foo foo{35, 'c'};
  const auto bytes = ToBytes(foo);
  EXPECT_EQ(bytes.size(), sizeof(Foo));

  const Foo converted = *FromBytes<Foo>(bytes);
  EXPECT_EQ(converted.x, foo.x);
  EXPECT_EQ(converted.y, foo.y);
}

TEST(Bytes, StringToCStringBytes) {
  EXPECT_EQ(StringToCStringBytes("abc"),
            (std::vector<uint8_t>{'a', 'b', 'c', '\0'}));
  EXPECT_EQ(StringToCStringBytes(std::string("abc\0", 4)),
            (std::vector<uint8_t>{'a', 'b', 'c', '\0'}));
}

TEST(Bytes, StringFromCStringBytes) {
  EXPECT_EQ(StringFromCStringBytes(std::vector<uint8_t>{'a', 'b'}), "ab");
  EXPECT_EQ(StringFromCStringBytes(std::vector<uint8_t>{'a', 'b', '\0'}), "ab");
  EXPECT_EQ(StringFromCStringBytes(std::vector<uint8_t>{'a', 'b', '\0', 'c'}),
            "ab");
}

TEST(Bytes, ByteStringToBytes) {
  EXPECT_EQ(ByteStringToBytes(std::string("abc", 3)),
            (std::vector<uint8_t>{'a', 'b', 'c'}));
  EXPECT_EQ(ByteStringToBytes(std::string("abc\0", 4)),
            (std::vector<uint8_t>{'a', 'b', 'c', '\0'}));
  EXPECT_EQ(ByteStringToBytes(std::string("abc\0d", 5)),
            (std::vector<uint8_t>{'a', 'b', 'c', '\0', 'd'}));
}

TEST(Bytes, ByteStringFromBytes) {
  EXPECT_EQ(ByteStringFromBytes(std::vector<uint8_t>{'a', 'b', 'c'}),
            std::string("abc", 3));
  EXPECT_EQ(ByteStringFromBytes(std::vector<uint8_t>{'a', 'b', 'c', '\0'}),
            std::string("abc\0", 4));
  EXPECT_EQ(ByteStringFromBytes(std::vector<uint8_t>{'a', 'b', 'c', '\0', 'd'}),
            std::string("abc\0d", 5));
}

}  // namespace
}  // namespace net_base::byte_utils
