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

TEST(MatchersTest, StringMatcherMustHave2Operands) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "STRING_EQUAL",
                                "operand": []
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "STRING_EQUAL",
                                "operand": ["field_a"]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "STRING_EQUAL",
                                "operand": ["field_a", "value_a", "value_b"]
                              }
                            )JSON"));
}

TEST(MatchersTest, IntegerMatcherMustHaveDigitalStringOperands) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "INTEGER_EQUAL",
                                "operand": []
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "INTEGER_EQUAL",
                                "operand": ["field_a"]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "INTEGER_EQUAL",
                                "operand": ["field_a", "value_a", "value_b"]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "INTEGER_EQUAL",
                                "operand": ["field_a", 123]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "INTEGER_EQUAL",
                                "operand": ["field_a", "not_int"]
                              }
                            )JSON"));
}

TEST(MatchersTest, HexMatcherMustHaveHexStringOperands) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "HEX_EQUAL",
                                "operand": []
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "HEX_EQUAL",
                                "operand": ["field_a"]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "HEX_EQUAL",
                                "operand": ["field_a", "value_a", "value_b"]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "HEX_EQUAL",
                                "operand": ["field_a", 123]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "HEX_EQUAL",
                                "operand": ["field_a", "not_hex"]
                              }
                            )JSON"));
}

TEST(MatchersTest, LogicalMatcherMustHaveOperand) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "AND",
                                "operand": []
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "OR",
                                "operand": []
                              }
                            )JSON"));
}

TEST(MatchersTest, LogicalMatcherOperandMustBeMatchers) {
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "AND",
                                "operand": ["no_a_matcher"]
                              }
                            )JSON"));
  EXPECT_FALSE(MakeMatcher(R"JSON(
                              {
                                "operator": "OR",
                                "operand": ["not_a_matcher"]
                              }
                            )JSON"));
}

TEST(MatchersTest, StringMatcher) {
  auto matcher = Matcher::FromValue(MakeDictValue(R"JSON(
      {
        "operator": "STRING_EQUAL",
        "operand": ["field_a", "value_a"]
      }
    )JSON"));
  EXPECT_TRUE(matcher);
  EXPECT_TRUE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "value_a"
      }
    )JSON")));
  // Wrong value
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "value_b"
      }
    )JSON")));
  // Field not found
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {}
    )JSON")));
}

TEST(MatchersTest, IntegerMatcher) {
  auto matcher = Matcher::FromValue(MakeDictValue(R"JSON(
      {
        "operator": "INTEGER_EQUAL",
        "operand": ["field_a", "123"]
      }
    )JSON"));
  EXPECT_TRUE(matcher);
  EXPECT_TRUE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "00123"
      }
    )JSON")));
  // Wrong value
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "456"
      }
    )JSON")));
  // Not integer
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "not int"
      }
    )JSON")));
  // Field not found
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {}
    )JSON")));
}

TEST(MatchersTest, HexMatcher) {
  auto matcher = Matcher::FromValue(MakeDictValue(R"JSON(
      {
        "operator": "HEX_EQUAL",
        "operand": ["field_a", "0x1a2b"]
      }
    )JSON"));
  EXPECT_TRUE(matcher);
  EXPECT_TRUE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "1A2B"
      }
    )JSON")));
  // Wrong value
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "3C4D"
      }
    )JSON")));
  // Not hex
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "not hex"
      }
    )JSON")));
  // Field not found
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {}
    )JSON")));
}

TEST(MatchersTest, AndMatcher) {
  auto matcher = Matcher::FromValue(MakeDictValue(R"JSON(
      {
        "operator": "AND",
        "operand": [
          {
            "operator": "STRING_EQUAL",
            "operand": ["field_a", "value_a"]
          },
          {
            "operator": "STRING_EQUAL",
            "operand": ["field_b", "value_b"]
          }
        ]
      }
    )JSON"));
  EXPECT_TRUE(matcher);
  EXPECT_TRUE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "value_a",
        "field_b": "value_b"
      }
    )JSON")));
  // One matcher not match.
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "value_a"
      }
    )JSON")));
  // All matchers not match.
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {}
    )JSON")));
}

TEST(MatchersTest, OrMatcher) {
  auto matcher = Matcher::FromValue(MakeDictValue(R"JSON(
      {
        "operator": "OR",
        "operand": [
          {
            "operator": "STRING_EQUAL",
            "operand": ["field_a", "value_a"]
          },
          {
            "operator": "STRING_EQUAL",
            "operand": ["field_b", "value_b"]
          }
        ]
      }
    )JSON"));
  EXPECT_TRUE(matcher);
  EXPECT_TRUE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "value_a",
        "field_b": "value_b"
      }
    )JSON")));
  // One matcher not match.
  EXPECT_TRUE(matcher->Match(MakeDictValue(R"JSON(
      {
        "field_a": "value_a"
      }
    )JSON")));
  // All matchers not match.
  EXPECT_FALSE(matcher->Match(MakeDictValue(R"JSON(
      {}
    )JSON")));
}

}  // namespace
}  // namespace runtime_probe::matchers
