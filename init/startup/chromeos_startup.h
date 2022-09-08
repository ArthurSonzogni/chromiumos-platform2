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
#include "init/startup/flags.h"
#include "init/startup/platform_impl.h"

namespace startup {

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
};

}  // namespace startup

#endif  // INIT_STARTUP_CHROMEOS_STARTUP_H_
