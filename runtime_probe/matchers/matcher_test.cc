// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <string_view>
#include <utility>

#include <base/values.h>
#include <gtest/gtest.h>

#include <base/json/json_reader.h>
#include <base/strings/string_util.h>

#include "runtime_probe/matchers/matcher.h"

namespace runtime_probe::matchers {
namespace {

base::Value::Dict MakeDictValue(std::string_view str) {
  auto res = base::JSONReader::Read(str);
  CHECK(res.has_value() && res->is_dict());
  return std::move(res->GetDict());
}

std::unique_ptr<Matcher> MakeMatcher(std::string_view str) {
  return Matcher::FromValue(MakeDictValue(str));
}

TEST(MatchersTest, MissingFields) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operand": []
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "STRING_EQUAL"
                              }
                            )JSON"));
}

TEST(MatchersTest, OperatorMustBeString) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": 123,
                                "operand": []
                              }
                            )JSON"));
}

TEST(MatchersTest, OperandMustBeList) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "STRING_EQUAL",
                                "operand": {}
                              }
                            )JSON"));
}

TEST(MatchersTest, UnknownOperator) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "UNKNOWN",
                                "operand": []
                              }
                            )JSON"));
}

}  // namespace
}  // namespace runtime_probe::matchers
