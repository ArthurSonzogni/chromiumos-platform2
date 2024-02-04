// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_INPUT_MANAGER_H_
#define FBPREPROCESSOR_INPUT_MANAGER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

class InputManager : public SessionStateManager::Observer {
 public:
  explicit InputManager(Manager* manager, dbus::Bus* bus);
  InputManager(const InputManager&) = delete;
  InputManager& operator=(const InputManager&) = delete;
  ~InputManager();

  void OnUserLoggedIn(const std::string& user_dir) override;
  void OnUserLoggedOut() override;

  void set_base_dir_for_test(const base::FilePath& base_dir) {
    base_dir_ = base_dir;
  }

 private:
  void OnNewFirmwareDump(const FirmwareDump& fw_dump) const;

  void OnFirmwareDumpCreated(dbus::Signal* signal) const;

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success) const;

  void DeleteAllFiles() const;

  // Base directory to the root of the daemon-store where the firmware dumps are
  // stored, typically /run/daemon-store/fbpreprocessord/. Unit tests can
  // replace this directory with local temporary directories.
  base::FilePath base_dir_;

  // Path to the user-specific directory of the daemon-store, typically
  // ${base_dir_}/${user_hash}. Updated when the user logs in/out.
  base::FilePath user_root_dir_;

  Manager* manager_;

  // Proxy to receive D-Bus signals from crash-reporter.
  scoped_refptr<dbus::ObjectProxy> crash_reporter_proxy_;

  base::WeakPtrFactory<InputManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_INPUT_MANAGER_H_
