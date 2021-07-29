// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <cstdlib>
#include <memory>
#include <string>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>

#include "cryptohome/crypto/fake_recovery_mediator_crypto.h"
#include "cryptohome/crypto/recovery_crypto.h"

using base::FilePath;
using brillo::SecureBlob;
using cryptohome::FakeRecoveryMediatorCrypto;
using cryptohome::RecoveryCrypto;

namespace {

// Hkdf salt and info used for encrypting/decrypting mediator share.
static const char kHkdfSaltHex[] = "0b0b0b0b";
static const char kHkdfInfoHex[] = "0b0b0b0b0b0b0b0b";

bool CheckMandatoryFlag(const std::string& flag_name,
                        const std::string& flag_value) {
  if (!flag_value.empty())
    return true;
  LOG(ERROR) << "--" << flag_name << " is mandatory.";
  return false;
}

bool ReadFileToSecureBlobLogged(const FilePath& file_path,
                                SecureBlob* contents) {
  std::string contents_string;
  if (!base::ReadFileToString(file_path, &contents_string)) {
    LOG(ERROR) << "Failed to read from file " << file_path.value() << ".";
    return false;
  }
  *contents = SecureBlob(contents_string);
  return true;
}

bool WriteFileLogged(const FilePath& file_path,
                     base::span<const uint8_t> contents) {
  if (base::WriteFile(file_path, contents))
    return true;
  LOG(ERROR) << "Failed to write to file " << file_path.value() << ".";
  return false;
}

bool DoRecoveryCryptoCreateAction(
    const FilePath& destination_share_out_file_path,
    const FilePath& mediator_share_out_file_path,
    const FilePath& publisher_pub_key_out_file_path,
    const FilePath& recovery_secret_out_file_path) {
  std::unique_ptr<RecoveryCrypto> recovery_crypto = RecoveryCrypto::Create();
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }

  SecureBlob hkdf_info, hkdf_salt, mediator_pub_key;
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(kHkdfInfoHex, &hkdf_info));
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(kHkdfSaltHex, &hkdf_salt));
  CHECK(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));

  SecureBlob destination_share, dealer_pub_key;
  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share;
  if (!recovery_crypto->GenerateShares(mediator_pub_key,
                                       &encrypted_mediator_share,
                                       &destination_share, &dealer_pub_key)) {
    LOG(ERROR) << "Failed to generate recovery shares.";
    return false;
  }

  SecureBlob serialized_encrypted_mediator_share;
  if (!RecoveryCrypto::SerializeEncryptedMediatorShareForTesting(
          encrypted_mediator_share, &serialized_encrypted_mediator_share)) {
    LOG(ERROR) << "Failed to serialize encrypted mediator share.";
    return false;
  }

  SecureBlob publisher_pub_key, publisher_dh;
  if (!recovery_crypto->GeneratePublisherKeys(
          dealer_pub_key, &publisher_pub_key, &publisher_dh)) {
    LOG(ERROR) << "Failed to generate recovery publisher keys.";
    return false;
  }
  return WriteFileLogged(destination_share_out_file_path, destination_share) &&
         WriteFileLogged(mediator_share_out_file_path,
                         serialized_encrypted_mediator_share) &&
         WriteFileLogged(publisher_pub_key_out_file_path, publisher_pub_key) &&
         WriteFileLogged(recovery_secret_out_file_path, publisher_dh);
}

bool DoRecoveryCryptoMediateAction(
    const FilePath& mediator_share_in_file_path,
    const FilePath& publisher_pub_key_in_file_path,
    const FilePath& mediated_publisher_pub_key_out_file_path) {
  SecureBlob serialized_encrypted_mediator_share;
  SecureBlob publisher_pub_key;
  if (!ReadFileToSecureBlobLogged(mediator_share_in_file_path,
                                  &serialized_encrypted_mediator_share) ||
      !ReadFileToSecureBlobLogged(publisher_pub_key_in_file_path,
                                  &publisher_pub_key)) {
    return false;
  }

  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share;
  if (!RecoveryCrypto::DeserializeEncryptedMediatorShareForTesting(
          serialized_encrypted_mediator_share, &encrypted_mediator_share)) {
    LOG(ERROR) << "Failed to deserialize encrypted mediator share.";
    return false;
  }

  std::unique_ptr<FakeRecoveryMediatorCrypto> fake_mediator =
      FakeRecoveryMediatorCrypto::Create();
  if (!fake_mediator) {
    LOG(ERROR) << "Failed to create fake mediator object.";
    return false;
  }

  SecureBlob hkdf_info, hkdf_salt, mediator_priv_key;
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(kHkdfInfoHex, &hkdf_info));
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(kHkdfSaltHex, &hkdf_salt));
  CHECK(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
      &mediator_priv_key));

  SecureBlob mediated_publisher_pub_key;
  if (!fake_mediator->Mediate(mediator_priv_key, publisher_pub_key,
                              encrypted_mediator_share,
                              &mediated_publisher_pub_key)) {
    LOG(ERROR) << "Failed to perform recovery mediation.";
    return false;
  }
  return WriteFileLogged(mediated_publisher_pub_key_out_file_path,
                         mediated_publisher_pub_key);
}

