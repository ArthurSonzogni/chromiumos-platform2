// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include <absl/strings/numbers.h>
#include <base/at_exit.h>
#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/factory/factory.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "cryptohome/auth_factor/types/password.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/features.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/platform.h"
#include "cryptohome/username.h"

using base::FilePath;
using brillo::SecureBlob;
using cryptohome::cryptorecovery::CryptoRecoveryEpochResponse;
using cryptohome::cryptorecovery::CryptoRecoveryRpcRequest;
using cryptohome::cryptorecovery::CryptoRecoveryRpcResponse;
using cryptohome::cryptorecovery::DecryptResponsePayloadRequest;
using cryptohome::cryptorecovery::FakeRecoveryMediatorCrypto;
using cryptohome::cryptorecovery::HsmAssociatedData;
using cryptohome::cryptorecovery::HsmPayload;
using cryptohome::cryptorecovery::HsmResponsePlainText;
using cryptohome::cryptorecovery::LedgerInfo;
using cryptohome::cryptorecovery::OnboardingMetadata;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;
using cryptohome::cryptorecovery::RecoveryRequest;
using cryptohome::cryptorecovery::RequestMetadata;
using cryptohome::cryptorecovery::ResponsePayload;

namespace cryptohome {
namespace {

const ObfuscatedUsername& GetTestObfuscatedUsername() {
  base::NoDestructor<ObfuscatedUsername> kValue("OBFUSCATED_USERNAME");
  return *kValue;
}

constexpr char kFakeGaiaId[] = "123456789012345678901";
constexpr char kFakeUserDeviceId[] = "fake_user_device_id";
constexpr char kLedgerInfoNameKey[] = "name";
constexpr char kLedgerInfoKeyHashKey[] = "key_hash";
constexpr char kLedgerInfoPubKeyKey[] = "public_key";

bool GenerateOnboardingMetadata(const FilePath& file_path,
                                RecoveryCryptoImpl* recovery_crypto,
                                OnboardingMetadata* onboarding_metadata) {
  if (!recovery_crypto) {
    return false;
  }
  std::string recovery_id =
      recovery_crypto->LoadStoredRecoveryIdFromFile(file_path);
  if (recovery_id.empty()) {
    return false;
  }
  recovery_crypto->GenerateOnboardingMetadata(kFakeGaiaId, kFakeUserDeviceId,
                                              recovery_id, onboarding_metadata);
  return true;
}

// Note: This function is not thread safe.
const hwsec::RecoveryCryptoFrontend* GetRecoveryCryptoFrontend() {
  static std::unique_ptr<hwsec::Factory> hwsec_factory;
  static std::unique_ptr<const hwsec::RecoveryCryptoFrontend> recovery_crypto;
  if (!hwsec_factory) {
    hwsec_factory = std::make_unique<hwsec::FactoryImpl>();
    recovery_crypto = hwsec_factory->GetRecoveryCryptoFrontend();
  }
  return recovery_crypto.get();
}

std::optional<LedgerInfo> LoadLedgerInfoFromJsonFile(
    const FilePath& file_path) {
  std::string contents_string;
  if (!base::ReadFileToString(file_path, &contents_string)) {
    LOG(ERROR) << "Failed to read from file " << file_path.value() << ".";
    return std::nullopt;
  }
  if (contents_string.empty()) {
    LOG(ERROR) << "File is empty: " << file_path.value() << ".";
    return std::nullopt;
  }
  auto result = base::JSONReader::Read(contents_string);
  if (!result || !result->is_dict()) {
    LOG(ERROR) << "Could not read JSON content: " << contents_string;
    return std::nullopt;
  }
  auto name = result->GetDict().FindString(kLedgerInfoNameKey);
  auto key_hash = result->GetDict().FindString(kLedgerInfoKeyHashKey);
  auto public_key = result->GetDict().FindString(kLedgerInfoPubKeyKey);
  if (!name || !key_hash || !public_key) {
    LOG(ERROR) << "Could not read JSON fields: " << contents_string;
    return std::nullopt;
  }
  uint32_t key_hash_val;
  if (!absl::SimpleAtoi(*key_hash, &key_hash_val)) {
    LOG(ERROR) << "key_hash is not a number: " << key_hash;
    return std::nullopt;
  }
  return LedgerInfo{.name = *name,
                    .key_hash = key_hash_val,
                    .public_key = SecureBlob(*public_key)};
}

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
  if (contents_string.empty()) {
    // The content of the file is empty. Return with empty SecureBlob.
    contents->clear();
    return true;
  }
  if (!SecureBlob::HexStringToSecureBlob(contents_string, contents)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob from file "
               << file_path.value() << ".";
    return false;
  }
  return true;
}

bool WriteHexFileLogged(const FilePath& file_path, const SecureBlob& contents) {
  if (base::WriteFile(file_path, hwsec_foundation::SecureBlobToHex(contents)))
    return true;
  LOG(ERROR) << "Failed to write to file " << file_path.value() << ".";
  return false;
}

user_data_auth::AuthFactorType GetAuthFactorTypeFromString(
    const std::string& raw_auth_factor_type) {
  if (raw_auth_factor_type == "password")
    return user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_PASSWORD;
  else if (raw_auth_factor_type == "pin")
    return user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_PIN;
  else if (raw_auth_factor_type == "kiosk")
    return user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_KIOSK;
  else if (raw_auth_factor_type == "smart_card")
    return user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_SMART_CARD;
  else
    return user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_UNSPECIFIED;
}

bool DoRecoveryCryptoCreateHsmPayloadAction(
    const FilePath& mediator_pub_key_in_file_path,
    const FilePath& rsa_priv_key_out_file_path,
    const FilePath& destination_share_out_file_path,
    const FilePath& extended_pcr_bound_destination_share_out_file_path,
    const FilePath& channel_pub_key_out_file_path,
    const FilePath& channel_priv_key_out_file_path,
    const FilePath& serialized_hsm_payload_out_file_path,
    const FilePath& recovery_secret_out_file_path,
    const FilePath& recovery_id_file_path,
    cryptohome::Platform* platform) {
  std::unique_ptr<RecoveryCryptoImpl> recovery_crypto =
      RecoveryCryptoImpl::Create(GetRecoveryCryptoFrontend(), platform);
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }
  SecureBlob mediator_pub_key;
  if (mediator_pub_key_in_file_path.empty()) {
    CHECK(FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
        &mediator_pub_key));
  } else if (!ReadHexFileToSecureBlobLogged(mediator_pub_key_in_file_path,
                                            &mediator_pub_key)) {
    return false;
  }

  // Generates a new recovery_id to be persisted on a chromebook.
  if (!recovery_crypto->GenerateRecoveryIdToFile(recovery_id_file_path)) {
    LOG(ERROR) << "Failed to generate a new recovery_id.";
    return false;
  }
  // Generates HSM payload that would be persisted on a chromebook.
  OnboardingMetadata onboarding_metadata;
  if (!GenerateOnboardingMetadata(recovery_id_file_path, recovery_crypto.get(),
                                  &onboarding_metadata)) {
    LOG(ERROR) << "Unable to generate OnboardingMetadata.";
    return false;
  }
  cryptorecovery::GenerateHsmPayloadRequest generate_hsm_payload_request(
      {.mediator_pub_key = mediator_pub_key,
       .onboarding_metadata = onboarding_metadata,
       .obfuscated_username = GetTestObfuscatedUsername()});
  cryptorecovery::GenerateHsmPayloadResponse generate_hsm_payload_response;
  if (!recovery_crypto->GenerateHsmPayload(generate_hsm_payload_request,
                                           &generate_hsm_payload_response)) {
    return false;
  }

  SecureBlob serialized_hsm_payload;
  if (!SerializeHsmPayloadToCbor(generate_hsm_payload_response.hsm_payload,
                                 &serialized_hsm_payload)) {
    LOG(ERROR) << "Failed to serialize HSM payload.";
    return false;
  }

  return WriteHexFileLogged(
             rsa_priv_key_out_file_path,
             generate_hsm_payload_response.encrypted_rsa_priv_key) &&
         WriteHexFileLogged(
             destination_share_out_file_path,
             generate_hsm_payload_response.encrypted_destination_share) &&
         WriteHexFileLogged(extended_pcr_bound_destination_share_out_file_path,
                            generate_hsm_payload_response
                                .extended_pcr_bound_destination_share) &&
         WriteHexFileLogged(channel_pub_key_out_file_path,
                            generate_hsm_payload_response.channel_pub_key) &&
         WriteHexFileLogged(
             channel_priv_key_out_file_path,
             generate_hsm_payload_response.encrypted_channel_priv_key) &&
         WriteHexFileLogged(serialized_hsm_payload_out_file_path,
                            serialized_hsm_payload) &&
         WriteHexFileLogged(recovery_secret_out_file_path,
                            generate_hsm_payload_response.recovery_key);
}

