// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/version.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>
#include <metrics/structured/event_base.h>
#include <metrics/structured/mock_recorder.h>
#include <metrics/structured/recorder_singleton.h>
#include <metrics/structured_events.h>

#include "oobe_config/filesystem/file_handler_for_testing.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"

namespace oobe_config {

namespace {

const base::Version kOsVersionM108("15183.1.2");
const base::Version kOsVersionM107("15117.3.4");
const base::Version kOsVersionM105("14989.5.6");
const base::Version kOsVersionM102("14695.7.8");

base::FilePath GetBuildDirectory() {
  // Required to spawn a new process and test file locking.
  base::FilePath executable_path =
      base::CommandLine::ForCurrentProcess()->GetProgram();
  return base::FilePath(executable_path.DirName());
}

bool OSVersionEqualOrigin(
    const base::Version& version,
    const EnterpriseRollbackMetricsData& rollback_metrics_data) {
  auto metadata = rollback_metrics_data.rollback_metadata();
  base::Version origin_chromeos_version(
      {metadata.origin_chromeos_version_major(),
       metadata.origin_chromeos_version_minor(),
       metadata.origin_chromeos_version_patch()});
  return origin_chromeos_version == version;
}

bool OSVersionEqualTarget(
    const base::Version& version,
    const EnterpriseRollbackMetricsData& rollback_metrics_data) {
  auto metadata = rollback_metrics_data.rollback_metadata();
  base::Version target_chromeos_version(
      {metadata.target_chromeos_version_major(),
       metadata.target_chromeos_version_minor(),
       metadata.target_chromeos_version_patch()});
  return target_chromeos_version == version;
}

}  // namespace

class EnterpriseRollbackMetricsHandlerTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(file_handler_.CreateDefaultExistingPaths());
    enterprise_rollback_metrics_handler_.SetFileHandlerForTesting(
        file_handler_);

    // Set mock recorder for structured metrics.
    auto recorder = std::make_unique<metrics::structured::MockRecorder>();
    recorder_ = recorder.get();
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(recorder));

    // Enable metrics by default in all tests.
    file_handler_.CreateMetricsReportingEnabledFile();
  }

  void TearDown() override {
    // Free recorder to ensure the expectations are run and avoid leaks.
    metrics::structured::RecorderSingleton::GetInstance()
        ->DestroyRecorderForTest();
  }

 protected:
  bool ReadRollbackMetricsData(
      EnterpriseRollbackMetricsData* rollback_metrics_data) {
    std::string rollback_metrics_data_str;
    if (!file_handler_.ReadRollbackMetricsData(&rollback_metrics_data_str)) {
      return false;
    }

    return rollback_metrics_data->ParseFromString(rollback_metrics_data_str);
  }

  FileHandlerForTesting file_handler_;
  EnterpriseRollbackMetricsHandler enterprise_rollback_metrics_handler_;
  metrics::structured::MockRecorder* recorder_;
};

