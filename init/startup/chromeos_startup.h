// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_CHROMEOS_STARTUP_H_
#define INIT_STARTUP_CHROMEOS_STARTUP_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <metrics/bootstat.h>

#include "init/crossystem.h"
#include "init/startup/platform_impl.h"

namespace startup {

// Define a struct to contain the flags we set when parsing USE flags.
struct Flags {
  // Indicates built with USE=encrypted_reboot_vault. Used to determine
  // if we need to setup the encrypted reboot vault.
  bool encrypted_reboot_vault;
  // Indicates built with USE=encrypted_stateful, used to determine which
  // mount function to use
  bool encstateful;
  // Indicates built with USE=direncryption. Used when mounting the stateful
  // partition to determine if we should enable directory encryption.
  bool direncryption;
  // Indicates built with USE=fsverity. Used when mounting the stateful
  // partition.
  bool fsverity;
  // Indicates built with USE=lvm_stateful_partition. Used when mounting the
  // stateful partition.
  bool lvm_stateful;
  // Indicates built with USE=prjquota. Used when mounting the stateful
  // partition.
  bool prjquota;
  // Not a USE flag, but indicates if built with both USE=tpm2 and
  // USE=encrypted_stateful. Used to determine if we will try to create
  // a system key.
  bool sys_key_util;
};

// This is the primary class for the startup functionality, making use of the
// other classes in the startup directory. chromeos_startup sets up different
// mount points, initializes kernel sysctl settings, configures security
// policies sets up the stateful partition, checks if we need a stateful wipe,
// gathers logs and collects crash reports.
class ChromeosStartup {
 public:
  // Process the included USE flags.
  static void ParseFlags(Flags* flags);

  // Constructor for the class
  ChromeosStartup(std::unique_ptr<CrosSystem> cros_system,
                  const Flags& flags,
                  const base::FilePath& root,
                  const base::FilePath& stateful,
                  const base::FilePath& lsb_file,
                  const base::FilePath& proc_file,
                  std::unique_ptr<Platform> platform);
  virtual ~ChromeosStartup() = default;

  void Sysctl();

  // EarlySetup contains the early mount calls of chromeos_startup. This
  // function exists to help break up the Run function into smaller functions.
  void EarlySetup();

  // Run the chromeos startup routine.
  int Run();

 private:
  void CheckClock();
  // Runs the bash version of chromeos startup to allow for incremental
  // migration.
  int RunChromeosStartupScript();

  std::unique_ptr<CrosSystem> cros_system_;
  const base::FilePath lsb_file_;
  const base::FilePath proc_;
  const base::FilePath root_;
  const base::FilePath stateful_;
  bootstat::BootStat bootstat_;
  std::unique_ptr<Platform> platform_;
  bool disable_stateful_security_hardening_;
};

}  // namespace startup

#endif  // INIT_STARTUP_CHROMEOS_STARTUP_H_
