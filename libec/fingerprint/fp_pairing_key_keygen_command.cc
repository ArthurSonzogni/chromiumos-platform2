// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_pairing_key_keygen_command.h"

namespace ec {

brillo::Blob FpPairingKeyKeygenCommand::PubX() const {
  return brillo::Blob(Resp()->pubkey.x,
                      Resp()->pubkey.x + sizeof(Resp()->pubkey.x));
}

brillo::Blob FpPairingKeyKeygenCommand::PubY() const {
  return brillo::Blob(Resp()->pubkey.y,
                      Resp()->pubkey.y + sizeof(Resp()->pubkey.y));
}

brillo::Blob FpPairingKeyKeygenCommand::EncryptedKey() const {
  return brillo::Blob(
      reinterpret_cast<const uint8_t*>(&Resp()->encrypted_private_key),
      reinterpret_cast<const uint8_t*>(&Resp()->encrypted_private_key) +
          sizeof(Resp()->encrypted_private_key));
}

}  // namespace ec
