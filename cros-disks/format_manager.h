// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_FORMAT_MANAGER_H_
#define CROS_DISKS_FORMAT_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/process/process_reaper.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>

#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"

namespace cros_disks {

class FormatManager {
 public:
  class Observer {
   public:
    // Called when a formatting operation on a device has completed.
    virtual void OnFormatCompleted(const std::string& device_path,
                                   FormatError error_type) = 0;
  };

  using Reaper = brillo::ProcessReaper;

  explicit FormatManager(Platform* platform,
                         Reaper* reaper,
                         Metrics* metrics = nullptr);

  FormatManager(const FormatManager&) = delete;

  void SetObserver(Observer* const observer) { observer_ = observer; }

  // Starts a formatting process of a given device.
  FormatError StartFormatting(const std::string& device_path,
                              const std::string& device_file,
                              const std::string& filesystem,
                              const std::vector<std::string>& options);

 private:
  FRIEND_TEST(FormatManagerTest, GetFormatProgramPath);
  FRIEND_TEST(FormatManagerTest, IsFilesystemSupported);

  void OnDone(const std::string& fs_type,
              const std::string& device_path,
              const base::ElapsedTimer& timer,
              const siginfo_t& info);

  // Returns the full path of an external formatting program if it is
  // found in some predefined locations. Otherwise, an empty string is
  // returned.
  std::string GetFormatProgramPath(const std::string& filesystem) const;

  // Returns true if formatting a given file system is supported.
  bool IsFilesystemSupported(const std::string& filesystem) const;

  // Platform service.
  Platform* const platform_;

  // Process reaper.
  Reaper* const reaper_;

  // Optional UMA metrics collector.
  Metrics* const metrics_;

  // Optional observer.
  Observer* observer_ = nullptr;

  // Outstanding formatting processes indexed by device path.
  std::map<std::string, SandboxedProcess> format_process_;

  base::WeakPtrFactory<FormatManager> weak_ptr_factory_{this};
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FORMAT_MANAGER_H_
