// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/json/json_reader.h>
#include <base/values.h>
#include <brillo/map_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_result_checker.h"

namespace runtime_probe {

TEST(ProbeResultCheckerTest, TestFromValue) {
  const auto json_string = R"({
    "string_field": [true, "str"],
    "string_field_exact_match": [true, "str", "!eq xx[yy"],
    "string_field_with_validate_rule": [true, "str", "!re hello_.*"],
    "int_field": [true, "int"],
    "double_field": [true, "double"],
    "hex_field": [false, "hex"]
  })";
  auto dict_value = base::JSONReader::Read(json_string);
  ASSERT_TRUE(dict_value.has_value());
  ASSERT_TRUE(dict_value->is_dict());

  auto expect_fields = ProbeResultChecker::FromValue(*dict_value);
  ASSERT_TRUE(expect_fields.get());

  const auto& required = expect_fields->required_fields_;
  ASSERT_THAT(brillo::GetMapKeys(required),
              ::testing::UnorderedElementsAre("string_field",
                                              "string_field_exact_match",
                                              "string_field_with_validate_rule",
                                              "int_field", "double_field"));
  ASSERT_TRUE(
      dynamic_cast<StringFieldConverter*>(required.at("string_field").get()));
  ASSERT_TRUE(dynamic_cast<StringFieldConverter*>(
      required.at("string_field_exact_match").get()));
  ASSERT_TRUE(dynamic_cast<StringFieldConverter*>(
      required.at("string_field_exact_match").get()));
  ASSERT_TRUE(dynamic_cast<StringFieldConverter*>(
      required.at("string_field_with_validate_rule").get()));
  ASSERT_TRUE(
      dynamic_cast<IntegerFieldConverter*>(required.at("int_field").get()));
  ASSERT_TRUE(
      dynamic_cast<DoubleFieldConverter*>(required.at("double_field").get()));

  const auto& optional = expect_fields->optional_fields_;
  ASSERT_THAT(brillo::GetMapKeys(optional),
              ::testing::UnorderedElementsAre("hex_field"));
  ASSERT_TRUE(dynamic_cast<HexFieldConverter*>(optional.at("hex_field").get()));
}

TEST(ProbeResultCheckerTest, TestApplySuccess) {
  const auto expect_string = R"({
    "str": [true, "str"],
    "int": [true, "int"],
    "hex": [true, "hex"],
    "double": [true, "double"]
  })";

  const auto probe_result_string = R"({
    "str": "string result",
    "int": "1024",
    "hex": "0x7b",
    "double": "1e2"
  })";

  auto expect = base::JSONReader::Read(expect_string);
  ASSERT_TRUE(expect.has_value());
  ASSERT_TRUE(expect->is_dict());

  auto probe_result = base::JSONReader::Read(probe_result_string);
  ASSERT_TRUE(probe_result.has_value());
  ASSERT_TRUE(probe_result->is_dict());

  auto checker = ProbeResultChecker::FromValue(*expect);

  ASSERT_TRUE(checker->Apply(&*probe_result));

  auto* str_value = probe_result->FindStringKey("str");
  ASSERT_NE(str_value, nullptr);
  ASSERT_EQ(*str_value, "string result");

  auto int_value = probe_result->FindIntKey("int");
  ASSERT_TRUE(int_value.has_value());
  ASSERT_EQ(*int_value, 1024);

  auto* hex_value = probe_result->FindStringKey("hex");
  ASSERT_NE(hex_value, nullptr);
  ASSERT_EQ(*hex_value, "123");

  auto double_value = probe_result->FindDoubleKey("double");
  ASSERT_TRUE(double_value.has_value());
  ASSERT_EQ(*double_value, 100);
}

TEST(ProbeResultCheckerTest, TestApplyWithLimitsSuccess) {
  const auto expect_string = R"({
    "str": [true, "str", "!eq string result"],
    "int": [true, "int", "!gt 1000"],
    "hex": [true, "hex", "!ne 0x0"],
    "double": [true, "double", "!lt 1e3"]
  })";

  const auto probe_result_string = R"({
    "str": "string result",
    "int": "1024",
    "hex": "0x7b",
    "double": "1e2"
  })";

  auto expect = base::JSONReader::Read(expect_string);
  ASSERT_TRUE(expect.has_value());
  ASSERT_TRUE(expect->is_dict());

  auto probe_result = base::JSONReader::Read(probe_result_string);
  ASSERT_TRUE(probe_result.has_value());
  ASSERT_TRUE(probe_result->is_dict());

  auto checker = ProbeResultChecker::FromValue(*expect);

  ASSERT_TRUE(checker->Apply(&*probe_result));

  auto* str_value = probe_result->FindStringKey("str");
  ASSERT_NE(str_value, nullptr);
  ASSERT_EQ(*str_value, "string result");

  auto int_value = probe_result->FindIntKey("int");
  ASSERT_TRUE(int_value.has_value());
  ASSERT_EQ(*int_value, 1024);

  auto* hex_value = probe_result->FindStringKey("hex");
  ASSERT_NE(hex_value, nullptr);
  ASSERT_EQ(*hex_value, "123");

  auto double_value = probe_result->FindDoubleKey("double");
  ASSERT_TRUE(double_value.has_value());
  ASSERT_EQ(*double_value, 100);
}

TEST(ProbeResultCheckerTest, TestApplyWithLimitsFail) {
  // For each field converter, |TestValidateRule| should already check each kind
  // of operators.  This function only checks if |Apply| function would return
  // |false| if any of the fields is invalid.
  const auto expect_string = R"({
    "str": [true, "str", "!eq string result"],
    "int": [true, "int", "!gt 1000"],
    "hex": [true, "hex", "!ne 0x0"],
    "double": [true, "double", "!lt 1e3"]
  })";
  const auto probe_result_string = R"({
    "str": "This doesn't match!",
    "int": "1024",
    "hex": "0x7b",
    "double": "1e2"
  })";

  auto expect = base::JSONReader::Read(expect_string);
  ASSERT_TRUE(expect.has_value());
  ASSERT_TRUE(expect->is_dict());

  auto probe_result = base::JSONReader::Read(probe_result_string);
  ASSERT_TRUE(probe_result.has_value());
  ASSERT_TRUE(probe_result->is_dict());

  auto checker = ProbeResultChecker::FromValue(*expect);

  ASSERT_FALSE(checker->Apply(&*probe_result));
}

}  // namespace runtime_probe
