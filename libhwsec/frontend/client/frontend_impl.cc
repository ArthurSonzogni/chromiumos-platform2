// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/client/frontend_impl.h"

#include <optional>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<brillo::Blob> ClientFrontendImpl::GetRandomBlob(size_t size) {
  return middleware_.CallSync<&Backend::Random::RandomBlob>(size);
}

StatusOr<bool> ClientFrontendImpl::IsSrkRocaVulnerable() {
  return middleware_.CallSync<&Backend::Vendor::IsSrkRocaVulnerable>();
}

StatusOr<uint32_t> ClientFrontendImpl::GetFamily() {
  return middleware_.CallSync<&Backend::Vendor::GetFamily>();
}

StatusOr<uint64_t> ClientFrontendImpl::GetSpecLevel() {
  return middleware_.CallSync<&Backend::Vendor::GetSpecLevel>();
}

StatusOr<uint32_t> ClientFrontendImpl::GetManufacturer() {
  return middleware_.CallSync<&Backend::Vendor::GetManufacturer>();
}

StatusOr<uint32_t> ClientFrontendImpl::GetTpmModel() {
  return middleware_.CallSync<&Backend::Vendor::GetTpmModel>();
}

StatusOr<uint64_t> ClientFrontendImpl::GetFirmwareVersion() {
  return middleware_.CallSync<&Backend::Vendor::GetFirmwareVersion>();
}

StatusOr<brillo::Blob> ClientFrontendImpl::GetVendorSpecific() {
  return middleware_.CallSync<&Backend::Vendor::GetVendorSpecific>();
}

StatusOr<IFXFieldUpgradeInfo> ClientFrontendImpl::GetIFXFieldUpgradeInfo() {
  return middleware_.CallSync<&Backend::Vendor::GetIFXFieldUpgradeInfo>();
}

}  // namespace hwsec
