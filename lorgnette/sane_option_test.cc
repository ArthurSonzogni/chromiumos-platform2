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
  desc.title = NULL;
  desc.desc = NULL;
  desc.type = type;
  desc.unit = SANE_UNIT_NONE;
  desc.size = size;
  desc.cap = 0;
  desc.constraint_type = SANE_CONSTRAINT_NONE;
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

TEST(SaneOptionsIntTest, InactiveFails) {
  auto descriptor =
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  descriptor.cap |= SANE_CAP_INACTIVE;
  SaneOption option(descriptor, 1);

  EXPECT_FALSE(option.Set(1));
  EXPECT_EQ(option.Get<int>(), std::nullopt);
  EXPECT_FALSE(option.Set(1.0));
  EXPECT_EQ(option.Get<int>(), std::nullopt);
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

TEST(SaneOptionFixedTest, DisplayValueLargeNumber) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(5000.0));
  EXPECT_EQ(option.DisplayValue(), "5000");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestInt) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(4999.96));
  EXPECT_EQ(option.DisplayValue(), "5000");
}

TEST(SaneOptionFixedTest, DisplayValueLargestOneDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(4999.949));
  EXPECT_EQ(option.DisplayValue(), "4999.9");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestOneDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(9.996));
  EXPECT_EQ(option.DisplayValue(), "10.0");
}

TEST(SaneOptionFixedTest, DisplayValueLargestTwoDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(9.9949));
  EXPECT_EQ(option.DisplayValue(), "9.99");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestTwoDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(0.0096));
  EXPECT_EQ(option.DisplayValue(), "0.01");
}

TEST(SaneOptionFixedTest, DisplayValueLargestThreeDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(0.00949));
  EXPECT_EQ(option.DisplayValue(), "0.009");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestThreeDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(0.00097));
  EXPECT_EQ(option.DisplayValue(), "0.001");
}

TEST(SaneOptionFixedTest, DisplayValueLargestFiveDigitDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(0.000949));
  EXPECT_EQ(option.DisplayValue(), "0.00095");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestNonZeroDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(0.0000153));
  EXPECT_EQ(option.DisplayValue(), "0.00002");
}

TEST(SaneOptionFixedTest, DisplayValueLargestZeroDecimal) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(0.000015));
  EXPECT_EQ(option.DisplayValue(), "0.0");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestFixedFraction) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(1.0 / 65536.0));
  EXPECT_EQ(option.DisplayValue(), "0.00002");
}

TEST(SaneOptionFixedTest, DisplayValueLargestFixedFraction) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(65535.0 / 65536.0));
  EXPECT_EQ(option.DisplayValue(), "1.0");
}

TEST(SaneOptionFixedTest, DisplayValueExactlyZero) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(0.0));
  EXPECT_EQ(option.DisplayValue(), "0.0");
}

