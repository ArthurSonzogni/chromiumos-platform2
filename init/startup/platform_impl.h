// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_PLATFORM_IMPL_H_
#define INIT_STARTUP_PLATFORM_IMPL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

#include "init/crossystem.h"

namespace startup {

// Platform defines functions that interface with the filesystem and
// other utilities that we want to override for testing. That includes
// wrappers functions for syscalls.
class Platform {
 public:
  Platform() {}
  virtual ~Platform() = default;

  // Wrapper around stat(2).
  virtual bool Stat(const base::FilePath& path, struct stat* st);

  // Wrapper around mount(2).
  virtual bool Mount(const base::FilePath& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     unsigned long flags,  // NOLINT(runtime/int)
                     const std::string& data);
  virtual bool Mount(const std::string& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     unsigned long flags,  // NOLINT(runtime/int)
                     const std::string& data);

  // Wrapper around umount(2).
  virtual bool Umount(const base::FilePath& path);

  // Wrapper around open(2).
  virtual base::ScopedFD Open(const base::FilePath& pathname, int flags);

  // Wrapper around ioctl(2).
  // Can't create virtual templated methods, so define per use case.
  // NOLINTNEXTLINE(runtime/int)
  virtual int Ioctl(int fd, unsigned long request, int* arg1);

  // Runs chromeos-boot-alert with the given arg.
  virtual void BootAlert(const std::string& arg);

  // Runs clobber-state with the given args.
  [[noreturn]] virtual void Clobber(const std::vector<std::string> args);

  // Runs hiberman resume-init with the given output file.
  virtual bool RunHiberman(const base::FilePath& output_file);

  // Run vpd with the given args.
  virtual bool VpdSlow(const std::vector<std::string>& args,
                       std::string* output);

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
  void AddClobberCrashReport(const std::vector<std::string> args);

  // Runs e2fsck for the given device.
  void ReplayExt4Journal(const base::FilePath& dev);

  // Runs clobber-log --repair for the given device with the given message.
  void ClobberLogRepair(const base::FilePath& dev, const std::string& msg);

  // Determine if the device is in dev mode.
  bool InDevMode(CrosSystem* cros_system);
  bool IsDebugBuild(CrosSystem* const cros_system);
};

}  // namespace startup

#endif  // INIT_STARTUP_PLATFORM_IMPL_H_
