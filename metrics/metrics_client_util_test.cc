// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metrics/metrics_client_util.h"

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;

namespace metrics_client {
TEST(MetricsClientUtilTest, ShowUsageDoesntCrash) {
  // Not much else to test, really.
  ShowUsage(stderr);
}

TEST(MetricsClientUtilTest, ParseStringStructuredMetricsArgReturnsArg) {
  EXPECT_THAT(ParseStringStructuredMetricsArg("hello hello"),
              Optional(std::string("hello hello")));
  EXPECT_THAT(ParseStringStructuredMetricsArg(""), Optional(std::string()));
}

TEST(MetricsClientUtilTest, ParseIntStructuredMetricsArg_WorksOnValidInput) {
  EXPECT_THAT(ParseIntStructuredMetricsArg("1234"), Optional(1234));
  EXPECT_THAT(ParseIntStructuredMetricsArg("0"), Optional(0));
  EXPECT_THAT(ParseIntStructuredMetricsArg("-20"), Optional(-20));
}

TEST(MetricsClientUtilTest,
     ParseIntStructuredMetricsArg_ReturnsNulloptOnBadInput) {
  EXPECT_THAT(ParseIntStructuredMetricsArg("hello"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntStructuredMetricsArg(""), Eq(std::nullopt));
  EXPECT_THAT(ParseIntStructuredMetricsArg("123abc"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntStructuredMetricsArg("  123"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntStructuredMetricsArg("123  "), Eq(std::nullopt));
  EXPECT_THAT(ParseIntStructuredMetricsArg("abc123"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntStructuredMetricsArg("16.0"), Eq(std::nullopt));
}

TEST(MetricsClientUtilTest, ParseDoubleStructuredMetricsArg_WorksOnValidInput) {
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("1234"), Optional(1234.0));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("0"), Optional(0.0));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("-20"), Optional(-20.0));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("16.5"), Optional(16.5));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("16."), Optional(16.0));
}

TEST(MetricsClientUtilTest,
     ParseDoubleStructuredMetricsArg_ReturnsNulloptOnBadInput) {
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("hello"), Eq(std::nullopt));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg(""), Eq(std::nullopt));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("123abc"), Eq(std::nullopt));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("  123"), Eq(std::nullopt));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("123  "), Eq(std::nullopt));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("abc123"), Eq(std::nullopt));
  EXPECT_THAT(ParseDoubleStructuredMetricsArg("16.q"), Eq(std::nullopt));
}

TEST(MetricsClientUtilTest,
     ParseIntArrayStructuredMetricsArg_WorksOnValidInput) {
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1234"),
              Optional(ElementsAre(1234)));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1,2,3,4"),
              Optional(ElementsAre(1, 2, 3, 4)));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1,-2,3,-4"),
              Optional(ElementsAre(1, -2, 3, -4)));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg(""), Optional(IsEmpty()));
}

TEST(MetricsClientUtilTest,
     ParseIntArrayStructuredMetricsArgReturnsNulloptOnBadInput) {
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("hello"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1,2,3,q"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1,q,3,4"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("q,2,3,4"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1,2,3,"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg("1,,3,4"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg(",2,3,4"), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg(","), Eq(std::nullopt));
  EXPECT_THAT(ParseIntArrayStructuredMetricsArg(",,"), Eq(std::nullopt));
}

}  // namespace metrics_client
