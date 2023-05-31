// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_ATTESTATION_H_
#define LIBHWSEC_BACKEND_ATTESTATION_H_

#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <brillo/secure_blob.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class Attestation {
 public:
  struct CertifyKeyResult {
    std::string certify_info;
    std::string signature;
  };
  // Quotes the |device_configs| with |key|. The |key| must be a restricted
  // signing key.
  virtual StatusOr<attestation::Quote> Quote(DeviceConfigs device_configs,
                                             Key key) = 0;

  // Checks if |quote| is valid for a single device config specified by
  // |device_configs|.
  virtual StatusOr<bool> IsQuoted(DeviceConfigs device_configs,
                                  const attestation::Quote& quote) = 0;

  // Create a key with |key_type|, |key_usage|, and |restriction|, and
  // certifies it by |identity_key| with |external_data|. When
  // |use_endorsement_hierarchy| is true, the key is created as a virtual
  // endorsement key (vEK).
  virtual StatusOr<attestation::CertifiedKey> CreateCertifiedKey(
      Key identity_key,
      attestation::KeyType key_type,
      attestation::KeyUsage key_usage,
      KeyRestriction restriction,
      EndorsementAuth endorsement_auth,
      const std::string& external_data) = 0;

 protected:
  Attestation() = default;
  ~Attestation() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_ATTESTATION_H_
