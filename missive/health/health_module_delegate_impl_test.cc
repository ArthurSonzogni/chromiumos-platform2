// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/health/health_module_delegate_impl.h"

#include <string_view>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/strings/strcat.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/health/health_module_delegate.h"
#include "missive/util/file.h"

using ::testing::StrEq;

namespace reporting {
namespace {

constexpr char kBaseFileOne[] = "base";
constexpr uint32_t kDefaultCallSize = 10u;
constexpr uint32_t kRepeatedPtrFieldSizeOverhead = 2u;
constexpr uint32_t kMaxWriteCount = 10u;
constexpr uint32_t kMaxStorage =
    kMaxWriteCount * (kRepeatedPtrFieldSizeOverhead + kDefaultCallSize);
constexpr uint32_t kTinyStorage = 2u;

constexpr char kHexCharLookup[0x10] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};

std::string BytesToHexString(std::string_view bytes) {
  std::string result;
  for (char byte : bytes) {
    result.push_back(kHexCharLookup[(byte >> 4) & 0xf]);
    result.push_back(kHexCharLookup[byte & 0xf]);
  }
  return result;
}

void CompareHealthData(std::string_view expected, ERPHealthData got) {
  EXPECT_THAT(expected, StrEq(got.SerializeAsString()));
}

class HealthModuleDelegateImplTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  HealthDataHistory AddEnqueueRecordCall() {
    HealthDataHistory history;
    EnqueueRecordCall call;
    call.set_priority(Priority::IMMEDIATE);
    *history.mutable_enqueue_record_call() = call;
    history.set_timestamp_seconds(base::Time::Now().ToTimeT());
    return history;
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(HealthModuleDelegateImplTest, TestInit) {
  ERPHealthData ref_data;
  const std::string file_name = base::StrCat({kBaseFileOne, "0"});
  auto call = AddEnqueueRecordCall();
  *ref_data.add_history() = call;
  ASSERT_TRUE(AppendLine(temp_dir_.GetPath().AppendASCII(file_name),
                         BytesToHexString(call.SerializeAsString()))
                  .ok());

  HealthModuleDelegateImpl delegate(temp_dir_.GetPath(), kMaxStorage,
                                    kBaseFileOne);
  ASSERT_FALSE(delegate.IsInitialized());

  delegate.Init();
  ASSERT_TRUE(delegate.IsInitialized());
  delegate.GetERPHealthData(Scoped<ERPHealthData>(
      base::BindOnce(CompareHealthData, ref_data.SerializeAsString()),
      ERPHealthData()));
}

TEST_F(HealthModuleDelegateImplTest, TestWrite) {
  ERPHealthData ref_data;
  HealthModuleDelegateImpl delegate(temp_dir_.GetPath(), kMaxStorage,
                                    kBaseFileOne);
  ASSERT_FALSE(delegate.IsInitialized());

  // Can not post before initiating.
  delegate.PostHealthRecord(AddEnqueueRecordCall());
  delegate.GetERPHealthData(Scoped<ERPHealthData>(
      base::BindOnce(CompareHealthData, ref_data.SerializeAsString()),
      ERPHealthData()));

  delegate.Init();
  ASSERT_TRUE(delegate.IsInitialized());

  // Fill the local storage.
  for (uint32_t i = 0; i < kMaxWriteCount; i++) {
    auto call = AddEnqueueRecordCall();
    *ref_data.add_history() = call;
    delegate.PostHealthRecord(call);
  }
  delegate.GetERPHealthData(Scoped<ERPHealthData>(
      base::BindOnce(CompareHealthData, ref_data.SerializeAsString()),
      ERPHealthData()));

  // Overwrite half of the local storage.
  for (uint32_t i = 0; i < kMaxWriteCount / 2; i++) {
    auto call = AddEnqueueRecordCall();
    *ref_data.add_history() = call;
    delegate.PostHealthRecord(call);
  }
  ref_data.mutable_history()->DeleteSubrange(0, kMaxWriteCount / 2);
  delegate.GetERPHealthData(Scoped<ERPHealthData>(
      base::BindOnce(CompareHealthData, ref_data.SerializeAsString()),
      ERPHealthData()));
}

TEST_F(HealthModuleDelegateImplTest, TestOversizedWrite) {
  ERPHealthData ref_data;
  HealthModuleDelegateImpl delegate(temp_dir_.GetPath(), kTinyStorage,
                                    kBaseFileOne);

  delegate.PostHealthRecord(AddEnqueueRecordCall());
  delegate.GetERPHealthData(Scoped<ERPHealthData>(
      base::BindOnce(CompareHealthData, ref_data.SerializeAsString()),
      ERPHealthData()));
}

TEST_F(HealthModuleDelegateImplTest, TestGetUponDestruction) {
  ERPHealthData ref_data;
  auto done = base::BindOnce(CompareHealthData, "");
  {
    HealthModuleDelegateImpl delegate(temp_dir_.GetPath(), kMaxStorage,
                                      kBaseFileOne);

    delegate.PostHealthRecord(AddEnqueueRecordCall());
    delegate.GetERPHealthData(
        Scoped<ERPHealthData>(std::move(done), ERPHealthData()));
  }
}
}  // namespace
}  // namespace reporting
