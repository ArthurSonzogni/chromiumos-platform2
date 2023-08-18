// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_option.h"

#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
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
  memset(&desc, 0, sizeof(SANE_Option_Descriptor));
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

TEST(SaneOptionIntTest, MultiValueEmpty) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_INT, 0), 2);
  EXPECT_EQ(option.GetSize(), 0);
  EXPECT_FALSE(option.Set(42));
  EXPECT_EQ(option.Get<bool>(), std::nullopt);
  EXPECT_EQ(option.Get<int>(), std::nullopt);
  EXPECT_EQ(option.Get<double>(), std::nullopt);
}

TEST(SaneOptionIntTest, MultiValueSingleValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, 2 * sizeof(SANE_Word)), 2);
  EXPECT_EQ(option.GetSize(), 2);
  EXPECT_TRUE(option.Set(42));
  EXPECT_EQ(option.Get<int>(), 42);
  EXPECT_EQ(option.Get<double>(), 42.0);
}

TEST(SaneOptionIntTest, MultiValueRoundsDown) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, 2 * sizeof(SANE_Word) - 1),
      2);
  EXPECT_EQ(option.GetSize(), 1);
  EXPECT_TRUE(option.Set(42));
  EXPECT_EQ(option.Get<int>(), 42);
  EXPECT_EQ(option.DisplayValue(), "42");
}

TEST(SaneOptionIntTest, MultiValueListRightSize) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, 2 * sizeof(SANE_Word)), 2);
  EXPECT_EQ(option.GetSize(), 2);
  EXPECT_TRUE(option.Set(std::vector<int>{42, 43}));
  EXPECT_THAT(option.Get<std::vector<int>>().value(), ElementsAre(42, 43));
  EXPECT_EQ(option.DisplayValue(), "42, 43");
}

TEST(SaneOptionIntTest, MultiValueListWrongSize) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, 2 * sizeof(SANE_Word)), 2);
  EXPECT_EQ(option.GetSize(), 2);
  EXPECT_FALSE(option.Set(std::vector<int>{42}));
  EXPECT_FALSE(option.Set(std::vector<int>{42, 43, 44}));
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

TEST(SaneOptionFixedTest, MultiValueEmpty) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_FIXED, 0), 2);
  EXPECT_EQ(option.GetSize(), 0);
  EXPECT_FALSE(option.Set(42.0));
  EXPECT_EQ(option.Get<bool>(), std::nullopt);
  EXPECT_EQ(option.Get<int>(), std::nullopt);
  EXPECT_EQ(option.Get<double>(), std::nullopt);
}

TEST(SaneOptionFixedTest, MultiValueSingleValue) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, 2 * sizeof(SANE_Word)), 2);
  EXPECT_EQ(option.GetSize(), 2);
  EXPECT_TRUE(option.Set(42.25));
  EXPECT_EQ(option.Get<int>(), 42);
  EXPECT_EQ(option.Get<double>(), 42.25);
}

TEST(SaneOptionFixedTest, MultiValueRoundsDown) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, 2 * sizeof(SANE_Word) - 1),
      2);
  EXPECT_EQ(option.GetSize(), 1);
  EXPECT_TRUE(option.Set(1.25));
  EXPECT_EQ(option.Get<int>(), 1);
  EXPECT_EQ(option.Get<double>(), 1.25);
  EXPECT_EQ(option.DisplayValue(), "1.25");
}

TEST(SaneOptionFixedTest, MultiValueListRightSize) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, 2 * sizeof(SANE_Word)), 2);
  EXPECT_EQ(option.GetSize(), 2);
  EXPECT_TRUE(option.Set(std::vector<double>{42.0, 43.0}));
  EXPECT_THAT(option.Get<std::vector<double>>().value(),
              ElementsAre(42.0, 43.0));
  EXPECT_EQ(option.DisplayValue(), "42.0, 43.0");
}

TEST(SaneOptionFixedTest, MultiValueListWrongSize) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, 2 * sizeof(SANE_Word)), 2);
  EXPECT_EQ(option.GetSize(), 2);
  EXPECT_FALSE(option.Set(std::vector<double>{42.0}));
  EXPECT_FALSE(option.Set(std::vector<double>{42.0, 43.0, 44.0}));
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

TEST(SaneOptionToProto, BasicProtoFields) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.title = "Test Title";
  desc.desc = "Long Test Description";
  desc.unit = SANE_UNIT_MM;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_EQ(proto->name(), "Test Name");
  EXPECT_EQ(proto->title(), "Test Title");
  EXPECT_EQ(proto->description(), "Long Test Description");
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_INT);
  EXPECT_EQ(proto->unit(), OptionUnit::UNIT_MM);
  EXPECT_FALSE(proto->has_constraint());
}