TEST(SaneOptionFixedTest, DisplayValueNegativeNumber) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(-100.0));
  EXPECT_EQ(option.DisplayValue(), "-100.0");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestEsclFraction) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(1.0 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.003");
  EXPECT_TRUE(option.Set(2.0 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.007");
  EXPECT_TRUE(option.Set(3.0 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.01");
}

TEST(SaneOptionFixedTest, DisplayValueLargestEsclFractions) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(299.0 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "1.0");
  EXPECT_TRUE(option.Set(298.0 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.99");
  EXPECT_TRUE(option.Set(297.0 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.99");
}

TEST(SaneOptionFixedTest, DisplayValueSmallestEsclFractionsAsMm) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(1.0 * 25.4 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.085");
  EXPECT_TRUE(option.Set(2.0 * 25.4 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.17");
  EXPECT_TRUE(option.Set(3.0 * 25.4 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "0.25");
}

TEST(SaneOptionFixedTest, DisplayValueLargestEsclFractionsAsMm) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(299.0 * 25.4 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "25.3");
  EXPECT_TRUE(option.Set(298.0 * 25.4 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "25.2");
  EXPECT_TRUE(option.Set(297.0 * 25.4 / 300.0));
  EXPECT_EQ(option.DisplayValue(), "25.1");
}

TEST(SaneOptionFixedTest, CopiesDoNotAlias) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 2);
  EXPECT_TRUE(option.Set(88));
  EXPECT_EQ(option.DisplayValue(), "88.0");

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.Set(9));
  EXPECT_EQ(option_two.DisplayValue(), "9.0");
  EXPECT_EQ(option.DisplayValue(), "88.0");
}

TEST(SaneOptionsFixedTest, InactiveFails) {
  auto descriptor =
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  descriptor.cap |= SANE_CAP_INACTIVE;
  SaneOption option(descriptor, 1);

  EXPECT_FALSE(option.Set(1.0));
  EXPECT_EQ(option.Get<double>(), std::nullopt);
  EXPECT_FALSE(option.Set(1));
  EXPECT_EQ(option.Get<double>(), std::nullopt);
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

TEST(SaneOptionStringTest, InactiveFails) {
  auto descriptor =
      CreateDescriptor("Test Name", SANE_TYPE_STRING, 5 * sizeof(SANE_Char));
  descriptor.cap |= SANE_CAP_INACTIVE;
  SaneOption option(descriptor, 1);

  EXPECT_FALSE(option.Set("true"));
  EXPECT_EQ(option.Get<std::string>(), std::nullopt);
  EXPECT_FALSE(option.Set(std::string("true")));
  EXPECT_EQ(option.Get<std::string>(), std::nullopt);
}

TEST(SaneOptionBoolTest, SetBooleanFromBoolSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);

  EXPECT_TRUE(option.Set(true));
  EXPECT_EQ(option.Get<bool>(), SANE_TRUE);
  EXPECT_TRUE(option.Set(false));
  EXPECT_EQ(option.Get<bool>(), SANE_FALSE);
}

TEST(SaneOptionBoolTest, SetBooleanFromValidIntSucceeds) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);

  EXPECT_TRUE(option.Set(SANE_TRUE));
  EXPECT_EQ(option.Get<bool>(), true);
  EXPECT_EQ(option.Get<int>(), SANE_TRUE);

  EXPECT_TRUE(option.Set(SANE_FALSE));
  EXPECT_EQ(option.Get<bool>(), false);
  EXPECT_EQ(option.Get<int>(), SANE_FALSE);
}

TEST(SaneOptionBoolTest, SetBooleanFromInvalidIntFails) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);

  EXPECT_FALSE(option.Set(2));
  EXPECT_FALSE(option.Set(-1));
}

TEST(SaneOptionBoolTest, SetBooleanFromInvalidTypeFails) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);

  EXPECT_FALSE(option.Set(1.0));
  EXPECT_FALSE(option.Set("true"));
}

TEST(SaneOptionBoolTest, InactiveFails) {
  auto descriptor =
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word));
  descriptor.cap |= SANE_CAP_INACTIVE;
  SaneOption option(descriptor, 1);

  EXPECT_FALSE(option.Set(true));
  EXPECT_EQ(option.Get<bool>(), std::nullopt);
}

TEST(SaneOptionBoolTest, DisplayValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(SANE_TRUE));
  EXPECT_EQ(option.DisplayValue(), "true");
  EXPECT_TRUE(option.Set(SANE_FALSE));
  EXPECT_EQ(option.DisplayValue(), "false");
}

TEST(SaneOptionBool, CopiesDoNotAlias) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);
  EXPECT_TRUE(option.Set(SANE_TRUE));
  EXPECT_EQ(option.Get<bool>(), true);

  SaneOption option_two = option;
  EXPECT_TRUE(option_two.Set(SANE_FALSE));
  EXPECT_EQ(option_two.Get<bool>(), false);
  EXPECT_EQ(option.Get<bool>(), true);
}

}  // namespace lorgnette