TEST_F(EnterpriseRollbackMetricsHandlerTest, NoMetricsFileInitially) {
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       NoMetricsFileIfReportingIsDisabled) {
  // Delete flag to simulate metrics not being enabled and ensure the file is
  // not created.
  file_handler_.RemoveMetricsReportingEnabledFile();

  ASSERT_FALSE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest, NewMetricsFileHasOriginAndTarget) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());

  // Verify file content.
  EnterpriseRollbackMetricsData rollback_metrics_data;
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));

  ASSERT_TRUE(OSVersionEqualOrigin(kOsVersionM108, rollback_metrics_data));
  ASSERT_TRUE(OSVersionEqualTarget(kOsVersionM107, rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 0);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       NewMetricsFileHasNewOriginAndTargetWhenPreviousMetricsFileExists) {
  // Create pre-existing file from a previous rollback process.
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM105, kOsVersionM102));
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());

  // Recreate file with a new rollback process.
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());

  // Verify the content of the file corresponds to the new process.
  EnterpriseRollbackMetricsData rollback_metrics_data;
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));

  ASSERT_TRUE(OSVersionEqualOrigin(kOsVersionM108, rollback_metrics_data));
  ASSERT_TRUE(OSVersionEqualTarget(kOsVersionM107, rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 0);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       NewMetricsFileHasNewOriginAndTargetEvenIfPreviousFileIsLocked) {
  // Create pre-existing file from a previous rollback process.
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM105, kOsVersionM102));
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());

  auto lock_process =
      file_handler_.StartLockMetricsFileProcess(GetBuildDirectory());
  ASSERT_NE(lock_process, nullptr);

  // Recreate file with a new rollback process.
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());

  lock_process->Kill(SIGKILL, /*timeout=*/5);

  // Verify the content of the file corresponds to the new process.
  EnterpriseRollbackMetricsData rollback_metrics_data;
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));

  ASSERT_TRUE(OSVersionEqualOrigin(kOsVersionM108, rollback_metrics_data));
  ASSERT_TRUE(OSVersionEqualTarget(kOsVersionM107, rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 0);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       DoNotTrackEventIfMetricsFileDoesNotExist) {
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
  ASSERT_FALSE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::EVENT_UNSPECIFIED));
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest, DoNotTrackEventIfFileIsLocked) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));

  auto lock_process =
      file_handler_.StartLockMetricsFileProcess(GetBuildDirectory());
  ASSERT_NE(lock_process, nullptr);
  ASSERT_FALSE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::EVENT_UNSPECIFIED));

  lock_process->Kill(SIGKILL, /*timeout=*/5);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       MetricsFileHasMetadataAndEventAfterTracking) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::EVENT_UNSPECIFIED));

  // Verify file content.
  EnterpriseRollbackMetricsData rollback_metrics_data;
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));

  ASSERT_TRUE(OSVersionEqualOrigin(kOsVersionM108, rollback_metrics_data));
  ASSERT_TRUE(OSVersionEqualTarget(kOsVersionM107, rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 1);
  ASSERT_EQ(rollback_metrics_data.event_data(0).event(),
            EnterpriseRollbackEvent::EVENT_UNSPECIFIED);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       MetricsFileHasMetadataAndEventsAfterTrackingMultipleEvents) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::EVENT_UNSPECIFIED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::EVENT_UNSPECIFIED));

  // Verify file content.
  EnterpriseRollbackMetricsData rollback_metrics_data;
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));

  ASSERT_TRUE(OSVersionEqualOrigin(kOsVersionM108, rollback_metrics_data));
  ASSERT_TRUE(OSVersionEqualTarget(kOsVersionM107, rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 3);
  ASSERT_EQ(rollback_metrics_data.event_data(0).event(),
            EnterpriseRollbackEvent::EVENT_UNSPECIFIED);
  ASSERT_EQ(rollback_metrics_data.event_data(1).event(),
            EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED);
  ASSERT_EQ(rollback_metrics_data.event_data(2).event(),
            EnterpriseRollbackEvent::EVENT_UNSPECIFIED);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest, ReportingFailsIfNoMetricsFile) {
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
  ASSERT_FALSE(enterprise_rollback_metrics_handler_.ReportTrackedEvents());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest, ReportingCorruptedFileFails) {
  ASSERT_TRUE(
      file_handler_.WriteRollbackMetricsData("This is not valid metrics data"));
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());
  ASSERT_FALSE(enterprise_rollback_metrics_handler_.ReportTrackedEvents());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       ReportingEventsDeleteEventEntriesFromMetricsFile) {
  EnterpriseRollbackMetricsData rollback_metrics_data;

  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::EVENT_UNSPECIFIED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 2);

  // EVENT_UNSPECIFIED is not reported but ROLLBACK_POLICY_ACTIVATED should
  // trigger the corresponding structured metric call.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(1);

  ASSERT_TRUE(enterprise_rollback_metrics_handler_.ReportTrackedEvents());

  // We verify the events are deleted but the file and header are intact.
  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));

  ASSERT_TRUE(OSVersionEqualOrigin(kOsVersionM108, rollback_metrics_data));
  ASSERT_TRUE(OSVersionEqualTarget(kOsVersionM107, rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 0);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       ReportingDoesNotModifyFileIfLocked) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  auto lock_process =
      file_handler_.StartLockMetricsFileProcess(GetBuildDirectory());
  ASSERT_NE(lock_process, nullptr);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(0);

  ASSERT_FALSE(enterprise_rollback_metrics_handler_.ReportTrackedEvents());

  EnterpriseRollbackMetricsData rollback_metrics_data;
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 2);

  lock_process->Kill(SIGKILL, /*timeout=*/5);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       ReportingEventNowAlsoReportsPreviouslyTrackedEvents) {
  EnterpriseRollbackMetricsData rollback_metrics_data;

  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 2);

  // The corresponding structured metrics for the tracked events and the new one
  // should be reported.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(3);

  ASSERT_TRUE(enterprise_rollback_metrics_handler_.ReportEventNow(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  // Previous events should have also been reported.
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 0);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       ReportingEventNowDoesNotReportPreviouslyTrackedEventsIfFileIsLocked) {
  EnterpriseRollbackMetricsData rollback_metrics_data;

  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 2);

  auto lock_process =
      file_handler_.StartLockMetricsFileProcess(GetBuildDirectory());
  EXPECT_NE(lock_process, nullptr);

  // Only the new corresponding structure metrics should be reported.
  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(1);

  ASSERT_TRUE(enterprise_rollback_metrics_handler_.ReportEventNow(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  lock_process->Kill(SIGKILL, /*timeout=*/5);

  // Previous events have not been reported.
  ASSERT_TRUE(ReadRollbackMetricsData(&rollback_metrics_data));
  ASSERT_EQ(rollback_metrics_data.event_data_size(), 2);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       StopTrackingReportsEventsAndDeletesMetricFile) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(2);

  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());
  enterprise_rollback_metrics_handler_.StopTrackingRollback();

  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       StopTrackingDoesNotReportEventsButDeletesMetricFileIfLocked) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.TrackEvent(
      EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED));

  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());
  // Events will not be reported but the file is deleted.
  auto lock_process =
      file_handler_.StartLockMetricsFileProcess(GetBuildDirectory());
  ASSERT_NE(lock_process, nullptr);

  EXPECT_CALL(*recorder_, Record(testing::Property(
                              &metrics::structured::EventBase::name_hash,
                              metrics::structured::events::rollback_enterprise::
                                  RollbackPolicyActivated::kEventNameHash)))
      .Times(0);

  enterprise_rollback_metrics_handler_.StopTrackingRollback();
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());

  lock_process->Kill(SIGKILL, /*timeout=*/5);
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       TrackEventsCheckIsFalseIfNoMetricsFile) {
  ASSERT_FALSE(file_handler_.HasRollbackMetricsData());
  ASSERT_FALSE(enterprise_rollback_metrics_handler_.IsTrackingRollbackEvents());
}

TEST_F(EnterpriseRollbackMetricsHandlerTest,
       TrackEventsCheckIsTrueIfMetricsFileExists) {
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.StartTrackingRollback(
      kOsVersionM108, kOsVersionM107));

  ASSERT_TRUE(file_handler_.HasRollbackMetricsData());
  ASSERT_TRUE(enterprise_rollback_metrics_handler_.IsTrackingRollbackEvents());
}

}  // namespace oobe_config
