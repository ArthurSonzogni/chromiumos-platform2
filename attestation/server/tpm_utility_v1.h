// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_SERVER_TPM_UTILITY_V1_H_
#define ATTESTATION_SERVER_TPM_UTILITY_V1_H_

#include "attestation/server/tpm_utility.h"

#include <string>

#include <base/macros.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>

namespace attestation {

// A TpmUtility implementation for TPM v1.2 modules.
class TpmUtilityV1 : public TpmUtility {
 public:
  TpmUtilityV1() = default;
  ~TpmUtilityV1() override;

  // Initializes a TpmUtilityV1 instance. This method must be called
  // successfully before calling any other methods.
  bool Initialize();

  // TpmUtility methods.
  bool IsTpmReady() override;
  bool ActivateIdentity(const std::string& delegate_blob,
                        const std::string& delegate_secret,
                        const std::string& identity_key_blob,
                        const std::string& asym_ca_contents,
                        const std::string& sym_ca_attestation,
                        std::string* credential) override;
  bool CreateCertifiedKey(KeyType key_type,
                          KeyUsage key_usage,
                          const std::string& identity_key_blob,
                          const std::string& external_data,
                          std::string* key_blob,
                          std::string* public_key,
                          std::string* public_key_tpm_format,
                          std::string* key_info,
                          std::string* proof) override;
  bool SealToPCR0(const std::string& data, std::string* sealed_data) override;
  bool Unseal(const std::string& sealed_data, std::string* data) override;

 private:
  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS. Returns true on success.
  bool ConnectContext(trousers::ScopedTssContext* context_handle,
                      TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object authorized by the given |delegate_blob| and
  // |delegate_secret|. Returns true on success.
  bool ConnectContextAsDelegate(const std::string& delegate_blob,
                                const std::string& delegate_secret,
                                trousers::ScopedTssContext* context,
                                TSS_HTPM* tpm);

  // Loads the storage root key (SRK) and populates |srk_handle|. The
  // |context_handle| must be connected and valid. Returns true on success.
  bool LoadSrk(TSS_HCONTEXT context_handle, trousers::ScopedTssKey* srk_handle);

  // Loads a key in the TPM given a |key_blob| and a |parent_key_handle|. The
  // |context_handle| must be connected and valid. Returns true and populates
  // |key_handle| on success.
  bool LoadKeyFromBlob(const std::string& key_blob,
                       TSS_HCONTEXT context_handle,
                       TSS_HKEY parent_key_handle,
                       trousers::ScopedTssKey* key_handle);

  // Retrieves a |data| attribute defined by |flag| and |sub_flag| from a TSS
  // |object_handle|. The |context_handle| is only used for TSS memory
  // management.
  bool GetDataAttribute(TSS_HCONTEXT context_handle,
                        TSS_HOBJECT object_handle,
                        TSS_FLAG flag,
                        TSS_FLAG sub_flag,
                        std::string* data);

  // Converts a public in TPM_PUBKEY format to a DER-encoded RSAPublicKey.
  bool ConvertPublicKeyToDER(const std::string& public_key,
                             std::string* public_key_der);

  bool is_ready_{false};
  trousers::ScopedTssContext context_handle_;
  TSS_HTPM tpm_handle_{0};
  trousers::ScopedTssKey srk_handle_{0};

  DISALLOW_COPY_AND_ASSIGN(TpmUtilityV1);
};

}  // namespace attestation

#endif  // ATTESTATION_SERVER_TPM_UTILITY_V1_H_
