// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/session_impl.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/logging.h>
#include <chromeos/utility.h>
#include <openssl/bio.h>
#include <openssl/des.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include "chaps/chaps.h"
#include "chaps/chaps_factory.h"
#include "chaps/chaps_utility.h"
#include "chaps/object.h"
#include "chaps/object_pool.h"
#include "chaps/tpm_utility.h"
#include "pkcs11/cryptoki.h"

using std::hex;
using std::map;
using std::set;
using std::string;
using std::vector;

namespace chaps {

static const int kDefaultAuthDataBytes = 20;
static const int kMaxCipherBlockBytes = 16;
static const int kMaxRSAOutputBytes = 256;
static const int kMaxDigestOutputBytes = EVP_MAX_MD_SIZE;
static const int kMinRSAKeyBits = 512;
static const int kMaxRSAKeyBits = 2048;

SessionImpl::SessionImpl(int slot_id,
                         ObjectPool* token_object_pool,
                         TPMUtility* tpm_utility,
                         ChapsFactory* factory,
                         HandleGenerator* handle_generator,
                         bool is_read_only)
    : factory_(factory),
      find_results_valid_(false),
      is_read_only_(is_read_only),
      slot_id_(slot_id),
      token_object_pool_(token_object_pool),
      tpm_utility_(tpm_utility),
      is_legacy_loaded_(false),
      private_root_key_(0),
      public_root_key_(0) {
  CHECK(token_object_pool_);
  CHECK(tpm_utility_);
  CHECK(factory_);
  session_object_pool_.reset(factory_->CreateObjectPool(handle_generator,
                                                        NULL,
                                                        NULL));
  CHECK(session_object_pool_.get());
}

SessionImpl::~SessionImpl() {
}

int SessionImpl::GetSlot() const {
  return slot_id_;
}

CK_STATE SessionImpl::GetState() const {
  return is_read_only_ ? CKS_RO_USER_FUNCTIONS : CKS_RW_USER_FUNCTIONS;
}

bool SessionImpl::IsReadOnly() const {
  return is_read_only_;
}

bool SessionImpl::IsOperationActive(OperationType type) const {
  CHECK(type < kNumOperationTypes);
  return operation_context_[type].is_valid_;
}

CK_RV SessionImpl::CreateObject(const CK_ATTRIBUTE_PTR attributes,
                                int num_attributes,
                                int* new_object_handle) {
  return CreateObjectInternal(attributes,
                              num_attributes,
                              NULL,
                              new_object_handle);
}

CK_RV SessionImpl::CopyObject(const CK_ATTRIBUTE_PTR attributes,
                              int num_attributes,
                              int object_handle,
                              int* new_object_handle) {
  const Object* orig_object = NULL;
  if (!GetObject(object_handle, &orig_object))
    return CKR_OBJECT_HANDLE_INVALID;
  CHECK(orig_object);
  return CreateObjectInternal(attributes,
                              num_attributes,
                              orig_object,
                              new_object_handle);
}

CK_RV SessionImpl::DestroyObject(int object_handle) {
  const Object* object = NULL;
  if (!GetObject(object_handle, &object))
    return CKR_OBJECT_HANDLE_INVALID;
  CHECK(object);
  ObjectPool* pool = object->IsTokenObject() ? token_object_pool_
      : session_object_pool_.get();
  if (!pool->Delete(object))
    return CKR_GENERAL_ERROR;
  return CKR_OK;
}

bool SessionImpl::GetObject(int object_handle, const Object** object) {
  CHECK(object);
  if (token_object_pool_->FindByHandle(object_handle, object))
    return true;
  return session_object_pool_->FindByHandle(object_handle, object);
}

bool SessionImpl::GetModifiableObject(int object_handle, Object** object) {
  CHECK(object);
  const Object* const_object;
  if (!GetObject(object_handle, &const_object))
    return false;
  ObjectPool* pool = const_object->IsTokenObject() ? token_object_pool_
      : session_object_pool_.get();
  *object = pool->GetModifiableObject(const_object);
  return true;
}

bool SessionImpl::FlushModifiableObject(Object* object) {
  CHECK(object);
  ObjectPool* pool = object->IsTokenObject() ? token_object_pool_
      : session_object_pool_.get();
  return pool->Flush(object);
}

CK_RV SessionImpl::FindObjectsInit(const CK_ATTRIBUTE_PTR attributes,
                                   int num_attributes) {
  if (find_results_valid_)
    return CKR_OPERATION_ACTIVE;
  scoped_ptr<Object> search_template(factory_->CreateObject());
  CHECK(search_template.get());
  search_template->SetAttributes(attributes, num_attributes);
  vector<const Object*> objects;
  if (!search_template->IsAttributePresent(CKA_TOKEN) ||
      search_template->IsTokenObject()) {
    if (!token_object_pool_->Find(search_template.get(), &objects))
      return CKR_GENERAL_ERROR;
  }
  if (!search_template->IsAttributePresent(CKA_TOKEN) ||
      !search_template->IsTokenObject()) {
    if (!session_object_pool_->Find(search_template.get(), &objects))
      return CKR_GENERAL_ERROR;
  }
  find_results_.clear();
  find_results_offset_ = 0;
  find_results_valid_ = true;
  for (size_t i = 0; i < objects.size(); ++i) {
    find_results_.push_back(objects[i]->handle());
  }
  return CKR_OK;
}

CK_RV SessionImpl::FindObjects(int max_object_count,
                               vector<int>* object_handles) {
  CHECK(object_handles);
  if (!find_results_valid_)
    return CKR_OPERATION_NOT_INITIALIZED;
  size_t end_offset = find_results_offset_ +
      static_cast<size_t>(max_object_count);
  if (end_offset > find_results_.size())
    end_offset = find_results_.size();
  for (size_t i = find_results_offset_; i < end_offset; ++i) {
    object_handles->push_back(find_results_[i]);
  }
  find_results_offset_ += object_handles->size();
  return CKR_OK;
}

CK_RV SessionImpl::FindObjectsFinal() {
  if (!find_results_valid_)
    return CKR_OPERATION_NOT_INITIALIZED;
  find_results_valid_ = false;
  return CKR_OK;
}

CK_RV SessionImpl::OperationInit(OperationType operation,
                                 CK_MECHANISM_TYPE mechanism,
                                 const string& mechanism_parameter,
                                 const Object* key) {
  CHECK(operation < kNumOperationTypes);
  OperationContext* context = &operation_context_[operation];
  if (context->is_valid_) {
    LOG(ERROR) << "Operation is already active.";
    return CKR_OPERATION_ACTIVE;
  }
  context->Clear();
  context->mechanism_ = mechanism;
  context->parameter_ = mechanism_parameter;
  if (!IsValidMechanism(operation, mechanism)) {
    LOG(ERROR) << "Mechanism not supported: 0x" << hex << mechanism;
    return CKR_MECHANISM_INVALID;
  }
  if (operation != kDigest) {
    // Make sure the key is valid for the mechanism.
    CHECK(key);
    if (!IsValidKeyType(operation,
                        mechanism,
                        key->GetObjectClass(),
                        key->GetAttributeInt(CKA_KEY_TYPE, -1))) {
      LOG(ERROR) << "Key type mismatch.";
      return CKR_KEY_TYPE_INCONSISTENT;
    }
    if (!key->GetAttributeBool(GetRequiredKeyUsage(operation), false)) {
      LOG(ERROR) << "Key function not permitted.";
      return CKR_KEY_FUNCTION_NOT_PERMITTED;
    }
    if (IsRSA(mechanism)) {
      int key_size = key->GetAttributeString(CKA_MODULUS).length() * 8;
      if (key_size < kMinRSAKeyBits || key_size > kMaxRSAKeyBits) {
        LOG(ERROR) << "Key size not supported: " << key_size;
        return CKR_KEY_SIZE_RANGE;
      }
    }
  }
  if (operation == kEncrypt || operation == kDecrypt) {
    if (mechanism == CKM_RSA_PKCS) {
      context->key_ = key;
      context->is_valid_ = true;
    } else {
      return CipherInit((operation == kEncrypt),
                        mechanism,
                        mechanism_parameter,
                        key);
    }
  } else {
    // It is valid for GetOpenSSLDigest to return NULL (e.g. CKM_RSA_PKCS).
    const EVP_MD* digest = GetOpenSSLDigest(mechanism);
    if (IsHMAC(mechanism)) {
      string key_material = key->GetAttributeString(CKA_VALUE);
      HMAC_CTX_init(&context->hmac_context_);
      HMAC_Init_ex(&context->hmac_context_,
                   key_material.data(),
                   key_material.length(),
                   digest,
                   NULL);
      context->is_hmac_ = true;
    } else if (digest) {
      EVP_DigestInit(&context->digest_context_, digest);
      context->is_digest_ = true;
    }
    if (IsRSA(mechanism))
      context->key_ = key;
    context->is_valid_ = true;
  }
  return CKR_OK;
}

CK_RV SessionImpl::OperationUpdate(OperationType operation,
                                   const string& data_in,
                                   int* required_out_length,
                                   string* data_out) {
  CHECK(operation < kNumOperationTypes);
  OperationContext* context = &operation_context_[operation];
  if (!context->is_valid_) {
    LOG(ERROR) << "Operation is not initialized.";
    return CKR_OPERATION_NOT_INITIALIZED;
  }
  if (context->is_cipher_) {
    return CipherUpdate(context, data_in, required_out_length, data_out);
  } else if (context->is_digest_) {
    EVP_DigestUpdate(&context->digest_context_,
                     data_in.data(),
                     data_in.length());
  } else if (context->is_hmac_) {
    HMAC_Update(&context->hmac_context_,
                ConvertStringToByteBuffer(data_in.c_str()),
                data_in.length());
  } else {
    // We don't need to process now; just queue the data.
    context->data_ += data_in;
  }
  if (required_out_length)
    *required_out_length = 0;
  return CKR_OK;
}

CK_RV SessionImpl::OperationFinal(OperationType operation,
                                  int* required_out_length,
                                  string* data_out) {
  CHECK(required_out_length);
  CHECK(data_out);
  CHECK(operation < kNumOperationTypes);
  OperationContext* context = &operation_context_[operation];
  if (!context->is_valid_) {
    LOG(ERROR) << "Operation is not initialized.";
    return CKR_OPERATION_NOT_INITIALIZED;
  }
  context->is_valid_ = false;
  // Complete the operation if it has not already been done.
  if (!context->is_finished_) {
    if (context->is_cipher_) {
      CK_RV result = CipherFinal(context);
      if (result != CKR_OK)
        return result;
    } else if (context->is_digest_) {
      unsigned char buffer[kMaxDigestOutputBytes];
      unsigned int out_length = 0;
      EVP_DigestFinal(&context->digest_context_, buffer, &out_length);
      context->data_ = string(reinterpret_cast<char*>(buffer), out_length);
    } else if (context->is_hmac_) {
      unsigned char buffer[kMaxDigestOutputBytes];
      unsigned int out_length = 0;
      HMAC_Final(&context->hmac_context_, buffer, &out_length);
      HMAC_CTX_cleanup(&context->hmac_context_);
      context->data_ = string(reinterpret_cast<char*>(buffer), out_length);
    }
    // Some RSA mechanisms use a digest so it's important to finish the digest
    // before finishing the RSA computation.
    if (IsRSA(context->mechanism_)) {
      if (operation == kEncrypt) {
        if (!RSAEncrypt(context))
          return CKR_FUNCTION_FAILED;
      } else if (operation == kDecrypt) {
        if (!RSADecrypt(context))
          return CKR_FUNCTION_FAILED;
      } else if (operation == kSign) {
        if (!RSASign(context))
          return CKR_FUNCTION_FAILED;
      }
    }
    context->is_finished_ = true;
  }
  CK_RV result = GetOperationOutput(context,
                                    required_out_length,
                                    data_out);
  if (result == CKR_BUFFER_TOO_SMALL) {
    // We'll keep the context valid so a subsequent call can pick up the data.
    context->is_valid_ = true;
  }
  return result;
}

CK_RV SessionImpl::VerifyFinal(const string& signature) {
  OperationContext* context = &operation_context_[kVerify];
  // Call the generic OperationFinal so any digest or HMAC computation gets
  // finalized.
  int max_out_length = INT_MAX;
  string data_out;
  CK_RV result = OperationFinal(kVerify, &max_out_length, &data_out);
  if (result != CKR_OK)
    return result;
  // We only support two Verify mechanisms, HMAC and RSA.
  if (context->is_hmac_) {
    // The data_out contents will be the computed HMAC. To verify an HMAC, it is
    // recomputed and literally compared.
    if (signature.length() != data_out.length())
      return CKR_SIGNATURE_LEN_RANGE;
    if (0 != chromeos::SafeMemcmp(signature.data(),
                                  data_out.data(),
                                  signature.length()))
      return CKR_SIGNATURE_INVALID;
  } else {
    // The data_out contents will be the computed digest.
    return RSAVerify(context, data_out, signature);
  }
  return CKR_OK;
}

CK_RV SessionImpl::OperationSinglePart(OperationType operation,
                                       const string& data_in,
                                       int* required_out_length,
                                       string* data_out) {
  CHECK(operation < kNumOperationTypes);
  OperationContext* context = &operation_context_[operation];
  CK_RV result = CKR_OK;
  if (!context->is_finished_) {
    string update, final;
    int max = INT_MAX;
    result = OperationUpdate(operation, data_in, &max, &update);
    if (result != CKR_OK)
      return result;
    max = INT_MAX;
    result = OperationFinal(operation, &max, &final);
    if (result != CKR_OK)
      return result;
    context->data_ = update + final;
    context->is_finished_ = true;
  }
  context->is_valid_ = false;
  result = GetOperationOutput(context,
                              required_out_length,
                              data_out);
  if (result == CKR_BUFFER_TOO_SMALL) {
    // We'll keep the context valid so a subsequent call can pick up the data.
    context->is_valid_ = true;
  }
  return result;
}

CK_RV SessionImpl::GenerateKey(CK_MECHANISM_TYPE mechanism,
                               const string& mechanism_parameter,
                               const CK_ATTRIBUTE_PTR attributes,
                               int num_attributes,
                               int* new_key_handle) {
  CHECK(new_key_handle);
  scoped_ptr<Object> object(factory_->CreateObject());
  CHECK(object.get());
  CK_RV result = object->SetAttributes(attributes, num_attributes);
  if (result != CKR_OK)
    return result;
  CK_KEY_TYPE key_type = 0;
  string key_material;
  switch (mechanism) {
    case CKM_DES_KEY_GEN: {
      key_type = CKK_DES;
      if (!GenerateDESKey(&key_material))
        return CKR_FUNCTION_FAILED;
      break;
    }
    case CKM_DES3_KEY_GEN: {
      key_type = CKK_DES3;
      string des[3];
      for (int i = 0; i < 3; ++i) {
        if (!GenerateDESKey(&des[i]))
          return CKR_FUNCTION_FAILED;
      }
      key_material = des[0] + des[1] + des[2];
      break;
    }
    case CKM_AES_KEY_GEN: {
      key_type = CKK_AES;
      if (!object->IsAttributePresent(CKA_VALUE_LEN))
        return CKR_TEMPLATE_INCOMPLETE;
      int key_length = object->GetAttributeInt(CKA_VALUE_LEN, 0);
      if (key_length != 16 && key_length != 24 && key_length != 32)
        return CKR_KEY_SIZE_RANGE;
      key_material = GenerateRandomSoftware(key_length);
      break;
    }
    case CKM_GENERIC_SECRET_KEY_GEN: {
      key_type = CKK_GENERIC_SECRET;
      if (!object->IsAttributePresent(CKA_VALUE_LEN))
        return CKR_TEMPLATE_INCOMPLETE;
      int key_length = object->GetAttributeInt(CKA_VALUE_LEN, 0);
      if (key_length < 1)
        return CKR_KEY_SIZE_RANGE;
      key_material = GenerateRandomSoftware(key_length);
      break;
    }
    default: {
      LOG(ERROR) << "GenerateKey: Mechanism not supported: " << hex
                 << mechanism;
      return CKR_MECHANISM_INVALID;
    }
  }
  object->SetAttributeInt(CKA_CLASS, CKO_SECRET_KEY);
  object->SetAttributeInt(CKA_KEY_TYPE, key_type);
  object->SetAttributeString(CKA_VALUE, key_material);
  object->SetAttributeBool(CKA_LOCAL, true);
  object->SetAttributeInt(CKA_KEY_GEN_MECHANISM, mechanism);
  result = object->FinalizeNewObject();
  if (result != CKR_OK)
    return result;
  ObjectPool* pool = object->IsTokenObject() ? token_object_pool_
      : session_object_pool_.get();
  if (!pool->Insert(object.get()))
    return CKR_FUNCTION_FAILED;
  *new_key_handle = object.release()->handle();
  return CKR_OK;
}

CK_RV SessionImpl::GenerateKeyPair(CK_MECHANISM_TYPE mechanism,
                                   const string& mechanism_parameter,
                                   const CK_ATTRIBUTE_PTR public_attributes,
                                   int num_public_attributes,
                                   const CK_ATTRIBUTE_PTR private_attributes,
                                   int num_private_attributes,
                                   int* new_public_key_handle,
                                   int* new_private_key_handle) {
  CHECK(new_public_key_handle);
  CHECK(new_private_key_handle);
  if (mechanism != CKM_RSA_PKCS_KEY_PAIR_GEN) {
    LOG(ERROR) << "GenerateKeyPair: Mechanism not supported: " << hex
               << mechanism;
    return CKR_MECHANISM_INVALID;
  }
  scoped_ptr<Object> public_object(factory_->CreateObject());
  CHECK(public_object.get());
  scoped_ptr<Object> private_object(factory_->CreateObject());
  CHECK(private_object.get());
  CK_RV result = public_object->SetAttributes(public_attributes,
                                              num_public_attributes);
  if (result != CKR_OK)
    return result;
  result = private_object->SetAttributes(private_attributes,
                                         num_private_attributes);
  if (result != CKR_OK)
    return result;
  // CKA_PUBLIC_EXPONENT is optional. The default is 65537 (0x10001).
  string public_exponent("\x01\x00\x01", 3);
  if (public_object->IsAttributePresent(CKA_PUBLIC_EXPONENT))
    public_exponent = public_object->GetAttributeString(CKA_PUBLIC_EXPONENT);
  public_object->SetAttributeString(CKA_PUBLIC_EXPONENT, public_exponent);
  private_object->SetAttributeString(CKA_PUBLIC_EXPONENT, public_exponent);
  if (!public_object->IsAttributePresent(CKA_MODULUS_BITS))
    return CKR_TEMPLATE_INCOMPLETE;
  int modulus_bits = public_object->GetAttributeInt(CKA_MODULUS_BITS, 0);
  if (modulus_bits < kMinRSAKeyBits || modulus_bits > kMaxRSAKeyBits)
    return CKR_KEY_SIZE_RANGE;
  ObjectPool* pool = token_object_pool_;
  if (private_object->IsTokenObject()) {
    string auth_data = GenerateRandomSoftware(kDefaultAuthDataBytes);
    string key_blob;
    int tpm_key_handle;
    if (!tpm_utility_->GenerateKey(slot_id_,
                                   modulus_bits,
                                   public_exponent,
                                   auth_data,
                                   &key_blob,
                                   &tpm_key_handle))
      return CKR_FUNCTION_FAILED;
    string modulus;
    if (!tpm_utility_->GetPublicKey(tpm_key_handle, &public_exponent, &modulus))
      return CKR_FUNCTION_FAILED;
    public_object->SetAttributeString(CKA_MODULUS, modulus);
    private_object->SetAttributeString(CKA_MODULUS, modulus);
    private_object->SetAttributeString(kAuthDataAttribute, auth_data);
    private_object->SetAttributeString(kKeyBlobAttribute, key_blob);
  } else {
    pool = session_object_pool_.get();
    if (!GenerateKeyPairSoftware(modulus_bits,
                                 public_exponent,
                                 public_object.get(),
                                 private_object.get()))
      return CKR_FUNCTION_FAILED;
  }
  public_object->SetAttributeInt(CKA_CLASS, CKO_PUBLIC_KEY);
  public_object->SetAttributeInt(CKA_KEY_TYPE, CKK_RSA);
  private_object->SetAttributeInt(CKA_CLASS, CKO_PRIVATE_KEY);
  private_object->SetAttributeInt(CKA_KEY_TYPE, CKK_RSA);
  public_object->SetAttributeBool(CKA_LOCAL, true);
  private_object->SetAttributeBool(CKA_LOCAL, true);
  public_object->SetAttributeInt(CKA_KEY_GEN_MECHANISM, mechanism);
  private_object->SetAttributeInt(CKA_KEY_GEN_MECHANISM, mechanism);
  result = public_object->FinalizeNewObject();
  if (result != CKR_OK)
    return result;
  result = private_object->FinalizeNewObject();
  if (result != CKR_OK)
    return result;
  if (!pool->Insert(public_object.get()))
    return CKR_FUNCTION_FAILED;
  if (!pool->Insert(private_object.get())) {
    pool->Delete(public_object.release());
    return CKR_FUNCTION_FAILED;
  }
  *new_public_key_handle = public_object.release()->handle();
  *new_private_key_handle = private_object.release()->handle();
  return CKR_OK;
}

CK_RV SessionImpl::SeedRandom(const string& seed) {
  RAND_seed(seed.data(), seed.length());
  return CKR_OK;
}

CK_RV SessionImpl::GenerateRandom(int num_bytes, string* random_data) {
  *random_data = GenerateRandomSoftware(num_bytes);
  return CKR_OK;
}

void SessionImpl::WaitForPrivateObjects() {
  scoped_ptr<Object> all_private(factory_->CreateObject());
  CHECK(all_private.get());
  all_private->SetAttributeBool(CKA_PRIVATE, true);
  vector<const Object*> found;
  token_object_pool_->Find(all_private.get(), &found);
}

bool SessionImpl::IsValidKeyType(OperationType operation,
                                 CK_MECHANISM_TYPE mechanism,
                                 CK_OBJECT_CLASS object_class,
                                 CK_KEY_TYPE key_type) {
  CK_KEY_TYPE expected_key_type = 0;
  CK_OBJECT_CLASS expected_class = 0;
  CK_OBJECT_CLASS asymmetric_class = CKO_PUBLIC_KEY;
  if (operation == kSign || operation == kDecrypt)
    asymmetric_class = CKO_PRIVATE_KEY;
  switch (mechanism) {
    case CKM_DES_ECB:
    case CKM_DES_CBC:
    case CKM_DES_CBC_PAD:
      expected_key_type = CKK_DES;
      expected_class = CKO_SECRET_KEY;
      break;
    case CKM_DES3_ECB:
    case CKM_DES3_CBC:
    case CKM_DES3_CBC_PAD:
      expected_key_type = CKK_DES3;
      expected_class = CKO_SECRET_KEY;
      break;
    case CKM_AES_ECB:
    case CKM_AES_CBC:
    case CKM_AES_CBC_PAD:
      expected_key_type = CKK_AES;
      expected_class = CKO_SECRET_KEY;
      break;
    case CKM_RSA_PKCS:
    case CKM_MD5_RSA_PKCS:
    case CKM_SHA1_RSA_PKCS:
    case CKM_SHA256_RSA_PKCS:
    case CKM_SHA384_RSA_PKCS:
    case CKM_SHA512_RSA_PKCS:
      expected_key_type = CKK_RSA;
      expected_class = asymmetric_class;
      break;
    case CKM_MD5_HMAC:
    case CKM_SHA_1_HMAC:
    case CKM_SHA256_HMAC:
    case CKM_SHA384_HMAC:
    case CKM_SHA512_HMAC:
      expected_key_type = CKK_GENERIC_SECRET;
      expected_class = CKO_SECRET_KEY;
      break;
    default:
      return false;
  }
  return (key_type == expected_key_type &&
          object_class == expected_class);
}

bool SessionImpl::IsValidMechanism(OperationType operation,
                                   CK_MECHANISM_TYPE mechanism) {
  if (operation == kEncrypt || operation == kDecrypt) {
    switch (mechanism) {
      case CKM_DES_ECB:
      case CKM_DES_CBC:
      case CKM_DES_CBC_PAD:
      case CKM_DES3_ECB:
      case CKM_DES3_CBC:
      case CKM_DES3_CBC_PAD:
      case CKM_AES_ECB:
      case CKM_AES_CBC:
      case CKM_AES_CBC_PAD:
      case CKM_RSA_PKCS:
        return true;
    }
  } else if (operation == kSign || operation == kVerify) {
    switch (mechanism) {
      case CKM_RSA_PKCS:
      case CKM_MD5_RSA_PKCS:
      case CKM_SHA1_RSA_PKCS:
      case CKM_SHA256_RSA_PKCS:
      case CKM_SHA384_RSA_PKCS:
      case CKM_SHA512_RSA_PKCS:
      case CKM_MD5_HMAC:
      case CKM_SHA_1_HMAC:
      case CKM_SHA256_HMAC:
      case CKM_SHA384_HMAC:
      case CKM_SHA512_HMAC:
        return true;
    }
  } else {
    switch (mechanism) {
      case CKM_MD5:
      case CKM_SHA_1:
      case CKM_SHA256:
      case CKM_SHA384:
      case CKM_SHA512:
        return true;
    }
  }
  return false;
}

CK_RV SessionImpl::CipherInit(bool is_encrypt,
                              CK_MECHANISM_TYPE mechanism,
                              const string& mechanism_parameter,
                              const Object* key) {
  OperationType operation = is_encrypt ? kEncrypt : kDecrypt;
  EVP_CIPHER_CTX* context =
      &operation_context_[operation].cipher_context_;
  string key_material = key->GetAttributeString(CKA_VALUE);
  const EVP_CIPHER* cipher_type = GetOpenSSLCipher(mechanism,
                                                   key_material.size());
  if (!cipher_type) {
    LOG(ERROR) << "Mechanism not supported: 0x" << hex << mechanism;
    return CKR_MECHANISM_INVALID;
  }
  // The mechanism parameter is the IV for cipher modes which require an IV,
  // otherwise it is expected to be empty.
  if (static_cast<int>(mechanism_parameter.size()) !=
      EVP_CIPHER_iv_length(cipher_type)) {
    LOG(ERROR) << "IV length is invalid: " << mechanism_parameter.size();
    return CKR_MECHANISM_PARAM_INVALID;
  }
  if (static_cast<int>(key_material.size()) !=
      EVP_CIPHER_key_length(cipher_type)) {
    LOG(ERROR) << "Key size not supported: " << key_material.size();
    return CKR_KEY_SIZE_RANGE;
  }
  if (!EVP_CipherInit(context,
                      cipher_type,
                      ConvertStringToByteBuffer(key_material.c_str()),
                      ConvertStringToByteBuffer(mechanism_parameter.c_str()),
                      is_encrypt)) {
    LOG(ERROR) << "EVP_CipherInit failed: " << GetOpenSSLError();
    return CKR_FUNCTION_FAILED;
  }
  EVP_CIPHER_CTX_set_padding(context, IsPaddingEnabled(mechanism));
  operation_context_[operation].is_valid_ = true;
  operation_context_[operation].is_cipher_ = true;
  return CKR_OK;
}

CK_RV SessionImpl::CipherUpdate(OperationContext* context,
                                const string& data_in,
                                int* required_out_length,
                                string* data_out) {
  CHECK(required_out_length);
  CHECK(data_out);
  // If we have output already waiting, we don't need to process input.
  if (context->data_.empty()) {
    int in_length = data_in.length();
    int out_length = in_length + kMaxCipherBlockBytes;
    context->data_.resize(out_length);
    if (!EVP_CipherUpdate(
        &context->cipher_context_,
        ConvertStringToByteBuffer(context->data_.c_str()),
        &out_length,
        ConvertStringToByteBuffer(data_in.c_str()),
        in_length)) {
      EVP_CIPHER_CTX_cleanup(&context->cipher_context_);
      context->is_valid_ = false;
      LOG(ERROR) << "EVP_CipherUpdate failed: " << GetOpenSSLError();
      return CKR_FUNCTION_FAILED;
    }
    context->data_.resize(out_length);
  }
  return GetOperationOutput(context,
                            required_out_length,
                            data_out);
}

CK_RV SessionImpl::CipherFinal(OperationContext* context) {
  if (context->data_.empty()) {
    int out_length = kMaxCipherBlockBytes * 2;
    context->data_.resize(out_length);
    if (!EVP_CipherFinal(
        &context->cipher_context_,
        ConvertStringToByteBuffer(context->data_.c_str()),
        &out_length)) {
      LOG(ERROR) << "EVP_CipherFinal failed: " << GetOpenSSLError();
      return CKR_FUNCTION_FAILED;
    }
    context->data_.resize(out_length);
  }
  return CKR_OK;
}

CK_RV SessionImpl::CreateObjectInternal(const CK_ATTRIBUTE_PTR attributes,
                                        int num_attributes,
                                        const Object* copy_from_object,
                                        int* new_object_handle) {
  CHECK(new_object_handle);
  CHECK(attributes || num_attributes == 0);
  scoped_ptr<Object> object(factory_->CreateObject());
  CHECK(object.get());
  CK_RV result = CKR_OK;
  if (copy_from_object) {
    result = object->Copy(copy_from_object);
    if (result != CKR_OK)
      return result;
  }
  result = object->SetAttributes(attributes, num_attributes);
  if (result != CKR_OK)
    return result;
  if (!copy_from_object) {
    result = object->FinalizeNewObject();
    if (result != CKR_OK)
      return result;
  }
  ObjectPool* pool = token_object_pool_;
  if (object->IsTokenObject()) {
    result = WrapPrivateKey(object.get());
    if (result != CKR_OK)
      return result;
  } else {
    pool = session_object_pool_.get();
  }
  if (!pool->Insert(object.get()))
    return CKR_GENERAL_ERROR;
  *new_object_handle = object.release()->handle();
  return CKR_OK;
}

bool SessionImpl::GenerateDESKey(string* key_material) {
  static const int kDESKeySizeBytes = 8;
  bool done = false;
  while (!done) {
    string tmp = GenerateRandomSoftware(kDESKeySizeBytes);
    DES_cblock des;
    memcpy(&des, tmp.data(), kDESKeySizeBytes);
    if (!DES_is_weak_key(&des)) {
      DES_set_odd_parity(&des);
      *key_material = string(reinterpret_cast<char*>(des), kDESKeySizeBytes);
      done = true;
    }
  }
  return true;
}

bool SessionImpl::GenerateKeyPairSoftware(int modulus_bits,
                                          const string& public_exponent,
                                          Object* public_object,
                                          Object* private_object) {
  if (public_exponent.length() > sizeof(unsigned long) ||
      public_exponent.empty())
    return false;
  BIGNUM* e = ConvertToBIGNUM(public_exponent);
  RSA* key = RSA_generate_key(modulus_bits, BN_get_word(e), NULL, NULL);
  CHECK(key);
  string n = ConvertFromBIGNUM(key->n);
  string d = ConvertFromBIGNUM(key->d);
  string p = ConvertFromBIGNUM(key->p);
  string q = ConvertFromBIGNUM(key->q);
  string dmp1 = ConvertFromBIGNUM(key->dmp1);
  string dmq1 = ConvertFromBIGNUM(key->dmq1);
  string iqmp = ConvertFromBIGNUM(key->iqmp);
  public_object->SetAttributeString(CKA_MODULUS, n);
  private_object->SetAttributeString(CKA_MODULUS, n);
  private_object->SetAttributeString(CKA_PRIVATE_EXPONENT, d);
  private_object->SetAttributeString(CKA_PRIME_1, p);
  private_object->SetAttributeString(CKA_PRIME_2, q);
  private_object->SetAttributeString(CKA_EXPONENT_1, dmp1);
  private_object->SetAttributeString(CKA_EXPONENT_2, dmq1);
  private_object->SetAttributeString(CKA_COEFFICIENT, iqmp);
  RSA_free(key);
  BN_free(e);
  return true;
}

string SessionImpl::GenerateRandomSoftware(int num_bytes) {
  string random(num_bytes, 0);
  RAND_bytes(ConvertStringToByteBuffer(random.data()), num_bytes);
  return random;
}

string SessionImpl::GetDERDigestInfo(CK_MECHANISM_TYPE mechanism) {
  // These strings are the DER encodings of the DigestInfo values for the
  // supported digest algorithms.  See PKCS #1 v2.1: 9.2.
  static const string kMD5DigestInfo(
      "\x30\x20\x30\x0c\x06\x08\x2a\x86\x48\x86\xf7\x0d\x02\x05\x05\x00\x04"
      "\x10",
      18);
  static const string kSHA1DigestInfo(
      "\x30\x21\x30\x09\x06\x05\x2b\x0e\x03\x02\x1a\x05\x00\x04\x14",
      15);
  static const string kSHA256DigestInfo(
      "\x30\x31\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01\x05\x00\x04"
      "\x20",
      19);
  static const string kSHA384DigestInfo(
      "\x30\x41\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x02\x05\x00\x04"
      "\x30",
      19);
  static const string kSHA512DigestInfo(
      "\x30\x51\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x03\x05\x00\x04"
      "\x40",
      19);
  const EVP_MD* md = GetOpenSSLDigest(mechanism);
  if (md == EVP_md5()) {
    return kMD5DigestInfo;
  } else if (md == EVP_sha1()) {
    return kSHA1DigestInfo;
  } else if (md == EVP_sha256()) {
    return kSHA256DigestInfo;
  } else if (md == EVP_sha384()) {
    return kSHA384DigestInfo;
  } else if (md == EVP_sha512()) {
    return kSHA512DigestInfo;
  }
  // This is valid in some cases (e.g. CKM_RSA_PKCS).
  return string();
}

CK_RV SessionImpl::GetOperationOutput(OperationContext* context,
                                      int* required_out_length,
                                      string* data_out) {
  int out_length = context->data_.length();
  int max_length = *required_out_length;
  *required_out_length = out_length;
  if (max_length < out_length)
    return CKR_BUFFER_TOO_SMALL;
  *data_out = context->data_;
  context->data_.clear();
  return CKR_OK;
}

CK_ATTRIBUTE_TYPE SessionImpl::GetRequiredKeyUsage(OperationType operation) {
  switch (operation) {
    case kEncrypt:
      return CKA_ENCRYPT;
    case kDecrypt:
      return CKA_DECRYPT;
    case kSign:
      return CKA_SIGN;
    case kVerify:
      return CKA_VERIFY;
    default:
      break;
  }
  return 0;
}

bool SessionImpl::GetTPMKeyHandle(const Object* key, int* key_handle) {
  map<const Object*, int>::iterator it = object_tpm_handle_map_.find(key);
  if (it == object_tpm_handle_map_.end()) {
    // Only private keys are loaded into the TPM. All public key operations do
    // not use the TPM (and use OpenSSL instead).
    if (key->GetObjectClass() == CKO_PRIVATE_KEY) {
      if (key->GetAttributeBool(kLegacyAttribute, false)) {
        // This is a legacy key and it needs to be loaded with the legacy root
        // key.
        if (!LoadLegacyRootKeys())
          return false;
        bool is_private = key->GetAttributeBool(CKA_PRIVATE, true);
        int root_key_handle = is_private ? private_root_key_ : public_root_key_;
        if (!tpm_utility_->LoadKeyWithParent(
            slot_id_,
            key->GetAttributeString(kKeyBlobAttribute),
            key->GetAttributeString(kAuthDataAttribute),
            root_key_handle,
            key_handle))
          return false;
      } else {
        if (!tpm_utility_->LoadKey(
            slot_id_,
            key->GetAttributeString(kKeyBlobAttribute),
            key->GetAttributeString(kAuthDataAttribute),
            key_handle))
          return false;
      }
    } else {
      LOG(ERROR) << "Invalid object class for loading into TPM.";
      return false;
    }
    object_tpm_handle_map_[key] = *key_handle;
  } else {
    *key_handle = it->second;
  }
  return true;
}

bool SessionImpl::LoadLegacyRootKeys() {
  if (is_legacy_loaded_)
    return true;

  // Load the legacy root keys. See http://trousers.sourceforge.net/pkcs11.html
  // for details on where these come from.
  string private_blob;
  if (!token_object_pool_->GetInternalBlob(kLegacyPrivateRootKey,
                                           &private_blob)) {
    LOG(ERROR) << "Failed to read legacy private root key blob.";
    return false;
  }
  if (!tpm_utility_->LoadKey(slot_id_,
                             private_blob,
                             "",
                             &private_root_key_)) {
    LOG(ERROR) << "Failed to load legacy private root key.";
    return false;
  }
  string public_blob;
  if (!token_object_pool_->GetInternalBlob(kLegacyPublicRootKey,
                                           &public_blob)) {
    LOG(ERROR) << "Failed to read legacy public root key blob.";
    return false;
  }
  if (!tpm_utility_->LoadKey(slot_id_, public_blob, "", &public_root_key_)) {
    LOG(ERROR) << "Failed to load legacy public root key.";
    return false;
  }
  is_legacy_loaded_ = true;
  return true;
}

bool SessionImpl::IsHMAC(CK_MECHANISM_TYPE mechanism) {
  switch (mechanism) {
    case CKM_MD5_HMAC:
    case CKM_SHA_1_HMAC:
    case CKM_SHA256_HMAC:
    case CKM_SHA384_HMAC:
    case CKM_SHA512_HMAC:
      return true;
  }
  return false;
}

bool SessionImpl::IsPaddingEnabled(CK_MECHANISM_TYPE mechanism) {
  switch (mechanism) {
    case CKM_DES_CBC_PAD:
    case CKM_DES3_CBC_PAD:
    case CKM_AES_CBC_PAD:
      return true;
  }
  return false;
}

bool SessionImpl::IsRSA(CK_MECHANISM_TYPE mechanism) {
  switch (mechanism) {
    case CKM_RSA_PKCS:
    case CKM_MD5_RSA_PKCS:
    case CKM_SHA1_RSA_PKCS:
    case CKM_SHA256_RSA_PKCS:
    case CKM_SHA384_RSA_PKCS:
    case CKM_SHA512_RSA_PKCS:
      return true;
  }
  return false;
}

// Both PKCS #11 and OpenSSL use big-endian binary representations of big
// integers.  To convert we can just use the OpenSSL converters.
string SessionImpl::ConvertFromBIGNUM(const BIGNUM* bignum) {
  string big_integer(BN_num_bytes(bignum), 0);
  BN_bn2bin(bignum, ConvertStringToByteBuffer(big_integer.data()));
  return big_integer;
}

BIGNUM* SessionImpl::ConvertToBIGNUM(const string& big_integer) {
  if (big_integer.empty())
    return NULL;
  BIGNUM* b = BN_bin2bn(ConvertStringToByteBuffer(big_integer.data()),
                        big_integer.length(),
                        NULL);
  CHECK(b);
  return b;
}

RSA* SessionImpl::CreateKeyFromObject(const Object* key_object) {
  RSA* rsa = RSA_new();
  CHECK(rsa);
  if (key_object->GetObjectClass() == CKO_PUBLIC_KEY) {
    string e = key_object->GetAttributeString(CKA_PUBLIC_EXPONENT);
    rsa->e = ConvertToBIGNUM(e);
    string n = key_object->GetAttributeString(CKA_MODULUS);
    rsa->n = ConvertToBIGNUM(n);
  } else {
    string n = key_object->GetAttributeString(CKA_MODULUS);
    rsa->n = ConvertToBIGNUM(n);
    string d = key_object->GetAttributeString(CKA_PRIVATE_EXPONENT);
    rsa->d = ConvertToBIGNUM(d);
    string p = key_object->GetAttributeString(CKA_PRIME_1);
    rsa->p = ConvertToBIGNUM(p);
    string q = key_object->GetAttributeString(CKA_PRIME_2);
    rsa->q = ConvertToBIGNUM(q);
    string dmp1 = key_object->GetAttributeString(CKA_EXPONENT_1);
    rsa->dmp1 = ConvertToBIGNUM(dmp1);
    string dmq1 = key_object->GetAttributeString(CKA_EXPONENT_2);
    rsa->dmq1 = ConvertToBIGNUM(dmq1);
    string iqmp = key_object->GetAttributeString(CKA_COEFFICIENT);
    rsa->iqmp = ConvertToBIGNUM(iqmp);
  }
  return rsa;
}

const EVP_CIPHER* SessionImpl::GetOpenSSLCipher(CK_MECHANISM_TYPE mechanism,
                                                size_t key_size) {
  switch (mechanism) {
    case CKM_DES_ECB:
      return EVP_des_ecb();
    case CKM_DES_CBC:
    case CKM_DES_CBC_PAD:
      return EVP_des_cbc();
    case CKM_DES3_ECB:
      return EVP_des_ede3();
    case CKM_DES3_CBC:
    case CKM_DES3_CBC_PAD:
      return EVP_des_ede3_cbc();
    case CKM_AES_ECB:
      switch (key_size) {
        case 16:
          return EVP_aes_128_ecb();
        case 24:
          return EVP_aes_192_ecb();
        default:
          return EVP_aes_256_ecb();
      }
      break;
    case CKM_AES_CBC:
    case CKM_AES_CBC_PAD:
      switch (key_size) {
        case 16:
          return EVP_aes_128_cbc();
        case 24:
          return EVP_aes_192_cbc();
        default:
          return EVP_aes_256_cbc();
      }
      break;
  }
  return NULL;
}

const EVP_MD* SessionImpl::GetOpenSSLDigest(CK_MECHANISM_TYPE mechanism) {
  switch (mechanism) {
    case CKM_MD5:
    case CKM_MD5_HMAC:
    case CKM_MD5_RSA_PKCS:
      return EVP_md5();
    case CKM_SHA_1:
    case CKM_SHA_1_HMAC:
    case CKM_SHA1_RSA_PKCS:
      return EVP_sha1();
    case CKM_SHA256:
    case CKM_SHA256_HMAC:
    case CKM_SHA256_RSA_PKCS:
      return EVP_sha256();
    case CKM_SHA384:
    case CKM_SHA384_HMAC:
    case CKM_SHA384_RSA_PKCS:
      return EVP_sha384();
    case CKM_SHA512:
    case CKM_SHA512_HMAC:
    case CKM_SHA512_RSA_PKCS:
      return EVP_sha512();
  }
  return NULL;
}

bool SessionImpl::RSADecrypt(OperationContext* context) {
  if (context->key_->IsTokenObject()) {
    int tpm_key_handle = 0;
    if (!GetTPMKeyHandle(context->key_, &tpm_key_handle))
      return false;
    string encrypted_data = context->data_;
    context->data_.clear();
    if (!tpm_utility_->Unbind(tpm_key_handle, encrypted_data, &context->data_))
      return false;
  } else {
    RSA* rsa = CreateKeyFromObject(context->key_);
    uint8_t buffer[kMaxRSAOutputBytes];
    int length = RSA_private_decrypt(
        context->data_.length(),
        ConvertStringToByteBuffer(context->data_.data()),
        buffer,
        rsa,
        RSA_PKCS1_PADDING);  // Strips PKCS #1 type 2 padding.
    RSA_free(rsa);
    if (length == -1) {
      LOG(ERROR) << "RSA_private_decrypt failed: " << GetOpenSSLError();
      return false;
    }
    context->data_ = ConvertByteBufferToString(buffer, length);
  }
  return true;
}

bool SessionImpl::RSAEncrypt(OperationContext* context) {
  RSA* rsa = CreateKeyFromObject(context->key_);
  uint8_t buffer[kMaxRSAOutputBytes];
  int length = RSA_public_encrypt(
      context->data_.length(),
      ConvertStringToByteBuffer(context->data_.data()),
      buffer,
      rsa,
      RSA_PKCS1_PADDING);  // Adds PKCS #1 type 2 padding.
  RSA_free(rsa);
  if (length == -1) {
    LOG(ERROR) << "RSA_public_encrypt failed: " << GetOpenSSLError();
    return false;
  }
  context->data_ = ConvertByteBufferToString(buffer, length);
  return true;
}

bool SessionImpl::RSASign(OperationContext* context) {
  string data_to_sign = GetDERDigestInfo(context->mechanism_) + context->data_;
  string signature;
  if (context->key_->IsTokenObject()) {
    int tpm_key_handle = 0;
    if (!GetTPMKeyHandle(context->key_, &tpm_key_handle))
      return false;
    if (!tpm_utility_->Sign(tpm_key_handle, data_to_sign, &signature))
      return false;
  } else {
    RSA* rsa = CreateKeyFromObject(context->key_);
    uint8_t buffer[kMaxRSAOutputBytes];
    int length = RSA_private_encrypt(
        data_to_sign.length(),
        ConvertStringToByteBuffer(data_to_sign.data()),
        buffer,
        rsa,
        RSA_PKCS1_PADDING);  // Adds PKCS #1 type 1 padding.
    RSA_free(rsa);
    if (length == -1) {
      LOG(ERROR) << "RSA_private_encrypt failed: " << GetOpenSSLError();
      return false;
    }
    signature = string(reinterpret_cast<char*>(buffer), length);
  }
  context->data_ = signature;
  return true;
}

CK_RV SessionImpl::RSAVerify(OperationContext* context,
                             const string& digest,
                             const string& signature) {
  if (context->key_->GetAttributeString(CKA_MODULUS).length() !=
      signature.length())
    return CKR_SIGNATURE_LEN_RANGE;
  RSA* rsa = CreateKeyFromObject(context->key_);
  uint8_t buffer[kMaxRSAOutputBytes];
  int length = RSA_public_decrypt(
      signature.length(),
      ConvertStringToByteBuffer(signature.data()),
      buffer,
      rsa,
      RSA_PKCS1_PADDING);  // Strips PKCS #1 type 1 padding.
  RSA_free(rsa);
  if (length == -1) {
    LOG(ERROR) << "RSA_public_decrypt failed: " << GetOpenSSLError();
    return CKR_SIGNATURE_INVALID;
  }
  string signed_data = GetDERDigestInfo(context->mechanism_) + digest;
  if (static_cast<size_t>(length) != signed_data.length() ||
      0 != chromeos::SafeMemcmp(buffer, signed_data.data(), length))
    return CKR_SIGNATURE_INVALID;
  return CKR_OK;
}

CK_RV SessionImpl::WrapPrivateKey(Object* object) {
  if (object->GetObjectClass() == CKO_PRIVATE_KEY) {
    if (!object->IsAttributePresent(CKA_PUBLIC_EXPONENT) ||
        !object->IsAttributePresent(CKA_MODULUS) ||
        !(object->IsAttributePresent(CKA_PRIME_1) ||
          object->IsAttributePresent(CKA_PRIME_2)))
      return CKR_TEMPLATE_INCOMPLETE;
    string prime;
    if (object->IsAttributePresent(CKA_PRIME_1)) {
      prime = object->GetAttributeString(CKA_PRIME_1);
    } else {
      prime = object->GetAttributeString(CKA_PRIME_2);
    }
    string auth_data = GenerateRandomSoftware(kDefaultAuthDataBytes);
    string key_blob;
    int tpm_key_handle = 0;
    if (!tpm_utility_->WrapKey(slot_id_,
                               object->GetAttributeString(CKA_PUBLIC_EXPONENT),
                               object->GetAttributeString(CKA_MODULUS),
                               prime,
                               auth_data,
                               &key_blob,
                               &tpm_key_handle))
      return CKR_FUNCTION_FAILED;
    object->SetAttributeString(kAuthDataAttribute, auth_data);
    object->SetAttributeString(kKeyBlobAttribute, key_blob);
    object->RemoveAttribute(CKA_PRIVATE_EXPONENT);
    object->RemoveAttribute(CKA_PRIME_1);
    object->RemoveAttribute(CKA_PRIME_2);
    object->RemoveAttribute(CKA_EXPONENT_1);
    object->RemoveAttribute(CKA_EXPONENT_2);
    object->RemoveAttribute(CKA_COEFFICIENT);
  }
  return CKR_OK;
}

SessionImpl::OperationContext::OperationContext() {
  Clear();
}

void SessionImpl::OperationContext::Clear() {
  is_valid_ = false;
  is_cipher_ = false;
  is_digest_ = false;
  is_hmac_ = false;
  is_finished_ = false;
  key_ = NULL;
  data_.clear();
  parameter_.clear();
}

}  // namespace
