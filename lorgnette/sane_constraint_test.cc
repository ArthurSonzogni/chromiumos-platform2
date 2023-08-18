// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_constraint.h"

#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/test_util.h"

using ::testing::ElementsAre;

namespace lorgnette {

namespace {

SANE_Option_Descriptor CreateDescriptor(const char* name,
                                        SANE_Value_Type type,
                                        int size) {
  SANE_Option_Descriptor desc;
  memset(&desc, 0, sizeof(desc));
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

TEST(SaneConstraintTest, NonConstraintReturnsNone) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_NONE;
  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_NONE);
}

TEST(SaneConstraintTest, IntRangeConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  SANE_Range range = {10, 20, 1};
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);
}

TEST(SaneConstraintTest, NoWordListFromStringListConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {nullptr};
  desc.constraint.string_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_STRING_LIST);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  EXPECT_FALSE(values.has_value());
}

TEST(SaneConstraintTest, EmptyWordList) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {0};
  desc.constraint.word_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_WORD_LIST);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 0);
}

TEST(SaneConstraintTest, NonEmptyWordListFixed) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, SANE_FIX(0), SANE_FIX(729.0),
                                         SANE_FIX(3682.34), SANE_FIX(15)};
  desc.constraint.word_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_WORD_LIST);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({0, 729, 3682, 15}));
}

TEST(SaneConstraintTest, NonEmptyWordListInt) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, 0, 729, 368234, 15};
  desc.constraint.word_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_WORD_LIST);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({0, 729, 368234, 15}));
}

TEST(SaneConstraintTest, NoStringListFromRangeConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<std::vector<std::string>> values =
      constraint->GetValidStringOptionValues();
  EXPECT_FALSE(values.has_value());
}

TEST(SaneConstraintTest, IntListFromEmptyRange) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 5;
  range.max = 4;
  range.quant = 1;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 0);
}

TEST(SaneConstraintTest, IntListFromSingleStepRangeFixed) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = SANE_FIX(5);
  range.max = SANE_FIX(11);
  range.quant = SANE_FIX(1.2);
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({5, 6, 7, 8, 9, 10}));
}

TEST(SaneConstraintTest, IntListFromSingleStepRangeInt) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 5;
  range.max = 11;
  range.quant = 1;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({5, 6, 7, 8, 9, 10, 11}));
}

TEST(SaneConstraintTest, IntListFromFourStepRangeFixed) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = SANE_FIX(13);
  range.max = SANE_FIX(28);
  range.quant = SANE_FIX(4);
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({13, 17, 21, 25}));
}

TEST(SaneConstraintTest, IntListFromFourStepRangeInt) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 13;
  range.max = 28;
  range.quant = 4;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<std::vector<uint32_t>> values =
      constraint->GetValidIntOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(), std::vector<uint32_t>({13, 17, 21, 25}));
}

TEST(SaneConstraintTest, NoStringListFromWordListConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, 0, 729, 368234, 15};
  desc.constraint.word_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_WORD_LIST);

  std::optional<std::vector<std::string>> values =
      constraint->GetValidStringOptionValues();
  EXPECT_FALSE(values.has_value());
}

TEST(SaneConstraintTest, EmptyStringList) {
  auto desc =
      CreateDescriptor("Test Name", SANE_TYPE_STRING, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {nullptr};
  desc.constraint.string_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_STRING_LIST);

  std::optional<std::vector<std::string>> values =
      constraint->GetValidStringOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value().size(), 0);
}

TEST(SaneConstraintTest, NonEmptyStringList) {
  auto desc =
      CreateDescriptor("Test Name", SANE_TYPE_STRING, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {"Color", "Gray", "Lineart",
                                                 nullptr};
  desc.constraint.string_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_STRING_LIST);

  std::optional<std::vector<std::string>> values =
      constraint->GetValidStringOptionValues();
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values.value(),
            std::vector<std::string>({"Color", "Gray", "Lineart"}));
}

TEST(SaneConstraintTest, InvalidConstraint) {
  auto desc =
      CreateDescriptor("Test Name", SANE_TYPE_STRING, sizeof(SANE_Word));

  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  EXPECT_FALSE(SaneConstraint::Create(desc).has_value());

  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  EXPECT_FALSE(SaneConstraint::Create(desc).has_value());

  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  EXPECT_FALSE(SaneConstraint::Create(desc).has_value());
}

TEST(SaneConstraintTest, NoRangeFromStringValueTypes) {
  auto desc =
      CreateDescriptor("Test Name", SANE_TYPE_STRING, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 13;
  range.max = 28;
  range.quant = 4;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);
  EXPECT_FALSE(constraint->GetOptionRange().has_value());
}

TEST(SaneConstraintTest, NoRangeFromBoolValueTypes) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_BOOL, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 13;
  range.max = 28;
  range.quant = 4;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);
  EXPECT_FALSE(constraint->GetOptionRange().has_value());
}

TEST(SaneConstraintTest, RangeFromValidFixedValue) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = SANE_FIX(2.3);
  range.max = SANE_FIX(4.9);
  range.quant = SANE_FIX(0.1);
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<OptionRange> range_result = constraint->GetOptionRange();
  ASSERT_TRUE(range_result.has_value());
  EXPECT_NEAR(range_result.value().start, 2.3, 1e-4);
  EXPECT_NEAR(range_result.value().size, 2.6, 1e-4);
}

