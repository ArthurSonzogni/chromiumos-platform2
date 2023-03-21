// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_pairing_key_wrap_command.h"

namespace ec {

brillo::Blob FpPairingKeyWrapCommand::EncryptedPairingKey() const {
  return brillo::Blob(
      reinterpret_cast<const uint8_t*>(&Resp()->encrypted_pairing_key),
      reinterpret_cast<const uint8_t*>(&Resp()->encrypted_pairing_key) +
          sizeof(Resp()->encrypted_pairing_key));
}

}  // namespace ec
