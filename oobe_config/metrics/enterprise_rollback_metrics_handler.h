// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_HANDLER_H_
#define OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_HANDLER_H_

#include <memory>

#include <base/version.h>
#include <brillo/brillo_export.h>

#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_data.pb.h"

namespace oobe_config {

// Shared library to track the events triggered during Enterprise Rollback.
// Events are tracked in a file that is preserved through powerwash. This file
// is expected to be created at the beginning of the Rollback process only if
// metrics are enabled. It contains metadata corresponding to the ongoing
// Rollback that survives until the file is removed.
// This library can be used simultaneously from multiple processes. However,
// if there are simultaneous calls to methods that lock the metrics file, it is
// possible that not all events are tracked or reported. If the lock is busy,
// the library does not wait for it to be released. By design, we prefer to
// lose events data than blocking the Rollback process.
class BRILLO_EXPORT EnterpriseRollbackMetricsHandler {
 public:
  EnterpriseRollbackMetricsHandler();
  EnterpriseRollbackMetricsHandler(const EnterpriseRollbackMetricsHandler&) =
      delete;
  EnterpriseRollbackMetricsHandler& operator=(
      const EnterpriseRollbackMetricsHandler&) = delete;

  virtual ~EnterpriseRollbackMetricsHandler();

  // Creates a new rollback metrics file if metrics are enabled. Stores
  // `current_os_version` and `target_os_version` as metadata of the file to
  // keep for the current rollback process. Returns false if the new file is not
  // created.
  // If an existing metrics file from a previous rollback already exists, the
  // events tracked are reported before it gets deleted.
  bool StartTrackingRollback(const base::Version& current_os_version,
                             const base::Version& target_os_version) const;

  // Add new event to the rollback metrics file if the file exists. Locks the
  // file during the process to avoid synchronization issues.
  // Returns false if there is no file because it means Rollback events are not
  // being tracked. If the file is already locked because another event is being
  // tracked or because the events tracked are being reported, the method ends
  // and the new event is not tracked.
  // Returns true if the new event is added to the file successfully. If the
  // rollback metrics file is deleted while the event is being added to the
  // file. the method will succeed but the event will be ultimately lost. This
  // limitation is expected.
  bool TrackEvent(const EnterpriseRollbackEvent& event) const;

  // Reports event immediately instead of adding it to the rollback metrics
  // file. Caller must ensure this method is called after powerwash. Returns
  // true if rollback events are being tracked and event is reported
  // successfully. Attempts to report old events tracked in the file as well.
  bool ReportEventNow(EnterpriseRollbackEvent event) const;

  // Attempts to report the events tracked in the rollback metrics file. Locks
  // the file during the process to avoid synchronization issues.
  // Removes reported events from the file, but keeps the rollback metadata.
  // If the file is already locked because a new event is being tracked or
  // because the events are already being reported, the method ends and returns
  // false.
  // Returns true if it was possible to lock the file and all the events were
  // reported.
  bool ReportTrackedEvents() const;

  // Report all tracked events and delete the rollback metrics file.
  void StopTrackingRollback() const;

  // Checks if we are tracking events of the current rollback process. Events
  // are tracked if the rollback metrics file exists.
  bool IsTrackingRollbackEvents() const;

  void SetFileHandlerForTesting(const FileHandler& file_handler);

 private:
  // Object for managing files.
  FileHandler file_handler_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_HANDLER_H_
