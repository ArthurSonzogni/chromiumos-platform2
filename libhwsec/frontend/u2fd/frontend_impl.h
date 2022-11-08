// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_U2FD_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_U2FD_FRONTEND_IMPL_H_

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/frontend/u2fd/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class U2fFrontendImpl : public U2fFrontend, public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~U2fFrontendImpl() override = default;

  StatusOr<bool> IsEnabled() override;
  StatusOr<bool> IsReady() override;
  StatusOr<CreateKeyResult> GenerateRSASigningKey(
      const brillo::SecureBlob& auth_value) override;
  StatusOr<RSAPublicInfo> GetRSAPublicKey(Key key) override;
  StatusOr<ScopedKey> LoadKey(const brillo::Blob& key_blob,
                              const brillo::SecureBlob& auth_value) override;
  StatusOr<brillo::Blob> RSASign(Key key, const brillo::Blob& data) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_U2FD_FRONTEND_IMPL_H_
