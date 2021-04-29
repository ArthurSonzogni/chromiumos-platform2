// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"

namespace runtime_probe {

class Mock1ProbeFunction : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("mock1");

  static constexpr auto FromKwargsValue =
      FromEmptyKwargsValue<Mock1ProbeFunction>;
  DataType EvalImpl() const override { return {}; }
};

class Mock2ProbeFunction : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("mock2");

  static std::unique_ptr<Mock2ProbeFunction> FromKwargsValue(
      const base::Value& dict_value) {
    PARSE_BEGIN(Mock2ProbeFunction);
    PARSE_ARGUMENT(a_str);
    PARSE_ARGUMENT(a_int);
    PARSE_ARGUMENT(a_bool);
    PARSE_ARGUMENT(default_int, 1);
    PARSE_END();
  }
  DataType EvalImpl() const override { return {}; }

  std::string a_str_;
  int a_int_;
  bool a_bool_;
  int default_int_;
};

TEST(ProbeFunctionArgumentTest, EmptyArgument) {
  const base::Value empty_value(base::Value::Type::DICTIONARY);
  base::Value arg_value(base::Value::Type::DICTIONARY);
  arg_value.SetKey("a_str", base::Value("a_str"));

  auto mock_func1 = Mock1ProbeFunction::FromKwargsValue(empty_value);
  EXPECT_NE(mock_func1, nullptr);

  auto mock_func2 = Mock1ProbeFunction::FromKwargsValue(arg_value);
  EXPECT_EQ(mock_func2, nullptr);
}

TEST(ProbeFunctionArgumentTest, WithArguments) {
  const base::Value empty_value(base::Value::Type::DICTIONARY);
  base::Value arg_value(base::Value::Type::DICTIONARY);
  arg_value.SetStringKey("a_str", "a_str");
  arg_value.SetIntKey("a_int", 1);
  arg_value.SetBoolKey("a_bool", true);

  auto mock_func1 = Mock2ProbeFunction::FromKwargsValue(empty_value);
  EXPECT_EQ(mock_func1, nullptr);

  auto mock_func2 = Mock2ProbeFunction::FromKwargsValue(arg_value);
  EXPECT_NE(mock_func2, nullptr);
  EXPECT_EQ(mock_func2->default_int_, 1);

  arg_value.SetKey("default_int", base::Value(2));
  auto mock_func3 = Mock2ProbeFunction::FromKwargsValue(arg_value);
  EXPECT_NE(mock_func3, nullptr);
  EXPECT_EQ(mock_func3->default_int_, 2);

  arg_value.SetKey("invalid_field", base::Value("invalid_field"));
  auto mock_func4 = Mock2ProbeFunction::FromKwargsValue(arg_value);
  EXPECT_EQ(mock_func4, nullptr);
}

}  // namespace runtime_probe
