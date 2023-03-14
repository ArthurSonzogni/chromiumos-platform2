// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/bootlockbox/frontend_impl.h"

#include <optional>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/space.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<BootLockboxFrontend::StorageState>
BootLockboxFrontendImpl::GetSpaceState() {
  return middleware_.CallSync<&Backend::Storage::IsReady>(Space::kBootlockbox);
}

Status BootLockboxFrontendImpl::PrepareSpace(uint32_t size) {
  return middleware_.CallSync<&Backend::Storage::Prepare>(Space::kBootlockbox,
                                                          size);
}

StatusOr<brillo::Blob> BootLockboxFrontendImpl::LoadSpace() {
  return middleware_.CallSync<&Backend::Storage::Load>(Space::kBootlockbox);
}

Status BootLockboxFrontendImpl::StoreSpace(const brillo::Blob& blob) {
  return middleware_.CallSync<&Backend::Storage::Store>(Space::kBootlockbox,
                                                        blob);
}

Status BootLockboxFrontendImpl::LockSpace() {
  return middleware_.CallSync<&Backend::Storage::Lock>(
      Space::kBootlockbox, Storage::LockOptions{.write_lock = true});
}

void BootLockboxFrontendImpl::WaitUntilReady(
    base::OnceCallback<void(Status)> callback) {
  middleware_.CallAsync<&Backend::State::WaitUntilReady>(std::move(callback));
}

}  // namespace hwsec
