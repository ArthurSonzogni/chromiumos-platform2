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
#include "oobe_config/metrics/structured_metrics_recorder.h"

namespace oobe_config {

namespace {
RollbackMetadata GetTestRollbackMetadata() {
  RollbackMetadata rollback_metadata;
  rollback_metadata.set_origin_chromeos_version_major(15183);
  rollback_metadata.set_origin_chromeos_version_minor(1);
  rollback_metadata.set_origin_chromeos_version_patch(2);

  rollback_metadata.set_target_chromeos_version_major(15117);
  rollback_metadata.set_target_chromeos_version_minor(3);
  rollback_metadata.set_target_chromeos_version_patch(4);

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

// TODO(b/261850979): Add test for all metrics when we support them.

TEST_F(StructuredMetricsRecorderTest,
       ReportStructuredMetricRollbackPolicyActivated) {
  EventData event_data;
  event_data.set_event(EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(1);

  RecordStructuredMetric(event_data, GetTestRollbackMetadata());
}

}  // namespace oobe_config
