// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_STATIC_UTILS_H_
#define LIBHWSEC_BACKEND_TPM2_STATIC_UTILS_H_

#include <string>

#include <brillo/secure_blob.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/device_config.h"

namespace hwsec {

StatusOr<std::string> SerializeFromTpmSignature(
    const trunks::TPMT_SIGNATURE& signature);

std::string GetTpm2PCRValueForMode(
    const DeviceConfigSettings::BootModeSetting::Mode& mode);

StatusOr<brillo::SecureBlob> GetEndorsementPassword(
    org::chromium::TpmManagerProxyInterface& tpm_manager);

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_STATIC_UTILS_H_
