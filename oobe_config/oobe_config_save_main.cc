// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/features/features.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"
#include "oobe_config/metrics/metrics_uma.h"
#include "oobe_config/oobe_config.h"

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

// Pass this to run oobe_config_save with TPM-based encryption. Only do this if
// the target you are rolling back to knows about TPM encryption and is able to
// clear out the TPM rollback space.
constexpr char kWithTpmEncryption[] = "tpm_encrypt";

bool TpmEncryptionEnabledByCommandLineFlag() {
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  return cl->HasSwitch(kWithTpmEncryption);
}

}  // namespace

int main(int argc, char* argv[]) {
  InitLog();

  oobe_config::EnterpriseRollbackMetricsHandler rollback_metrics;
  // TODO(b/301924474): Clean old UMA metrics.
  oobe_config::MetricsUMA metrics_uma;

  base::CommandLine::Init(argc, argv);
  LOG(INFO) << "Starting oobe_config_save";
  hwsec::FactoryImpl hwsec_factory(hwsec::ThreadingMode::kCurrentThread);
  std::unique_ptr<const hwsec::OobeConfigFrontend> hwsec_oobe_config =
      hwsec_factory.GetOobeConfigFrontend();
  oobe_config::OobeConfig oobe_config(hwsec_oobe_config.get());

  // TPM encryption is run if either the command line flag is passed to run it,
  // or the feature is turned on.
  // TODO(b:263065223) Remove feature flag and command line flag and make TPM
  // encryption the default behavior once M125 is stable (assuming we
  // encountered no issues).
  bool run_tpm_encryption = TpmEncryptionEnabledByCommandLineFlag() ||
                            oobe_config::TpmEncryptionFeatureEnabled();

  bool save_result = oobe_config.EncryptedRollbackSave(run_tpm_encryption);

  if (!save_result) {
    LOG(ERROR) << "Failed to save rollback data";
    rollback_metrics.TrackEvent(
        oobe_config::EnterpriseRollbackMetricsHandler::CreateEventData(
            EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_SAVE_FAILURE));
    metrics_uma.RecordSaveResult(
        oobe_config::MetricsUMA::RollbackSaveResult::kStage2Failure);
    return 0;
  }

  LOG(INFO) << "Exiting oobe_config_save";
  rollback_metrics.TrackEvent(
      oobe_config::EnterpriseRollbackMetricsHandler::CreateEventData(
          EnterpriseRollbackEvent::ROLLBACK_OOBE_CONFIG_SAVE_SUCCESS));
  metrics_uma.RecordSaveResult(
      oobe_config::MetricsUMA::RollbackSaveResult::kSuccess);
  return 0;
}
