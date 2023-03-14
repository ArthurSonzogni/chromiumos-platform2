// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_BOOTLOCKBOX_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_BOOTLOCKBOX_FRONTEND_IMPL_H_

#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/bootlockbox/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/status.h"

namespace hwsec {

class BootLockboxFrontendImpl : public BootLockboxFrontend,
                                public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~BootLockboxFrontendImpl() override = default;

  StatusOr<StorageState> GetSpaceState() override;
  Status PrepareSpace(uint32_t size) override;
  StatusOr<brillo::Blob> LoadSpace() override;
  Status StoreSpace(const brillo::Blob& blob) override;
  Status LockSpace() override;
  void WaitUntilReady(base::OnceCallback<void(Status)> callback) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_BOOTLOCKBOX_FRONTEND_IMPL_H_
