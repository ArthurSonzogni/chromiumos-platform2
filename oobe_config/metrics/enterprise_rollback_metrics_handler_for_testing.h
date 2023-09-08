// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_HANDLER_FOR_TESTING_H_
#define OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_HANDLER_FOR_TESTING_H_

#include <memory>

#include <brillo/brillo_export.h>

#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"

namespace oobe_config {

class FileHandlerForTesting;

class BRILLO_EXPORT EnterpriseRollbackMetricsHandlerForTesting
    : public EnterpriseRollbackMetricsHandler {
 public:
  EnterpriseRollbackMetricsHandlerForTesting();
  explicit EnterpriseRollbackMetricsHandlerForTesting(
      std::unique_ptr<FileHandlerForTesting> file_handler);

  // Create flag to simulate metrics reporting being enabled.
  bool EnableMetrics();

  // Delete flag to simulate metrics not being enabled.
  bool DisableMetrics();

  // Compares `version` to the device version stored as metadata in the rollback
  // metrics file. Returns true if both versions are the same.
  // Returns false if the versions are different or there is an error reading
  // the target version from the file.
  bool IsTrackingForDeviceVersion(const base::Version& version) const;

  // Looks for `event` in the rollback metrics file and counts the number of
  // times it appears. If the file does not exists or it is corrupted, it is
  // considered the event has been tracked 0 times.
  int TimesEventHasBeenTracked(const EnterpriseRollbackEvent& event) const;

 private:
  FileHandlerForTesting* file_handler_testing_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_HANDLER_FOR_TESTING_H_
