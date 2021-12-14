// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "minios/metrics_reporter.h"
#include "minios/minios.h"

namespace {

constexpr char kRecoveryReason[] = "Installer.Recovery.Reason";

// Metrics file path in the stateful partition. See:
// init/upstart/send-recovery-metrics.conf
constexpr char kStatefulEventsPath[] = "/stateful/.recovery_histograms";

constexpr int kRecoveryReasonCode_NBR = 200;
constexpr int kRecoveryReasonCode_MAX = 255;

}  // namespace

namespace minios {

MetricsReporter::MetricsReporter(
    ProcessManagerInterface* process_manager,
    std::unique_ptr<MetricsLibraryInterface> metrics_lib)
    : process_manager_(process_manager) {
  if (metrics_lib)
    metrics_lib_ = std::move(metrics_lib);
  else
    metrics_lib_ = std::make_unique<MetricsLibrary>();

  metrics_lib_->Init();
}

void MetricsReporter::ReportNBRComplete() {
  if (!process_manager_ ||
      !process_manager_->RunCommand(
          {"/usr/bin/stateful_partition_for_recovery", "--mount"},
          ProcessManager::IORedirection{
              .input = minios::kDebugConsole,
              .output = minios::kDebugConsole,
          })) {
    // Ignore EBADF (b/187206298).
    if (errno != EBADF) {
      PLOG(WARNING) << "Failed to mount stateful partition.";
      return;
    }
  }

  metrics_lib_->SetOutputFile(kStatefulEventsPath);
  metrics_lib_->SendEnumToUMA(kRecoveryReason, kRecoveryReasonCode_NBR,
                              kRecoveryReasonCode_MAX);
}

}  // namespace minios
