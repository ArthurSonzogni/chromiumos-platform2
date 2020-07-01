// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/json/json_reader.h>
#include <base/values.h>
#include <brillo/map_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_result_checker.h"

namespace runtime_probe {

typedef FieldConverter::ReturnCode ReturnCode;

TEST(StringFieldConverterTest, TestIntToString) {
  base::DictionaryValue dict_value;

  dict_value.SetInteger("key", 123);

  auto converter = StringFieldConverter::Build("");

  ASSERT_EQ(converter->Convert("key", &dict_value), ReturnCode::OK)
      << "failed to convert 123 to string";

  std::string string_value;
  ASSERT_TRUE(dict_value.GetString("key", &string_value));
  ASSERT_EQ(string_value, "123");
}

TEST(StringFieldConverterTest, TestInvalidRegexPattern) {
  auto invalid = StringFieldConverter::Build("!re hello[");
  ASSERT_EQ(invalid, nullptr);

  auto valid = StringFieldConverter::Build("!eq hello[");
  ASSERT_TRUE(valid);
}

TEST(IntegerFieldConverterTest, TestStringToInt) {
  for (const auto s : {"123", "  123", "123  ", "  123  "}) {
    base::DictionaryValue dict_value;

    dict_value.SetString("key", s);

    auto converter = IntegerFieldConverter::Build("");

    ASSERT_EQ(converter->Convert("key", &dict_value), ReturnCode::OK)
        << "failed to convert string: " << s;

    int int_value;
    ASSERT_TRUE(dict_value.GetInteger("key", &int_value));
    ASSERT_EQ(int_value, 123) << s << " is not converted to 123";
  }
}

TEST(HexFieldConverterTest, TestStringToInt) {
  for (const auto s : {"7b", "0x7b", "  0x7b", "  0x7b  ", "0x7b  "}) {
    base::DictionaryValue dict_value;

    dict_value.SetString("key", s);

    auto converter = HexFieldConverter::Build("");

    ASSERT_EQ(converter->Convert("key", &dict_value), ReturnCode::OK)
        << "failed to convert string: " << s;

    int int_value;
    ASSERT_TRUE(dict_value.GetInteger("key", &int_value));
    ASSERT_EQ(int_value, 123) << s << " is not converted to 123";
  }
}

TEST(IntegerFieldConverterTest, TestDoubleToInt) {
  double v = 123.5;
  base::DictionaryValue dict_value;

  dict_value.SetDouble("key", v);

  auto converter = IntegerFieldConverter::Build("");

  ASSERT_EQ(converter->Convert("key", &dict_value), ReturnCode::OK)
      << "failed to convert double";

  int int_value;
  ASSERT_TRUE(dict_value.GetInteger("key", &int_value));
  ASSERT_EQ(int_value, 123) << v << " is not converted to 123";
}

TEST(DoubleFieldConverterTest, TestStringToDouble) {
  for (const auto s : {"123.5", "  123.5", "123.5  ", "  123.5  "}) {
    base::DictionaryValue dict_value;

    dict_value.SetString("key", s);

    auto converter = DoubleFieldConverter::Build("");

    ASSERT_EQ(converter->Convert("key", &dict_value), ReturnCode::OK)
        << "failed to convert string: " << s;

    double double_value;
    ASSERT_TRUE(dict_value.GetDouble("key", &double_value));
    ASSERT_EQ(double_value, 123.5) << s << " is not converted to 123.5";
  }
}

TEST(DoubleFieldConverterTest, TestInvalidStringToDouble) {
  for (const auto s : {"this is not double", "", "   "}) {
    base::DictionaryValue dict_value;

    dict_value.SetString("key", s);

    auto converter = DoubleFieldConverter::Build("");

    ASSERT_EQ(converter->Convert("key", &dict_value),
              ReturnCode::INCOMPATIBLE_VALUE)
        << "Converting " << s << " to double should fail";
  }
}

TEST(StringFieldConverterTest, TestValidateRule) {
  const auto json_string = R"({
    "0": "hello world",
    "1": "hello ???",
    "2": "??? hello ???",
    "3": "??? hello"
  })";
  auto val = base::JSONReader::Read(json_string);
  base::DictionaryValue* dict_value = nullptr;
  val->GetAsDictionary(&dict_value);
  ASSERT_TRUE(dict_value);
  {
    auto converter = StringFieldConverter::Build("!ne hello world");
    ASSERT_EQ(converter->operator_, ValidatorOperator::NE);
    ASSERT_EQ(converter->regex_, nullptr);
    ASSERT_EQ(converter->operand_, "hello world");
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = StringFieldConverter::Build("!eq hello world");
    ASSERT_EQ(converter->operator_, ValidatorOperator::EQ);
    ASSERT_EQ(converter->regex_, nullptr);
    ASSERT_EQ(converter->operand_, "hello world");
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = StringFieldConverter::Build("!re hello .*");
    ASSERT_EQ(converter->operator_, ValidatorOperator::RE);
    ASSERT_EQ(converter->regex_->pattern(), "hello .*");
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = StringFieldConverter::Build("!re .* hello");
    ASSERT_EQ(converter->operator_, ValidatorOperator::RE);
    ASSERT_EQ(converter->regex_->pattern(), ".* hello");
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("3", dict_value), ReturnCode::OK);
  }
}