bool DoRecoveryCryptoCreateRecoveryRequestAction(
    const FilePath& gaia_rapt_in_file_path,
    const FilePath& epoch_response_in_file_path,
    const FilePath& rsa_priv_key_in_file_path,
    const FilePath& channel_pub_key_in_file_path,
    const FilePath& channel_priv_key_in_file_path,
    const FilePath& serialized_hsm_payload_in_file_path,
    const FilePath& ephemeral_pub_key_out_file_path,
    const FilePath& recovery_request_out_file_path,
    Platform* platform) {
  SecureBlob rsa_priv_key;
  SecureBlob channel_pub_key;
  SecureBlob channel_priv_key;
  SecureBlob serialized_hsm_payload;
  if (!ReadHexFileToSecureBlobLogged(rsa_priv_key_in_file_path,
                                     &rsa_priv_key) ||
      !ReadHexFileToSecureBlobLogged(channel_pub_key_in_file_path,
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
      RecoveryCryptoImpl::Create(GetRecoveryCryptoFrontend(), platform);
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }

  CryptoRecoveryEpochResponse epoch_response;
  if (epoch_response_in_file_path.empty()) {
    CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response));
  } else {
    SecureBlob epoch_response_bytes;
    if (!ReadHexFileToSecureBlobLogged(epoch_response_in_file_path,
                                       &epoch_response_bytes)) {
      return false;
    }
    if (!epoch_response.ParseFromString(epoch_response_bytes.to_string())) {
      LOG(ERROR) << "Failed to parse epoch response.";
      return false;
    }
  }

  RequestMetadata request_metadata;
  if (!gaia_rapt_in_file_path.empty()) {
    SecureBlob gaia_rapt;
    if (!ReadHexFileToSecureBlobLogged(gaia_rapt_in_file_path, &gaia_rapt)) {
      return false;
    }
    request_metadata.auth_claim.gaia_reauth_proof_token = gaia_rapt.to_string();
  }
  cryptorecovery::GenerateRecoveryRequestRequest
      generate_recovery_request_input_param(
          {.hsm_payload = hsm_payload,
           .request_meta_data = request_metadata,
           .epoch_response = epoch_response,
           .encrypted_rsa_priv_key = rsa_priv_key,
           .encrypted_channel_priv_key = channel_priv_key,
           .channel_pub_key = channel_pub_key,
           .obfuscated_username = GetTestObfuscatedUsername()});
  brillo::SecureBlob ephemeral_pub_key;
  CryptoRecoveryRpcRequest recovery_request;
  if (!recovery_crypto->GenerateRecoveryRequest(
          generate_recovery_request_input_param, &recovery_request,
          &ephemeral_pub_key)) {
    return false;
  }

  return WriteHexFileLogged(ephemeral_pub_key_out_file_path,
                            ephemeral_pub_key) &&
         WriteHexFileLogged(
             recovery_request_out_file_path,
             brillo::SecureBlob(recovery_request.SerializeAsString()));
}

