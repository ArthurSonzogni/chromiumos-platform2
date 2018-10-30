// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a utility to clear internal crypto entropy (if applicable) from
// |BiometricsManager|s, so as to render useless templates and other user data
// encrypted with old secrets.

#include <sys/types.h>
#include <unistd.h>

#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/process/process.h>
#include <base/time/time.h>

#include "biod/cros_fp_biometrics_manager.h"

namespace {

static int64_t kTimeoutSeconds = 30;

int DoBioWash() {
  base::MessageLoopForIO message_loop;
  std::vector<std::unique_ptr<biod::BiometricsManager>> managers;
  // Add all the possible BiometricsManagers available.
  std::unique_ptr<biod::BiometricsManager> cros_fp_bio =
      biod::CrosFpBiometricsManager::Create();
  if (cros_fp_bio) {
    managers.emplace_back(std::move(cros_fp_bio));
  }

  if (managers.empty()) {
    LOG(ERROR) << "No biometrics managers instantiated correctly.";
    return -1;
  }

  int ret = 0;
  for (const auto& biometrics_manager : managers) {
    if (!biometrics_manager->ResetEntropy()) {
      LOG(ERROR) << "Failed to reset entropy for sensor type: "
                 << biometrics_manager->GetType();
      ret = -1;
    }
  }

  return ret;
}

}  // namespace

int main(int argc, char* argv[]) {
  pid_t pid;
  pid = fork();

  if (pid == -1) {
    PLOG(ERROR) << "Failed to fork child process for bio_wash.";
    return -1;
  }

  if (pid == 0) {
    return DoBioWash();
  }

  auto process = base::Process::Open(pid);
  int exit_code;
  if (!process.WaitForExitWithTimeout(
          base::TimeDelta::FromSeconds(kTimeoutSeconds), &exit_code)) {
    LOG(ERROR) << "Bio wash timeout out, exit code: " << exit_code;
    process.Terminate(-1, false);
  }

  return exit_code;
}
