// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attribute.h"

#include <string>

#include "frame.h"
#include <gtest/gtest.h>

namespace ipp {
namespace {

class CollectionTest : public testing::Test {
 public:
  CollectionTest() : frame_(Version::_1_1, Operation::Print_Job, 1) {
    coll_ = frame_.GetGroup(GroupTag::operation_attributes);
  }

 protected:
  Frame frame_;
  Collection* coll_;
};

TEST_F(CollectionTest, AddAttrOutOfBand) {
  auto err = coll_->AddAttr("test", ValueTag::not_settable);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::not_settable);
}

TEST_F(CollectionTest, AddAttrEnumAsInt) {
  auto err = coll_->AddAttr("test-enum", ValueTag::enum_, 1234);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test-enum");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::enum_);
  int value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value, 1234);
}

TEST_F(CollectionTest, AddAttrString) {
  auto err = coll_->AddAttr("abc123", ValueTag::mimeMediaType, "abc&123 DEF");
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("abc123");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::mimeMediaType);
  std::string value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value, "abc&123 DEF");
}

TEST_F(CollectionTest, AddAttrStringWithLanguage) {
  StringWithLanguage sl;
  sl.language = "lang_def";
  sl.value = "str value";
  auto err = coll_->AddAttr("lang", ValueTag::textWithLanguage, sl);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("lang");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::textWithLanguage);
  StringWithLanguage value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value.language, "lang_def");
  EXPECT_EQ(value.value, "str value");
}

TEST_F(CollectionTest, AddAttrBool) {
  auto err = coll_->AddAttr("test", true);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::boolean);
  int value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value, 1);
}

TEST_F(CollectionTest, AddAttrInteger) {
  auto err = coll_->AddAttr("test", -1234567890);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::integer);
  int32_t value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value, -1234567890);
}

TEST_F(CollectionTest, AddAttrDateTime) {
  DateTime dt;
  dt.year = 2034;
  dt.month = 6;
  dt.day = 23;
  dt.hour = 19;
  dt.minutes = 59;
  dt.deci_seconds = 7;
  dt.UTC_hours = 5;
  dt.UTC_minutes = 44;
  auto err = coll_->AddAttr("test", dt);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::dateTime);
  DateTime value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value.year, 2034);
  EXPECT_EQ(value.month, 6);
  EXPECT_EQ(value.day, 23);
  EXPECT_EQ(value.hour, 19);
  EXPECT_EQ(value.minutes, 59);
  EXPECT_EQ(value.seconds, 0);
  EXPECT_EQ(value.deci_seconds, 7);
  EXPECT_EQ(value.UTC_direction, '+');
  EXPECT_EQ(value.UTC_hours, 5);
  EXPECT_EQ(value.UTC_minutes, 44);
}

TEST_F(CollectionTest, AddAttrResolution) {
  Resolution res(123, 456, Resolution::Units::kDotsPerInch);
  auto err = coll_->AddAttr("test", res);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::resolution);
  Resolution value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value.xres, 123);
  EXPECT_EQ(value.yres, 456);
  EXPECT_EQ(value.units, Resolution::Units::kDotsPerInch);
}

TEST_F(CollectionTest, AddAttrRangeOfInteger) {
  RangeOfInteger roi(-123, 456);
  auto err = coll_->AddAttr("test", roi);
  EXPECT_EQ(err, Code::kOK);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::rangeOfInteger);
  RangeOfInteger value;
  ASSERT_TRUE(attr->GetValue(&value));
  EXPECT_EQ(value.min_value, -123);
  EXPECT_EQ(value.max_value, 456);
}

TEST_F(CollectionTest, AddAttrCollection) {
  Collection* attr_coll;
  auto err = coll_->AddAttr("test", attr_coll);
  EXPECT_EQ(err, Code::kOK);
  EXPECT_NE(attr_coll, nullptr);
  auto attr = coll_->GetAttribute("test");
  ASSERT_NE(attr, nullptr);
  EXPECT_EQ(attr->Tag(), ValueTag::collection);
  EXPECT_EQ(attr->GetCollection(), attr_coll);
}

TEST_F(CollectionTest, AddAttrInvalidName) {
  auto err = coll_->AddAttr("", true);
  EXPECT_EQ(err, Code::kInvalidName);
}

TEST_F(CollectionTest, AddAttrNameConflict) {
  auto err = coll_->AddAttr("test", true);
  EXPECT_EQ(err, Code::kOK);
  EXPECT_EQ(coll_->AddAttr("test", true), Code::kNameConflict);
  EXPECT_EQ(coll_->AddAttr("test", ValueTag::unknown), Code::kNameConflict);
}

TEST_F(CollectionTest, AddAttrValueOutOfRange) {
  auto err = coll_->AddAttr("aaa", ValueTag::boolean, -1);
  EXPECT_EQ(err, Code::kValueOutOfRange);
  EXPECT_EQ(coll_->GetAttribute("aaa"), nullptr);
}

TEST_F(CollectionTest, AddAttrInvalidValueTag) {
  auto err = coll_->AddAttr("xxx", static_cast<ValueTag>(0x0f));
  EXPECT_EQ(err, Code::kInvalidValueTag);
}

TEST(ToStrView, ValueTag) {
  EXPECT_EQ(ToStrView(ValueTag::keyword), "keyword");
  EXPECT_EQ(ToStrView(ValueTag::delete_attribute), "delete-attribute");
  EXPECT_EQ(ToStrView(ValueTag::enum_), "enum");
  EXPECT_EQ(ToStrView(ValueTag::nameWithoutLanguage), "nameWithoutLanguage");
}

}  // namespace
}  // namespace ipp
