// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_option.h"

#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/manager.h"
#include "lorgnette/test_util.h"

using ::testing::ElementsAre;

namespace lorgnette {

namespace {

SANE_Option_Descriptor CreateDescriptor(const char* name,
                                        SANE_Value_Type type,
                                        int size) {
  SANE_Option_Descriptor desc;
  desc.name = name;
  desc.type = type;
  desc.constraint_type = SANE_CONSTRAINT_NONE;
  desc.size = size;
  return desc;
}

}  // namespace

TEST(SaneOptionIntTest, SetIntSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.Set(54));
  EXPECT_EQ(*static_cast<SANE_Int*>(option.GetPointer()), 54);
}

TEST(SaneOptionIntTest, SetDoubleSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  // Should round towards 0.
  EXPECT_TRUE(option.Set(295.7));
  EXPECT_EQ(option.Get<int>().value(), 295);
}

TEST(SaneOptionIntTest, SetStringFails) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.Set(17));
  EXPECT_FALSE(option.Set("test"));
  EXPECT_EQ(option.Get<int>().value(), 17);
}

TEST(SaneOptionIntTest, GetIndex) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetIndex(), 7);
}

TEST(SaneOptionIntTest, GetName) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetName(), "Test Name");
}

TEST(SaneOptionIntTest, DisplayValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(247));
  EXPECT_EQ(option.DisplayValue(), "247");
}

TEST(SaneOptionIntTest, CopiesDoNotAlias) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(88));
  EXPECT_EQ(option.DisplayValue(), "88");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.Set(9));
  EXPECT_EQ(option_two.DisplayValue(), "9");
  EXPECT_EQ(option.DisplayValue(), "88");
}

TEST(SaneOptionFixedTest, SetIntSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.Set(54));
  SANE_Fixed f = *static_cast<SANE_Fixed*>(option.GetPointer());
  EXPECT_EQ(static_cast<int>(SANE_UNFIX(f)), 54);
}

TEST(SaneOptionFixedTest, SetDoubleSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.Set(436.2));
  SANE_Fixed f = *static_cast<SANE_Fixed*>(option.GetPointer());
  EXPECT_FLOAT_EQ(SANE_UNFIX(f), 436.2);
}

TEST(SaneOptionFixedTest, SetStringFails) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_TRUE(option.Set(17));
  EXPECT_FALSE(option.Set("test"));
  SANE_Fixed f = *static_cast<SANE_Fixed*>(option.GetPointer());
  EXPECT_EQ(static_cast<int>(SANE_UNFIX(f)), 17);
}

TEST(SaneOptionFixedTest, GetIndex) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetIndex(), 7);
}

TEST(SaneOptionFixedTest, GetName) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 7);
  EXPECT_EQ(option.GetName(), "Test Name");
}

TEST(SaneOptionFixedTest, DisplayValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(247));
  EXPECT_EQ(option.DisplayValue(), "247");
}

TEST(SaneOptionFixedTest, CopiesDoNotAlias) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(88));
  EXPECT_EQ(option.DisplayValue(), "88");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.Set(9));
  EXPECT_EQ(option_two.DisplayValue(), "9");
  EXPECT_EQ(option.DisplayValue(), "88");
}

TEST(SaneOptionStringTest, SetStringSucceeds) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 8), 7);
  EXPECT_TRUE(option.Set("test"));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "test");

  // Longest string that fits (with null terminator).
  EXPECT_TRUE(option.Set("1234567"));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "1234567");
}

TEST(SaneOptionStringTest, SetStringTooLongFails) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 8), 7);
  EXPECT_TRUE(option.Set("test"));

  // String that is exactly one character too long.
  EXPECT_FALSE(option.Set("12345678"));

  // String that is many characters too long.
  EXPECT_FALSE(option.Set("This is a much longer string than can fit."));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "test");
}

TEST(SaneOptionStringTest, SetIntFails) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 7);
  EXPECT_TRUE(option.Set("test"));
  EXPECT_FALSE(option.Set(54));
  EXPECT_STREQ(static_cast<char*>(option.GetPointer()), "test");
}

TEST(SaneOptionStringTest, GetIndex) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 7);
  EXPECT_EQ(option.GetIndex(), 7);
}

TEST(SaneOptionStringTest, GetName) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 7);
  EXPECT_EQ(option.GetName(), "Test Name");
}

TEST(SaneOptionStringTest, DisplayValue) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 2);
  EXPECT_TRUE(option.Set("test string"));
  EXPECT_EQ(option.DisplayValue(), "test string");
}

TEST(SaneOptionStringTest, CopiesDoNotAlias) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 32), 2);
  EXPECT_TRUE(option.Set("test string"));
  EXPECT_EQ(option.DisplayValue(), "test string");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.Set("other value"));
  EXPECT_EQ(option.DisplayValue(), "test string");
  EXPECT_EQ(option_two.DisplayValue(), "other value");
}

}  // namespace lorgnette
