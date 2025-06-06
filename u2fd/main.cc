// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <brillo/vcsid.h>
#include <libhwsec-foundation/profiling/profiling.h>
#include <libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h>
#include <sysexits.h>

#include "u2fd/u2f_daemon.h"

int main(int argc, char* argv[]) {
  DEFINE_bool(force_u2f, false, "force U2F mode even if disabled by policy");
  DEFINE_bool(force_g2f, false,
              "force U2F mode plus extensions regardless of policy");
  DEFINE_bool(g2f_allowlist_data, false,
              "append allowlisting data to G2F register responses");
  DEFINE_bool(verbose, false, "verbose logging");
  DEFINE_bool(force_disable_corp_protocol, false,
              "disable corp internal APDU protocol");
  DEFINE_bool(force_activate_fips, false, "force activate FIPS mode in GSC");
  DEFINE_bool(force_enable_global_key, false, "force enable global keys");

  brillo::FlagHelper::Init(argc, argv, "u2fd, U2FHID emulation daemon.");

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);
  if (FLAGS_verbose) {
    logging::SetMinLogLevel(-1);
  }

  LOG(INFO) << "Daemon version " << brillo::kShortVCSID.value_or("<unknown>");

  // Set TPM metrics client ID.
  hwsec_foundation::SetTpmMetricsClientID(
      hwsec_foundation::TpmMetricsClientID::kU2f);

  u2f::U2fDaemon daemon(FLAGS_force_u2f, FLAGS_force_g2f,
                        !FLAGS_force_disable_corp_protocol,
                        FLAGS_g2f_allowlist_data, FLAGS_force_activate_fips,
                        FLAGS_force_enable_global_key);

  // Start profiling.
  hwsec_foundation::SetUpProfiling();

  int rc = daemon.Run();

  return rc == EX_UNAVAILABLE ? EX_OK : rc;
}
