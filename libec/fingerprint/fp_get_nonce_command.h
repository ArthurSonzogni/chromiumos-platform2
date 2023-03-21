// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_GET_NONCE_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_GET_NONCE_COMMAND_H_

#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpGetNonceCommand
    : public EcCommand<EmptyParam, struct ec_response_fp_generate_nonce> {
 public:
  FpGetNonceCommand() : EcCommand(EC_CMD_FP_GENERATE_NONCE) {}
  ~FpGetNonceCommand() override = default;

  virtual brillo::Blob Nonce() const;
};

static_assert(!std::is_copy_constructible<FpGetNonceCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpGetNonceCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_GET_NONCE_COMMAND_H_
