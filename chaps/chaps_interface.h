// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_CHAPS_INTERFACE_H_
#define CHAPS_CHAPS_INTERFACE_H_

#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <chaps/proto_bindings/ck_structs.pb.h>

#include "chaps/chaps.h"

namespace chaps {

// ChapsInterface provides an abstract interface closely matching the
// bindings that would be generated by e.g. chromeos-dbus-bindings.
// Since this is not available on linux, we need to provide the interface
// ourselves.
//
// Implemented By:
// - ChapsProxyImpl: On the Chaps client side; sends calls over IPC.
// - ChapsServiceImpl: On the Chaps daemon side; receives and implements IPC
//   calls.
class ChapsInterface {
 public:
  ChapsInterface() {}
  ChapsInterface(const ChapsInterface&) = delete;
  ChapsInterface& operator=(const ChapsInterface&) = delete;
  virtual ~ChapsInterface() {}

  // The following methods map to PKCS #11 calls. Each method name is identical
  // to the corresponding PKCS #11 function name except for the "C_" prefix.

  // PKCS #11 v2.20 section 11.5 page 106.
  virtual uint32_t GetSlotList(const brillo::SecureBlob& isolate_credential,
                               bool token_present,
                               std::vector<uint64_t>* slot_list) = 0;
  // PKCS #11 v2.20 section 11.5 page 108.
  virtual uint32_t GetSlotInfo(const brillo::SecureBlob& isolate_credential,
                               uint64_t slot_id,
                               SlotInfo* slot_info) = 0;
  // PKCS #11 v2.20 section 11.5 page 109.
  virtual uint32_t GetTokenInfo(const brillo::SecureBlob& isolate_credential,
                                uint64_t slot_id,
                                TokenInfo* token_info) = 0;
  // PKCS #11 v2.20 section 11.5 page 111.
  virtual uint32_t GetMechanismList(
      const brillo::SecureBlob& isolate_credential,
      uint64_t slot_id,
      std::vector<uint64_t>* mechanism_list) = 0;
  // PKCS #11 v2.20 section 11.5 page 112.
  virtual uint32_t GetMechanismInfo(
      const brillo::SecureBlob& isolate_credential,
      uint64_t slot_id,
      uint64_t mechanism_type,
      MechanismInfo* mechanism_info) = 0;
  // PKCS #11 v2.20 section 11.5 page 113.
  virtual uint32_t InitToken(const brillo::SecureBlob& isolate_credential,
                             uint64_t slot_id,
                             const std::string* so_pin,
                             const std::vector<uint8_t>& label) = 0;
  // PKCS #11 v2.20 section 11.5 page 115.
  virtual uint32_t InitPIN(const brillo::SecureBlob& isolate_credential,
                           uint64_t session_id,
                           const std::string* pin) = 0;
  // PKCS #11 v2.20 section 11.5 page 116.
  virtual uint32_t SetPIN(const brillo::SecureBlob& isolate_credential,
                          uint64_t session_id,
                          const std::string* old_pin,
                          const std::string* new_pin) = 0;
  // PKCS #11 v2.20 section 11.6 page 117.
  virtual uint32_t OpenSession(const brillo::SecureBlob& isolate_credential,
                               uint64_t slot_id,
                               uint64_t flags,
                               uint64_t* session) = 0;
  // PKCS #11 v2.20 section 11.6 page 118.
  virtual uint32_t CloseSession(const brillo::SecureBlob& isolate_credential,
                                uint64_t session) = 0;
  // PKCS #11 v2.20 section 11.6 page 120.
  virtual uint32_t GetSessionInfo(const brillo::SecureBlob& isolate_credential,
                                  uint64_t session_id,
                                  SessionInfo* session_info) = 0;
  // PKCS #11 v2.20 section 11.6 page 121.
  virtual uint32_t GetOperationState(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      std::vector<uint8_t>* operation_state) = 0;
  // PKCS #11 v2.20 section 11.6 page 123.
  virtual uint32_t SetOperationState(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      const std::vector<uint8_t>& operation_state,
      uint64_t encryption_key_handle,
      uint64_t authentication_key_handle) = 0;
  // PKCS #11 v2.20 section 11.6 page 125.
  virtual uint32_t Login(const brillo::SecureBlob& isolate_credential,
                         uint64_t session_id,
                         uint64_t user_type,
                         const std::string* pin) = 0;
  // PKCS #11 v2.20 section 11.6 page 127.
  virtual uint32_t Logout(const brillo::SecureBlob& isolate_credential,
                          uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.7 page 128.
  virtual uint32_t CreateObject(const brillo::SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const std::vector<uint8_t>& attributes,
                                uint64_t* new_object_handle) = 0;
  // PKCS #11 v2.20 section 11.7 page 130.
  virtual uint32_t CopyObject(const brillo::SecureBlob& isolate_credential,
                              uint64_t session_id,
                              uint64_t object_handle,
                              const std::vector<uint8_t>& attributes,
                              uint64_t* new_object_handle) = 0;
  // PKCS #11 v2.20 section 11.7 page 131.
  virtual uint32_t DestroyObject(const brillo::SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 uint64_t object_handle) = 0;
  // PKCS #11 v2.20 section 11.7 page 132.
  virtual uint32_t GetObjectSize(const brillo::SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 uint64_t object_handle,
                                 uint64_t* object_size) = 0;
  // PKCS #11 v2.20 section 11.7 page 133.
  virtual uint32_t GetAttributeValue(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t object_handle,
      const std::vector<uint8_t>& attributes_in,
      std::vector<uint8_t>* attributes_out) = 0;
  // PKCS #11 v2.20 section 11.7 page 135.
  virtual uint32_t SetAttributeValue(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t object_handle,
      const std::vector<uint8_t>& attributes) = 0;
  // PKCS #11 v2.20 section 11.7 page 136.
  virtual uint32_t FindObjectsInit(const brillo::SecureBlob& isolate_credential,
                                   uint64_t session_id,
                                   const std::vector<uint8_t>& attributes) = 0;
  // PKCS #11 v2.20 section 11.7 page 137.
  virtual uint32_t FindObjects(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               uint64_t max_object_count,
                               std::vector<uint64_t>* object_list) = 0;
  // PKCS #11 v2.20 section 11.7 page 138.
  virtual uint32_t FindObjectsFinal(
      const brillo::SecureBlob& isolate_credential, uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.8 page 139.
  virtual uint32_t EncryptInit(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               uint64_t mechanism_type,
                               const std::vector<uint8_t>& mechanism_parameter,
                               uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.8 page 140.
  virtual uint32_t Encrypt(const brillo::SecureBlob& isolate_credential,
                           uint64_t session_id,
                           const std::vector<uint8_t>& data_in,
                           uint64_t max_out_length,
                           uint64_t* actual_out_length,
                           std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.8 page 141.
  virtual uint32_t EncryptUpdate(const brillo::SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 const std::vector<uint8_t>& data_in,
                                 uint64_t max_out_length,
                                 uint64_t* actual_out_length,
                                 std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.8 page 141.
  virtual uint32_t EncryptFinal(const brillo::SecureBlob& isolate_credential,
                                uint64_t session_id,
                                uint64_t max_out_length,
                                uint64_t* actual_out_length,
                                std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.8 page 140,142: any errors terminate the active
  // encryption operation.
  virtual void EncryptCancel(const brillo::SecureBlob& isolate_credential,
                             uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.9 page 144.
  virtual uint32_t DecryptInit(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               uint64_t mechanism_type,
                               const std::vector<uint8_t>& mechanism_parameter,
                               uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.9 page 145.
  virtual uint32_t Decrypt(const brillo::SecureBlob& isolate_credential,
                           uint64_t session_id,
                           const std::vector<uint8_t>& data_in,
                           uint64_t max_out_length,
                           uint64_t* actual_out_length,
                           std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.9 page 146.
  virtual uint32_t DecryptUpdate(const brillo::SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 const std::vector<uint8_t>& data_in,
                                 uint64_t max_out_length,
                                 uint64_t* actual_out_length,
                                 std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.9 page 146.
  virtual uint32_t DecryptFinal(const brillo::SecureBlob& isolate_credential,
                                uint64_t session_id,
                                uint64_t max_out_length,
                                uint64_t* actual_out_length,
                                std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.9 page 145,146: any errors terminate the active
  // decryption operation.
  virtual void DecryptCancel(const brillo::SecureBlob& isolate_credential,
                             uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.10 page 148.
  virtual uint32_t DigestInit(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t mechanism_type,
      const std::vector<uint8_t>& mechanism_parameter) = 0;
  // PKCS #11 v2.20 section 11.10 page 149.
  virtual uint32_t Digest(const brillo::SecureBlob& isolate_credential,
                          uint64_t session_id,
                          const std::vector<uint8_t>& data_in,
                          uint64_t max_out_length,
                          uint64_t* actual_out_length,
                          std::vector<uint8_t>* digest) = 0;
  // PKCS #11 v2.20 section 11.10 page 150.
  virtual uint32_t DigestUpdate(const brillo::SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const std::vector<uint8_t>& data_in) = 0;
  // PKCS #11 v2.20 section 11.10 page 150.
  virtual uint32_t DigestKey(const brillo::SecureBlob& isolate_credential,
                             uint64_t session_id,
                             uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.10 page 151.
  virtual uint32_t DigestFinal(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               uint64_t max_out_length,
                               uint64_t* actual_out_length,
                               std::vector<uint8_t>* digest) = 0;
  // PKCS #11 v2.20 section 11.10 page 149,151: any errors terminate the active
  // digest operation.
  virtual void DigestCancel(const brillo::SecureBlob& isolate_credential,
                            uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.11 page 152.
  virtual uint32_t SignInit(const brillo::SecureBlob& isolate_credential,
                            uint64_t session_id,
                            uint64_t mechanism_type,
                            const std::vector<uint8_t>& mechanism_parameter,
                            uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.11 page 153.
  virtual uint32_t Sign(const brillo::SecureBlob& isolate_credential,
                        uint64_t session_id,
                        const std::vector<uint8_t>& data,
                        uint64_t max_out_length,
                        uint64_t* actual_out_length,
                        std::vector<uint8_t>* signature) = 0;
  // PKCS #11 v2.20 section 11.11 page 154.
  virtual uint32_t SignUpdate(const brillo::SecureBlob& isolate_credential,
                              uint64_t session_id,
                              const std::vector<uint8_t>& data_part) = 0;
  // PKCS #11 v2.20 section 11.11 page 154.
  virtual uint32_t SignFinal(const brillo::SecureBlob& isolate_credential,
                             uint64_t session_id,
                             uint64_t max_out_length,
                             uint64_t* actual_out_length,
                             std::vector<uint8_t>* signature) = 0;
  // PKCS #11 v2.20 section 11.11 page 153,154: any errors terminate the active
  // signing operation.
  virtual void SignCancel(const brillo::SecureBlob& isolate_credential,
                          uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.11 page 155.
  virtual uint32_t SignRecoverInit(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t mechanism_type,
      const std::vector<uint8_t>& mechanism_parameter,
      uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.11 page 156.
  virtual uint32_t SignRecover(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               const std::vector<uint8_t>& data,
                               uint64_t max_out_length,
                               uint64_t* actual_out_length,
                               std::vector<uint8_t>* signature) = 0;
  // PKCS #11 v2.20 section 11.12 page 157.
  virtual uint32_t VerifyInit(const brillo::SecureBlob& isolate_credential,
                              uint64_t session_id,
                              uint64_t mechanism_type,
                              const std::vector<uint8_t>& mechanism_parameter,
                              uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.12 page 158.
  virtual uint32_t Verify(const brillo::SecureBlob& isolate_credential,
                          uint64_t session_id,
                          const std::vector<uint8_t>& data,
                          const std::vector<uint8_t>& signature) = 0;
  // PKCS #11 v2.20 section 11.12 page 159.
  virtual uint32_t VerifyUpdate(const brillo::SecureBlob& isolate_credential,
                                uint64_t session_id,
                                const std::vector<uint8_t>& data_part) = 0;
  // PKCS #11 v2.20 section 11.12 page 159.
  virtual uint32_t VerifyFinal(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               const std::vector<uint8_t>& signature) = 0;
  // PKCS #11 v2.20 section 11.12 page 159: any errors terminate the active
  // verification operation.
  virtual void VerifyCancel(const brillo::SecureBlob& isolate_credential,
                            uint64_t session_id) = 0;
  // PKCS #11 v2.20 section 11.12 page 161.
  virtual uint32_t VerifyRecoverInit(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t mechanism_type,
      const std::vector<uint8_t>& mechanism_parameter,
      uint64_t key_handle) = 0;
  // PKCS #11 v2.20 section 11.12 page 161.
  virtual uint32_t VerifyRecover(const brillo::SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 const std::vector<uint8_t>& signature,
                                 uint64_t max_out_length,
                                 uint64_t* actual_out_length,
                                 std::vector<uint8_t>* data) = 0;
  // PKCS #11 v2.20 section 11.13 page 163.
  virtual uint32_t DigestEncryptUpdate(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      const std::vector<uint8_t>& data_in,
      uint64_t max_out_length,
      uint64_t* actual_out_length,
      std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.13 page 165.
  virtual uint32_t DecryptDigestUpdate(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      const std::vector<uint8_t>& data_in,
      uint64_t max_out_length,
      uint64_t* actual_out_length,
      std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.13 page 169.
  virtual uint32_t SignEncryptUpdate(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      const std::vector<uint8_t>& data_in,
      uint64_t max_out_length,
      uint64_t* actual_out_length,
      std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.13 page 171.
  virtual uint32_t DecryptVerifyUpdate(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      const std::vector<uint8_t>& data_in,
      uint64_t max_out_length,
      uint64_t* actual_out_length,
      std::vector<uint8_t>* data_out) = 0;
  // PKCS #11 v2.20 section 11.14 page 175.
  virtual uint32_t GenerateKey(const brillo::SecureBlob& isolate_credential,
                               uint64_t session_id,
                               uint64_t mechanism_type,
                               const std::vector<uint8_t>& mechanism_parameter,
                               const std::vector<uint8_t>& attributes,
                               uint64_t* key_handle) = 0;
  // PKCS #11 v2.20 section 11.14 page 176.
  virtual uint32_t GenerateKeyPair(
      const brillo::SecureBlob& isolate_credential,
      uint64_t session_id,
      uint64_t mechanism_type,
      const std::vector<uint8_t>& mechanism_parameter,
      const std::vector<uint8_t>& public_attributes,
      const std::vector<uint8_t>& private_attributes,
      uint64_t* public_key_handle,
      uint64_t* private_key_handle) = 0;
  // PKCS #11 v2.20 section 11.14 page 178.
  virtual uint32_t WrapKey(const brillo::SecureBlob& isolate_credential,
                           uint64_t session_id,
                           uint64_t mechanism_type,
                           const std::vector<uint8_t>& mechanism_parameter,
                           uint64_t wrapping_key_handle,
                           uint64_t key_handle,
                           uint64_t max_out_length,
                           uint64_t* actual_out_length,
                           std::vector<uint8_t>* wrapped_key) = 0;
  // PKCS #11 v2.20 section 11.14 page 180.
  virtual uint32_t UnwrapKey(const brillo::SecureBlob& isolate_credential,
                             uint64_t session_id,
                             uint64_t mechanism_type,
                             const std::vector<uint8_t>& mechanism_parameter,
                             uint64_t unwrapping_key_handle,
                             const std::vector<uint8_t>& wrapped_key,
                             const std::vector<uint8_t>& attributes,
                             uint64_t* key_handle) = 0;
  // PKCS #11 v2.20 section 11.14 page 182.
  virtual uint32_t DeriveKey(const brillo::SecureBlob& isolate_credential,
                             uint64_t session_id,
                             uint64_t mechanism_type,
                             const std::vector<uint8_t>& mechanism_parameter,
                             uint64_t base_key_handle,
                             const std::vector<uint8_t>& attributes,
                             uint64_t* key_handle) = 0;
  // PKCS #11 v2.20 section 11.15 page 184.
  virtual uint32_t SeedRandom(const brillo::SecureBlob& isolate_credential,
                              uint64_t session_id,
                              const std::vector<uint8_t>& seed) = 0;
  // PKCS #11 v2.20 section 11.15 page 184.
  virtual uint32_t GenerateRandom(const brillo::SecureBlob& isolate_credential,
                                  uint64_t session_id,
                                  uint64_t num_bytes,
                                  std::vector<uint8_t>* random_data) = 0;
};

}  // namespace chaps

#endif  // CHAPS_CHAPS_INTERFACE_H_
