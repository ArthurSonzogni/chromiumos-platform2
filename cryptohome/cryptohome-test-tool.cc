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

#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

using base::FilePath;
using brillo::SecureBlob;
using cryptohome::cryptorecovery::FakeRecoveryMediatorCrypto;
using cryptohome::cryptorecovery::HsmPayload;
using cryptohome::cryptorecovery::HsmResponsePlainText;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;
using cryptohome::cryptorecovery::RecoveryRequest;
using cryptohome::cryptorecovery::RecoveryResponse;

namespace {

bool CheckMandatoryFlag(const std::string& flag_name,
                        const std::string& flag_value) {
  if (!flag_value.empty())
    return true;
  LOG(ERROR) << "--" << flag_name << " is mandatory.";
  return false;
}

bool ReadHexFileToSecureBlobLogged(const FilePath& file_path,
                                   SecureBlob* contents) {
  std::string contents_string;
  if (!base::ReadFileToString(file_path, &contents_string)) {
    LOG(ERROR) << "Failed to read from file " << file_path.value() << ".";
    return false;
  }
  if (!SecureBlob::HexStringToSecureBlob(contents_string, contents)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob.";
    return false;
  }
  return true;
}

bool WriteHexFileLogged(const FilePath& file_path, const SecureBlob& contents) {
  if (base::WriteFile(file_path, cryptohome::SecureBlobToHex(contents)))
    return true;
  LOG(ERROR) << "Failed to write to file " << file_path.value() << ".";
  return false;
}

bool DoRecoveryCryptoCreateHsmPayloadAction(
    const FilePath& destination_share_out_file_path,
    const FilePath& channel_pub_key_out_file_path,
    const FilePath& channel_priv_key_out_file_path,
    const FilePath& serialized_hsm_payload_out_file_path,
    const FilePath& recovery_secret_out_file_path) {
  std::unique_ptr<RecoveryCryptoImpl> recovery_crypto =
      RecoveryCryptoImpl::Create();
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }

  SecureBlob mediator_pub_key;
  CHECK(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));

  // Generates HSM payload that would be persisted on a chromebook.
  HsmPayload hsm_payload;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob recovery_key;
  brillo::SecureBlob channel_pub_key;
  brillo::SecureBlob channel_priv_key;
  if (!recovery_crypto->GenerateHsmPayload(
          mediator_pub_key,
          /*rsa_pub_key=*/brillo::SecureBlob(),
          brillo::SecureBlob("Fake Enrollment Meta Data"), &hsm_payload,
          &destination_share, &recovery_key, &channel_pub_key,
          &channel_priv_key)) {
    return false;
  }

  SecureBlob serialized_hsm_payload;
  if (!SerializeHsmPayloadToCbor(hsm_payload, &serialized_hsm_payload)) {
    LOG(ERROR) << "Failed to serialize HSM payload.";
    return false;
  }

  return WriteHexFileLogged(destination_share_out_file_path,
                            destination_share) &&
         WriteHexFileLogged(channel_pub_key_out_file_path, channel_pub_key) &&
         WriteHexFileLogged(channel_priv_key_out_file_path, channel_priv_key) &&
         WriteHexFileLogged(serialized_hsm_payload_out_file_path,
                            serialized_hsm_payload) &&
         WriteHexFileLogged(recovery_secret_out_file_path, recovery_key);
}

bool DoRecoveryCryptoCreateRecoveryRequestAction(
    const FilePath& channel_pub_key_in_file_path,
    const FilePath& channel_priv_key_in_file_path,
    const FilePath& serialized_hsm_payload_in_file_path,
    const FilePath& ephemeral_pub_key_out_file_path,
    const FilePath& recovery_request_out_file_path) {
  SecureBlob channel_pub_key;
  SecureBlob channel_priv_key;
  SecureBlob serialized_hsm_payload;
  if (!ReadHexFileToSecureBlobLogged(channel_pub_key_in_file_path,
                                     &channel_pub_key) ||
      !ReadHexFileToSecureBlobLogged(channel_priv_key_in_file_path,
                                     &channel_priv_key) ||
      !ReadHexFileToSecureBlobLogged(serialized_hsm_payload_in_file_path,
                                     &serialized_hsm_payload)) {
    return false;
  }

  HsmPayload hsm_payload;
  if (!DeserializeHsmPayloadFromCbor(serialized_hsm_payload, &hsm_payload)) {
    LOG(ERROR) << "Failed to deserialize HSM payload.";
    return false;
  }

  std::unique_ptr<RecoveryCryptoImpl> recovery_crypto =
      RecoveryCryptoImpl::Create();
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }

  SecureBlob epoch_pub_key;
  CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key));

  brillo::SecureBlob ephemeral_pub_key;
  brillo::SecureBlob recovery_request_cbor;
  if (!recovery_crypto->GenerateRecoveryRequest(
          hsm_payload, brillo::SecureBlob("Fake Request Meta Data"),
          channel_priv_key, channel_pub_key, epoch_pub_key,
          &recovery_request_cbor, &ephemeral_pub_key)) {
    return false;
  }

  return WriteHexFileLogged(ephemeral_pub_key_out_file_path,
                            ephemeral_pub_key) &&
         WriteHexFileLogged(recovery_request_out_file_path,
                            recovery_request_cbor);
}

