// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_OUTPUT_MANAGER_H_
#define FBPREPROCESSOR_OUTPUT_MANAGER_H_

#include <set>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

class OutputManager : public SessionStateManager::Observer {
 public:
  static constexpr base::TimeDelta kDefaultExpiration = base::Minutes(30);

  explicit OutputManager(Manager* manager);
  OutputManager(const OutputManager&) = delete;
  OutputManager& operator=(const OutputManager&) = delete;
  ~OutputManager();

  void OnUserLoggedIn(const std::string& user_dir) override;
  void OnUserLoggedOut() override;

  // Adds a new firmware dump to be managed by the lifecycle manager. It will
  // automatically be deleted after |expiration|.
  void AddNewFile(const FirmwareDump& fw_dump,
                  const base::TimeDelta& expiration);

  void AddNewFile(const FirmwareDump& fw_dump);

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

  // Schedule a task that will delete the file with the expiration timestamp
  // closest to the current time |now|.
  // This function reads |files_| so it must be called while holding
  // |files_lock_|.
  void RestartExpirationTask(const base::Time& now);

  void OnExpiredFile();

  void DeleteAllManagedFiles();

  void DeleteAllFiles();

  std::set<OutputFile> files_;

  base::Lock files_lock_;

  base::OneShotTimer expiration_timer_;

  base::TimeDelta default_expiration_;

  base::FilePath user_root_dir_;

  Manager* manager_;

  base::WeakPtrFactory<OutputManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_OUTPUT_MANAGER_H_