bool DoRecoveryCryptoMediateAction(
    const FilePath& recovery_request_in_file_path,
    const FilePath& recovery_response_out_file_path) {
  SecureBlob serialized_recovery_request;
  if (!ReadHexFileToSecureBlobLogged(recovery_request_in_file_path,
                                     &serialized_recovery_request)) {
    return false;
  }
  CryptoRecoveryRpcRequest recovery_request;
  if (!recovery_request.ParseFromString(
          serialized_recovery_request.to_string())) {
    LOG(ERROR) << "Failed to parse CryptoRecoveryRpcRequest.";
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

  CryptoRecoveryRpcResponse response_proto;
  if (!fake_mediator->MediateRequestPayload(epoch_pub_key, epoch_priv_key,
                                            mediator_priv_key, recovery_request,
                                            &response_proto)) {
    return false;
  }

  return WriteHexFileLogged(
      recovery_response_out_file_path,
      brillo::SecureBlob(response_proto.SerializeAsString()));
}

bool DoRecoveryCryptoDecryptAction(
    const FilePath& recovery_response_in_file_path,
    const FilePath& epoch_response_in_file_path,
    const FilePath& channel_priv_key_in_file_path,
    const FilePath& ephemeral_pub_key_in_file_path,
    const FilePath& destination_share_in_file_path,
    const FilePath& extended_pcr_bound_destination_share_in_file_path,
    const FilePath& ledger_info_in_file_path,
    const FilePath& serialized_hsm_payload_in_file_path,
    const FilePath& recovery_secret_out_file_path,
    Platform* platform) {
  SecureBlob recovery_response, ephemeral_pub_key, channel_priv_key,
      destination_share, extended_pcr_bound_destination_share,
      serialized_hsm_payload;
  if (!ReadHexFileToSecureBlobLogged(recovery_response_in_file_path,
                                     &recovery_response) ||
      !ReadHexFileToSecureBlobLogged(channel_priv_key_in_file_path,
                                     &channel_priv_key) ||
      !ReadHexFileToSecureBlobLogged(ephemeral_pub_key_in_file_path,
                                     &ephemeral_pub_key) ||
      !ReadHexFileToSecureBlobLogged(destination_share_in_file_path,
                                     &destination_share) ||
      !ReadHexFileToSecureBlobLogged(serialized_hsm_payload_in_file_path,
                                     &serialized_hsm_payload) ||
      !ReadHexFileToSecureBlobLogged(
          extended_pcr_bound_destination_share_in_file_path,
          &extended_pcr_bound_destination_share)) {
    return false;
  }

  CryptoRecoveryRpcResponse recovery_response_proto;
  if (!recovery_response_proto.ParseFromString(recovery_response.to_string())) {
    LOG(ERROR) << "Failed to parse CryptoRecoveryRpcResponse.";
    return false;
  }

  CryptoRecoveryEpochResponse epoch_response;
  if (epoch_response_in_file_path.empty()) {
    CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response));
  } else {
    SecureBlob epoch_response_bytes;
    if (!ReadHexFileToSecureBlobLogged(epoch_response_in_file_path,
                                       &epoch_response_bytes)) {
      return false;
    }
    if (!epoch_response.ParseFromString(epoch_response_bytes.to_string())) {
      LOG(ERROR) << "Failed to parse epoch response.";
      return false;
    }
  }

  HsmPayload hsm_payload;
  if (!DeserializeHsmPayloadFromCbor(serialized_hsm_payload, &hsm_payload)) {
    LOG(ERROR) << "Failed to deserialize HSM payload.";
    return false;
  }

  HsmAssociatedData hsm_associated_data;
  if (!DeserializeHsmAssociatedDataFromCbor(hsm_payload.associated_data,
                                            &hsm_associated_data)) {
    LOG(ERROR) << "Failed to deserialize HSM payload AD.";
    return false;
  }

  std::unique_ptr<RecoveryCryptoImpl> recovery_crypto =
      RecoveryCryptoImpl::Create(GetRecoveryCryptoFrontend(), platform);
  if (!recovery_crypto) {
    LOG(ERROR) << "Failed to create recovery crypto object.";
    return false;
  }

  LedgerInfo ledger_info = FakeRecoveryMediatorCrypto::GetFakeLedgerInfo();
  if (!ledger_info_in_file_path.empty()) {
    auto parsed_info = LoadLedgerInfoFromJsonFile(ledger_info_in_file_path);
    if (!parsed_info) {
      LOG(ERROR) << "Failed to read ledger info.";
      return false;
    }
    ledger_info = parsed_info.value();
  }

  HsmResponsePlainText response_plain_text;
  CryptoStatus decrypt_result = recovery_crypto->DecryptResponsePayload(
      DecryptResponsePayloadRequest(
          {.encrypted_channel_priv_key = channel_priv_key,
           .epoch_response = epoch_response,
           .recovery_response_proto = recovery_response_proto,
           .obfuscated_username = GetTestObfuscatedUsername(),
           .ledger_info = ledger_info,
           .hsm_associated_data = hsm_associated_data}),
      &response_plain_text);
  if (!decrypt_result.ok()) {
    LOG(ERROR) << "Failed to decrypt response payload "
               << decrypt_result.ToFullString();
    return false;
  }
  brillo::SecureBlob mediated_recovery_key;
  if (!recovery_crypto->RecoverDestination(
          cryptorecovery::RecoverDestinationRequest(
              {.dealer_pub_key = response_plain_text.dealer_pub_key,
               .key_auth_value = response_plain_text.key_auth_value,
               .encrypted_destination_share = destination_share,
               .extended_pcr_bound_destination_share =
                   extended_pcr_bound_destination_share,
               .ephemeral_pub_key = ephemeral_pub_key,
               .mediated_publisher_pub_key = response_plain_text.mediated_point,
               .obfuscated_username = GetTestObfuscatedUsername()}),
          &mediated_recovery_key)) {
    return false;
  }

  return WriteHexFileLogged(recovery_secret_out_file_path,
                            mediated_recovery_key);
}

