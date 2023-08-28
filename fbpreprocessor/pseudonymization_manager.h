// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_PSEUDONYMIZATION_MANAGER_H_
#define FBPREPROCESSOR_PSEUDONYMIZATION_MANAGER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>

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

  void StartPseudonymization(const FirmwareDump& fw_dump);

  void OnUserLoggedIn(const std::string& user_dir) override;
  void OnUserLoggedOut() override;

 private:
  void DoNoOpPseudonymization(const FirmwareDump& input,
                              const FirmwareDump& output);

  void OnPseudonymizationComplete(const FirmwareDump& input,
                                  const FirmwareDump& output,
                                  bool success);

  base::FilePath user_root_dir_;

  Manager* manager_;

  base::WeakPtrFactory<PseudonymizationManager> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_PSEUDONYMIZATION_MANAGER_H_
