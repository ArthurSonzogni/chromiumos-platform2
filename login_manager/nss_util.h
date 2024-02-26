// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_NSS_UTIL_H_
#define LOGIN_MANAGER_NSS_UTIL_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <crypto/scoped_nss_types.h>
#include <crypto/signature_verifier.h>

namespace crypto {
class RSAPrivateKey;
}

namespace login_manager {
// Forward declaration.
typedef struct PK11SlotInfoStr PK11SlotInfo;

struct PK11SlotDescriptor {
  crypto::ScopedPK11Slot slot;
  std::optional<base::FilePath> ns_mnt_path;
};

using OptionalFilePath = std::optional<base::FilePath>;

using ScopedPK11SlotDescriptor = std::unique_ptr<PK11SlotDescriptor>;

// TODO(b/259362896): Most of the methods here should be removed.
// An interface to wrap the usage of crypto/nss_util.h and allow for mocking.
class NssUtil {
 public:
  NssUtil();
  NssUtil(const NssUtil&) = delete;
  NssUtil& operator=(const NssUtil&) = delete;

  virtual ~NssUtil();

  // Creates an NssUtil. If there is no Factory (the default) this creates and
  // returns a new NssUtil.
  static std::unique_ptr<NssUtil> Create();

  virtual base::FilePath GetOwnerKeyFilePath() = 0;

  // Returns true if |blob| is a validly encoded NSS SubjectPublicKeyInfo.
  virtual bool CheckPublicKeyBlob(const std::vector<uint8_t>& blob) = 0;

  virtual bool Verify(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& data,
      const std::vector<uint8_t>& public_key,
      const crypto::SignatureVerifier::SignatureAlgorithm algorithm) = 0;

  virtual bool Sign(const std::vector<uint8_t>& data,
                    crypto::RSAPrivateKey* key,
                    std::vector<uint8_t>* out_signature) = 0;
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_NSS_UTIL_H_