bool DoRecoveryCryptoGetFakeEpochAction(
    const FilePath& epoch_response_out_file_path) {
  CryptoRecoveryEpochResponse epoch_response;
  CHECK(FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response));
  return WriteHexFileLogged(
      epoch_response_out_file_path,
      brillo::SecureBlob(epoch_response.SerializeAsString()));
}

bool DoRecoveryCryptoGetFakeMediatorPublicKeyAction(
    const FilePath& mediator_pub_key_out_file_path) {
  SecureBlob mediator_pub_key;
  CHECK(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));
  return WriteHexFileLogged(mediator_pub_key_out_file_path, mediator_pub_key);
}

bool DoRecoveryCryptoGetLedgerInfoAction(
    const FilePath& ledger_info_out_file_path) {
  LedgerInfo ledger_info = FakeRecoveryMediatorCrypto::GetFakeLedgerInfo();
  auto dict =
      base::Value::Dict()
          .Set(kLedgerInfoNameKey, ledger_info.name)
          // Convert the key hash to string, for easier parsing by the test,
          // since it doesn't need to read/change the content.
          .Set(kLedgerInfoKeyHashKey, std::to_string(*ledger_info.key_hash))
          .Set(kLedgerInfoPubKeyKey, ledger_info.public_key->to_string());
  std::string ledger_info_json;
  if (!base::JSONWriter::Write(dict, &ledger_info_json)) {
    LOG(ERROR) << "Failed to write to json.";
    return false;
  }
  return base::WriteFile(ledger_info_out_file_path, ledger_info_json);
}

