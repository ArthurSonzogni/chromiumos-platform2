// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Standalone tool that executes tests on a live TPM.

#include <cstdlib>

#include <base/at_exit.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <openssl/evp.h>

#include "cryptohome/tpm.h"
#include "cryptohome/tpm_live_test.h"

int main(int argc, char** argv) {
  brillo::FlagHelper::Init(argc, argv,
                           "Executes cryptohome tests on a live TPM.\nNOTE: "
                           "the TPM must be available and owned.");
  brillo::InitLog(brillo::kLogToStderr);
  base::AtExitManager exit_manager;
  OpenSSL_add_all_algorithms();
  LOG(INFO) << "Running TPM live tests.";

  const bool success = cryptohome::TpmLiveTest().RunLiveTests();
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
