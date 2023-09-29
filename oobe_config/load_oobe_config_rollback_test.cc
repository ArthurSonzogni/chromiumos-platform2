// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_rollback.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <base/version.h>
#include <gtest/gtest.h>
#include <metrics/structured/event_base.h>
#include <metrics/structured/mock_recorder.h>
#include <metrics/structured/recorder_singleton.h>
#include <metrics/structured_events.h>

#include "oobe_config/filesystem/file_handler_for_testing.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler_for_testing.h"
#include "oobe_config/oobe_config.h"
#include "oobe_config/oobe_config_test.h"

namespace oobe_config {

namespace {

const char kDeviceVersionM108LsbRelease[] = "15183.1.2";

const base::Version kDeviceVersionM108("15183.1.2");
const base::Version kTargetVersionM107("15117.0.0");

}  // namespace

class LoadOobeConfigRollbackTest : public OobeConfigTest {
 protected:
  void SetUp() override {
    OobeConfigTest::SetUp();

    // Set mock recorder for structured metrics.
    auto recorder = std::make_unique<metrics::structured::MockRecorder>();
    recorder_ = recorder.get();
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(recorder));

    rollback_metrics_ = std::make_unique<
        oobe_config::EnterpriseRollbackMetricsHandlerForTesting>();

    load_config_ = std::make_unique<LoadOobeConfigRollback>(
        oobe_config_.get(), rollback_metrics_.get(), file_handler_);
  }

  void TearDown() override {
    // Free recorder to ensure the expectations are run and avoid leaks.
    metrics::structured::RecorderSingleton::GetInstance()
        ->DestroyRecorderForTest();
    OobeConfigTest::TearDown();
  }

  void FakePreceedingRollback() {
    ASSERT_TRUE(oobe_config_->EncryptedRollbackSave());
    SimulatePowerwash();
    load_config_ = std::make_unique<LoadOobeConfigRollback>(
        oobe_config_.get(), rollback_metrics_.get(), file_handler_);
  }

  void EnableRollbackMetricsReporting() {
    ASSERT_TRUE(rollback_metrics_->EnableMetrics());
    ASSERT_TRUE(rollback_metrics_->StartTrackingRollback(kDeviceVersionM108,
                                                         kTargetVersionM107));
    ASSERT_TRUE(rollback_metrics_->IsTrackingRollback());
  }

  void ExpectOobeConfigRestoreMetricRecord(int times) {
    EXPECT_CALL(*recorder_,
                Record(testing::Property(
                    &metrics::structured::EventBase::name_hash,
                    metrics::structured::events::rollback_enterprise::
                        RollbackOobeConfigRestore::kEventNameHash)))
        .Times(times);
  }

  void DeletePstoreData() { ASSERT_TRUE(file_handler_.RemoveRamoopsData()); }

  std::unique_ptr<LoadOobeConfigRollback> load_config_;
  std::unique_ptr<oobe_config::EnterpriseRollbackMetricsHandlerForTesting>
      rollback_metrics_;
  metrics::structured::MockRecorder* recorder_;
  // Need to keep the variable around for the test version to be read. These
  // tests do not verify the version is read because the reporting works anyway
  // but this is the way.
  base::test::ScopedChromeOSVersionInfo version_info_{
      base::StringPrintf("CHROMEOS_RELEASE_VERSION=%s",
                         kDeviceVersionM108LsbRelease),
      base::Time()};
};

TEST_F(LoadOobeConfigRollbackTest, NoRollbackNoJson) {
  EnableRollbackMetricsReporting();
  ExpectOobeConfigRestoreMetricRecord(0);

  std::string config, enrollment_domain;
  ASSERT_FALSE(load_config_->GetOobeConfigJson(&config, &enrollment_domain));
}

TEST_F(LoadOobeConfigRollbackTest, DecryptAndParse) {
  FakePreceedingRollback();
  EnableRollbackMetricsReporting();
  ExpectOobeConfigRestoreMetricRecord(1);

  std::string config, enrollment_domain;
  ASSERT_TRUE(load_config_->GetOobeConfigJson(&config, &enrollment_domain));
}

TEST_F(LoadOobeConfigRollbackTest, SecondRequestDoesNotNeedPstore) {
  FakePreceedingRollback();
  EnableRollbackMetricsReporting();
  ExpectOobeConfigRestoreMetricRecord(2);

  std::string config, enrollment_domain;
  ASSERT_TRUE(load_config_->GetOobeConfigJson(&config, &enrollment_domain));

  // Delete pstore data to make decryption impossible. This fakes the scenario
  // where a reboot happens during rollback OOBE.
  DeletePstoreData();

  // Requesting config should still work because it re-uses previous data.
  std::string config_saved;
  ASSERT_TRUE(
      load_config_->GetOobeConfigJson(&config_saved, &enrollment_domain));
  ASSERT_EQ(config_saved, config);
}

TEST_F(LoadOobeConfigRollbackTest, DecryptionFailsGracefully) {
  FakePreceedingRollback();
  EnableRollbackMetricsReporting();

  // Delete pstore data to fake the scenario where the device crashed or shut
  // down during rollback. Pstore data is gone, so decryption will fail.
  DeletePstoreData();
  ExpectOobeConfigRestoreMetricRecord(1);

  std::string config, enrollment_domain;
  ASSERT_FALSE(load_config_->GetOobeConfigJson(&config, &enrollment_domain));
}

}  // namespace oobe_config
