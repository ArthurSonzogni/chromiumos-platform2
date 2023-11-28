// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWSEC_TEST_UTILS_OWNERSHIP_ID_OWNERSHIP_ID_TPM2_H_
#define HWSEC_TEST_UTILS_OWNERSHIP_ID_OWNERSHIP_ID_TPM2_H_

#include <memory>
#include <optional>
#include <string>

#include <brillo/dbus/dbus_connection.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "hwsec-test-utils/ownership_id/ownership_id.h"

namespace hwsec_test_utils {

class OwnershipIdTpm2 : public OwnershipId {
 public:
  OwnershipIdTpm2() = default;
  virtual ~OwnershipIdTpm2() = default;

  std::optional<std::string> Get() override;

 private:
  bool InitializeTpmManager();

  brillo::DBusConnection connection_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_;
};

}  // namespace hwsec_test_utils

#endif  // HWSEC_TEST_UTILS_OWNERSHIP_ID_OWNERSHIP_ID_TPM2_H_