TEST(SaneConstraintTest, RangeFromValidIntValue) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range;
  range.min = 3;
  range.max = 27;
  range.quant = 1;
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  std::optional<OptionRange> range_result = constraint->GetOptionRange();
  EXPECT_TRUE(range_result.has_value());
  EXPECT_NEAR(range_result.value().start, 3, 1e-4);
  EXPECT_NEAR(range_result.value().size, 24, 1e-4);
}

TEST(SaneConstraintTest, NoneConstraintToEmptyProto) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_NONE;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_NONE);

  auto proto = constraint->ToOptionConstraint();
  ASSERT_TRUE(proto.has_value());
  EXPECT_THAT(proto.value(), EqualsProto(OptionConstraint()));
}

TEST(SaneConstraintTest, IntRangeToProtoConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range = {3 /* min */, 27 /* max */, 1 /* quant */};
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  auto proto = constraint->ToOptionConstraint();
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto->constraint_type(), OptionConstraint::CONSTRAINT_INT_RANGE);
  EXPECT_FALSE(proto->has_fixed_range());
  EXPECT_EQ(proto->valid_int_size(), 0);
  EXPECT_EQ(proto->valid_fixed_size(), 0);
  EXPECT_EQ(proto->valid_string_size(), 0);
  EXPECT_EQ(proto->int_range().min(), 3);
  EXPECT_EQ(proto->int_range().max(), 27);
  EXPECT_EQ(proto->int_range().quant(), 1);
}

TEST(SaneConstraintTest, FixedRangeToProtoConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_RANGE;
  SANE_Range range = {SANE_FIX(3.25) /* min */, SANE_FIX(27.5) /* max */,
                      SANE_FIX(1.75) /* quant */};
  desc.constraint.range = &range;

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_RANGE);

  auto proto = constraint->ToOptionConstraint();
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto->constraint_type(), OptionConstraint::CONSTRAINT_FIXED_RANGE);
  EXPECT_FALSE(proto->has_int_range());
  EXPECT_EQ(proto->valid_int_size(), 0);
  EXPECT_EQ(proto->valid_fixed_size(), 0);
  EXPECT_EQ(proto->valid_string_size(), 0);
  EXPECT_EQ(proto->fixed_range().min(), 3.25);
  EXPECT_EQ(proto->fixed_range().max(), 27.5);
  EXPECT_EQ(proto->fixed_range().quant(), 1.75);
}

TEST(SaneConstraintTest, IntListToProtoConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_INT, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, 0, 42, 368234, 314};
  desc.constraint.word_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_WORD_LIST);

  auto proto = constraint->ToOptionConstraint();
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto->constraint_type(), OptionConstraint::CONSTRAINT_INT_LIST);
  EXPECT_FALSE(proto->has_int_range());
  EXPECT_FALSE(proto->has_fixed_range());
  EXPECT_EQ(proto->valid_fixed_size(), 0);
  EXPECT_EQ(proto->valid_string_size(), 0);
  EXPECT_THAT(proto->valid_int(), ElementsAre(0, 42, 368234, 314));
}

TEST(SaneConstraintTest, FixedListToProtoConstraint) {
  auto desc = CreateDescriptor("Test Name", SANE_TYPE_FIXED, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_WORD_LIST;
  std::vector<SANE_Word> valid_values = {4, SANE_FIX(0), SANE_FIX(42.25),
                                         SANE_FIX(-3234.5), SANE_FIX(314.75)};
  desc.constraint.word_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_WORD_LIST);

  auto proto = constraint->ToOptionConstraint();
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto->constraint_type(), OptionConstraint::CONSTRAINT_FIXED_LIST);
  EXPECT_FALSE(proto->has_int_range());
  EXPECT_FALSE(proto->has_fixed_range());
  EXPECT_EQ(proto->valid_int_size(), 0);
  EXPECT_EQ(proto->valid_string_size(), 0);
  EXPECT_THAT(proto->valid_fixed(), ElementsAre(0, 42.25, -3234.5, 314.75));
}

TEST(SaneConstraintTest, StringListToProtoConstraint) {
  auto desc =
      CreateDescriptor("Test Name", SANE_TYPE_STRING, sizeof(SANE_Word));
  desc.constraint_type = SANE_CONSTRAINT_STRING_LIST;
  std::vector<SANE_String_Const> valid_values = {"Color", "Gray", "Lineart",
                                                 nullptr};
  desc.constraint.string_list = valid_values.data();

  auto constraint = SaneConstraint::Create(desc);
  ASSERT_TRUE(constraint.has_value());
  EXPECT_EQ(constraint->GetType(), SANE_CONSTRAINT_STRING_LIST);

  auto proto = constraint->ToOptionConstraint();
  ASSERT_TRUE(proto.has_value());
  EXPECT_EQ(proto->constraint_type(), OptionConstraint::CONSTRAINT_STRING_LIST);
  EXPECT_FALSE(proto->has_int_range());
  EXPECT_FALSE(proto->has_fixed_range());
  EXPECT_EQ(proto->valid_fixed_size(), 0);
  EXPECT_EQ(proto->valid_int_size(), 0);
  EXPECT_THAT(proto->valid_string(), ElementsAre("Color", "Gray", "Lineart"));
}

}  // namespace lorgnette