bool DoRecoveryCryptoMediateAction(
    const FilePath& recovery_request_in_file_path,
    const FilePath& recovery_response_out_file_path) {
  SecureBlob serialized_recovery_request;
  if (!ReadHexFileToSecureBlobLogged(recovery_request_in_file_path,
                                     &serialized_recovery_request)) {
    return false;
  }

  std::unique_ptr<FakeRecoveryMediatorCrypto> fake_mediator =
      FakeRecoveryMediatorCrypto::Create();
  if (!fake_mediator) {
    LOG(ERROR) << "Failed to create fake mediator object.";
    return false;
  }

  SecureBlob mediator_priv_key, epoch_pub_key, epoch_priv_key;
  CHECK(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
      &mediator_priv_key));
  CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key));
  CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochPrivateKey(&epoch_priv_key));

  brillo::SecureBlob response_cbor;
  if (!fake_mediator->MediateRequestPayload(
          epoch_pub_key, epoch_priv_key, mediator_priv_key,
          serialized_recovery_request, &response_cbor)) {
    return false;
  }

  return WriteHexFileLogged(recovery_response_out_file_path, response_cbor);
}

bool DoRecoveryCryptoDecryptAction(
    const FilePath& recovery_response_in_file_path,
    const FilePath& channel_priv_key_in_file_path,
    const FilePath& ephemeral_pub_key_in_file_path,
    const FilePath& destination_share_in_file_path,
    const FilePath& recovery_secret_out_file_path) {
  SecureBlob recovery_response, ephemeral_pub_key, channel_priv_key,
      destination_share;
  if (!ReadHexFileToSecureBlobLogged(recovery_response_in_file_path,
                                     &recovery_response) ||
      !ReadHexFileToSecureBlobLogged(channel_priv_key_in_file_path,
                                     &channel_priv_key) ||
      !ReadHexFileToSecureBlobLogged(ephemeral_pub_key_in_file_path,
                                     &ephemeral_pub_key) ||
      !ReadHexFileToSecureBlobLogged(destination_share_in_file_path,
                                     &destination_share)) {
    return false;
  }

  SecureBlob epoch_pub_key;
  CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key));

  std::unique_ptr<RecoveryCryptoImpl> recovery_crypto =
      RecoveryCryptoImpl::Create();
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }

  HsmResponsePlainText response_plain_text;
  if (!recovery_crypto->DecryptResponsePayload(channel_priv_key, epoch_pub_key,
                                               recovery_response,
                                               &response_plain_text)) {
    return false;
  }

  brillo::SecureBlob mediated_recovery_key;
  if (!recovery_crypto->RecoverDestination(response_plain_text.dealer_pub_key,
                                           destination_share, ephemeral_pub_key,
                                           response_plain_text.mediated_point,
                                           &mediated_recovery_key)) {
    return false;
  }

  return WriteHexFileLogged(recovery_secret_out_file_path,
                            mediated_recovery_key);
}

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr);

  DEFINE_string(
      action, "",
      "One of: recovery_crypto_create_hsm_payload, "
      "recovery_crypto_create_recovery_request, recovery_crypto_mediate, "
      "recovery_crypto_decrypt.");
  DEFINE_string(
      destination_share_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "encrypted destination share.");
  DEFINE_string(destination_share_in_file, "",
                "Path to the file containing the hex-encoded Cryptohome "
                "Recovery encrypted "
                "destination share.");
  DEFINE_string(
      channel_pub_key_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "channel public key.");
  DEFINE_string(
      channel_pub_key_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "channel public key.");
  DEFINE_string(
      channel_priv_key_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "channel private key.");
  DEFINE_string(
      channel_priv_key_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery  "
      "channel private key.");
  DEFINE_string(
      ephemeral_pub_key_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "ephemeral public key.");
  DEFINE_string(
      ephemeral_pub_key_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery  "
      "ephemeral public key.");
  DEFINE_string(
      serialized_hsm_payload_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "serialized HSM payload.");
  DEFINE_string(
      serialized_hsm_payload_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "serialized HSM payload.");
  DEFINE_string(
      recovery_request_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "Request.");
  DEFINE_string(
      recovery_request_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "Request.");
  DEFINE_string(
      recovery_response_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "Response.");
  DEFINE_string(
      recovery_response_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "Response.");
  DEFINE_string(
      recovery_secret_out_file, "",
      "Path to the file where to store the Cryptohome Recovery secret.");
  brillo::FlagHelper::Init(argc, argv,
                           "cryptohome-test-tool - Test tool for cryptohome.");

  bool success = false;
  if (FLAGS_action.empty()) {
    LOG(ERROR) << "--action is required.";
  } else if (FLAGS_action == "recovery_crypto_create_hsm_payload") {
    if (CheckMandatoryFlag("destination_share_out_file",
                           FLAGS_destination_share_out_file) &&
        CheckMandatoryFlag("channel_pub_key_out_file",
                           FLAGS_channel_pub_key_out_file) &&
        CheckMandatoryFlag("channel_priv_key_out_file",
                           FLAGS_channel_priv_key_out_file) &&
        CheckMandatoryFlag("serialized_hsm_payload_out_file",
                           FLAGS_serialized_hsm_payload_out_file) &&
        CheckMandatoryFlag("recovery_secret_out_file",
                           FLAGS_recovery_secret_out_file)) {
      success = DoRecoveryCryptoCreateHsmPayloadAction(
          FilePath(FLAGS_destination_share_out_file),
          FilePath(FLAGS_channel_pub_key_out_file),
          FilePath(FLAGS_channel_priv_key_out_file),
          FilePath(FLAGS_serialized_hsm_payload_out_file),
          FilePath(FLAGS_recovery_secret_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_create_recovery_request") {
    if (CheckMandatoryFlag("channel_pub_key_in_file",
                           FLAGS_channel_pub_key_in_file) &&
        CheckMandatoryFlag("channel_priv_key_in_file",
                           FLAGS_channel_priv_key_in_file) &&
        CheckMandatoryFlag("serialized_hsm_payload_in_file",
                           FLAGS_serialized_hsm_payload_in_file) &&
        CheckMandatoryFlag("ephemeral_pub_key_out_file",
                           FLAGS_ephemeral_pub_key_out_file) &&
        CheckMandatoryFlag("recovery_request_out_file",
                           FLAGS_recovery_request_out_file)) {
      success = DoRecoveryCryptoCreateRecoveryRequestAction(
          FilePath(FLAGS_channel_pub_key_in_file),
          FilePath(FLAGS_channel_priv_key_in_file),
          FilePath(FLAGS_serialized_hsm_payload_in_file),
          FilePath(FLAGS_ephemeral_pub_key_out_file),
          FilePath(FLAGS_recovery_request_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_mediate") {
    if (CheckMandatoryFlag("recovery_request_in_file",
                           FLAGS_recovery_request_in_file) &&
        CheckMandatoryFlag("recovery_response_out_file",
                           FLAGS_recovery_response_out_file)) {
      success = DoRecoveryCryptoMediateAction(
          FilePath(FLAGS_recovery_request_in_file),
          FilePath(FLAGS_recovery_response_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_decrypt") {
    if (CheckMandatoryFlag("recovery_response_in_file",
                           FLAGS_recovery_response_in_file) &&
        CheckMandatoryFlag("channel_priv_key_in_file",
                           FLAGS_channel_priv_key_in_file) &&
        CheckMandatoryFlag("ephemeral_pub_key_in_file",
                           FLAGS_ephemeral_pub_key_in_file) &&
        CheckMandatoryFlag("destination_share_in_file",
                           FLAGS_destination_share_in_file) &&
        CheckMandatoryFlag("recovery_secret_out_file",
                           FLAGS_recovery_secret_out_file)) {
      success = DoRecoveryCryptoDecryptAction(
          FilePath(FLAGS_recovery_response_in_file),
          FilePath(FLAGS_channel_priv_key_in_file),
          FilePath(FLAGS_ephemeral_pub_key_in_file),
          FilePath(FLAGS_destination_share_in_file),
          FilePath(FLAGS_recovery_secret_out_file));
    }
  } else {
    LOG(ERROR) << "Unknown --action.";
  }
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
