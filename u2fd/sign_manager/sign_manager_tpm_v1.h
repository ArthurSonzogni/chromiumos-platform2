// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_SIGN_MANAGER_SIGN_MANAGER_TPM_V1_H_
#define U2FD_SIGN_MANAGER_SIGN_MANAGER_TPM_V1_H_

#include <stdint.h>
#include <string>
#include <vector>

#include <base/macros.h>
#include <brillo/secure_blob.h>
#include <openssl/rsa.h>
#include <tpm_manager/client/tpm_manager_utility.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>

#include "u2fd/sign_manager/sign_manager.h"

namespace u2f {

class SignManagerTpmV1 : public SignManager {
 public:
  SignManagerTpmV1();

  SignManagerTpmV1(const SignManagerTpmV1&) = delete;
  SignManagerTpmV1& operator=(const SignManagerTpmV1&) = delete;

  ~SignManagerTpmV1() override {}

  bool IsReady() override;

  bool CreateKey(KeyType key_type,
                 const brillo::SecureBlob& auth_data,
                 std::string* key_blob,
                 std::vector<uint8_t>* public_key_cbor) override;

  bool Sign(const std::string& key_blob,
            const std::string& data_to_sign,
            const brillo::SecureBlob& auth_data,
            std::string* signature_der) override;

 private:
  bool IsTpmReady();

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle|
  // with its matching TPM object iff the context can be created and a TPM
  // object exists in the TSS. Returns true on success.
  bool ConnectContextAsUser(trousers::ScopedTssContext* context_handle,
                            TSS_HTPM* tpm_handle);

  // Sets up srk_handle_ if necessary. Returns true iff the SRK is ready.
  bool SetupSrk();

  // Loads the storage root key (SRK) and populates |srk_handle|. The
  // |context_handle| must be connected and valid. Returns true on success.
  bool LoadSrk(TSS_HCONTEXT context_handle, trousers::ScopedTssKey* srk_handle);

  bool CreateKeyPolicy(TSS_HKEY key,
                       const brillo::SecureBlob& auth_data,
                       bool auth_only);

  // Retrieves a |data| attribute defined by |flag| and |sub_flag| from a TSS
  // |object_handle|. The |context_handle| is only used for TSS memory
  // management.
  bool GetDataAttribute(TSS_HCONTEXT context_handle,
                        TSS_HOBJECT object_handle,
                        TSS_FLAG flag,
                        TSS_FLAG sub_flag,
                        std::string* data);

  // Initializes |context_handle_| if not yet. |consumer_name| refers to the
  // consumer of |context_handle_| after initialization; usually it is the
  // function name of the caller.
  bool InitializeContextHandle(const std::string& consumer_name);

  // Long-live TSS context in order reduce the overhead of context connection.
  trousers::ScopedTssContext context_handle_;
  TSS_HTPM tpm_handle_{0};
  trousers::ScopedTssKey srk_handle_{0};
  bool is_ready_ = false;
  tpm_manager::TpmManagerUtility* tpm_manager_utility_;
};

}  // namespace u2f

#endif  // U2FD_SIGN_MANAGER_SIGN_MANAGER_TPM_V1_H_
