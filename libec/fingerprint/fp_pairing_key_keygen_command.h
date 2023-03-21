// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_PAIRING_KEY_KEYGEN_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_PAIRING_KEY_KEYGEN_COMMAND_H_

#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpPairingKeyKeygenCommand
    : public EcCommand<EmptyParam,
                       struct ec_response_fp_establish_pairing_key_keygen> {
 public:
  FpPairingKeyKeygenCommand()
      : EcCommand(EC_CMD_FP_ESTABLISH_PAIRING_KEY_KEYGEN) {}
  ~FpPairingKeyKeygenCommand() override = default;

  virtual brillo::Blob PubX() const;
  virtual brillo::Blob PubY() const;
  // This returns the serialized encrypted private key of the ECDH key exchange.
  // This will be soon loaded back to FPMCU and won't be parsed in userland, so
  // it's ok to just treat it as a serialized blob.
  virtual brillo::Blob EncryptedKey() const;
};

static_assert(!std::is_copy_constructible<FpPairingKeyKeygenCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpPairingKeyKeygenCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_PAIRING_KEY_KEYGEN_COMMAND_H_
