// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "libhwsec/factory/factory_impl.h"
#include "libhwsec/structures/threading_mode.h"
#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/metrics/enterprise_rollback_metrics_handler.h"
#include "oobe_config/rollback_cleanup.h"

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(/*enable_process_id=*/true, /*enable_thread_id=*/true,
                       /*enable_timestamp=*/true, /*enable_tickcount=*/true);
}

}  // namespace

int main(int argc, char* argv[]) {
  InitLog();

  oobe_config::FileHandler file_handler;
  oobe_config::EnterpriseRollbackMetricsHandler metrics_handler;
  hwsec::FactoryImpl hwsec_factory;

  oobe_config::RollbackCleanup(&file_handler, &metrics_handler, &hwsec_factory);

  return 0;
}
