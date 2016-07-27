// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/boot_lockbox.h"

#include <sys/types.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/stl_util.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"

using base::FilePath;
using brillo::SecureBlob;

namespace {

const int kPCRIndex = 15;

const unsigned char kPCRValue[20] = {0};

// This is an arbitrary value, our only goal is for the PCR to be non-zero.
const char kPCRExtension[] = "CROS_PCR15_845A4A757B94";

const FilePath::CharType kKeyFilePath[] = "/var/lib/boot-lockbox/key";

const mode_t kKeyFilePermissions = 0600;

// So we can use std::unique_ptr with openssl types.
struct RSADeleter {
  void operator()(void* ptr) const {
    if (ptr)
      RSA_free(reinterpret_cast<RSA*>(ptr));
  }
};

}  // namespace

namespace cryptohome {

BootLockbox::BootLockbox(Tpm* tpm, Platform* platform, Crypto* crypto)
    : tpm_(tpm), platform_(platform), crypto_(crypto) {
}

BootLockbox::~BootLockbox() {}

bool BootLockbox::Sign(const brillo::SecureBlob& data,
                       brillo::SecureBlob* signature) {
  CHECK(signature);
  brillo::SecureBlob key_blob;
  if (!GetKeyBlob(&key_blob)) {
    return false;
  }
  return tpm_->Sign(key_blob, data, kPCRIndex, signature);
}

bool BootLockbox::Verify(const brillo::SecureBlob& data,
                         const brillo::SecureBlob& signature) {
  brillo::SecureBlob public_key;
  if (!GetPublicKey(&public_key)) {
    return false;
  }
  if (!VerifySignature(public_key, data, signature)) {
    return false;
  }
  brillo::SecureBlob key_blob;
  if (!GetKeyBlob(&key_blob)) {
    return false;
  }
  brillo::SecureBlob creation_blob;
  if (!GetCreationBlob(&creation_blob)) {
    return false;
  }
  brillo::SecureBlob pcr_value(std::begin(kPCRValue), std::end(kPCRValue));
  if (!tpm_->VerifyPCRBoundKey(kPCRIndex, pcr_value, key_blob, creation_blob)) {
    return false;
  }
  return true;
}

bool BootLockbox::FinalizeBoot() {
  if (IsFinalized()) {
      // The PCR is already not at the initial value, no need to extend again.
      return true;
  }
  return tpm_->ExtendPCR(kPCRIndex, CryptoLib::Sha1(
      brillo::SecureBlob(std::begin(kPCRExtension),
                           std::end(kPCRExtension))));
}

bool BootLockbox::IsFinalized() {
  brillo::SecureBlob actual_pcr_value;
  return tpm_->ReadPCR(kPCRIndex, &actual_pcr_value) &&
         actual_pcr_value.size() == arraysize(kPCRValue) &&
         memcmp(actual_pcr_value.data(), kPCRValue, arraysize(kPCRValue));
}

bool BootLockbox::GetKeyBlob(brillo::SecureBlob* key_blob) {
  if (!key_.has_key_blob() && !LoadKey() && !CreateKey()) {
    return false;
  }
  CHECK(key_.has_key_blob());
  if (key_blob) {
    key_blob->assign(key_.key_blob().begin(), key_.key_blob().end());
  }
  return true;
}

bool BootLockbox::GetPublicKey(brillo::SecureBlob* public_key) {
  if (!key_.has_public_key_der() && !LoadKey()) {
    return false;
  }
  CHECK(key_.has_public_key_der());
  if (public_key) {
    public_key->assign(key_.public_key_der().begin(),
                       key_.public_key_der().end());
  }
  return true;
}

bool BootLockbox::GetCreationBlob(brillo::SecureBlob* creation_blob) {
  if (!key_.has_creation_blob() && !LoadKey()) {
    return false;
  }
  if (creation_blob) {
    creation_blob->assign(key_.creation_blob().begin(),
                          key_.creation_blob().end());
  }
  return true;
}

bool BootLockbox::LoadKey() {
  std::string file_contents;
  if (!platform_->ReadFileToString(FilePath(kKeyFilePath), &file_contents)) {
    return false;
  }
  brillo::SecureBlob protobuf;
  if (!crypto_->DecryptWithTpm(file_contents, &protobuf)) {
    LOG(WARNING) << "Failed to decrypt boot-lockbox key.";
    return false;
  }
  if (!key_.ParseFromArray(protobuf.data(), protobuf.size())) {
    LOG(ERROR) << "Invalid boot-lockbox key.";
    return false;
  }
  return true;
}

bool BootLockbox::SaveKey() {
  brillo::SecureBlob protobuf(key_.ByteSize());
  if (!key_.SerializeToArray(protobuf.data(), protobuf.size())) {
    LOG(ERROR) << "Failed to serialize boot-lockbox key.";
    return false;
  }
  std::string encrypted_protobuf;
  if (!crypto_->EncryptWithTpm(protobuf, &encrypted_protobuf)) {
    LOG(ERROR) << "Failed to encrypt boot-lockbox key.";
    return false;
  }
  if (!platform_->WriteStringToFileAtomicDurable(FilePath(kKeyFilePath),
                                                 encrypted_protobuf,
                                                 kKeyFilePermissions)) {
    LOG(ERROR) << "Failed to write boot-lockbox key.";
    return false;
  }
  return true;
}

bool BootLockbox::CreateKey() {
  LOG(INFO) <<  "Creating new boot-lockbox key.";
  brillo::SecureBlob key_blob;
  brillo::SecureBlob public_key;
  brillo::SecureBlob creation_blob;
  brillo::SecureBlob pcr_value(std::begin(kPCRValue), std::end(kPCRValue));
  if (!tpm_->CreatePCRBoundKey(kPCRIndex, pcr_value, &key_blob,
                               &public_key, &creation_blob)) {
    LOG(ERROR) << "Failed to create boot-lockbox key.";
    return false;
  }
  key_.set_key_blob(key_blob.to_string());
  key_.set_public_key_der(public_key.to_string());
  key_.set_creation_blob(creation_blob.to_string());
  return SaveKey();
}

bool BootLockbox::VerifySignature(const brillo::SecureBlob& public_key,
                                  const brillo::SecureBlob& signed_data,
                                  const brillo::SecureBlob& signature) {
  const unsigned char* asn1_ptr = public_key.data();
  std::unique_ptr<RSA, RSADeleter> rsa(
      d2i_RSAPublicKey(NULL, &asn1_ptr, public_key.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public key.";
    return false;
  }
  brillo::SecureBlob digest = CryptoLib::Sha256(signed_data);
  if (!RSA_verify(NID_sha256, digest.data(), digest.size(),
                  signature.data(), signature.size(), rsa.get())) {
    LOG(ERROR) << "Failed to verify signature.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
