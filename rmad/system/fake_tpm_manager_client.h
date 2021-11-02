// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_FAKE_TPM_MANAGER_CLIENT_H_
#define RMAD_SYSTEM_FAKE_TPM_MANAGER_CLIENT_H_

#include "rmad/system/tpm_manager_client.h"

#include <base/files/file_path.h>

namespace rmad {
namespace fake {

class FakeTpmManagerClient : public TpmManagerClient {
 public:
  explicit FakeTpmManagerClient(const base::FilePath& working_dir_path);
  FakeTpmManagerClient(const FakeTpmManagerClient&) = delete;
  FakeTpmManagerClient& operator=(const FakeTpmManagerClient&) = delete;
  ~FakeTpmManagerClient() override = default;

  bool GetRoVerificationStatus(
      RoVerificationStatus* ro_verification_status) override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_SYSTEM_FAKE_TPM_MANAGER_CLIENT_H_
