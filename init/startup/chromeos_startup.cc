// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <time.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/process/process.h>

#include "init/crossystem.h"
#include "init/crossystem_impl.h"
#include "init/startup/chromeos_startup.h"
#include "init/startup/constants.h"
#include "init/startup/lib.h"

namespace startup {

// Process the arguments from included USE flags.
void ChromeosStartup::ParseFlags(Flags* flags) {
  flags->direncryption = USE_DIRENCRYPTION;
  flags->fsverity = USE_FSVERITY;
  flags->prjquota = USE_PRJQUOTA;
  flags->encstateful = USE_ENCRYPTED_STATEFUL;
  if (flags->encstateful) {
    flags->sys_key_util = USE_TPM2;
  }
  // Note: encrypted_reboot_vault is disabled only for Gale
  // to be able to use openssl 1.1.1.
  flags->encrypted_reboot_vault = USE_ENCRYPTED_REBOOT_VAULT;
  flags->lvm_stateful = USE_LVM_STATEFUL_PARTITION;
}

// We manage this base timestamp by hand. It isolates us from bad clocks on
// the system where this image was built/modified, and on the runtime image
// (in case a dev modified random paths while the clock was out of sync).
// TODO(b/234157809): Our namespaces module doesn't support time namespaces
// currently. Add unittests for CheckClock once we add support.
void ChromeosStartup::CheckClock() {
  time_t cur_time;
  time(&cur_time);

  if (cur_time < kBaseSecs) {
    struct timespec stime;
    stime.tv_sec = kBaseSecs;
    stime.tv_nsec = 0;
    if (clock_settime(CLOCK_REALTIME, &stime) != 0) {
      PLOG(WARNING) << "Unable to set time.";
    }
  }
}

ChromeosStartup::ChromeosStartup(std::unique_ptr<CrosSystem> cros_system,
                                 const Flags& flags,
                                 const base::FilePath& root,
                                 const base::FilePath& stateful,
                                 const base::FilePath& lsb_file,
                                 const base::FilePath& proc_file)
    : cros_system_(std::move(cros_system)),
      lsb_file_(lsb_file),
      proc_(proc_file),
      root_(root),
      stateful_(stateful) {}

// Main function to run chromeos_startup.
int ChromeosStartup::Run() {
  // Make sure our clock is somewhat up-to-date. We don't need any resources
  // mounted below, so do this early on.
  CheckClock();

  // bootstat writes timings to tmpfs.
  bootstat_.LogEvent("pre-startup");

  int ret = RunChromeosStartupScript();
  if (ret) {
    LOG(WARNING) << "chromeos_startup.sh returned with code " << ret;
  }

  bootstat_.LogEvent("post-startup");

  return ret;
}

// Temporary function during the migration of the code. Run the bash
// version of chromeos_startup, which has been copied to chromeos_startup.sh
// to allow editing without effecting existing script. As more functionality
// moves to c++, it will be removed from chromeos_startup.sh.
int ChromeosStartup::RunChromeosStartupScript() {
  brillo::ProcessImpl proc;
  proc.AddArg("/sbin/chromeos_startup.sh");
  return proc.Run();
}

}  // namespace startup
