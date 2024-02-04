// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_PSEUDONYMIZATION_MANAGER_H_
#define FBPREPROCESSOR_PSEUDONYMIZATION_MANAGER_H_

#include <set>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/time/time.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/session_state_manager.h"

namespace fbpreprocessor {

class PseudonymizationManager : public SessionStateManager::Observer {
 public:
  explicit PseudonymizationManager(Manager* manager);
  PseudonymizationManager(const PseudonymizationManager&) = delete;
  PseudonymizationManager& operator=(const PseudonymizationManager&) = delete;
  ~PseudonymizationManager();

  // Start the pseudonymization of a firmware dump. The pseudonymization is done
  // asynchronously.
  // Returns false if we failed to start the pseudonymization process, true
  // otherwise. Note that the method will return true if the pseudonymization
  // started successfully, but the pseudonymization process could still fail.
  bool StartPseudonymization(const FirmwareDump& fw_dump);

  void OnUserLoggedIn(const std::string& user_dir) override;
  void OnUserLoggedOut() override;

  void set_base_dir_for_test(const base::FilePath& base_dir) {
    base_dir_ = base_dir;
  }

 private:
  void DoNoOpPseudonymization(const FirmwareDump& input,
                              const FirmwareDump& output) const;

  void OnPseudonymizationComplete(const FirmwareDump& input,
                                  const FirmwareDump& output,
                                  bool success) const;

  // Returns true if we haven't handled "too many" pseudonymizations recently
  // and we can start a new one without exceeding the rate limits.
  // Returns false otherwise.
  bool RateLimitingAllowsNewPseudonymization();

  // Reset the state of the logic that verifies that we don't start "too many"
  // pseudonymizations. Typically called on login/logout to clear the state left
  // by previous users.
  void ResetRateLimiter();

  // Base directory to the root of the daemon-store where the firmware dumps are
  // stored, typically /run/daemon-store/fbpreprocessord/. Unit tests can
  // replace this directory with local temporary directories.
  base::FilePath base_dir_;

  // Path to the user-specific directory of the daemon-store, typically
  // ${base_dir_}/${user_hash}. Updated when the user logs in/out.
  base::FilePath user_root_dir_;

  // Keep track of the timestamps when recent pseudonymization operations were
  // started. Every time we receive a request to start a pseudonymization, we
  // look up how many operations happened recently and check that we're not
  // exceeding the rate limits.
  std::set<base::Time> recently_processed_;

  // Lock that protects accesses to |recently_processed_|.
  base::Lock recently_processed_lock_;

  Manager* manager_;

  base::WeakPtrFactory<PseudonymizationManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_PSEUDONYMIZATION_MANAGER_H_
