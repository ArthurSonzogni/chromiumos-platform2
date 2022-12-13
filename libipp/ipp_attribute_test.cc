// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libipp/ipp_attribute.h"

#include <limits>
#include <string_view>

#include <gtest/gtest.h>

namespace ipp {

namespace {

void TestNewAttribute(Attribute* attr, std::string_view name, ValueTag tag) {
  EXPECT_TRUE(attr != nullptr);
  EXPECT_EQ(attr->Name(), name);
  EXPECT_EQ(attr->Tag(), tag);
  // default state after creation
  EXPECT_EQ(attr->Size(), 0);
}

TEST(attribute, UnknownValueAttribute) {
  Collection coll;
  Attribute* attr = coll.AddUnknownAttribute("abc", ValueTag::nameWithLanguage);
  TestNewAttribute(attr, "abc", ValueTag::nameWithLanguage);
  ASSERT_TRUE(attr->SetValue("val"));
  StringWithLanguage sl;
  ASSERT_TRUE(attr->GetValue(&sl));
  EXPECT_EQ(sl.language, "");
  EXPECT_EQ(sl.value, "val");
}

TEST(attribute, UnknownCollectionAttribute) {
  Collection coll;
  Attribute* attr = coll.AddUnknownAttribute("abcd", ValueTag::collection);
  TestNewAttribute(attr, "abcd", ValueTag::collection);
  EXPECT_EQ(attr->GetCollection(), nullptr);
  attr->Resize(3);
  EXPECT_NE(attr->GetCollection(), nullptr);
  EXPECT_NE(attr->GetCollection(2), nullptr);
  EXPECT_EQ(attr->GetCollection(3), nullptr);
  const Attribute* attr_const = attr;
  EXPECT_NE(attr_const->GetCollection(), nullptr);
  EXPECT_NE(attr_const->GetCollection(2), nullptr);
  EXPECT_EQ(attr_const->GetCollection(3), nullptr);
}

TEST(attribute, FromStringToInt) {
  int val = 123456;
  // incorrect values: return false, no changes to val
  EXPECT_FALSE(FromString("123", static_cast<int*>(nullptr)));
  EXPECT_FALSE(FromString("12341s", &val));
  EXPECT_EQ(123456, val);
  EXPECT_FALSE(FromString("-", &val));
  EXPECT_EQ(123456, val);
  EXPECT_FALSE(FromString("", &val));
  EXPECT_EQ(123456, val);
  // correct values: return true
  EXPECT_TRUE(FromString("-239874", &val));
  EXPECT_EQ(-239874, val);
  EXPECT_TRUE(FromString("9238", &val));
  EXPECT_EQ(9238, val);
  EXPECT_TRUE(FromString("0", &val));
  EXPECT_EQ(0, val);
  const int int_min = std::numeric_limits<int>::min();
  const int int_max = std::numeric_limits<int>::max();
  EXPECT_TRUE(FromString(ToString(int_min), &val));
  EXPECT_EQ(int_min, val);
  EXPECT_TRUE(FromString(ToString(int_max), &val));
  EXPECT_EQ(int_max, val);
}

}  // end of namespace

}  // end of namespace ipp
