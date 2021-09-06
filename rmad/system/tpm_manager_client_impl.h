// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_TPM_MANAGER_CLIENT_IMPL_H_
#define RMAD_SYSTEM_TPM_MANAGER_CLIENT_IMPL_H_

#include "rmad/system/tpm_manager_client.h"

#include <memory>

#include <brillo/errors/error.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

namespace rmad {

class TpmManagerClientImpl : public TpmManagerClient {
 public:
  TpmManagerClientImpl();
  explicit TpmManagerClientImpl(
      std::unique_ptr<org::chromium::TpmManagerProxyInterface>
          tpm_manager_proxy);
  TpmManagerClientImpl(const TpmManagerClientImpl&) = delete;
  TpmManagerClientImpl& operator=(const TpmManagerClientImpl&) = delete;

  ~TpmManagerClientImpl() override = default;

  bool GetRoVerificationStatus(
      RoVerificationStatus* ro_verification_status) override;

 private:
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_TPM_MANAGER_CLIENT_IMPL_H_
