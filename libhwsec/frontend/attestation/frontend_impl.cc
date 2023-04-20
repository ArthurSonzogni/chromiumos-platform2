// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/attestation/frontend_impl.h"

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"

namespace hwsec {

StatusOr<brillo::SecureBlob> AttestationFrontendImpl::Unseal(
    const brillo::Blob& sealed_data) {
  return middleware_.CallSync<&Backend::Sealing::Unseal>(
      OperationPolicy{
          .device_configs =
              DeviceConfigs{
                  DeviceConfig::kBootMode,
              },
          .permission =
              Permission{
                  .auth_value = brillo::SecureBlob(""),
              },
      },
      sealed_data, Sealing::UnsealOptions{});
}
StatusOr<brillo::Blob> AttestationFrontendImpl::Seal(
    const brillo::SecureBlob& unsealed_data) {
  return middleware_.CallSync<&Backend::Sealing::Seal>(
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .boot_mode =
                      DeviceConfigSettings::BootModeSetting{
                          .mode = std::nullopt,
                      },
              },
          .permission =
              Permission{
                  .auth_value = brillo::SecureBlob(""),
              },
      },
      unsealed_data);
}

}  // namespace hwsec
