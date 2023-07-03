// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/logging.h>
#include <base/notreached.h>

namespace oobe_config {

namespace {

std::unique_ptr<RollbackMetadata> MetadataFromVersions(
    const base::Version& current, const base::Version& target) {
  auto metadata = std::make_unique<RollbackMetadata>();
  metadata->set_origin_chromeos_version_major(current.components()[0]);
  metadata->set_origin_chromeos_version_minor(current.components()[1]);
  metadata->set_origin_chromeos_version_patch(current.components()[2]);
  metadata->set_target_chromeos_version_major(target.components()[0]);
  metadata->set_target_chromeos_version_minor(target.components()[1]);
  metadata->set_target_chromeos_version_patch(target.components()[2]);
  return metadata;
}

}  // namespace

EnterpriseRollbackMetricsHandler::EnterpriseRollbackMetricsHandler()
    : file_handler_(FileHandler()) {}

EnterpriseRollbackMetricsHandler::~EnterpriseRollbackMetricsHandler() = default;

bool EnterpriseRollbackMetricsHandler::StartTrackingRollback(
    const base::Version& current_os_version,
    const base::Version& target_os_version) const {
  if (!file_handler_.HasMetricsReportingEnabledFlag()) {
    LOG(INFO) << "Metrics are not enabled. Not creating the Rollback metrics "
                 "file because Rollback events should not be tracked.";
    // As existing Rollback metrics will not be reported even if they were
    // already tracked, we can delete a pre-existent file.
    file_handler_.RemoveRollbackMetricsData();
    return false;
  }

  if (file_handler_.HasRollbackMetricsData()) {
    LOG(INFO) << "Previous metrics data file encountered. Attempting to report "
                 "old events and delete it.";
    StopTrackingRollback();
  }

  // Create header containing information about the current Enterprise Rollback.
  auto rollback_metadata =
      MetadataFromVersions(current_os_version, target_os_version);
  EnterpriseRollbackMetricsData metrics_data;
  metrics_data.set_allocated_rollback_metadata(rollback_metadata.release());

  std::string metrics_data_str;
  metrics_data.SerializeToString(&metrics_data_str);

  // The rollback metrics data file must always contain the information about
  // the current Rollback process so the metrics we report are accurate. Every
  // time a new rollback process starts, the file is overwritten and associated
  // to the ongoing rollback. Therefore, the creation of the new file and the
  // metadata writing must happen in a unique step to ensure any previous data
  // is overwritten.
  return file_handler_.CreateRollbackMetricsDataAtomically(metrics_data_str);
}

bool EnterpriseRollbackMetricsHandler::TrackEvent(
    const EnterpriseRollbackEvent& event) const {
  // We only track rollback events if the metrics file was created. Calling
  // this method if metrics are not enabled is not an error.
  if (!file_handler_.HasRollbackMetricsData()) {
    LOG(INFO) << "Not recording metrics. Rollback event " << event
              << " not tracked.";
    return false;
  }

  std::optional<base::File> rollback_metrics_file =
      file_handler_.OpenRollbackMetricsDataFile();
  if (!rollback_metrics_file.has_value()) {
    LOG(ERROR) << "Cannot open Rollback metrics file. Rollback event " << event
               << " not tracked.";
    return false;
  }

  // We use flock to avoid synchronization issues between processes when
  // handling events in the metrics file. We are ok with the possibility of the
  // file being deleted while performing this action and losing the
  // corresponding metric.
  // If the lock is busy we do not wait for the lock to be released. It is
  // preferable to lose the metric than risk blocking Rollback.
  if (!file_handler_.LockFileNoBlocking(*rollback_metrics_file)) {
    LOG(ERROR) << "Cannot lock Rollback metrics file. Rollback event " << event
               << " not tracked.";
    return false;
  }

  // Proto de-serialization can re-build the content of
  // EnterpriseRollbackMetricsData even if the data comes from the serialization
  // of multiple messages. Therefore, we do not need to read and override the
  // existing EnterpriseRollbackMetricsData. We create a new message with only
  // the new event, serialize it, and append it at the end of the file.
  EnterpriseRollbackMetricsData metrics_data;
  EventData* event_data = metrics_data.add_event_data();
  event_data->set_event(event);
  std::string event_data_serialized;
  metrics_data.SerializeToString(&event_data_serialized);

  if (!file_handler_.ExtendOpenedFile(*rollback_metrics_file,
                                      event_data_serialized)) {
    LOG(ERROR) << "Cannot extend Rollback metrics file." << event
               << " not tracked.";
    file_handler_.UnlockFile(*rollback_metrics_file);
    return false;
  }

  file_handler_.UnlockFile(*rollback_metrics_file);
  return true;
}

bool EnterpriseRollbackMetricsHandler::ReportTrackedEvents() const {
  // This method should only be called if the rollback metrics file exists, but
  // it is possible that the file was deleted by another process simultaneously.
  if (!file_handler_.HasRollbackMetricsData()) {
    LOG(ERROR) << "No Rollback metrics file.";
    return false;
  }

  // The file contains the events that have not been reported yet. Once we read
  // the events and report the corresponding metrics, we need to delete them
  // from the file. The header is not modified.
  // We need to lock for the whole duration of the read and truncate process to
  // ensure the events are removed from the file when reported.
  std::optional<base::File> rollback_metrics_file =
      file_handler_.OpenRollbackMetricsDataFile();
  if (!rollback_metrics_file.has_value()) {
    LOG(ERROR) << "Cannot open Rollback metrics file.";
    return false;
  }

  if (!file_handler_.LockFileNoBlocking(*rollback_metrics_file)) {
    LOG(ERROR)
        << "Cannot lock Rollback metrics file. Not reporting the events.";
    return false;
  }

  std::optional<std::string> rollback_metrics_data =
      file_handler_.GetOpenedFileData(*rollback_metrics_file);
  if (!rollback_metrics_data.has_value()) {
    file_handler_.UnlockFile(*rollback_metrics_file);
    return false;
  }

  EnterpriseRollbackMetricsData metrics_data;
  if (!metrics_data.ParseFromString(rollback_metrics_data.value())) {
    LOG(ERROR) << "Could not parse EnterpriseRollbackMetricsData proto.";
    file_handler_.UnlockFile(*rollback_metrics_file);
    return false;
  }

  if (metrics_data.event_data_size() > 0) {
    for (const EventData& event_data : metrics_data.event_data()) {
      LOG(INFO) << "Event found: " << event_data.event() << ".";
      RecordStructuredMetric(event_data, metrics_data.rollback_metadata());
    }

    // Truncate the file to the size of the header so only the Rollback metadata
    // is kept in the metrics file.
    metrics_data.clear_event_data();
    std::string rollback_metrics_header;
    metrics_data.SerializeToString(&rollback_metrics_header);
    file_handler_.TruncateOpenedFile(*rollback_metrics_file,
                                     rollback_metrics_header.length());
  }

  file_handler_.UnlockFile(*rollback_metrics_file);
  return true;
}

void EnterpriseRollbackMetricsHandler::StopTrackingRollback() const {
  if (!ReportTrackedEvents()) {
    LOG(ERROR) << "Unable to report the events before deleting the rollback "
                  "metrics file.";
  }
  file_handler_.RemoveRollbackMetricsData();
}

bool EnterpriseRollbackMetricsHandler::IsTrackingRollbackEvents() const {
  return file_handler_.HasRollbackMetricsData();
}

void EnterpriseRollbackMetricsHandler::RecordStructuredMetric(
    const EventData& event_data,
    const RollbackMetadata& rollback_metadata) const {
  // TODO(b/261850979): Report all events using structured metrics.
  switch (event_data.event()) {
    case EnterpriseRollbackEvent::EVENT_UNSPECIFIED:
      LOG(INFO) << "Event to record undefined.";
      break;
    default:
      NOTREACHED();
      break;
  }
}

void EnterpriseRollbackMetricsHandler::SetFileHandlerForTesting(
    const FileHandler& file_handler) {
  file_handler_ = file_handler;
}

}  // namespace oobe_config
