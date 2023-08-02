// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/oobe_config/frontend.h>
#include <oobe_config/metrics/enterprise_rollback_metrics_handler.h>

#include "oobe_config/filesystem/file_handler.h"

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(/*enable_process_id=*/true, /*enable_thread_id=*/true,
                       /*enable_timestamp=*/true, /*enable_tickcount=*/true);
}

void ZeroTpmSpaceIfExists() {
  auto hwsec_factory_ = std::make_unique<hwsec::FactoryImpl>();
  std::unique_ptr<const hwsec::OobeConfigFrontend> hwsec_ =
      hwsec_factory_->GetOobeConfigFrontend();

  hwsec::Status space_ready = hwsec_->IsRollbackSpaceReady();
  if (space_ready.ok()) {
    hwsec::Status space_reset = hwsec_->ResetRollbackSpace();
    if (!space_reset.ok()) {
      LOG(ERROR) << space_reset.status();
      // TODO(b/262235959): Report failure to reset rollback space.
    }
  } else if (space_ready->ToTPMRetryAction() ==
             hwsec::TPMRetryAction::kSpaceNotFound) {
    // Not finding space is expected, log as informational.
    LOG(INFO) << space_ready.status();
  } else {
    LOG(ERROR) << space_ready.status();
  }
}

// If encrypted rollback data is present it means that enterprise rollback just
// finished. Should be called only when the device is owned and before cleaning
// up the leftovers.
bool RollbackJustFinished() {
  oobe_config::FileHandler file_handler;
  return file_handler.HasOpensslEncryptedRollbackData() ||
         file_handler.HasTpmEncryptedRollbackData();
}

// Should be called only when enterprise rollback has finished, otherwise we may
// be cleaning data too early.
void CleanEnterpriseRollbackMetrics() {
  oobe_config::EnterpriseRollbackMetricsHandler metrics_handler;
  if (metrics_handler.IsTrackingRollbackEvents()) {
    metrics_handler.StopTrackingRollback();
  }
}

// Delete leftovers from a preceding enterprise rollback. Should be called only
// when the device is owned.
void CleanEnterpriseRollbackLeftovers() {
  oobe_config::FileHandler file_handler;
  file_handler.RemoveRestorePath();
  file_handler.RemoveOpensslEncryptedRollbackData();
  file_handler.RemoveTpmEncryptedRollbackData();
  ZeroTpmSpaceIfExists();
}

// Should be called when the device is not owned to ensure the rollback metrics
// file is deleted if it has not been updated in a while.
void CleanEnterpriseRollbackMetricsIfStale() {
  oobe_config::EnterpriseRollbackMetricsHandler metrics_handler;
  if (metrics_handler.IsTrackingRollbackEvents()) {
    metrics_handler.CleanRollbackTrackingIfStale();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  InitLog();

  oobe_config::FileHandler file_handler;
  if (file_handler.HasOobeCompletedFlag()) {
    // Device is owned so enterprise rollback data is not necessary anymore.
    if (RollbackJustFinished()) {
      CleanEnterpriseRollbackMetrics();
    }
    CleanEnterpriseRollbackLeftovers();
  } else {
    // If the device is not owned and the enterprise rollback metrics file has
    // not been updated in a while, clean it to avoid leaking information.
    CleanEnterpriseRollbackMetricsIfStale();
  }

  return 0;
}
