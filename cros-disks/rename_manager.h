// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_RENAME_MANAGER_H_
#define CROS_DISKS_RENAME_MANAGER_H_

#include <map>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/process/process_reaper.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/cros-disks/dbus-constants.h>
#include <gtest/gtest_prod.h>

#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"

namespace cros_disks {

class RenameManager {
 public:
  class Observer {
   public:
    // Called when a renaming operation on a device has completed.
    virtual void OnRenameCompleted(const std::string& device_path,
                                   RenameError error_type) = 0;
  };

  using Reaper = brillo::ProcessReaper;

  RenameManager(Platform* platform, Reaper* reaper, Metrics* metrics = nullptr);

  RenameManager(const RenameManager&) = delete;

  // Starts a renaming process of a given device.
  RenameError StartRenaming(const std::string& device_path,
                            const std::string& device_file,
                            const std::string& volume_name,
                            const std::string& filesystem_type);

  void set_observer(Observer* observer) { observer_ = observer; }

 private:
  FRIEND_TEST(RenameManagerTest, CanRename);

  void OnRenameProcessTerminated(const std::string& device_path,
                                 const siginfo_t& info);

  // Returns true if renaming |source_path| is supported.
  bool CanRename(const std::string& source_path) const;

  // Platform service
  Platform* const platform_;

  // Process reaper.
  Reaper* const reaper_;

  // Optional UMA metrics collector.
  Metrics* const metrics_;

  // Optional observer.
  Observer* observer_ = nullptr;

  // Outstanding renaming processes indexed by device path.
  std::map<std::string, SandboxedProcess> rename_process_;

  base::WeakPtrFactory<RenameManager> weak_ptr_factory_{this};
};

}  // namespace cros_disks

#endif  // CROS_DISKS_RENAME_MANAGER_H_
