// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_TPM_MANAGER_CLIENT_H_
#define RMAD_SYSTEM_TPM_MANAGER_CLIENT_H_

namespace rmad {

enum class RoVerificationStatus {
  NOT_TRIGGERED,
  PASS,
  FAIL,
  UNSUPPORTED,
  UNSUPPORTED_NOT_TRIGGERED,
  UNSUPPORTED_TRIGGERED
};

class TpmManagerClient {
 public:
  TpmManagerClient() = default;
  virtual ~TpmManagerClient() = default;

  virtual bool GetRoVerificationStatus(
      RoVerificationStatus* ro_verification_status) = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_TPM_MANAGER_CLIENT_H_
