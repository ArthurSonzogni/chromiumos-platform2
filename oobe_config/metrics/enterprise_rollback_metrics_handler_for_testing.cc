// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/metrics/enterprise_rollback_metrics_handler_for_testing.h"

#include <utility>

#include <base/logging.h>

#include "oobe_config/filesystem/file_handler_for_testing.h"

namespace oobe_config {

EnterpriseRollbackMetricsHandlerForTesting::
    EnterpriseRollbackMetricsHandlerForTesting() {
  auto file_handler = std::make_unique<FileHandlerForTesting>();

  file_handler_testing_ = file_handler.get();
  file_handler_testing_->CreateDefaultExistingPaths();

  file_handler_ = std::move(file_handler);
}

EnterpriseRollbackMetricsHandlerForTesting::
    EnterpriseRollbackMetricsHandlerForTesting(
        std::unique_ptr<FileHandlerForTesting> file_handler) {
  file_handler_testing_ = file_handler.get();
  file_handler_testing_->CreateDefaultExistingPaths();

  file_handler_ = std::move(file_handler);
}

bool EnterpriseRollbackMetricsHandlerForTesting::EnableMetrics() {
  return file_handler_testing_->CreateMetricsReportingEnabledFile();
}

bool EnterpriseRollbackMetricsHandlerForTesting::DisableMetrics() {
  return file_handler_testing_->RemoveMetricsReportingEnabledFile();
}

bool EnterpriseRollbackMetricsHandlerForTesting::IsTrackingForDeviceVersion(
    const base::Version& version) const {
  std::optional<EnterpriseRollbackMetricsData> metrics_data =
      GetRollbackMetricsData();
  if (!metrics_data.has_value()) {
    return false;
  }

  base::Version origin(
      {metrics_data->rollback_metadata().origin_chromeos_version_major(),
       metrics_data->rollback_metadata().origin_chromeos_version_minor(),
       metrics_data->rollback_metadata().origin_chromeos_version_patch()});

  if (!origin.IsValid()) {
    LOG(ERROR) << "Version parsed not valid";
    return false;
  }

  return (version == origin);
}

int EnterpriseRollbackMetricsHandlerForTesting::TimesEventHasBeenTracked(
    const EnterpriseRollbackEvent& event) const {
  std::optional<EnterpriseRollbackMetricsData> metrics_data =
      GetRollbackMetricsData();
  if (!metrics_data.has_value()) {
    return 0;
  }

  int times_tracked = 0;
  if (metrics_data->event_data_size() > 0) {
    for (const EventData& tracked_event : metrics_data->event_data()) {
      if (tracked_event.event() == event) {
        times_tracked++;
      }
    }
  }

  return times_tracked;
}

}  // namespace oobe_config
