// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_collection_status.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::AllOf;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::StartsWith;

TEST(CrashCollectionStatusTest, IsSuccessCode) {
  EXPECT_TRUE(IsSuccessCode(CrashCollectionStatus::kSuccess));
  EXPECT_FALSE(IsSuccessCode(CrashCollectionStatus::kUnknownStatus));
  EXPECT_FALSE(IsSuccessCode(CrashCollectionStatus::kOutOfCapacity));
}

TEST(CrashCollectionStatusTest, IsSuccessCode_AllValues) {
  for (int i = 0;
       i <= static_cast<int>(CrashCollectionStatus::kLastSuccessCode); ++i) {
    EXPECT_TRUE(IsSuccessCode(static_cast<CrashCollectionStatus>(i)));
  }
  for (int i = static_cast<int>(CrashCollectionStatus::kFirstErrorValue);
       i <= static_cast<int>(CrashCollectionStatus::kMaxValue); ++i) {
    EXPECT_FALSE(IsSuccessCode(static_cast<CrashCollectionStatus>(i)));
  }
}

TEST(CrashCollectionStatusTest, CrashCollectionToString_KnownValues) {
  EXPECT_EQ(CrashCollectionStatusToString(CrashCollectionStatus::kSuccess),
            "Success");
  EXPECT_EQ(
      CrashCollectionStatusToString(CrashCollectionStatus::kUnknownStatus),
      "Unknown Status");
  EXPECT_EQ(
      CrashCollectionStatusToString(CrashCollectionStatus::kOutOfCapacity),
      "Out of capacity");
  EXPECT_EQ(
      CrashCollectionStatusToString(CrashCollectionStatus::kInvalidPayloadName),
      "Payload had invalid name");
  EXPECT_EQ(
      CrashCollectionStatusToString(CrashCollectionStatus::kInvalidCrashType),
      "Invalid crash type");
  EXPECT_EQ(
      CrashCollectionStatusToString(CrashCollectionStatus::kBadProcessState),
      "Bad process_status");
  EXPECT_EQ(CrashCollectionStatusToString(
                CrashCollectionStatus::kCoreCollectorFailed),
            "Failure running core collector");
}

TEST(CrashCollectionStatusTest, CrashCollectionToString_AllValuesListed) {
  // Note: If you remove values from the CrashCollectionStatus enum, you'll
  // need to skip them in the loops below. Just add a set of values to skip.
  for (int i = 0;
       i <= static_cast<int>(CrashCollectionStatus::kLastSuccessCode); ++i) {
    EXPECT_THAT(
        CrashCollectionStatusToString(static_cast<CrashCollectionStatus>(i)),
        AllOf(Not(StartsWith("Invalid status enum")), Not(IsEmpty())));
  }
  for (int i = static_cast<int>(CrashCollectionStatus::kFirstErrorValue);
       i <= static_cast<int>(CrashCollectionStatus::kMaxValue); ++i) {
    EXPECT_THAT(
        CrashCollectionStatusToString(static_cast<CrashCollectionStatus>(i)),
        AllOf(Not(StartsWith("Invalid status enum")), Not(IsEmpty())));
  }
}

TEST(CrashCollectionStatusTest,
     CrashCollectionToString_InvalidValuesDontCrash) {
  const int kAfterSuccess =
      static_cast<int>(CrashCollectionStatus::kLastSuccessCode) + 1;
  EXPECT_THAT(CrashCollectionStatusToString(
                  static_cast<CrashCollectionStatus>(kAfterSuccess)),
              StartsWith("Invalid status enum"));
  const int kBeforeFailure =
      static_cast<int>(CrashCollectionStatus::kFirstErrorValue) - 1;
  EXPECT_THAT(CrashCollectionStatusToString(
                  static_cast<CrashCollectionStatus>(kBeforeFailure)),
              StartsWith("Invalid status enum"));
  const int kAfterFailure =
      static_cast<int>(CrashCollectionStatus::kMaxValue) + 1;
  EXPECT_THAT(CrashCollectionStatusToString(
                  static_cast<CrashCollectionStatus>(kAfterFailure)),
              StartsWith("Invalid status enum"));
}
