// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_VENDOR_H_
#define LIBHWSEC_BACKEND_TPM2_VENDOR_H_

#include <cstdint>
#include <optional>

#include <brillo/secure_blob.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/ifx_info.h"

namespace hwsec {

class BackendTpm2;

class VendorTpm2 : public Backend::Vendor,
                   public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<uint32_t> GetFamily() override;
  StatusOr<uint64_t> GetSpecLevel() override;
  StatusOr<uint32_t> GetManufacturer() override;
  StatusOr<uint32_t> GetTpmModel() override;
  StatusOr<uint64_t> GetFirmwareVersion() override;
  StatusOr<brillo::Blob> GetVendorSpecific() override;
  StatusOr<int32_t> GetFingerprint() override;
  StatusOr<bool> IsSrkRocaVulnerable() override;
  StatusOr<brillo::Blob> GetRsuDeviceId() override;
  StatusOr<IFXFieldUpgradeInfo> GetIFXFieldUpgradeInfo() override;
  Status DeclareTpmFirmwareStable() override;
  StatusOr<brillo::Blob> SendRawCommand(const brillo::Blob& command) override;

 private:
  Status EnsureVersionInfo();

  bool fw_declared_stable_ = false;
  std::optional<tpm_manager::GetVersionInfoReply> version_info_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_VENDOR_H_
