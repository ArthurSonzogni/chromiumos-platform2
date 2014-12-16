// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_HMAC_AUTHORIZATION_DELEGATE_H_
#define TRUNKS_HMAC_AUTHORIZATION_DELEGATE_H_

#include <string>

#include <base/gtest_prod_util.h>
#include <chromeos/chromeos_export.h>
#include <crypto/secure_hash.h>
#include <gtest/gtest_prod.h>

#include "trunks/authorization_delegate.h"
#include "trunks/tpm_generated.h"

namespace trunks {

const uint8_t kContinueSession = 1;
const size_t kAesKeySize = 16;      // 128 bits is minimum AES key size.
const size_t kHashDigestSize = 32;  // 256 bits is SHA256 digest size.

/* HmacAuthorizationDelegate is an implementation of the AuthorizationDelegate
 * interface. It provides the necessary Auth data for HMAC sessions.
 * This delegate also does parameter encryption on sessions that support it.
 *
 * Usage:
 * 1) After running the StartAuthSession command on the TPM2.0, we declare this
 * delegate using the constructor. We can specify if we want parameter
 * obfuscation enabled or not.
 * 2) We initialize the session using |InitSession|. We feed in the handle and
 * tpm_nonce returned by StartAuthSession. Additionally we inject the
 * caller_nonce, salt and auth_value of the bound entity we fed into
 * StartAuthSession.
 * 3) Pass a pointer to this delegate to any TPM command that needs
 * authorization using this delegate.
 *
 * Sample control flow:
 *  TrunksProxy proxy;
 *  proxy.Init();
 *  Tpm tpm(&proxy);
 *  tpm.StartAuthSession(...);
 *  HmacAuthorizationDelegate hmac();
 *  hmac.InitSession(...);
 *  tpm.Create(..., &hmac);
 *  hmac.set_entity_auth_value(...);
 *  tpm.Load(..., &hmac);
 */
class CHROMEOS_EXPORT HmacAuthorizationDelegate: public AuthorizationDelegate {
 public:
  HmacAuthorizationDelegate();
  ~HmacAuthorizationDelegate() override;

  // AuthorizationDelegate methods.
  bool GetCommandAuthorization(const std::string& command_hash,
                               bool is_command_parameter_encryption_possible,
                               bool is_response_parameter_encryption_possible,
                               std::string* authorization);
  bool CheckResponseAuthorization(const std::string& response_hash,
                                  const std::string& authorization) override;
  bool EncryptCommandParameter(std::string* parameter) override;
  bool DecryptResponseParameter(std::string* parameter) override;

  // This function is called with the return data of |StartAuthSession|. It
  // will initialize the session to start providing auth information. It can
  // only be called once per delegate, and must be called before the delegate
  // is used for any operation. The boolean arg |enable_parameter_encryption|
  // specifies if parameter encryption should be enabled for this delegate.
  // |salt| and |bind_auth_value| specify the injected auth values into this
  // delegate.
  bool InitSession(TPM_HANDLE session_handle,
                   const TPM2B_NONCE& tpm_nonce,
                   const TPM2B_NONCE& caller_nonce,
                   const std::string& salt,
                   const std::string& bind_auth_value,
                   bool enable_parameter_encryption);

  // This method sets the FutureAuthorizationValue. This value is used in
  // computing the HMAC response of TPM2_HierarchyChangeAuth.
  void set_future_authorization_value(const std::string& auth_value);

  // This method is used to inject an auth_value associated with an entity.
  // This auth_value is then used when generating HMACs and encryption keys.
  // Note: This value will be used for all commands until explicitly reset.
  void set_entity_auth_value(const std::string& auth_value) {
    entity_auth_value_ = auth_value;
  }

  std::string entity_auth_value() const {
    return entity_auth_value_;
  }

  TPM_HANDLE session_handle() const {
    return session_handle_;
  }

 protected:
  FRIEND_TEST(HmacAuthorizationDelegateFixture, NonceRegenerationTest);
  FRIEND_TEST(HmacAuthorizationDelegateTest, EncryptDecryptTest);
  FRIEND_TEST(HmacAuthorizationDelegateTest, SessionKeyTest);

 private:
  // This method implements the key derivation function used in the TPM.
  // NOTE: It only returns 32 byte keys.
  std::string CreateKey(const std::string& hmac_key,
                        const std::string& label,
                        const TPM2B_NONCE& nonce_newer,
                        const TPM2B_NONCE& nonce_older);
  // This method performs a FIPS198 HMAC operation on |data| using |key|
  std::string HmacSha256(const std::string& key,
                         const std::string& data);
  // This method performs an AES operation using a 128 bit key.
  // |operation_type| can be either AES_ENCRYPT or AES_DECRYPT and it
  // determines if the operation is an encryption or decryption.
  void AesOperation(std::string* parameter,
                    const TPM2B_NONCE& nonce_newer,
                    const TPM2B_NONCE& nonce_older,
                    int operation_type);
  // This method regenerates the caller nonce. The new nonce is the same
  // length as the previous nonce. The buffer is filled with random data using
  // openssl's |RAND_bytes| function.
  // NOTE: This operation is DESTRUCTIVE, and rewrites the caller_nonce_ field.
  void RegenerateCallerNonce();

  TPM_HANDLE session_handle_;
  TPM2B_NONCE caller_nonce_;
  TPM2B_NONCE tpm_nonce_;
  bool is_parameter_encryption_enabled_;
  bool nonce_generated_;
  std::string session_key_;
  std::string entity_auth_value_;
  bool future_authorization_value_set_;
  std::string future_authorization_value_;

  DISALLOW_COPY_AND_ASSIGN(HmacAuthorizationDelegate);
};

}  // namespace trunks

#endif  // TRUNKS_HMAC_AUTHORIZATION_DELEGATE_H_
