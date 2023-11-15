// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <metrics/structured/event_base.h>
#include <metrics/structured/mock_recorder.h>
#include <metrics/structured/recorder_singleton.h>
#include <metrics/structured_events.h>

#include "oobe_config/metrics/enterprise_rollback_metrics_data.pb.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_recorder.h"

namespace oobe_config {

namespace {
void SetTestChromeOSVersion(ChromeOSVersion* version) {
  version->set_major(15183);
  version->set_minor(34);
  version->set_patch(24);
}

RollbackMetadata GetTestRollbackMetadata() {
  RollbackMetadata rollback_metadata;
  SetTestChromeOSVersion(rollback_metadata.mutable_origin_chromeos_version());
  SetTestChromeOSVersion(rollback_metadata.mutable_target_chromeos_version());
  return rollback_metadata;
}

}  // namespace

class StructuredMetricsRecorderTest : public ::testing::Test {
 public:
  void SetUp() override {
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

 protected:
  metrics::structured::MockRecorder* recorder_;
};

// TODO(b/300861453): Add test for all metrics when we support them.

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackPolicyActivated) {
  EventData event_data;
  event_data.set_event(EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigSaveSuccess) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_SAVE_SUCCESS);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigSave::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigSaveFailure) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_SAVE_FAILURE);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigSave::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigRestoreNoChromeOSResultVersion) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_SUCCESS);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigRestore::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigRestoreSuccess) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_SUCCESS);
  SetTestChromeOSVersion(event_data.mutable_event_chromeos_version());

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigRestore::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigRestoreFailureDecrypt) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_DECRYPT);
  SetTestChromeOSVersion(event_data.mutable_event_chromeos_version());

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigRestore::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigRestoreFailureRead) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_READ);
  SetTestChromeOSVersion(event_data.mutable_event_chromeos_version());

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigRestore::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigRestoreFailureParse) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_PARSE);
  SetTestChromeOSVersion(event_data.mutable_event_chromeos_version());

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigRestore::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackOobeConfigRestoreFailureConfig) {
  EventData event_data;
  event_data.set_event(
      EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_CONFIG);
  SetTestChromeOSVersion(event_data.mutable_event_chromeos_version());

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackOobeConfigRestore::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackUpdateFailure) {
  EventData event_data;
  event_data.set_event(EnterpriseRollbackEvent::ROLLBACK_UPDATE_FAILURE);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackUpdateFailure::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

TEST_F(StructuredMetricsRecorderTest, ReportStructuredMetricRollbackCompleted) {
  EventData event_data;
  event_data.set_event(EnterpriseRollbackEvent::ROLLBACK_COMPLETED);
  SetTestChromeOSVersion(event_data.mutable_event_chromeos_version());

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackCompleted::kEventNameHash)))
      .Times(1);

  RecordEnterpriseRollbackMetric(event_data, GetTestRollbackMetadata());
}

}  // namespace oobe_config
