// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_OUTPUT_MANAGER_H_
#define FBPREPROCESSOR_OUTPUT_MANAGER_H_

#include <memory>
#include <set>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <brillo/dbus/dbus_method_response.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/platform_features_client.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

class OutputManager : public SessionStateManagerInterface::Observer,
                      public PlatformFeaturesClientInterface::Observer {
 public:
  static constexpr base::TimeDelta kDefaultExpiration = base::Minutes(30);

  explicit OutputManager(Manager* manager);
  OutputManager(const OutputManager&) = delete;
  OutputManager& operator=(const OutputManager&) = delete;
  ~OutputManager();

  void OnUserLoggedIn(const std::string& user_dir) override;
  void OnUserLoggedOut() override;

  void OnFeatureChanged(bool allowed) override;

  // Adds a new firmware dump to be managed by the lifecycle manager. It will
  // automatically be deleted after |expire_after_|.
  void AddFirmwareDump(const FirmwareDump& fw_dump);

  // A proxy for |GetAllAvailableDebugDumps| that collects all debug dumps.
  // Used by the async D-Bus method |org.chromium.FbPreprocessor.GetDebugDumps|.
  void GetDebugDumps(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<DebugDumps>>
          response);

  void set_base_dir_for_test(const base::FilePath& base_dir) {
    base_dir_ = base_dir;
  }

 private:
  class OutputFile {
   public:
    explicit OutputFile(const FirmwareDump& fw_dump,
                        const base::Time& expiration)
        : fw_dump_(fw_dump), expiration_(expiration) {}
    ~OutputFile() = default;

    FirmwareDump fw_dump() const { return fw_dump_; }
    base::Time expiration() const { return expiration_; }

    bool operator<(const OutputFile& other) const {
      return expiration_ < other.expiration_;
    }

   private:
    FirmwareDump fw_dump_;
    base::Time expiration_;
  };

  // Reads |files_| (with lock), generates corresponding |DebugDumps|, and
  // outputs |DebugDumps| as async D-Bus method response.
  void GetAllAvailableDebugDumps(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<DebugDumps>>
          response);

  // Schedule a task that will delete the file with the expiration timestamp
  // closest to the current time |now|.
  // This function reads |files_| so it must be called while holding
  // |files_lock_|.
  void RestartExpirationTask(const base::Time& now);

  void OnExpiredFile();

  // OutputManager maintains a list of firmware dumps "under management" that
  // have been added to the list with calls to AddFirmwareDump().
  // ClearManagedFiles() clears the list of firmware dumps managed by
  // OutputManager.
  // In addition, if |delete_files| is true then the function will attempt to
  // delete the files from disk. Otherwise it will only clear the list without
  // deleting the files from disk.
  void ClearManagedFiles(bool delete_files);

  void DeleteAllFiles();

  std::set<OutputFile> files_;

  base::Lock files_lock_;

  base::OneShotTimer expiration_timer_;

  // Delete pseudonymized firmware dumps from disk after |expire_after_|.
  base::TimeDelta expire_after_;

  // Base directory to the root of the daemon-store where the firmware dumps are
  // stored, typically /run/daemon-store/fbpreprocessord/. Unit tests can
  // replace this directory with local temporary directories.
  base::FilePath base_dir_;

  // Path to the user-specific directory of the daemon-store, typically
  // ${base_dir_}/${user_hash}. Updated when the user logs in/out.
  base::FilePath user_root_dir_;

  Manager* manager_;

  base::WeakPtrFactory<OutputManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_OUTPUT_MANAGER_H_
