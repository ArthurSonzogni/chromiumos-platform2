// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_TPM_MANAGER_CLIENT_IMPL_H_
#define RMAD_SYSTEM_TPM_MANAGER_CLIENT_IMPL_H_

#include <memory>

#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "rmad/system/tpm_manager_client.h"

namespace rmad {

class TpmManagerClientImpl : public TpmManagerClient {
 public:
  TpmManagerClientImpl();
  explicit TpmManagerClientImpl(
      std::unique_ptr<org::chromium::TpmManagerProxyInterface>
          tpm_manager_proxy);
  TpmManagerClientImpl(const TpmManagerClientImpl&) = delete;
  TpmManagerClientImpl& operator=(const TpmManagerClientImpl&) = delete;

  ~TpmManagerClientImpl() override;

  bool GetRoVerificationStatus(
      RoVerificationStatus* ro_verification_status) override;
  bool GetGscVersion(GscVersion* gsc_version) override;

 private:
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_TPM_MANAGER_CLIENT_IMPL_H_
