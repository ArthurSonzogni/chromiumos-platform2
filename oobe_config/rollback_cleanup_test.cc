// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_cleanup.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "libhwsec/backend/mock_backend.h"
#include "libhwsec/factory/tpm2_simulator_factory_for_test.h"
#include "libhwsec/structures/space.h"
#include "oobe_config/filesystem/file_handler_for_testing.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler_for_testing.h"

namespace oobe_config {
namespace {
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
  }

  bool CreateRollbackSpace() {
    return hwsec_factory_.GetFakeTpmNvramForTest().DefinePlatformCreateSpace(
               kRollbackSpaceIndex, kRollbackSpaceSize) &&
           hwsec_oobe_config_->IsRollbackSpaceReady().ok();
  }

  FileHandlerForTesting file_handler_;
  EnterpriseRollbackMetricsHandlerForTesting metrics_handler_;
  hwsec::Tpm2SimulatorFactoryForTest hwsec_factory_;
  std::unique_ptr<const hwsec::OobeConfigFrontend> hwsec_oobe_config_;
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
  EXPECT_TRUE(metrics_handler_.EnableMetrics());
  EXPECT_TRUE(metrics_handler_.StartTrackingRollback(kDeviceVersionM108,
                                                     kTargetVersionM107));
  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));

  RollbackCleanup(&file_handler_, &metrics_handler_, &hwsec_factory_);

  EXPECT_TRUE(metrics_handler_.IsTrackingForDeviceVersion(kDeviceVersionM108));
}

TEST_F(RollbackCleanupTest,
       DoNotRemoveMetricsFileIfOobeIsCompletedButNoPrecedingRollback) {
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
