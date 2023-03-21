// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_get_nonce_command.h"

namespace ec {

brillo::Blob FpGetNonceCommand::Nonce() const {
  return brillo::Blob(Resp()->nonce, Resp()->nonce + sizeof(Resp()->nonce));
}

}  // namespace ec
