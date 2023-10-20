// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_cleanup.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/oobe_config/frontend.h>
#include <oobe_config/metrics/enterprise_rollback_metrics_handler.h>

#include "oobe_config/filesystem/file_handler.h"

namespace oobe_config {
namespace {

void ZeroTpmSpaceIfExists(hwsec::FactoryImpl* hwsec_factory) {
  std::unique_ptr<const hwsec::OobeConfigFrontend> hwsec_ =
      hwsec_factory->GetOobeConfigFrontend();

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
bool RollbackJustFinished(const FileHandler* file_handler) {
  return file_handler->HasOpensslEncryptedRollbackData() ||
         file_handler->HasTpmEncryptedRollbackData();
}

// Should be called only when enterprise rollback has finished, otherwise we may
// be cleaning data too early.
void CleanEnterpriseRollbackMetrics(
    const EnterpriseRollbackMetricsHandler* metrics_handler) {
  if (metrics_handler->IsTrackingRollback()) {
    metrics_handler->StopTrackingRollback();
  }
}

// Delete leftovers from a preceding enterprise rollback. Should be called only
// when the device is owned.
void CleanEnterpriseRollbackLeftovers(const FileHandler* file_handler,
                                      hwsec::FactoryImpl* hwsec_factory) {
  if (!file_handler->RemoveDecryptedRollbackData()) {
    LOG(ERROR) << "Failed to remove decrypted rollback data.";
  }
  if (!file_handler->RemoveOpensslEncryptedRollbackData()) {
    LOG(ERROR) << "Failed to remove OpenSSL encrypted rollback data.";
  }
  if (!file_handler->RemoveTpmEncryptedRollbackData()) {
    LOG(ERROR) << "Failed to remove TPM encrypted rollback data.";
  }
  ZeroTpmSpaceIfExists(hwsec_factory);
}

// Should be called when the device is not owned to ensure the rollback metrics
// file is deleted if it has not been updated in a while.
void CleanEnterpriseRollbackMetricsIfStale(
    const EnterpriseRollbackMetricsHandler* metrics_handler) {
  if (metrics_handler->IsTrackingRollback()) {
    metrics_handler->CleanRollbackTrackingIfStale();
  }
}

}  // namespace

void RollbackCleanup(
    const oobe_config::FileHandler* file_handler,
    const oobe_config::EnterpriseRollbackMetricsHandler* metrics_handler,
    hwsec::FactoryImpl* hwsec_factory) {
  if (file_handler->HasOobeCompletedFlag()) {
    // Device is owned so enterprise rollback data is not necessary anymore.
    if (RollbackJustFinished(file_handler)) {
      CleanEnterpriseRollbackMetrics(metrics_handler);
    }
    CleanEnterpriseRollbackLeftovers(file_handler, hwsec_factory);
  } else {
    // If the device is not owned and the enterprise rollback metrics file has
    // not been updated in a while, clean it to avoid leaking information.
    CleanEnterpriseRollbackMetricsIfStale(metrics_handler);
  }
}

}  // namespace oobe_config