bool DoRecoveryCryptoDecryptAction(
    const FilePath& destination_share_in_file_path,
    const FilePath& publisher_pub_key_in_file_path,
    const FilePath& mediated_publisher_pub_key_in_file_path,
    const FilePath& recovery_secret_out_file_path) {
  SecureBlob hkdf_salt;
  CHECK(brillo::SecureBlob::HexStringToSecureBlob(kHkdfSaltHex, &hkdf_salt));
  SecureBlob destination_share, publisher_pub_key, mediated_publisher_pub_key;
  if (!ReadFileToSecureBlobLogged(destination_share_in_file_path,
                                  &destination_share) ||
      !ReadFileToSecureBlobLogged(publisher_pub_key_in_file_path,
                                  &publisher_pub_key) ||
      !ReadFileToSecureBlobLogged(mediated_publisher_pub_key_in_file_path,
                                  &mediated_publisher_pub_key)) {
    return false;
  }
  std::unique_ptr<RecoveryCrypto> recovery_crypto = RecoveryCrypto::Create();
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }
  SecureBlob destination_dh;
  if (!recovery_crypto->RecoverDestination(publisher_pub_key, destination_share,
                                           /*ephemeral_pub_key=*/base::nullopt,
                                           mediated_publisher_pub_key,
                                           &destination_dh)) {
    LOG(ERROR) << "Failed to perform destination recovery.";
    return false;
  }
  return WriteFileLogged(recovery_secret_out_file_path, destination_dh);
}

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr);

  DEFINE_string(action, "",
                "One of: recovery_crypto_create, recovery_crypto_mediate, "
                "recovery_crypto_decrypt.");
  DEFINE_string(destination_share_out_file, "",
                "Path to the file where to store the Cryptohome Recovery "
                "encrypted destination share.");
  DEFINE_string(destination_share_in_file, "",
                "Path to the file containing the Cryptohome Recovery encrypted "
                "destination share.");
  DEFINE_string(mediator_share_out_file, "",
                "Path to the file where to store the Cryptohome Recovery "
                "encrypted mediator share.");
  DEFINE_string(mediator_share_in_file, "",
                "Path to the file containing the Cryptohome Recovery encrypted "
                "mediator share.");
  DEFINE_string(publisher_pub_key_out_file, "",
                "Path to the file where to store the Cryptohome Recovery "
                "publisher public key.");
  DEFINE_string(publisher_pub_key_in_file, "",
                "Path to the file containing the Cryptohome Recovery publisher "
                "public key.");
  DEFINE_string(mediated_publisher_pub_key_out_file, "",
                "Path to the file where to store the Cryptohome Recovery "
                "mediated publisher public key.");
  DEFINE_string(mediated_publisher_pub_key_in_file, "",
                "Path to the file containing the Cryptohome Recovery "
                "mediated publisher public key.");
  DEFINE_string(
      recovery_secret_out_file, "",
      "Path to the file where to store the Cryptohome Recovery secret.");
  brillo::FlagHelper::Init(argc, argv,
                           "cryptohome-test-tool - Test tool for cryptohome.");

  bool success = false;
  if (FLAGS_action.empty()) {
    LOG(ERROR) << "--action is required.";
  } else if (FLAGS_action == "recovery_crypto_create") {
    if (CheckMandatoryFlag("destination_share_out_file",
                           FLAGS_destination_share_out_file) &&
        CheckMandatoryFlag("mediator_share_out_file",
                           FLAGS_mediator_share_out_file) &&
        CheckMandatoryFlag("publisher_pub_key_out_file",
                           FLAGS_publisher_pub_key_out_file) &&
        CheckMandatoryFlag("recovery_secret_out_file",
                           FLAGS_recovery_secret_out_file)) {
      success = DoRecoveryCryptoCreateAction(
          FilePath(FLAGS_destination_share_out_file),
          FilePath(FLAGS_mediator_share_out_file),
          FilePath(FLAGS_publisher_pub_key_out_file),
          FilePath(FLAGS_recovery_secret_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_mediate") {
    if (CheckMandatoryFlag("mediator_share_in_file",
                           FLAGS_mediator_share_in_file) &&
        CheckMandatoryFlag("publisher_pub_key_in_file",
                           FLAGS_publisher_pub_key_in_file) &&
        CheckMandatoryFlag("mediated_publisher_pub_key_out_file",
                           FLAGS_mediated_publisher_pub_key_out_file)) {
      success = DoRecoveryCryptoMediateAction(
          FilePath(FLAGS_mediator_share_in_file),
          FilePath(FLAGS_publisher_pub_key_in_file),
          FilePath(FLAGS_mediated_publisher_pub_key_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_decrypt") {
    if (CheckMandatoryFlag("destination_share_in_file",
                           FLAGS_destination_share_in_file) &&
        CheckMandatoryFlag("publisher_pub_key_in_file",
                           FLAGS_publisher_pub_key_in_file) &&
        CheckMandatoryFlag("mediated_publisher_pub_key_in_file",
                           FLAGS_mediated_publisher_pub_key_in_file) &&
        CheckMandatoryFlag("recovery_secret_out_file",
                           FLAGS_recovery_secret_out_file)) {
      success = DoRecoveryCryptoDecryptAction(
          FilePath(FLAGS_destination_share_in_file),
          FilePath(FLAGS_publisher_pub_key_in_file),
          FilePath(FLAGS_mediated_publisher_pub_key_in_file),
          FilePath(FLAGS_recovery_secret_out_file));
    }
  } else {
    LOG(ERROR) << "Unknown --action.";
  }
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
