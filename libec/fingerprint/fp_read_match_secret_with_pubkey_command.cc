// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_read_match_secret_with_pubkey_command.h"

namespace ec {

brillo::Blob FpReadMatchSecretWithPubkeyCommand::EncryptedSecret() const {
  return brillo::Blob(Resp()->enc_secret,
                      Resp()->enc_secret + sizeof(Resp()->enc_secret));
}

brillo::Blob FpReadMatchSecretWithPubkeyCommand::Iv() const {
  return brillo::Blob(Resp()->iv, Resp()->iv + sizeof(Resp()->iv));
}

brillo::Blob FpReadMatchSecretWithPubkeyCommand::PkOutX() const {
  return brillo::Blob(Resp()->pubkey.x,
                      Resp()->pubkey.x + sizeof(Resp()->pubkey.x));
}

brillo::Blob FpReadMatchSecretWithPubkeyCommand::PkOutY() const {
  return brillo::Blob(Resp()->pubkey.y,
                      Resp()->pubkey.y + sizeof(Resp()->pubkey.y));
}

}  // namespace ec
