// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_READ_MATCH_SECRET_WITH_PUBKEY_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_READ_MATCH_SECRET_WITH_PUBKEY_COMMAND_H_

#include <algorithm>
#include <memory>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpReadMatchSecretWithPubkeyCommand
    : public EcCommand<struct ec_params_fp_read_match_secret_with_pubkey,
                       struct ec_response_fp_read_match_secret_with_pubkey> {
 public:
  template <typename T = FpReadMatchSecretWithPubkeyCommand>
  static std::unique_ptr<T> Create(uint16_t index,
                                   const brillo::Blob& pk_in_x,
                                   const brillo::Blob& pk_in_y) {
    static_assert(std::is_base_of<FpReadMatchSecretWithPubkeyCommand, T>::value,
                  "Only classes derived from "
                  "FpReadMatchSecretWithPubkeyCommand can use Create");

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto cmd = base::WrapUnique(new T());
    auto* req = cmd->Req();
    if (pk_in_x.size() != sizeof(req->pubkey.x) ||
        pk_in_y.size() != sizeof(req->pubkey.y)) {
      return nullptr;
    }
    req->fgr = index;
    std::copy(pk_in_x.begin(), pk_in_x.end(), req->pubkey.x);
    std::copy(pk_in_y.begin(), pk_in_y.end(), req->pubkey.y);
    return cmd;
  }
  ~FpReadMatchSecretWithPubkeyCommand() override = default;

  virtual brillo::Blob EncryptedSecret() const;
  virtual brillo::Blob Iv() const;
  virtual brillo::Blob PkOutX() const;
  virtual brillo::Blob PkOutY() const;

 protected:
  FpReadMatchSecretWithPubkeyCommand()
      : EcCommand(EC_CMD_FP_READ_MATCH_SECRET_WITH_PUBKEY) {}
};

static_assert(
    !std::is_copy_constructible<FpReadMatchSecretWithPubkeyCommand>::value,
    "EcCommands are not copyable by default");
static_assert(
    !std::is_copy_assignable<FpReadMatchSecretWithPubkeyCommand>::value,
    "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_READ_MATCH_SECRET_WITH_PUBKEY_COMMAND_H_
