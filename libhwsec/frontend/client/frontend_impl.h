// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CLIENT_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_CLIENT_FRONTEND_IMPL_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/client/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class HWSEC_EXPORT ClientFrontendImpl : public ClientFrontend,
                                        public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~ClientFrontendImpl() override = default;

  StatusOr<brillo::Blob> GetRandomBlob(size_t size) override;
  StatusOr<bool> IsSrkRocaVulnerable() override;
  StatusOr<uint32_t> GetFamily() override;
  StatusOr<uint64_t> GetSpecLevel() override;
  StatusOr<uint32_t> GetManufacturer() override;
  StatusOr<uint32_t> GetTpmModel() override;
  StatusOr<uint64_t> GetFirmwareVersion() override;
  StatusOr<brillo::Blob> GetVendorSpecific() override;
  StatusOr<IFXFieldUpgradeInfo> GetIFXFieldUpgradeInfo() override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CLIENT_FRONTEND_IMPL_H_