TEST(SaneOptionToProto, CapabilitiesDetectable) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_SOFT_DETECT;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_TRUE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesSwSettable) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_SOFT_SELECT;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_TRUE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesHwSettable) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_HARD_SELECT;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_TRUE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesAutoSettable) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_AUTOMATIC;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_TRUE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesEmulated) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_EMULATED;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_TRUE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesInactive) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_INACTIVE;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_FALSE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesAdvanced) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = SANE_CAP_ADVANCED;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_TRUE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesAllBits) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = 0xff;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_TRUE(proto->detectable());
  EXPECT_TRUE(proto->sw_settable());
  EXPECT_TRUE(proto->hw_settable());
  EXPECT_TRUE(proto->auto_settable());
  EXPECT_TRUE(proto->emulated());
  EXPECT_FALSE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_TRUE(proto->advanced());
}

TEST(SaneOptionToProto, CapabilitiesNoBits) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.cap = 0;
  SaneOption option(desc, 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);

  EXPECT_FALSE(proto->detectable());
  EXPECT_FALSE(proto->sw_settable());
  EXPECT_FALSE(proto->hw_settable());
  EXPECT_FALSE(proto->auto_settable());
  EXPECT_FALSE(proto->emulated());
  EXPECT_TRUE(proto->active());  // Active is the opposite sense of other bits.
  EXPECT_FALSE(proto->advanced());
}

TEST(SaneOptionToProto, BoolOptionToProto) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word)), 1);
  option.Set(SANE_TRUE);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_BOOL);

  EXPECT_TRUE(proto->has_bool_value());
  EXPECT_FALSE(proto->has_int_value());
  EXPECT_FALSE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
  EXPECT_EQ(proto->bool_value(), true);
}

TEST(SaneOptionToProto, IntOptionToProto) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word)), 1);
  option.Set(42);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_INT);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_TRUE(proto->has_int_value());
  EXPECT_FALSE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
  EXPECT_THAT(proto->int_value().value(), ElementsAre(42));
}

TEST(SaneOptionToProto, IntListOptionToProto) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_INT, 3 * sizeof(SANE_Word)), 1);
  option.Set(std::vector<int>{0, 42, 314});

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_INT);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_TRUE(proto->has_int_value());
  EXPECT_FALSE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
  EXPECT_THAT(proto->int_value().value(), ElementsAre(0, 42, 314));
}

TEST(SaneOptionToProto, FixedOptionToProto) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word)), 1);
  option.Set(42.25);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_FIXED);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_FALSE(proto->has_int_value());
  EXPECT_TRUE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
  EXPECT_THAT(proto->fixed_value().value(), ElementsAre(42.25));
}

TEST(SaneOptionToProto, FixedListOptionToProto) {
  SaneOption option(
      CreateDescriptor("Test Name", SANE_TYPE_FIXED, 3 * sizeof(SANE_Word)), 1);
  option.Set(std::vector<double>{0, 42.25, -314.5});

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_FIXED);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_FALSE(proto->has_int_value());
  EXPECT_TRUE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
  EXPECT_THAT(proto->fixed_value().value(), ElementsAre(0, 42.25, -314.5));
}

TEST(SaneOptionToProto, StringOptionToProto) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_STRING, 16), 1);
  option.Set("test_1234567890");

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_STRING);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_FALSE(proto->has_int_value());
  EXPECT_FALSE(proto->has_fixed_value());
  EXPECT_TRUE(proto->has_string_value());
  EXPECT_EQ(proto->string_value(), "test_1234567890");
}

TEST(SaneOptionToProto, ButtonOptionToProto) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_BUTTON, 0), 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_BUTTON);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_FALSE(proto->has_int_value());
  EXPECT_FALSE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
}

TEST(SaneOptionToProto, GroupOptionToProto) {
  SaneOption option(CreateDescriptor("Test Name", SANE_TYPE_GROUP, 0), 1);

  auto proto = option.ToScannerOption();
  ASSERT_TRUE(proto);
  EXPECT_EQ(proto->option_type(), OptionType::TYPE_GROUP);

  EXPECT_FALSE(proto->has_bool_value());
  EXPECT_FALSE(proto->has_int_value());
  EXPECT_FALSE(proto->has_fixed_value());
  EXPECT_FALSE(proto->has_string_value());
}

TEST(SaneOptionToProto, UnitMapping) {
  std::unordered_map<SANE_Unit, OptionUnit> expected = {
      {SANE_UNIT_NONE, OptionUnit::UNIT_NONE},
      {SANE_UNIT_PIXEL, OptionUnit::UNIT_PIXEL},
      {SANE_UNIT_BIT, OptionUnit::UNIT_BIT},
      {SANE_UNIT_MM, OptionUnit::UNIT_MM},
      {SANE_UNIT_DPI, OptionUnit::UNIT_DPI},
      {SANE_UNIT_PERCENT, OptionUnit::UNIT_PERCENT},
      {SANE_UNIT_MICROSECOND, OptionUnit::UNIT_MICROSECOND},
  };

  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  for (auto kv : expected) {
    desc.unit = kv.first;
    SaneOption option(desc, 1);

    auto proto = option.ToScannerOption();
    ASSERT_TRUE(proto);
    EXPECT_EQ(proto->unit(), kv.second);
  }
}

}  // namespace lorgnette
