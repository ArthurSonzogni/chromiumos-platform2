// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_TSS_HELPER_H_
#define LIBHWSEC_BACKEND_TPM1_TSS_HELPER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "libhwsec/proxy/proxy.h"
#include "libhwsec/status.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

namespace hwsec {

// The helper class for TSS context and objects.
class TssHelper {
 public:
  TssHelper(org::chromium::TpmManagerProxyInterface& tpm_manager,
            overalls::Overalls& overalls)
      : tpm_manager_(tpm_manager), overalls_(overalls) {}

  StatusOr<ScopedTssContext> GetScopedTssContext();
  StatusOr<TSS_HCONTEXT> GetTssContext();
  StatusOr<TSS_HTPM> GetTpmHandle();

  // The delegate TPM handle would not be cached to prevent leaking the delegate
  // permission.
  StatusOr<base::ScopedClosureRunner> SetTpmHandleAsDelegate();

  // The owner TPM handle would not be cached to prevent leaking the owner
  // permission.
  StatusOr<base::ScopedClosureRunner> SetTpmHandleAsOwner();

  // Set TpmHandle according to EK readability. If EK can be read by delegate,
  // the TpmHandle would set as delegate, otherwise as owner.
  StatusOr<base::ScopedClosureRunner> SetTpmHandleByEkReadability();

 private:
  StatusOr<tpm_manager::GetTpmStatusReply> GetTpmStatusReply();

  StatusOr<base::ScopedClosureRunner> SetAsDelegate(
      TSS_HTPM local_tpm_handle,
      const tpm_manager::AuthDelegate& owner_delegate);

  StatusOr<base::ScopedClosureRunner> SetAsOwner(
      TSS_HTPM local_tpm_handle, const std::string& owner_password);

  StatusOr<bool> CanDelegateReadInternalPub(brillo::Blob& delegate_blob);

  org::chromium::TpmManagerProxyInterface& tpm_manager_;
  overalls::Overalls& overalls_;

  std::optional<ScopedTssContext> tss_context_;
  std::optional<ScopedTssObject<TSS_HTPM>> user_tpm_handle_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_TSS_HELPER_H_
