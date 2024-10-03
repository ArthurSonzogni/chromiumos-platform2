// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_TPM_MANAGER_CLIENT_H_
#define RMAD_SYSTEM_TPM_MANAGER_CLIENT_H_

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

enum class GscDevice {
  GSC_DEVICE_NOT_GSC = 0,
  GSC_DEVICE_H1 = 1,
  GSC_DEVICE_DT = 2,
};

class TpmManagerClient {
 public:
  TpmManagerClient() = default;
  virtual ~TpmManagerClient() = default;

  virtual bool GetRoVerificationStatus(
      RoVerificationStatus* ro_verification_status) = 0;
  virtual bool GetGscDevice(GscDevice* gsc_device) = 0;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_TPM_MANAGER_CLIENT_H_