bool DoCreateVaultKeyset(const std::string& auth_session_id_hex,
                         const std::string& key_data_label,
                         const std::string& raw_auth_factor_type,
                         const std::string& raw_password,
                         const std::string& key_delegate_dbus_service_name,
                         const std::string& public_key_spki_der,
                         bool disable_key_data,
                         bool use_public_mount_salt,
                         Platform* platform) {
  brillo::DBusConnection connection;
  scoped_refptr<dbus::Bus> bus = connection.Connect();
  CHECK(bus) << "Failed to connect to system bus through libbrillo";

  auto cryptohome_proxy = org::chromium::UserDataAuthInterfaceProxy(bus);

  // Format auth_session_id to match cryptohome.cc command line
  std::string auth_session_id;
  base::HexStringToString(auth_session_id_hex.c_str(), &auth_session_id);

  // Determine and enumerate AuthFactorType from raw input.
  user_data_auth::AuthFactorType auth_factor_type =
      GetAuthFactorTypeFromString(raw_auth_factor_type);

  user_data_auth::CreateVaultKeysetRequest request;
  request.set_auth_session_id(auth_session_id);
  request.set_key_label(key_data_label);
  request.set_type(auth_factor_type);
  request.set_disable_key_data(disable_key_data);

  // Fill challenge credential parameters depending on AuthFactorType.
  if (auth_factor_type ==
      user_data_auth::AuthFactorType::AUTH_FACTOR_TYPE_SMART_CARD) {
    if (!key_delegate_dbus_service_name.empty())
      request.set_key_delegate_dbus_service_name(
          key_delegate_dbus_service_name);

    if (!public_key_spki_der.empty())
      request.set_public_key_spki_der(public_key_spki_der);
  } else {
    // Anything factor that isn't a challenge credntial has a passkey.
    // Format passkey to match cryptohome.cc command line.
    std::string trimmed_password;
    base::TrimString(raw_password, "/r/n", &trimmed_password);
    brillo::SecureBlob passkey;
    brillo::SecureBlob salt;
    if (use_public_mount_salt) {
      GetPublicMountSalt(platform, &salt);
    } else {
      GetSystemSalt(platform, &salt);
    }
    Crypto::PasswordToPasskey(trimmed_password.c_str(), salt, &passkey);
    request.set_passkey(passkey.to_string());
  }

  user_data_auth::CreateVaultKeysetReply reply;
  brillo::ErrorPtr error;

  if (!cryptohome_proxy.CreateVaultKeyset(
          request, &reply, &error,
          user_data_auth::kUserDataAuthServiceTimeoutInMs) ||
      error) {
    LOG(ERROR)
        << "Failed to create or persist the VaultKeyset from cryptohomed: "
        << error->GetMessage();
    return false;
  }

  if (reply.error() !=
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "CreateVaultKeyset failed: " << reply.error();
    return false;
  }

  return true;
}

}  // namespace

}  // namespace cryptohome

