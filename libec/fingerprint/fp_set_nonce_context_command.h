// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_SET_NONCE_CONTEXT_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_SET_NONCE_CONTEXT_COMMAND_H_

#include <algorithm>
#include <memory>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpSetNonceContextCommand
    : public EcCommand<struct ec_params_fp_nonce_context, EmptyParam> {
 public:
  template <typename T = FpSetNonceContextCommand>
  static std::unique_ptr<T> Create(const brillo::Blob& nonce,
                                   const brillo::Blob& encrypted_user_id,
                                   const brillo::Blob& iv) {
    static_assert(
        std::is_base_of<FpSetNonceContextCommand, T>::value,
        "Only classes derived from FpSetNonceContextCommand can use Create");

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto cmd = base::WrapUnique(new T());
    auto* req = cmd->Req();
    if (nonce.size() != sizeof(req->gsc_nonce) ||
        encrypted_user_id.size() != sizeof(req->enc_user_id) ||
        iv.size() != sizeof(req->enc_user_id_iv)) {
      return nullptr;
    }
    std::copy(nonce.begin(), nonce.end(), req->gsc_nonce);
    std::copy(encrypted_user_id.begin(), encrypted_user_id.end(),
              req->enc_user_id);
    std::copy(iv.begin(), iv.end(), req->enc_user_id_iv);
    return cmd;
  }
  ~FpSetNonceContextCommand() override = default;

 protected:
  FpSetNonceContextCommand() : EcCommand(EC_CMD_FP_NONCE_CONTEXT) {}
};

static_assert(!std::is_copy_constructible<FpSetNonceContextCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpSetNonceContextCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_SET_NONCE_CONTEXT_COMMAND_H_
