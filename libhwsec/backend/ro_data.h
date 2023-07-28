// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_RO_DATA_H_
#define LIBHWSEC_BACKEND_RO_DATA_H_

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <brillo/secure_blob.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/space.h"

namespace hwsec {

// Storage provide the functions for read-only space.
class RoData {
 public:
  // Is the |space| ready to use (defined correctly) or not.
  virtual StatusOr<bool> IsReady(RoSpace space) = 0;

  // Reads the data from the |space|.
  virtual StatusOr<brillo::Blob> Read(RoSpace space) = 0;

  // Certifies data of the |space| with a |key|.
  virtual StatusOr<attestation::Quote> Certify(RoSpace space, Key key) = 0;

  // Certifies data of the |space| with a |key|. The size of data to be
  // certified will be |size|.
  virtual StatusOr<attestation::Quote> CertifyWithSize(RoSpace space,
                                                       Key key,
                                                       int size) = 0;

 protected:
  RoData() = default;
  ~RoData() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_RO_DATA_H_
