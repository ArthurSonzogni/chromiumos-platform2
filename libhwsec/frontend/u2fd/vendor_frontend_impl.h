// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_U2FD_VENDOR_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_U2FD_VENDOR_FRONTEND_IMPL_H_

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/frontend/u2fd/vendor_frontend.h"
#include "libhwsec/status.h"

namespace hwsec {

class U2fVendorFrontendImpl : public U2fVendorFrontend, public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~U2fVendorFrontendImpl() override = default;

  StatusOr<bool> IsEnabled() override;
  StatusOr<u2f::GenerateResult> GenerateUserPresenceOnly(
      const brillo::Blob& app_id,
      const brillo::SecureBlob& user_secret,
      u2f::ConsumeMode consume_mode,
      u2f::UserPresenceMode up_mode) override;
  StatusOr<u2f::GenerateResult> Generate(
      const brillo::Blob& app_id,
      const brillo::SecureBlob& user_secret,
      u2f::ConsumeMode consume_mode,
      u2f::UserPresenceMode up_mode,
      const brillo::Blob& auth_time_secret_hash) override;
  StatusOr<u2f::Signature> SignUserPresenceOnly(
      const brillo::Blob& app_id,
      const brillo::SecureBlob& user_secret,
      const brillo::Blob& hash_to_sign,
      u2f::ConsumeMode consume_mode,
      u2f::UserPresenceMode up_mode,
      const brillo::Blob& key_handle) override;
  StatusOr<u2f::Signature> Sign(
      const brillo::Blob& app_id,
      const brillo::SecureBlob& user_secret,
      const std::optional<brillo::SecureBlob>& auth_time_secret,
      const brillo::Blob& hash_to_sign,
      u2f::ConsumeMode consume_mode,
      u2f::UserPresenceMode up_mode,
      const brillo::Blob& key_handle) override;
  Status CheckUserPresenceOnly(const brillo::Blob& app_id,
                               const brillo::SecureBlob& user_secret,
                               const brillo::Blob& key_handle) override;
  Status Check(const brillo::Blob& app_id,
               const brillo::SecureBlob& user_secret,
               const brillo::Blob& key_handle) override;
  StatusOr<u2f::Signature> G2fAttest(const brillo::Blob& app_id,
                                     const brillo::SecureBlob& user_secret,
                                     const brillo::Blob& challenge,
                                     const brillo::Blob& key_handle,
                                     const brillo::Blob& public_key) override;
  StatusOr<brillo::Blob> GetG2fAttestData(
      const brillo::Blob& app_id,
      const brillo::Blob& challenge,
      const brillo::Blob& key_handle,
      const brillo::Blob& public_key) override;
  StatusOr<u2f::Signature> CorpAttest(const brillo::Blob& app_id,
                                      const brillo::SecureBlob& user_secret,
                                      const brillo::Blob& challenge,
                                      const brillo::Blob& key_handle,
                                      const brillo::Blob& public_key,
                                      const brillo::Blob& salt) override;
  StatusOr<brillo::Blob> GetG2fCert() override;
  StatusOr<u2f::Config> GetConfig() override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_U2FD_VENDOR_FRONTEND_IMPL_H_
