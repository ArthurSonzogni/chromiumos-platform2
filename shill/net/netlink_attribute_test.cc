// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides tests for individual messages.  It tests
// NetlinkMessageFactory's ability to create specific message types and it
// tests the various NetlinkMessage types' ability to parse those
// messages.

// This file tests some public interface methods of NetlinkAttribute subclasses.
#include "shill/net/netlink_attribute.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::Test;

namespace shill {

class NetlinkAttributeTest : public Test {};

TEST_F(NetlinkAttributeTest, StringAttribute) {
  NetlinkStringAttribute attr(0, "string id");

  // An empty input should yield an empty string.
  EXPECT_TRUE(attr.InitFromValue({}));
  std::string value;
  EXPECT_TRUE(attr.GetStringValue(&value));
  EXPECT_EQ("", value);

  // An un-terminated string span should yield a terminated string.
  std::string str("hello");
  base::span<const uint8_t> unterminated_span{
      reinterpret_cast<const uint8_t*>(str.data()), str.size()};
  EXPECT_EQ(5, unterminated_span.size());
  EXPECT_TRUE(attr.InitFromValue(unterminated_span));
  EXPECT_TRUE(attr.GetStringValue(&value));
  EXPECT_EQ("hello", value);
  EXPECT_EQ(5, value.size());

  // A terminated string span should also work correctly.
  str.push_back('\0');
  base::span<const uint8_t> terminated_span{
      reinterpret_cast<const uint8_t*>(str.data()), str.size()};
  EXPECT_EQ(6, terminated_span.size());
  EXPECT_TRUE(attr.InitFromValue(terminated_span));
  EXPECT_TRUE(attr.GetStringValue(&value));
  EXPECT_EQ("hello", value);
  EXPECT_EQ(5, value.size());

  // Extra data after termination should be removed.
  str += "abc";
  base::span<const uint8_t> span_with_extra{
      reinterpret_cast<const uint8_t*>(str.c_str()), str.size()};
  EXPECT_EQ(9, span_with_extra.size());
  EXPECT_TRUE(attr.InitFromValue(span_with_extra));
  EXPECT_TRUE(attr.GetStringValue(&value));
  EXPECT_EQ("hello", value);
  EXPECT_EQ(5, value.size());
}

}  // namespace shill
