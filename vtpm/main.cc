// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <brillo/syslog_logging.h>
#include <libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h>

#include "vtpm/commands/null_command.h"
#include "vtpm/commands/virtualizer.h"
#include "vtpm/vtpm_daemon.h"

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int flags = brillo::kLogToSyslog;
  if (cl->HasSwitch("log_to_stderr")) {
    flags |= brillo::kLogToStderr;
  }
  brillo::InitLog(flags);
  // Set TPM metrics client ID.
  hwsec_foundation::SetTpmMetricsClientID(
      hwsec_foundation::TpmMetricsClientID::kChaps);

  std::unique_ptr<vtpm::Command> vtpm =
      vtpm::Virtualizer::Create(vtpm::Virtualizer::Profile::kGLinux);

  return vtpm::VtpmDaemon(vtpm.get()).Run();
}
