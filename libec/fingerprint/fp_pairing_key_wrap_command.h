// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_PAIRING_KEY_WRAP_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_PAIRING_KEY_WRAP_COMMAND_H_

#include <algorithm>
#include <memory>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpPairingKeyWrapCommand
    : public EcCommand<struct ec_params_fp_establish_pairing_key_wrap,
                       struct ec_response_fp_establish_pairing_key_wrap> {
 public:
  template <typename T = FpPairingKeyWrapCommand>
  static std::unique_ptr<T> Create(const brillo::Blob& pub_x,
                                   const brillo::Blob& pub_y,
                                   const brillo::Blob& encrypted_priv) {
    static_assert(std::is_base_of<FpPairingKeyWrapCommand, T>::value,
                  "Only classes derived from "
                  "FpPairingKeyWrapCommand can use Create");

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto cmd = base::WrapUnique(new T());
    auto* req = cmd->Req();
    if (pub_x.size() != sizeof(req->peers_pubkey.x) ||
        pub_y.size() != sizeof(req->peers_pubkey.y) ||
        encrypted_priv.size() != sizeof(req->encrypted_private_key)) {
      return nullptr;
    }
    std::copy(pub_x.begin(), pub_x.end(), req->peers_pubkey.x);
    std::copy(pub_y.begin(), pub_y.end(), req->peers_pubkey.y);
    std::copy(encrypted_priv.begin(), encrypted_priv.end(),
              reinterpret_cast<uint8_t*>(&req->encrypted_private_key));
    return cmd;
  }
  ~FpPairingKeyWrapCommand() override = default;

  // This returns the serialized encrypted pairing key, the shared key between
  // FPMCU and GSC. This is persisted in the userland storage instead of the
  // FPMCU flash space, but we don't need to parse it in userland, so it's ok
  // to just treat it as a serialized blob.
  virtual brillo::Blob EncryptedPairingKey() const;

 protected:
  FpPairingKeyWrapCommand() : EcCommand(EC_CMD_FP_ESTABLISH_PAIRING_KEY_WRAP) {}
};

static_assert(!std::is_copy_constructible<FpPairingKeyWrapCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpPairingKeyWrapCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_PAIRING_KEY_WRAP_COMMAND_H_
