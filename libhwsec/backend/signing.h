// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_SIGNING_H_
#define LIBHWSEC_BACKEND_SIGNING_H_

#include <brillo/secure_blob.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

// Signing provide the functions to sign and verify.
class Signing {
 public:
  // Signs the |data| with |policy| and |key|.
  virtual StatusOr<brillo::Blob> Sign(const OperationPolicy& policy,
                                      Key key,
                                      const brillo::Blob& data) = 0;

  // Verifies the |signed_data| with |policy| and |key|.
  virtual Status Verify(const OperationPolicy& policy,
                        Key key,
                        const brillo::Blob& signed_data) = 0;

 protected:
  Signing() = default;
  ~Signing() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_SIGNING_H_
