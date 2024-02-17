// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_collector_names.h"

#include <string>

#include <gtest/gtest.h>

TEST(CrashReporterCollectorNameTest, CorrectNames) {
  // Some simple, hand-crafted, hard-coded tests that we get the names we
  // expect.
  EXPECT_STREQ(GetNameForCollector(CrashReporterCollector::kChrome), "chrome");
  EXPECT_STREQ(GetNameForCollector(CrashReporterCollector::kMock), "mock");
  EXPECT_STREQ(GetNameForCollector(CrashReporterCollector::kArcvmKernel),
               "ARCVM_kernel");
}

TEST(CrashReporterCollectorNameTest, CorrectEnums) {
  // Some simple, hand-crafted, hard-coded tests that we get the enum values we
  // expect.
  EXPECT_EQ(GetCollectorForName("chrome"), CrashReporterCollector::kChrome);
  EXPECT_EQ(GetCollectorForName("mock"), CrashReporterCollector::kMock);
  EXPECT_EQ(GetCollectorForName("ARCVM_kernel"),
            CrashReporterCollector::kArcvmKernel);
}

TEST(CrashReporterCollectorNameTest, NamesMapBackToEnums) {
  for (int i = 0; i <= static_cast<int>(CrashReporterCollector::kMaxValue);
       ++i) {
    auto collector = static_cast<CrashReporterCollector>(i);
    const char* name = GetNameForCollector(collector);

    // ASSERT_NE instead of EXPECT_NE because constructing a string from a
    // nullptr later is undefined behavior.
    ASSERT_NE(name, nullptr);
    EXPECT_STRNE(name, "");
    EXPECT_STRNE(name, "bad_collector_enum");

    // Make a string to prove GetCollectorForName() isn't doing pointer
    // comparisons.
    std::string name_string(name);
    EXPECT_EQ(collector, GetCollectorForName(name_string));
  }
}

TEST(CrashReporterCollectorNameTest, BadEnumValuesDontCrash) {
  const char* name = GetNameForCollector(static_cast<CrashReporterCollector>(
      static_cast<int>(CrashReporterCollector::kMaxValue) + 1));
  ASSERT_NE(name, nullptr);
  EXPECT_STRNE(name, "");
  name = GetNameForCollector(static_cast<CrashReporterCollector>(-1));
  ASSERT_NE(name, nullptr);
  EXPECT_STRNE(name, "");
}

TEST(CrashReporterCollectorNameTest, BadNamesDontCrash) {
  EXPECT_EQ(GetCollectorForName("not a collector"),
            CrashReporterCollector::kUnknownCollector);
}
