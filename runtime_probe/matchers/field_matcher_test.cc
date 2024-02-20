// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

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

}  // namespace
}  // namespace runtime_probe::internal