int main(int argc, char* argv[]) {
  using cryptohome::CheckMandatoryFlag;
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  base::AtExitManager exit_manager;
  cryptohome::Platform platform;

  DEFINE_string(
      action, "",
      "One of: recovery_crypto_create_hsm_payload, "
      "recovery_crypto_create_recovery_request, recovery_crypto_mediate, "
      "recovery_crypto_decrypt, create_vault_keyset.");
  DEFINE_string(
      mediator_pub_key_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "mediator key.");
  DEFINE_string(
      rsa_priv_key_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "encrypted rsa private key.");
  DEFINE_string(
      rsa_priv_key_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "encrypted rsa private key.");
  DEFINE_string(
      destination_share_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "encrypted destination share.");
  DEFINE_string(
      extended_pcr_bound_destination_share_out_file, "",
      "Path to the file where to store the hex-encoded Cryptohome Recovery "
      "extended pcr bound destination share.");
  DEFINE_string(destination_share_in_file, "",
                "Path to the file containing the hex-encoded Cryptohome "
                "Recovery encrypted "
                "destination share.");
  DEFINE_string(extended_pcr_bound_destination_share_in_file, "",
                "Path to the file containing the hex-encoded Cryptohome "
                "Recovery encrypted "
                "extended pcr bound destination share.");
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
  DEFINE_string(
      epoch_response_in_file, "",
      "Path to the file containing the hex-encoded Cryptohome Recovery "
      "epoch response proto.");
  DEFINE_string(ledger_info_in_file, "",
                "Path to the file containing the ledger info in JSON format.");
  DEFINE_string(
      ledger_info_out_file, "",
      "Path to the file where to store the ledger info in JSON format.");
  DEFINE_string(
      gaia_rapt_in_file, "",
      "Path to the file containing the hex-encoded Gaia RAPT to be added to "
      "RequestMetaData.");
  DEFINE_string(
      epoch_response_out_file, "",
      "Path to the file containing the hex-encoded fake epoch response.");
  DEFINE_string(
      mediator_pub_key_out_file, "",
      "Path to the file containing the hex-encoded fake mediator pub key.");
  DEFINE_string(
      recovery_id_file, "",
      "Path to the file containing serialized data to generate recovery_id.");
  DEFINE_string(auth_session_id, "",
                "AuthSessionID, which must be authenticated, used to generate "
                "a custom vault keyset.");
  DEFINE_string(key_data_label, "",
                "Requested key data label to generate a custom vault keyset.");
  DEFINE_string(auth_factor_type, "",
                "Requested type of factor for authentication to generate "
                "the custom vault keyset.");
  DEFINE_string(passkey, "",
                "User passkey input, or password used to generate a custom "
                "vault keyset.");
  DEFINE_string(
      key_delegate_name, "",
      "For connecting and making requests to a key delegate service: "
      "|dbus_service_name|, the D-Bus service name of the key delegate "
      "service that exports the key delegate object.");
  DEFINE_string(challenge_spki, "",
                "Specifies the key which is asked to sign the data. Contains "
                "the DER-encoded blob of the X.509 Subject Public Key Info.");
  DEFINE_bool(disable_key_data, false,
              "Boolean to enable or disable adding a KeyData object to "
              "the VaultKeyset during creation.");
  DEFINE_bool(use_public_mount_salt, false,
              "When set, use the public mount salt for creating passkeys in a "
              "custom vault keyset. Otherwise the system salt will be used");
  brillo::FlagHelper::Init(argc, argv,
                           "cryptohome-test-tool - Test tool for cryptohome.");

  bool success = false;
  if (FLAGS_action.empty()) {
    LOG(ERROR) << "--action is required.";
  } else if (FLAGS_action == "create_vault_keyset") {
    if (CheckMandatoryFlag("auth_session_id", FLAGS_auth_session_id)) {
      success = cryptohome::DoCreateVaultKeyset(
          FLAGS_auth_session_id, FLAGS_key_data_label, FLAGS_auth_factor_type,
          FLAGS_passkey, FLAGS_key_delegate_name, FLAGS_challenge_spki,
          FLAGS_disable_key_data, FLAGS_use_public_mount_salt, &platform);
    }
  } else if (FLAGS_action == "recovery_crypto_create_hsm_payload") {
    if (CheckMandatoryFlag("rsa_priv_key_out_file",
                           FLAGS_rsa_priv_key_out_file) &&
        CheckMandatoryFlag("destination_share_out_file",
                           FLAGS_destination_share_out_file) &&
        CheckMandatoryFlag(
            "extended_pcr_bound_destination_share_out_file",
            FLAGS_extended_pcr_bound_destination_share_out_file) &&
        CheckMandatoryFlag("channel_pub_key_out_file",
                           FLAGS_channel_pub_key_out_file) &&
        CheckMandatoryFlag("channel_priv_key_out_file",
                           FLAGS_channel_priv_key_out_file) &&
        CheckMandatoryFlag("serialized_hsm_payload_out_file",
                           FLAGS_serialized_hsm_payload_out_file) &&
        CheckMandatoryFlag("recovery_secret_out_file",
                           FLAGS_recovery_secret_out_file) &&
        CheckMandatoryFlag("recovery_id_file", FLAGS_recovery_id_file)) {
      success = cryptohome::DoRecoveryCryptoCreateHsmPayloadAction(
          FilePath(FLAGS_mediator_pub_key_in_file),
          FilePath(FLAGS_rsa_priv_key_out_file),
          FilePath(FLAGS_destination_share_out_file),
          FilePath(FLAGS_extended_pcr_bound_destination_share_out_file),
          FilePath(FLAGS_channel_pub_key_out_file),
          FilePath(FLAGS_channel_priv_key_out_file),
          FilePath(FLAGS_serialized_hsm_payload_out_file),
          FilePath(FLAGS_recovery_secret_out_file),
          FilePath(FLAGS_recovery_id_file), &platform);
    }
  } else if (FLAGS_action == "recovery_crypto_create_recovery_request") {
    if (CheckMandatoryFlag("rsa_priv_key_in_file",
                           FLAGS_rsa_priv_key_in_file) &&
        CheckMandatoryFlag("channel_pub_key_in_file",
                           FLAGS_channel_pub_key_in_file) &&
        CheckMandatoryFlag("channel_priv_key_in_file",
                           FLAGS_channel_priv_key_in_file) &&
        CheckMandatoryFlag("serialized_hsm_payload_in_file",
                           FLAGS_serialized_hsm_payload_in_file) &&
        CheckMandatoryFlag("ephemeral_pub_key_out_file",
                           FLAGS_ephemeral_pub_key_out_file) &&
        CheckMandatoryFlag("recovery_request_out_file",
                           FLAGS_recovery_request_out_file)) {
      success = cryptohome::DoRecoveryCryptoCreateRecoveryRequestAction(
          FilePath(FLAGS_gaia_rapt_in_file),
          FilePath(FLAGS_epoch_response_in_file),
          FilePath(FLAGS_rsa_priv_key_in_file),
          FilePath(FLAGS_channel_pub_key_in_file),
          FilePath(FLAGS_channel_priv_key_in_file),
          FilePath(FLAGS_serialized_hsm_payload_in_file),
          FilePath(FLAGS_ephemeral_pub_key_out_file),
          FilePath(FLAGS_recovery_request_out_file), &platform);
    }
  } else if (FLAGS_action == "recovery_crypto_mediate") {
    if (CheckMandatoryFlag("recovery_request_in_file",
                           FLAGS_recovery_request_in_file) &&
        CheckMandatoryFlag("recovery_response_out_file",
                           FLAGS_recovery_response_out_file)) {
      success = cryptohome::DoRecoveryCryptoMediateAction(
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
        CheckMandatoryFlag(
            "extended_pcr_bound_destination_share_in_file",
            FLAGS_extended_pcr_bound_destination_share_in_file) &&
        CheckMandatoryFlag("serialized_hsm_payload_in_file",
                           FLAGS_serialized_hsm_payload_in_file) &&
        CheckMandatoryFlag("recovery_secret_out_file",
                           FLAGS_recovery_secret_out_file)) {
      success = cryptohome::DoRecoveryCryptoDecryptAction(
          FilePath(FLAGS_recovery_response_in_file),
          FilePath(FLAGS_epoch_response_in_file),
          FilePath(FLAGS_channel_priv_key_in_file),
          FilePath(FLAGS_ephemeral_pub_key_in_file),
          FilePath(FLAGS_destination_share_in_file),
          FilePath(FLAGS_extended_pcr_bound_destination_share_in_file),
          FilePath(FLAGS_ledger_info_in_file),
          FilePath(FLAGS_serialized_hsm_payload_in_file),
          FilePath(FLAGS_recovery_secret_out_file), &platform);
    }
  } else if (FLAGS_action == "recovery_crypto_get_fake_epoch") {
    if (CheckMandatoryFlag("epoch_response_out_file",
                           FLAGS_epoch_response_out_file)) {
      success = cryptohome::DoRecoveryCryptoGetFakeEpochAction(
          FilePath(FLAGS_epoch_response_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_get_fake_mediator_pub_key") {
    if (CheckMandatoryFlag("mediator_pub_key_out_file",
                           FLAGS_mediator_pub_key_out_file)) {
      success = cryptohome::DoRecoveryCryptoGetFakeMediatorPublicKeyAction(
          FilePath(FLAGS_mediator_pub_key_out_file));
    }
  } else if (FLAGS_action == "recovery_crypto_get_fake_ledger_info") {
    if (CheckMandatoryFlag("ledger_info_out_file",
                           FLAGS_ledger_info_out_file)) {
      success = cryptohome::DoRecoveryCryptoGetLedgerInfoAction(
          FilePath(FLAGS_ledger_info_out_file));
    }
  } else {
    LOG(ERROR) << "Unknown --action.";
  }
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
