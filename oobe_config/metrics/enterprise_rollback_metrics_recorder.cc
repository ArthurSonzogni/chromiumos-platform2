// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/metrics/enterprise_rollback_metrics_recorder.h"

#include <base/logging.h>
#include <base/notreached.h>
#include <metrics/structured_events.h>

namespace oobe_config {

namespace {

template <typename Event>
void SetEventMetadata(Event& event, const RollbackMetadata& rollback_metadata) {
  event
      .Setorigin_chromeos_version_major(
          rollback_metadata.origin_chromeos_version().major())
      .Setorigin_chromeos_version_minor(
          rollback_metadata.origin_chromeos_version().minor())
      .Setorigin_chromeos_version_patch(
          rollback_metadata.origin_chromeos_version().patch())
      .Settarget_chromeos_version_major(
          rollback_metadata.target_chromeos_version().major())
      .Settarget_chromeos_version_minor(
          rollback_metadata.target_chromeos_version().minor())
      .Settarget_chromeos_version_patch(
          rollback_metadata.target_chromeos_version().patch());

  return;
}

void StructuredMetricRollbackPolicyActivated(
    const RollbackMetadata& rollback_metadata) {
  LOG(INFO) << "Record RollbackPolicyActivated event.";
  auto event = metrics::structured::events::rollback_enterprise::
      RollbackPolicyActivated();
  SetEventMetadata(event, rollback_metadata);
  event.Record();
}

enum class OobeSaveResult {
  kSuccess = 0,
  kFailure = 1,
};

void StructuredMetricRollbackOobeConfigSave(
    const RollbackMetadata& rollback_metadata, OobeSaveResult result) {
  LOG(INFO) << "Record RollbackOobeConfigSave event with result "
            << static_cast<int>(result) << ".";
  auto event = metrics::structured::events::rollback_enterprise::
      RollbackOobeConfigSave();
  SetEventMetadata(event, rollback_metadata);
  event.Setresult(static_cast<int>(result));
  event.Record();
}

enum class OobeRestoreResult {
  kSuccess = 0,
  kFailureDecrypt = 1,
  kFailureRead = 2,
  kFailureParse = 3,
  kFailureConfig = 4,
};

void StructuredMetricRollbackOobeConfigRestore(
    const RollbackMetadata& rollback_metadata,
    ChromeOSVersion result_version,
    OobeRestoreResult result) {
  LOG(INFO) << "Record RollbackOobeConfigRestore event with result "
            << static_cast<int>(result) << ".";
  auto event = metrics::structured::events::rollback_enterprise::
      RollbackOobeConfigRestore();
  SetEventMetadata(event, rollback_metadata);
  event.Setresult_chromeos_version_major(result_version.major())
      .Setresult_chromeos_version_minor(result_version.minor())
      .Setresult_chromeos_version_patch(result_version.patch())
      .Setresult(static_cast<int>(result));
  event.Record();
}

void StructuredMetricRollbackUpdateFailure(
    const RollbackMetadata& rollback_metadata) {
  LOG(INFO) << "Record RollbackUpdateFailure event.";
  auto event =
      metrics::structured::events::rollback_enterprise::RollbackUpdateFailure();
  SetEventMetadata(event, rollback_metadata);
  event.Record();
}

// TODO(b/261850979): Create methods to report metrics for each Rollback event.

}  // namespace

void RecordEnterpriseRollbackMetric(const EventData& event_data,
                                    const RollbackMetadata& rollback_metadata) {
  // TODO(b/261850979): Report all events.
  switch (event_data.event()) {
    case EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED:
      StructuredMetricRollbackPolicyActivated(rollback_metadata);
      break;

    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_SAVE_SUCCESS:
      StructuredMetricRollbackOobeConfigSave(rollback_metadata,
                                             OobeSaveResult::kSuccess);
      break;
    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_SAVE_FAILURE:
      StructuredMetricRollbackOobeConfigSave(rollback_metadata,
                                             OobeSaveResult::kFailure);
      break;

    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_SUCCESS:
      StructuredMetricRollbackOobeConfigRestore(
          rollback_metadata, event_data.event_chromeos_version(),
          OobeRestoreResult::kSuccess);
      break;
    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_DECRYPT:
      StructuredMetricRollbackOobeConfigRestore(
          rollback_metadata, event_data.event_chromeos_version(),
          OobeRestoreResult::kFailureDecrypt);
      break;
    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_READ:
      StructuredMetricRollbackOobeConfigRestore(
          rollback_metadata, event_data.event_chromeos_version(),
          OobeRestoreResult::kFailureRead);
      break;
    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_PARSE:
      StructuredMetricRollbackOobeConfigRestore(
          rollback_metadata, event_data.event_chromeos_version(),
          OobeRestoreResult::kFailureParse);
      break;
    case EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_RESTORE_FAILURE_CONFIG:
      StructuredMetricRollbackOobeConfigRestore(
          rollback_metadata, event_data.event_chromeos_version(),
          OobeRestoreResult::kFailureConfig);
      break;

    case EnterpriseRollbackEvent::ROLLBACK_UPDATE_FAILURE:
      StructuredMetricRollbackUpdateFailure(rollback_metadata);
      break;

    case EnterpriseRollbackEvent::EVENT_UNSPECIFIED:
      LOG(INFO) << "Event to record undefined.";
      break;
    default:
      // Recording is called in the target version. The default case is
      // expected when the proto does not support the event tracked in a
      // newest version yet. This is not an error but the newest metric
      // event will not be recorded.
      LOG(WARNING) << "Event to record not supported yet.";
      break;
  }
}

}  // namespace oobe_config
