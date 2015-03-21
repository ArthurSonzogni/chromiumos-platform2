// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_UTILITY_IMPL_H_
#define TRUNKS_TPM_UTILITY_IMPL_H_

#include "trunks/tpm_utility.h"

#include <map>
#include <string>

#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <gtest/gtest_prod.h>

#include "trunks/trunks_export.h"

namespace trunks {

class AuthorizationDelegate;
class AuthorizationSession;
class TrunksFactory;

// A default implementation of TpmUtility.
class TRUNKS_EXPORT TpmUtilityImpl : public TpmUtility {
 public:
  explicit TpmUtilityImpl(const TrunksFactory& factory);
  ~TpmUtilityImpl() override;

  // TpmUtility methods.
  TPM_RC Startup() override;
  TPM_RC Clear() override;
  void Shutdown() override;
  TPM_RC InitializeTpm() override;
  TPM_RC TakeOwnership(const std::string& owner_password,
                       const std::string& endorsement_password,
                       const std::string& lockout_password) override;
  TPM_RC StirRandom(const std::string& entropy_data,
                    AuthorizationSession* session) override;
  TPM_RC GenerateRandom(size_t num_bytes,
                        AuthorizationSession* session,
                        std::string* random_data) override;
  TPM_RC ExtendPCR(int pcr_index,
                   const std::string& extend_data,
                   AuthorizationSession* session) override;
  TPM_RC ReadPCR(int pcr_index, std::string* pcr_value) override;
  TPM_RC AsymmetricEncrypt(TPM_HANDLE key_handle,
                           TPM_ALG_ID scheme,
                           TPM_ALG_ID hash_alg,
                           const std::string& plaintext,
                           AuthorizationSession* session,
                           std::string* ciphertext) override;
  TPM_RC AsymmetricDecrypt(TPM_HANDLE key_handle,
                           TPM_ALG_ID scheme,
                           TPM_ALG_ID hash_alg,
                           const std::string& ciphertext,
                           AuthorizationSession* session,
                           std::string* plaintext) override;
  TPM_RC Sign(TPM_HANDLE key_handle,
              TPM_ALG_ID scheme,
              TPM_ALG_ID hash_alg,
              const std::string& plaintext,
              AuthorizationSession* session,
              std::string* signature) override;
  TPM_RC Verify(TPM_HANDLE key_handle,
                TPM_ALG_ID scheme,
                TPM_ALG_ID hash_alg,
                const std::string& plaintext,
                const std::string& signature) override;
  TPM_RC ChangeKeyAuthorizationData(TPM_HANDLE key_handle,
                                    const std::string& new_password,
                                    AuthorizationSession* session,
                                    std::string* key_blob) override;
  TPM_RC ImportRSAKey(AsymmetricKeyUsage key_type,
                      const std::string& modulus,
                      uint32_t public_exponent,
                      const std::string& prime_factor,
                      const std::string& password,
                      AuthorizationSession* session,
                      std::string* key_blob) override;
  TPM_RC CreateAndLoadRSAKey(AsymmetricKeyUsage key_type,
                             const std::string& password,
                             AuthorizationSession* session,
                             TPM_HANDLE* key_handle,
                             std::string* key_blob) override;
  TPM_RC CreateRSAKeyPair(AsymmetricKeyUsage key_type,
                          int modulus_bits,
                          uint32_t public_exponent,
                          const std::string& password,
                          AuthorizationSession* session,
                          std::string* key_blob) override;
  TPM_RC LoadKey(const std::string& key_blob,
                 AuthorizationSession* session,
                 TPM_HANDLE* key_handle) override;
  TPM_RC GetKeyName(TPM_HANDLE handle, std::string* name) override;
  TPM_RC GetKeyPublicArea(TPM_HANDLE handle,
                          TPMT_PUBLIC* public_data) override;
  TPM_RC DefineNVSpace(uint32_t index,
                       size_t num_bytes,
                       AuthorizationSession* session) override;
  TPM_RC DestroyNVSpace(uint32_t index, AuthorizationSession* session) override;
  TPM_RC LockNVSpace(uint32_t index, AuthorizationSession* session) override;
  TPM_RC WriteNVSpace(uint32_t index,
                      uint32_t offset,
                      const std::string& nvram_data,
                      AuthorizationSession* session) override;
  TPM_RC ReadNVSpace(uint32_t index,
                     uint32_t offset,
                     size_t num_bytes,
                     std::string* nvram_data,
                     AuthorizationSession* session) override;
  TPM_RC GetNVSpaceName(uint32_t index, std::string* name) override;
  TPM_RC GetNVSpacePublicArea(uint32_t index,
                              TPMS_NV_PUBLIC* public_data) override;

 private:
  friend class TpmUtilityTest;

  const TrunksFactory& factory_;
  std::map<uint32_t, TPMS_NV_PUBLIC> nvram_public_area_map_;

  // Synchronously derives storage root keys for RSA and ECC and persists the
  // keys in the TPM. This operation must be authorized by the |owner_password|
  // and, on success, KRSAStorageRootKey and kECCStorageRootKey can be used
  // with an empty authorization value until the TPM is cleared.
  TPM_RC CreateStorageRootKeys(const std::string& owner_password);

  // This method creates an RSA decryption key to be used for salting sessions.
  // This method also makes the salting key permanent under the storage
  // hierarchy.
  TPM_RC CreateSaltingKey(const std::string& owner_password);

  // This method returns a partially filled TPMT_PUBLIC strucutre,
  // which can then be modified by other methods to create the public
  // template for a key. It takes a valid |key_type| tp construct the
  // parameters.
  TPMT_PUBLIC CreateDefaultPublicArea(TPM_ALG_ID key_alg);

  // Sets TPM |hierarchy| authorization to |password| using |authorization|.
  TPM_RC SetHierarchyAuthorization(TPMI_RH_HIERARCHY_AUTH hierarchy,
                                   const std::string& password,
                                   AuthorizationDelegate* authorization);

  // Disables the TPM platform hierarchy until the next startup. This requires
  // platform |authorization|.
  TPM_RC DisablePlatformHierarchy(AuthorizationDelegate* authorization);

  TPM_RC StringToKeyData(const std::string& key_blob,
                         TPM2B_PUBLIC* public_info,
                         TPM2B_PRIVATE* private_info);

  TPM_RC KeyDataToString(const TPM2B_PUBLIC& public_info,
                         const TPM2B_PRIVATE& private_info,
                         std::string* key_blob);

  // Given a public area, this method computes the object name. Following
  // TPM2.0 Specification Part 1 section 16,
  // object_name = HashAlg || Hash(public_area);
  TPM_RC ComputeKeyName(const TPMT_PUBLIC& public_area,
                        std::string* object_name);

  // Given a public area, this method computers the NVSpace's name.
  // It follows TPM2.0 Specification Part 1 section 16,
  // nv_name = HashAlg || Hash(nv_public_area);
  TPM_RC ComputeNVSpaceName(const TPMS_NV_PUBLIC& nv_public_area,
                            std::string* nv_name);

  // This encrypts the |sensitive_data| struct according to the specification
  // defined in TPM2.0 spec Part 1: Figure 19.
  TPM_RC EncryptPrivateData(const TPMT_SENSITIVE& sensitive_area,
                            const TPMT_PUBLIC& public_area,
                            TPM2B_PRIVATE* encrypted_private_data,
                            TPM2B_DATA* encryption_key);

  DISALLOW_COPY_AND_ASSIGN(TpmUtilityImpl);
};

}  // namespace trunks

#endif  // TRUNKS_TPM_UTILITY_IMPL_H_
