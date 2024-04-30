// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_ENDORSEMENT_PASSWORD_CHANGER_H_
#define VTPM_BACKENDS_ENDORSEMENT_PASSWORD_CHANGER_H_

#include "vtpm/backends/password_changer.h"

#include <optional>
#include <string>

#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <trunks/tpm_generated.h>

namespace vtpm {

class EndorsementPasswordChanger : public PasswordChanger {
 public:
  EndorsementPasswordChanger(
      org::chromium::TpmManagerProxyInterface* tpm_manager,
      const std::string virtual_password);
  ~EndorsementPasswordChanger() override = default;
  trunks::TPM_RC Change(std::string& command) override;

 private:
  std::optional<std::string> GetEndorsementPassword();
  org::chromium::TpmManagerProxyInterface* tpm_manager_;
  const std::string virtual_password_;
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_ENDORSEMENT_PASSWORD_CHANGER_H_