TEST(IntegerFieldConverterTest, TestValidateRule) {
  const auto json_string = R"({
    "0": 0,
    "1": 1,
    "2": 2
  })";
  auto val = base::JSONReader::Read(json_string);
  base::DictionaryValue* dict_value = nullptr;
  val->GetAsDictionary(&dict_value);
  ASSERT_TRUE(dict_value);
  {
    auto converter = IntegerFieldConverter::Build("!ne 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::NE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = IntegerFieldConverter::Build("!eq 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::EQ);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = IntegerFieldConverter::Build("!gt 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::GT);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = IntegerFieldConverter::Build("!ge 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::GE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = IntegerFieldConverter::Build("!lt 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::LT);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = IntegerFieldConverter::Build("!le 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::LE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
}

TEST(HexFieldConverterTest, TestValidateRule) {
  const auto json_string = R"({
    "0": 0,
    "1": 1,
    "2": 2
  })";
  auto val = base::JSONReader::Read(json_string);
  base::DictionaryValue* dict_value = nullptr;
  val->GetAsDictionary(&dict_value);
  ASSERT_TRUE(dict_value);
  {
    auto converter = HexFieldConverter::Build("!ne 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::NE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = HexFieldConverter::Build("!eq 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::EQ);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = HexFieldConverter::Build("!gt 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::GT);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = HexFieldConverter::Build("!ge 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::GE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = HexFieldConverter::Build("!lt 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::LT);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = HexFieldConverter::Build("!le 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::LE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
}

TEST(DoubleFieldConverterTest, TestValidateRule) {
  const auto json_string = R"({
    "0": 0,
    "1": 1,
    "2": 2
  })";
  auto val = base::JSONReader::Read(json_string);
  base::DictionaryValue* dict_value = nullptr;
  val->GetAsDictionary(&dict_value);
  ASSERT_TRUE(dict_value);
  {
    auto converter = DoubleFieldConverter::Build("!ne 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::NE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = DoubleFieldConverter::Build("!eq 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::EQ);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = DoubleFieldConverter::Build("!gt 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::GT);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = DoubleFieldConverter::Build("!ge 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::GE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::OK);
  }
  {
    auto converter = DoubleFieldConverter::Build("!lt 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::LT);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::INVALID_VALUE);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
  {
    auto converter = DoubleFieldConverter::Build("!le 1");
    ASSERT_EQ(converter->operator_, ValidatorOperator::LE);
    ASSERT_EQ(converter->operand_, 1);
    ASSERT_EQ(converter->Validate("0", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("1", dict_value), ReturnCode::OK);
    ASSERT_EQ(converter->Validate("2", dict_value), ReturnCode::INVALID_VALUE);
  }
}

TEST(ProbeResultCheckerTest, TestFromDictionaryValue) {
  const auto json_string = R"({
    "string_field": [true, "str"],
    "string_field_exact_match": [true, "str", "!eq xx[yy"],
    "string_field_with_validate_rule": [true, "str", "!re hello_.*"],
    "int_field": [true, "int"],
    "double_field": [true, "double"],
    "hex_field": [false, "hex"]
  })";
  auto val = base::JSONReader::Read(json_string);
  base::DictionaryValue* dict_value = nullptr;
  val->GetAsDictionary(&dict_value);
  ASSERT_TRUE(dict_value);

  auto expect_fields = ProbeResultChecker::FromDictionaryValue(*dict_value);
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
  const auto expect = R"({
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

  auto probe_result_val = base::JSONReader::Read(probe_result_string);
  base::DictionaryValue* probe_result = nullptr;
  probe_result_val->GetAsDictionary(&probe_result);

  auto expect_val = base::JSONReader::Read(expect);
  base::DictionaryValue* expect_dict_val = nullptr;
  expect_val->GetAsDictionary(&expect_dict_val);
  auto checker = ProbeResultChecker::FromDictionaryValue(*expect_dict_val);

  ASSERT_TRUE(checker->Apply(probe_result));

  std::string str_value;
  ASSERT_TRUE(probe_result->GetString("str", &str_value));
  ASSERT_EQ(str_value, "string result");

  int int_value;
  ASSERT_TRUE(probe_result->GetInteger("int", &int_value));
  ASSERT_EQ(int_value, 1024);

  int hex_value;
  ASSERT_TRUE(probe_result->GetInteger("hex", &hex_value));
  ASSERT_EQ(hex_value, 123);

  double double_value;
  ASSERT_TRUE(probe_result->GetDouble("double", &double_value));
  ASSERT_EQ(double_value, 100);
}

TEST(ProbeResultCheckerTest, TestApplyWithLimitsSuccess) {
  const auto expect = R"({
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

  auto probe_result_val = base::JSONReader::Read(probe_result_string);
  base::DictionaryValue* probe_result = nullptr;
  probe_result_val->GetAsDictionary(&probe_result);

  auto expect_val = base::JSONReader::Read(expect);
  base::DictionaryValue* expect_dict_val = nullptr;
  expect_val->GetAsDictionary(&expect_dict_val);
  auto checker = ProbeResultChecker::FromDictionaryValue(*expect_dict_val);

  ASSERT_TRUE(checker->Apply(probe_result));

  std::string str_value;
  ASSERT_TRUE(probe_result->GetString("str", &str_value));
  ASSERT_EQ(str_value, "string result");

  int int_value;
  ASSERT_TRUE(probe_result->GetInteger("int", &int_value));
  ASSERT_EQ(int_value, 1024);

  int hex_value;
  ASSERT_TRUE(probe_result->GetInteger("hex", &hex_value));
  ASSERT_EQ(hex_value, 123);

  double double_value;
  ASSERT_TRUE(probe_result->GetDouble("double", &double_value));
  ASSERT_EQ(double_value, 100);
}

TEST(ProbeResultCheckerTest, TestApplyWithLimitsFail) {
  // For each field converter, |TestValidateRule| should already check each kind
  // of operators.  This function only checks if |Apply| function would return
  // |false| if any of the fields is invalid.
  const auto expect = R"({
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
  auto probe_result_val = base::JSONReader::Read(probe_result_string);
  base::DictionaryValue* probe_result = nullptr;
  probe_result_val->GetAsDictionary(&probe_result);

  auto expect_val = base::JSONReader::Read(expect);
  base::DictionaryValue* expect_dict_val = nullptr;
  expect_val->GetAsDictionary(&expect_dict_val);
  auto checker = ProbeResultChecker::FromDictionaryValue(*expect_dict_val);

  ASSERT_FALSE(checker->Apply(probe_result));
}

}  // namespace runtime_probe
