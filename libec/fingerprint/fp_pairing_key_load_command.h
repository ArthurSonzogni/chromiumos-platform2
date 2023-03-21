// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_PAIRING_KEY_LOAD_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_PAIRING_KEY_LOAD_COMMAND_H_

#include <algorithm>
#include <memory>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpPairingKeyLoadCommand
    : public EcCommand<ec_params_fp_load_pairing_key, EmptyParam> {
 public:
  template <typename T = FpPairingKeyLoadCommand>
  static std::unique_ptr<T> Create(const brillo::Blob& encrypted_pairing_key) {
    static_assert(std::is_base_of<FpPairingKeyLoadCommand, T>::value,
                  "Only classes derived from "
                  "FpPairingKeyLoadCommand can use Create");
    static_assert(
        sizeof(std::declval<ec_params_fp_load_pairing_key>()
                   .encrypted_pairing_key) == kEncryptedPairingKeySize,
        "Changing size of encrypted_pairing_key can break existing users");

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto cmd = base::WrapUnique(new T());
    auto* req = cmd->Req();
    if (encrypted_pairing_key.size() != sizeof(req->encrypted_pairing_key)) {
      return nullptr;
    }
    std::copy(encrypted_pairing_key.begin(), encrypted_pairing_key.end(),
              reinterpret_cast<uint8_t*>(&req->encrypted_pairing_key));
    return cmd;
  }
  ~FpPairingKeyLoadCommand() override = default;

 protected:
  FpPairingKeyLoadCommand() : EcCommand(EC_CMD_FP_LOAD_PAIRING_KEY) {}

 private:
  // We persist this data on the disk, so its size should stay the same.
  // If the size is changed, a new command struct has to be introduced.
  constexpr static size_t kEncryptedPairingKeySize = 80;
};

static_assert(!std::is_copy_constructible<FpPairingKeyLoadCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpPairingKeyLoadCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_PAIRING_KEY_LOAD_COMMAND_H_
