// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_STARTUP_DEP_IMPL_H_
#define INIT_STARTUP_STARTUP_DEP_IMPL_H_

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <libcrossystem/crossystem.h>
#include <libstorage/platform/platform.h>

namespace startup {

// Determine if the device is using a test image.
bool IsTestImage(libstorage::Platform* platform,
                 const base::FilePath& lsb_file);

// Determines if the device is in factory test mode.
bool IsFactoryTestMode(libstorage::Platform* platform,
                       crossystem::Crossystem* crossystem,
                       const base::FilePath& base_dir);

// Determine if the device is in dev mode.
bool InDevMode(crossystem::Crossystem* crossystem);
bool IsDebugBuild(crossystem::Crossystem* crossystem);

// Determines if the device is in either factory test mode or in factory
// installer mode.
bool IsFactoryMode(libstorage::Platform* platform,
                   crossystem::Crossystem* crossystem,
                   const base::FilePath& base_dir);

// StartupDep defines functions that interface with the filesystem and
// other utilities that we want to override for testing. That includes
// wrappers functions for syscalls.
class StartupDep {
 public:
  StartupDep() {}
  virtual ~StartupDep() = default;

  // Runs mount-encrypted with the given arg.
  virtual int MountEncrypted(const std::vector<std::string>& args,
                             std::string* output);

  // Runs chromeos-boot-alert with the given arg.
  virtual void BootAlert(const std::string& arg);

  // Runs clobber-state with the given args.
  [[noreturn]] virtual void Clobber(const std::vector<std::string> args);

  // Run clobber-log with the given message.
  virtual void ClobberLog(const std::string& msg);

  // Execute a clobber by first calling BootAlert and then
  // ClobberLog with the given messages, then exec clobber-state.
  virtual void Clobber(const std::string& boot_alert_msg,
                       const std::vector<std::string>& args,
                       const std::string& clobber_log_msg);

  virtual void RemoveInBackground(const std::vector<base::FilePath>& paths);

  // Run cmd_path as a brillo process.
  virtual void RunProcess(const base::FilePath& cmd_path);

  // Runs crash_reporter with the given args.
  virtual void AddClobberCrashReport(const std::vector<std::string> args);

  // Get the path of a partition on the root device with the given label.
  virtual std::optional<base::FilePath> GetRootDevicePartitionPath(
      const std::string& partition_label);

  // Runs e2fsck for the given device.
  void ReplayExt4Journal(const base::FilePath& dev);

  // Runs clobber-log --repair for the given device with the given message.
  void ClobberLogRepair(const base::FilePath& dev, const std::string& msg);
};

}  // namespace startup

#endif  // INIT_STARTUP_STARTUP_DEP_IMPL_H_
