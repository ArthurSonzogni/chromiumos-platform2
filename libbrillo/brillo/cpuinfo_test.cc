// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libbrillo/brillo/cpuinfo.h"

#include <string>

#include <gtest/gtest.h>

#include "base/strings/stringprintf.h"

namespace brillo {

TEST(CpuInfoTest, SingleEmptyRecord) {
  auto c =
      CpuInfo::Create(base::FilePath("testdata/cpuinfo/SingleEmptyRecord.txt"));
  ASSERT_TRUE(c.has_value());

  ASSERT_EQ(c->NumProcRecords(), 1);

  ASSERT_TRUE(c->LookUp(0, "a").has_value());
  EXPECT_EQ(c->LookUp(0, "a").value(), "");
}

TEST(CpuInfoTest, SingleRecord) {
  auto c = CpuInfo::Create(base::FilePath("testdata/cpuinfo/SingleRecord.txt"));
  ASSERT_TRUE(c.has_value());

  ASSERT_EQ(c->NumProcRecords(), 1);

  ASSERT_TRUE(c->LookUp(0, "a").has_value());
  EXPECT_EQ(c->LookUp(0, "a").value(), "0x42");
}

TEST(CpuInfoTest, SingleMixedRecord) {
  auto c =
      CpuInfo::Create(base::FilePath("testdata/cpuinfo/SingleMixedRecord.txt"));
  ASSERT_TRUE(c.has_value());

  ASSERT_EQ(c->NumProcRecords(), 1);

  ASSERT_TRUE(c->LookUp(0, "a").has_value());
  EXPECT_EQ(c->LookUp(0, "a").value(), "0x42");

  ASSERT_TRUE(c->LookUp(0, "bb").has_value());
  EXPECT_EQ(c->LookUp(0, "bb").value(), "42");

  ASSERT_TRUE(c->LookUp(0, "ccc").has_value());
  EXPECT_EQ(c->LookUp(0, "ccc").value(), "41.99");

  ASSERT_TRUE(c->LookUp(0, "dddddddd").has_value());
  EXPECT_EQ(c->LookUp(0, "dddddddd").value(), "foo bar");

  ASSERT_TRUE(c->LookUp(0, "e").has_value());
  EXPECT_EQ(c->LookUp(0, "e").value(), "");
}

TEST(CpuInfoTest, TwoRecords) {
  auto c = CpuInfo::Create(base::FilePath("testdata/cpuinfo/TwoRecords.txt"));
  ASSERT_TRUE(c.has_value());

  ASSERT_EQ(c->NumProcRecords(), 2);

  ASSERT_TRUE(c->LookUp(0, "a").has_value());
  EXPECT_EQ(c->LookUp(0, "a").value(), "0x42");

  ASSERT_TRUE(c->LookUp(1, "a").has_value());
  EXPECT_EQ(c->LookUp(1, "a").value(), "0x24");
}

TEST(CpuInfoTest, BadProcNum) {
  auto c = CpuInfo::Create(base::FilePath("testdata/cpuinfo/BadProcNum.txt"));
  ASSERT_TRUE(c.has_value());

  EXPECT_FALSE(c->LookUp(2, "a").has_value());
}

TEST(CpuInfoTest, Badkey) {
  auto c = CpuInfo::Create(base::FilePath("testdata/cpuinfo/BadKey.txt"));
  ASSERT_TRUE(c.has_value());

  EXPECT_FALSE(c->LookUp(1, "b").has_value());
}

TEST(CpuInfoTest, NotKeyValuePair) {
  auto c =
      CpuInfo::Create(base::FilePath("testdata/cpuinfo/NotKeyValuePair.txt"));
  EXPECT_FALSE(c.has_value());
}

TEST(CpuInfoTest, EmptyKey) {
  auto c = CpuInfo::Create(base::FilePath("testdata/cpuinfo/EmptyKey.txt"));
  EXPECT_FALSE(c.has_value());
}

TEST(CpuInfoTest, RealX86) {
  auto c = CpuInfo::Create(base::FilePath("testdata/cpuinfo/RealX86.txt"));
  ASSERT_TRUE(c.has_value());

  ASSERT_EQ(c->NumProcRecords(), 2);

  for (int i = 0; i < c->NumProcRecords(); i++) {
    ASSERT_TRUE(c->LookUp(i, "processor").has_value());
    EXPECT_EQ(c->LookUp(i, "processor").value(), base::StringPrintf("%d", i));

    ASSERT_TRUE(c->LookUp(i, "microcode").has_value());
    EXPECT_EQ(c->LookUp(i, "microcode").value(), "0x38");

    ASSERT_TRUE(c->LookUp(i, "cpu MHz").has_value());
    EXPECT_EQ(c->LookUp(i, "cpu MHz").value(),
              i == 0 ? "1601.569" : "2347.164");

    ASSERT_TRUE(c->LookUp(i, "model name").has_value());
    EXPECT_EQ(c->LookUp(i, "model name").value(),
              "Intel(R) Celeron(R) N4000 CPU @ 1.10GHz");

    ASSERT_TRUE(c->LookUp(i, "bogomips").has_value());
    EXPECT_EQ(c->LookUp(i, "bogomips").value(), "2188.80");

    ASSERT_TRUE(c->LookUp(i, "power management").has_value());
    EXPECT_EQ(c->LookUp(i, "power management").value(), "");
  }
}

}  // namespace brillo
