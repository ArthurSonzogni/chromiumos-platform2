// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_cleanup.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/scoped_chromeos_version_info.h>
#include <base/version.h>
#include <gtest/gtest.h>
#include <libhwsec/backend/mock_backend.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/structures/space.h>
#include <metrics/structured/event_base.h>
#include <metrics/structured/mock_recorder.h>
#include <metrics/structured/recorder_singleton.h>
#include <metrics/structured_events.h>

#include "oobe_config/filesystem/file_handler_for_testing.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler_for_testing.h"

namespace oobe_config {
namespace {

const char kDeviceVersionM108LsbRelease[] = "15183.1.2";

const base::Version kDeviceVersionM108("15183.1.2");
const base::Version kTargetVersionM107("15117.0.0");

constexpr uint32_t kRollbackSpaceIndex = 0x100e;
constexpr uint32_t kRollbackSpaceSize = 32;

// Random data to write to rollback files.
const std::string data(128, 0x66);
}  // namespace

class RollbackCleanupTest : public testing::Test {
 protected:
  void SetUp() override {
    file_handler_.CreateDefaultExistingPaths();
    hwsec_oobe_config_ = hwsec_factory_.GetOobeConfigFrontend();

    // Set mock recorder for structured metrics.
    auto recorder = std::make_unique<metrics::structured::MockRecorder>();
    recorder_ = recorder.get();
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(recorder));
  }

  void TearDown() override {
    // Free recorder to ensure the expectations are run and avoid leaks.
    metrics::structured::RecorderSingleton::GetInstance()
        ->DestroyRecorderForTest();
  }

  bool CreateRollbackSpace() {
    return hwsec_factory_.GetFakeTpmNvramForTest().DefinePlatformCreateSpace(
               kRollbackSpaceIndex, kRollbackSpaceSize) &&
           hwsec_oobe_config_->IsRollbackSpaceReady().ok();
  }

  void ExpectRollbackCompletedMetricRecord(int times) {
    EXPECT_CALL(*recorder_,
                Record(testing::Property(
                    &metrics::structured::EventBase::name_hash,
                    metrics::structured::events::rollback_enterprise::
                        RollbackCompleted::kEventNameHash)))
        .Times(times);
  }

  FileHandlerForTesting file_handler_;
  EnterpriseRollbackMetricsHandlerForTesting metrics_handler_;
  metrics::structured::MockRecorder* recorder_;
  hwsec::Tpm2SimulatorFactoryForTest hwsec_factory_;
  std::unique_ptr<const hwsec::OobeConfigFrontend> hwsec_oobe_config_;
  // Need to keep the variable around for the test version to be read. These
  // tests do not verify the version is read because the reporting works anyway
  // but this is the way.
  base::test::ScopedChromeOSVersionInfo version_info_{
      base::StringPrintf("CHROMEOS_RELEASE_VERSION=%s",
                         kDeviceVersionM108LsbRelease),
      base::Time()};
};

TEST_F(RollbackCleanupTest, DoNotRemoveAnyRollbackDataIfOobeIsNotCompleted) {
  EXPECT_TRUE(file_handler_.WriteOpensslEncryptedRollbackData(data));
  EXPECT_TRUE(file_handler_.WriteTpmEncryptedRollbackData(data));
  EXPECT_TRUE(file_handler_.WriteDecryptedRollbackData(data));

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_TRUE(file_handler_.HasOpensslEncryptedRollbackData());
  EXPECT_TRUE(file_handler_.HasTpmEncryptedRollbackData());
  EXPECT_TRUE(file_handler_.HasDecryptedRollbackData());
}

TEST_F(RollbackCleanupTest, RemoveRollbackDataIfOobeIsCompleted) {
  EXPECT_TRUE(file_handler_.CreateOobeCompletedFlag());
  EXPECT_TRUE(file_handler_.WriteOpensslEncryptedRollbackData(data));
  EXPECT_TRUE(file_handler_.WriteTpmEncryptedRollbackData(data));
  EXPECT_TRUE(file_handler_.WriteDecryptedRollbackData(data));

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_FALSE(file_handler_.HasOpensslEncryptedRollbackData());
  EXPECT_FALSE(file_handler_.HasTpmEncryptedRollbackData());
  EXPECT_FALSE(file_handler_.HasDecryptedRollbackData());
}

TEST_F(RollbackCleanupTest, ClearTpmSpaceIfOobeIsCompleted) {
  EXPECT_TRUE(CreateRollbackSpace());
  EXPECT_TRUE(file_handler_.CreateOobeCompletedFlag());

  const brillo::Blob zero(kRollbackSpaceSize);
  EXPECT_CALL(hwsec_factory_.GetMockBackend().GetMock().storage,
              Store(hwsec::Space::kEnterpriseRollback, zero))
      .Times(1);

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);
}

TEST_F(RollbackCleanupTest, DoNotClearTpmSpaceIfOobeIsNotCompleted) {
  EXPECT_TRUE(CreateRollbackSpace());

  const brillo::Blob zero(kRollbackSpaceSize);
  EXPECT_CALL(hwsec_factory_.GetMockBackend().GetMock().storage,
              Store(hwsec::Space::kEnterpriseRollback, zero))
      .Times(0);

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);
}

TEST_F(RollbackCleanupTest,
       DoNotCleanupNonStaleMetricsFileIfOobeIsNotCompleted) {
  ExpectRollbackCompletedMetricRecord(/*times=*/0);
  EXPECT_TRUE(metrics_handler_.EnableMetrics());
  EXPECT_TRUE(metrics_handler_.StartTrackingRollback(kDeviceVersionM108,
                                                     kTargetVersionM107));
  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
}

TEST_F(RollbackCleanupTest,
       DoNotRemoveMetricsFileIfOobeIsCompletedButNoPrecedingRollback) {
  ExpectRollbackCompletedMetricRecord(/*times=*/0);
  EXPECT_TRUE(metrics_handler_.EnableMetrics());
  EXPECT_TRUE(metrics_handler_.StartTrackingRollback(kDeviceVersionM108,
                                                     kTargetVersionM107));
  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
  EXPECT_TRUE(file_handler_.CreateOobeCompletedFlag());

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
}

TEST_F(RollbackCleanupTest,
       RemoveMetricsFileIfOobeIsCompletedAndPrecedingOpensslRollback) {
  ExpectRollbackCompletedMetricRecord(/*times=*/1);
  EXPECT_TRUE(metrics_handler_.EnableMetrics());
  EXPECT_TRUE(metrics_handler_.StartTrackingRollback(kDeviceVersionM108,
                                                     kTargetVersionM107));
  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
  EXPECT_TRUE(file_handler_.CreateOobeCompletedFlag());
  EXPECT_TRUE(file_handler_.WriteOpensslEncryptedRollbackData(data));

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_FALSE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
}

TEST_F(RollbackCleanupTest,
       RemoveMetricsFileIfOobeIsCompletedAndPrecedingTpmRollback) {
  ExpectRollbackCompletedMetricRecord(/*times=*/1);
  EXPECT_TRUE(metrics_handler_.EnableMetrics());
  EXPECT_TRUE(metrics_handler_.StartTrackingRollback(kDeviceVersionM108,
                                                     kTargetVersionM107));
  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
  EXPECT_TRUE(file_handler_.CreateOobeCompletedFlag());
  EXPECT_TRUE(file_handler_.WriteTpmEncryptedRollbackData(data));

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_FALSE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
}

}  // namespace oobe_config
