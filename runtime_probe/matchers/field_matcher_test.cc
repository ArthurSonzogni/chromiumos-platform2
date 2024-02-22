// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <base/strings/string_util.h>
#include <base/values.h>

#include "runtime_probe/matchers/field_matcher.h"

namespace runtime_probe::internal {
namespace {

constexpr char kField[] = "field";

template <typename T>
auto MakeMatcher(std::string_view field_value) {
  return T::Create(kField, std::string{field_value});
}

base::Value::Dict MakeComponent(std::string_view field_value) {
  base::Value::Dict res;
  res.Set(kField, field_value);
  return res;
}

TEST(StringEqualMatcherTest, Match) {
  EXPECT_TRUE(
      MakeMatcher<StringEqualMatcher>("value")->Match(MakeComponent("value")));
  EXPECT_FALSE(MakeMatcher<StringEqualMatcher>("value")->Match(
      MakeComponent("not_value")));
}

class IntegerEqualMathcerTest
    : public testing::TestWithParam<
          std::pair<std::string_view, std::string_view>> {};

TEST_P(IntegerEqualMathcerTest, Match) {
  EXPECT_TRUE(MakeMatcher<IntegerEqualMatcher>(GetParam().first)
                  ->Match(MakeComponent(GetParam().second)));
  EXPECT_TRUE(MakeMatcher<IntegerEqualMatcher>(GetParam().second)
                  ->Match(MakeComponent(GetParam().first)));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    IntegerEqualMathcerTest,
    testing::ValuesIn(
        std::vector<std::pair<std::string_view, std::string_view>>{
            {"123", "123"},
            {"123", "  123   "},
            {"123", "  000123  "},
            {"-123", "-123"},
            {"-123", "  -123  "},
            {"-123", "  -000123  "},
            {"0", "0"},
            {"0", "000"},
            {"0", "-000"},
            {"0", "  000  "},
            {"0", "  -000  "}}));

class IntegerEqualMathcerInvalidTest
    : public testing::TestWithParam<std::string_view> {};

TEST_P(IntegerEqualMathcerInvalidTest, NotMatch) {
  EXPECT_FALSE(MakeMatcher<IntegerEqualMatcher>(GetParam()));
  EXPECT_FALSE(MakeMatcher<IntegerEqualMatcher>("123")->Match(
      MakeComponent(GetParam())));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    IntegerEqualMathcerInvalidTest,
    testing::Values("", "   ", "-", "  -  ", "abc", "-abc", "123a"));

}  // namespace
}  // namespace runtime_probe::internal
