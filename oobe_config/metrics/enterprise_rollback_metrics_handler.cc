// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"

#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/logging.h>

#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_recorder.h"

namespace oobe_config {

namespace {

const int kNumberExpectedVersionComponents = 3;
const int kNumberStaleDaysBeforeDeletion = 15;

void SetChromeOSVersionFromVersionComponents(ChromeOSVersion* chromeos_version,
                                             const base::Version& version) {
  chromeos_version->set_major(version.components()[0]);
  chromeos_version->set_minor(version.components()[1]);
  chromeos_version->set_patch(version.components()[2]);

  return;
}

bool ExpectedVersion(base::Version version) {
  if (!version.IsValid()) {
    return false;
  }

  if (version.components().size() < kNumberExpectedVersionComponents) {
    return false;
  }

  return true;
}

std::unique_ptr<RollbackMetadata> MetadataFromVersions(
    const base::Version& current, const base::Version& target) {
  if (!ExpectedVersion(current) || !ExpectedVersion(target)) {
    return nullptr;
  }

  auto metadata = std::make_unique<RollbackMetadata>();
  SetChromeOSVersionFromVersionComponents(
      metadata->mutable_origin_chromeos_version(), current);
  SetChromeOSVersionFromVersionComponents(
      metadata->mutable_target_chromeos_version(), target);

  return metadata;
}

}  // namespace

EnterpriseRollbackMetricsHandler::EnterpriseRollbackMetricsHandler() {
  file_handler_ = std::make_unique<FileHandler>();
}

EnterpriseRollbackMetricsHandler::~EnterpriseRollbackMetricsHandler() = default;

// static
EventData EnterpriseRollbackMetricsHandler::CreateEventData(
    EnterpriseRollbackEvent event) {
  EventData event_data;
  event_data.set_event(event);

  return event_data;
}

// static
EventData EnterpriseRollbackMetricsHandler::CreateEventData(
    EnterpriseRollbackEvent event, const base::Version& event_version) {
  EventData event_data = CreateEventData(event);

  if (ExpectedVersion(event_version)) {
    SetChromeOSVersionFromVersionComponents(
        event_data.mutable_event_chromeos_version(), event_version);
  }

  return event_data;
}

bool EnterpriseRollbackMetricsHandler::StartTrackingRollback(
    const base::Version& current_os_version,
    const base::Version& target_os_version) const {
  if (!file_handler_->HasMetricsReportingEnabledFlag()) {
    LOG(INFO) << "Metrics are not enabled. Not creating the Rollback metrics "
                 "file because Rollback events should not be tracked.";
    // As existing Rollback metrics will not be reported even if they were
    // already tracked, we can delete a pre-existent file.
    file_handler_->RemoveRollbackMetricsData();
    return false;
  }

  if (file_handler_->HasRollbackMetricsData()) {
    LOG(INFO) << "Previous metrics data file encountered. Attempting to report "
                 "old events and delete it.";
    StopTrackingRollback();
  }

  LOG(INFO) << "Start tracking rollback metrics with "
            << current_os_version.GetString() << " and "
            << target_os_version.GetString();

  // Create header containing information about the current Enterprise Rollback.
  auto rollback_metadata =
      MetadataFromVersions(current_os_version, target_os_version);
  if (!rollback_metadata) {
    LOG(ERROR) << "Not possible to create metadata with versions provided";
    return false;
  }

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
  return file_handler_->CreateRollbackMetricsDataAtomically(metrics_data_str);
}

bool EnterpriseRollbackMetricsHandler::TrackEvent(
    const EventData& event_data) const {
  // We only track rollback events if the metrics file was created. Calling
  // this method if metrics are not enabled is not an error.
  if (!file_handler_->HasRollbackMetricsData()) {
    LOG(INFO) << "Not recording metrics. Rollback event " << event_data.event()
              << " not tracked.";
    return false;
  }

  std::optional<base::File> rollback_metrics_file =
      file_handler_->OpenRollbackMetricsDataFile();
  if (!rollback_metrics_file.has_value()) {
    LOG(ERROR) << "Cannot open Rollback metrics file. Rollback event "
               << event_data.event() << " not tracked.";
    return false;
  }

  // We use flock to avoid synchronization issues between processes when
  // handling events in the metrics file. We are ok with the possibility of the
  // file being deleted while performing this action and losing the
  // corresponding metric.
  // If the lock is busy we do not wait for the lock to be released. It is
  // preferable to lose the metric than risk blocking Rollback.
  if (!file_handler_->LockFileNoBlocking(*rollback_metrics_file)) {
    LOG(ERROR) << "Cannot lock Rollback metrics file. Rollback event "
               << event_data.event() << " not tracked.";
    return false;
  }

  // Proto de-serialization can re-build the content of
  // EnterpriseRollbackMetricsData even if the data comes from the serialization
  // of multiple messages. Therefore, we do not need to read and override the
  // existing EnterpriseRollbackMetricsData. We create a new message with only
  // the new event, serialize it, and append it at the end of the file.
  EnterpriseRollbackMetricsData metrics_data;
  *metrics_data.add_event_data() = event_data;
  std::string event_data_serialized;
  metrics_data.SerializeToString(&event_data_serialized);

  if (!file_handler_->ExtendOpenedFile(*rollback_metrics_file,
                                       event_data_serialized)) {
    LOG(ERROR) << "Cannot extend Rollback metrics file." << event_data.event()
               << " not tracked.";
    file_handler_->UnlockFile(*rollback_metrics_file);
    return false;
  }

  file_handler_->UnlockFile(*rollback_metrics_file);
  return true;
}

bool EnterpriseRollbackMetricsHandler::ReportEventNow(
    const EventData& event_data) const {
  std::optional<EnterpriseRollbackMetricsData> metrics_data =
      GetRollbackMetricsData();
  if (!metrics_data.has_value()) {
    LOG(INFO) << "Rollback event: " << event_data.event() << " not reported.";
    return false;
  }

  RecordEnterpriseRollbackMetric(event_data, metrics_data->rollback_metadata());

  // If there were previous events tracked in the file, we get this chance to
  // attempt to report them as well.
  if (metrics_data->event_data_size() > 0) {
    if (!ReportTrackedEvents()) {
      LOG(ERROR) << "Not possible to report previously tracked events.";
    }
  }

  return true;
}

bool EnterpriseRollbackMetricsHandler::ReportTrackedEvents() const {
  // This method should only be called if the rollback metrics file exists, but
  // it is possible that the file was deleted by another process simultaneously.
  if (!file_handler_->HasRollbackMetricsData()) {
    LOG(ERROR) << "No Rollback metrics file.";
    return false;
  }

  // The file contains the events that have not been reported yet. Once we read
  // the events and report the corresponding metrics, we need to delete them
  // from the file. The header is not modified.
  // We need to lock for the whole duration of the read and truncate process to
  // ensure the events are removed from the file when reported.
  std::optional<base::File> rollback_metrics_file =
      file_handler_->OpenRollbackMetricsDataFile();
  if (!rollback_metrics_file.has_value()) {
    LOG(ERROR) << "Cannot open Rollback metrics file.";
    return false;
  }

  if (!file_handler_->LockFileNoBlocking(*rollback_metrics_file)) {
    LOG(ERROR)
        << "Cannot lock Rollback metrics file. Not reporting the events.";
    return false;
  }

  std::optional<std::string> rollback_metrics_data =
      file_handler_->GetOpenedFileData(*rollback_metrics_file);
  if (!rollback_metrics_data.has_value()) {
    file_handler_->UnlockFile(*rollback_metrics_file);
    return false;
  }

  EnterpriseRollbackMetricsData metrics_data;
  if (!metrics_data.ParseFromString(rollback_metrics_data.value())) {
    LOG(ERROR) << "Could not parse EnterpriseRollbackMetricsData proto.";
    file_handler_->UnlockFile(*rollback_metrics_file);
    return false;
  }

  if (metrics_data.event_data_size() > 0) {
    for (const EventData& event_data : metrics_data.event_data()) {
      LOG(INFO) << "Event found: " << event_data.event() << ".";
      RecordEnterpriseRollbackMetric(event_data,
                                     metrics_data.rollback_metadata());
    }

    // Truncate the file to the size of the header so only the Rollback metadata
    // is kept in the metrics file.
    metrics_data.clear_event_data();
    std::string rollback_metrics_header;
    metrics_data.SerializeToString(&rollback_metrics_header);
    file_handler_->TruncateOpenedFile(*rollback_metrics_file,
                                      rollback_metrics_header.length());
  }

  file_handler_->UnlockFile(*rollback_metrics_file);
  return true;
}

bool EnterpriseRollbackMetricsHandler::StopTrackingRollback() const {
  LOG(INFO) << "Stopping rollback metrics tracking.";
  if (!ReportTrackedEvents()) {
    LOG(ERROR) << "Unable to report the events before deleting the rollback "
                  "metrics file.";
  }

  if (!file_handler_->RemoveRollbackMetricsData()) {
    LOG(ERROR) << "Error when deleting the rollback metrics file.";
    return false;
  }

  return true;
}

bool EnterpriseRollbackMetricsHandler::CleanRollbackTrackingIfStale() const {
  // Rollback metrics file should be updated periodically to track the events
  // before powerwash. When recording metrics after powerwash, the file header
  // is read but not modified but it is updated when previous events are
  // recorded. If the file has not been modified for days, it can mean that
  // something went wrong in the process and the file is stale.
  std::optional<base::Time> last_modification =
      file_handler_->LastModifiedTimeRollbackMetricsDataFile();
  if (!last_modification.has_value()) {
    return true;
  }

  base::TimeDelta stale_delta = base::Time::Now() - last_modification.value();
  if (stale_delta.InDays() > kNumberStaleDaysBeforeDeletion) {
    // TODO(b/261850979): Add UMA metric to control how often the file stales.
    LOG(INFO) << "Deleting stale rollback metrics file.";
    return StopTrackingRollback();
  }

  return true;
}

bool EnterpriseRollbackMetricsHandler::IsTrackingRollback() const {
  return file_handler_->HasRollbackMetricsData();
}

bool EnterpriseRollbackMetricsHandler::IsTrackingForTargetVersion(
    const base::Version& target_os_version) const {
  std::optional<EnterpriseRollbackMetricsData> metrics_data =
      GetRollbackMetricsData();
  if (!metrics_data.has_value()) {
    return false;
  }

  base::Version target(
      {metrics_data->rollback_metadata().target_chromeos_version().major(),
       metrics_data->rollback_metadata().target_chromeos_version().minor(),
       metrics_data->rollback_metadata().target_chromeos_version().patch()});

  if (!target.IsValid()) {
    LOG(ERROR) << "Version parsed not valid.";
    return false;
  }

  return target_os_version == target;
}

std::optional<EnterpriseRollbackMetricsData>
EnterpriseRollbackMetricsHandler::GetRollbackMetricsData() const {
  if (!file_handler_->HasRollbackMetricsData()) {
    return std::nullopt;
  }

  std::string rollback_metrics_data;
  if (!file_handler_->ReadRollbackMetricsData(&rollback_metrics_data)) {
    LOG(ERROR) << "Error reading rollback metrics data.";
    return std::nullopt;
  }

  EnterpriseRollbackMetricsData metrics_data;
  if (!metrics_data.ParseFromString(rollback_metrics_data)) {
    LOG(ERROR) << "Could not parse EnterpriseRollbackMetricsData proto.";
    return std::nullopt;
  }

  if (!metrics_data.has_rollback_metadata()) {
    LOG(ERROR) << "No RollbackMetadata in proto.";
    return std::nullopt;
  }

  if ((!metrics_data.rollback_metadata().has_origin_chromeos_version()) ||
      (!metrics_data.rollback_metadata().has_target_chromeos_version())) {
    LOG(ERROR) << "No valid RollbackMetadata in proto.";
    return std::nullopt;
  }

  return metrics_data;
}

}  // namespace oobe_config
