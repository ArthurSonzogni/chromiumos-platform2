// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/cryptohome/frontend_impl.h"

#include <string>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<bool> CryptohomeFrontendImpl::IsEnabled() {
  return middleware_.CallSync<&Backend::State::IsEnabled>();
}

StatusOr<bool> CryptohomeFrontendImpl::IsReady() {
  return middleware_.CallSync<&Backend::State::IsReady>();
}

StatusOr<bool> CryptohomeFrontendImpl::IsDAMitigationReady() {
  return middleware_.CallSync<&Backend::DAMitigation::IsReady>();
}

StatusOr<bool> CryptohomeFrontendImpl::IsSrkRocaVulnerable() {
  return middleware_.CallSync<&Backend::Vendor::IsSrkRocaVulnerable>();
}

Status CryptohomeFrontendImpl::MitigateDACounter() {
  return middleware_.CallSync<&Backend::DAMitigation::Mitigate>();
}

StatusOr<brillo::Blob> CryptohomeFrontendImpl::GetRsuDeviceId() {
  return middleware_.CallSync<&Backend::Vendor::GetRsuDeviceId>();
}

StatusOr<absl::flat_hash_set<KeyAlgoType>>
CryptohomeFrontendImpl::GetSupportedAlgo() {
  return middleware_.CallSync<&Backend::KeyManagement::GetSupportedAlgo>();
}

StatusOr<CryptohomeFrontend::CreateKeyResult>
CryptohomeFrontendImpl::CreateCryptohomeKey(KeyAlgoType key_algo) {
  return middleware_.CallSync<&Backend::KeyManagement::CreateAutoReloadKey>(
      OperationPolicySetting{}, key_algo,
      Backend::KeyManagement::CreateKeyOptions{
          .allow_software_gen = true,
          .allow_decrypt = true,
          .allow_sign = false,
      });
}

StatusOr<ScopedKey> CryptohomeFrontendImpl::LoadKey(
    const brillo::Blob& key_blob) {
  return middleware_.CallSync<&Backend::KeyManagement::LoadAutoReloadKey>(
      OperationPolicy{}, key_blob);
}

StatusOr<brillo::Blob> CryptohomeFrontendImpl::GetPubkeyHash(Key key) {
  return middleware_.CallSync<&Backend::KeyManagement::GetPubkeyHash>(key);
}

StatusOr<ScopedKey> CryptohomeFrontendImpl::SideLoadKey(uint32_t key_handle) {
  return middleware_.CallSync<&Backend::KeyManagement::SideLoadKey>(key_handle);
}

StatusOr<uint32_t> CryptohomeFrontendImpl::GetKeyHandle(Key key) {
  return middleware_.CallSync<&Backend::KeyManagement::GetKeyHandle>(key);
}

Status CryptohomeFrontendImpl::SetCurrentUser(const std::string& current_user) {
  return middleware_.CallSync<&Backend::Config::SetCurrentUser>(current_user);
}

StatusOr<bool> CryptohomeFrontendImpl::IsCurrentUserSet() {
  return middleware_.CallSync<&Backend::Config::IsCurrentUserSet>();
}

StatusOr<bool> CryptohomeFrontendImpl::IsSealingSupported() {
  return middleware_.CallSync<&Backend::Sealing::IsSupported>();
}

StatusOr<brillo::Blob> CryptohomeFrontendImpl::SealWithCurrentUser(
    const std::optional<std::string>& current_user,
    const brillo::SecureBlob& auth_value,
    const brillo::SecureBlob& unsealed_data) {
  if (auth_value.empty()) {
    return MakeStatus<TPMError>("Empty auth value", TPMRetryAction::kNoRetry);
  }

  return middleware_.CallSync<&Backend::Sealing::Seal>(
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .current_user =
                      DeviceConfigSettings::CurrentUserSetting{
                          .username = current_user,
                      },
              },
          .permission =
              Permission{
                  .auth_value = auth_value,
              },
      },
      unsealed_data);
}

StatusOr<std::optional<ScopedKey>> CryptohomeFrontendImpl::PreloadSealedData(
    const brillo::Blob& sealed_data) {
  return middleware_.CallSync<&Backend::Sealing::PreloadSealedData>(
      OperationPolicy{}, sealed_data);
}

StatusOr<brillo::SecureBlob> CryptohomeFrontendImpl::UnsealWithCurrentUser(
    std::optional<Key> preload_data,
    const brillo::SecureBlob& auth_value,
    const brillo::Blob& sealed_data) {
  if (auth_value.empty()) {
    return MakeStatus<TPMError>("Empty auth value", TPMRetryAction::kNoRetry);
  }

  return middleware_.CallSync<&Backend::Sealing::Unseal>(
      OperationPolicy{
          .device_configs =
              DeviceConfigs{
                  DeviceConfig::kCurrentUser,
              },
          .permission =
              Permission{
                  .auth_value = auth_value,
              },
      },
      sealed_data,
      Backend::Sealing::UnsealOptions{
          .preload_data = preload_data,
      });
}

StatusOr<brillo::Blob> CryptohomeFrontendImpl::Encrypt(
    Key key, const brillo::SecureBlob& plaintext) {
  return middleware_.CallSync<&Backend::Encryption::Encrypt>(
      key, plaintext,
      Backend::Encryption::EncryptionOptions{
          .schema = Backend::Encryption::EncryptionOptions::Schema::kDefault,
      });
}

StatusOr<brillo::SecureBlob> CryptohomeFrontendImpl::Decrypt(
    Key key, const brillo::Blob& ciphertext) {
  return middleware_.CallSync<&Backend::Encryption::Decrypt>(
      key, ciphertext,
      Backend::Encryption::EncryptionOptions{
          .schema = Backend::Encryption::EncryptionOptions::Schema::kDefault,
      });
}

StatusOr<brillo::SecureBlob> CryptohomeFrontendImpl::GetAuthValue(
    Key key, const brillo::SecureBlob& pass_blob) {
  return middleware_.CallSync<&Backend::Deriving::SecureDerive>(key, pass_blob);
}

StatusOr<brillo::Blob> CryptohomeFrontendImpl::GetRandomBlob(size_t size) {
  return middleware_.CallSync<&Backend::Random::RandomBlob>(size);
}

StatusOr<brillo::SecureBlob> CryptohomeFrontendImpl::GetRandomSecureBlob(
    size_t size) {
  return middleware_.CallSync<&Backend::Random::RandomSecureBlob>(size);
}

StatusOr<uint32_t> CryptohomeFrontendImpl::GetManufacturer() {
  return middleware_.CallSync<&Backend::Vendor::GetManufacturer>();
}

StatusOr<bool> CryptohomeFrontendImpl::IsPinWeaverEnabled() {
  return middleware_.CallSync<&Backend::PinWeaver::IsEnabled>();
}

StatusOr<CryptohomeFrontend::StorageState>
CryptohomeFrontendImpl::GetSpaceState(Space space) {
  return middleware_.CallSync<&Backend::Storage::IsReady>(space);
}

Status CryptohomeFrontendImpl::PrepareSpace(Space space, uint32_t size) {
  return middleware_.CallSync<&Backend::Storage::Prepare>(space, size);
}

StatusOr<brillo::Blob> CryptohomeFrontendImpl::LoadSpace(Space space) {
  return middleware_.CallSync<&Backend::Storage::Load>(space);
}

Status CryptohomeFrontendImpl::StoreSpace(Space space,
                                          const brillo::Blob& blob) {
  return middleware_.CallSync<&Backend::Storage::Store>(space, blob);
}

Status CryptohomeFrontendImpl::DestroySpace(Space space) {
  return middleware_.CallSync<&Backend::Storage::Destroy>(space);
}

StatusOr<bool> CryptohomeFrontendImpl::IsSpaceWriteLocked(Space space) {
  return middleware_.CallSync<&Backend::Storage::IsWriteLocked>(space);
}

Status CryptohomeFrontendImpl::DeclareTpmFirmwareStable() {
  return middleware_.CallSync<&Backend::Vendor::DeclareTpmFirmwareStable>();
}

StatusOr<SignatureSealedData>
CryptohomeFrontendImpl::SealWithSignatureAndCurrentUser(
    const std::string& current_user,
    const brillo::SecureBlob& unsealed_data,
    const brillo::Blob& public_key_spki_der,
    const std::vector<SignatureSealingAlgorithm>& key_algorithms) {
  OperationPolicySetting prior_login_setting{
      .device_config_settings = DeviceConfigSettings{
          .current_user = DeviceConfigSettings::CurrentUserSetting{
              .username = std::nullopt}}};

  OperationPolicySetting current_user_setting{
      .device_config_settings = DeviceConfigSettings{
          .current_user = DeviceConfigSettings::CurrentUserSetting{
              .username = current_user}}};

  return middleware_.CallSync<&Backend::SignatureSealing::Seal>(
      std::vector<OperationPolicySetting>{prior_login_setting,
                                          current_user_setting},
      unsealed_data, public_key_spki_der, key_algorithms);
}

StatusOr<CryptohomeFrontend::ChallengeResult>
CryptohomeFrontendImpl::ChallengeWithSignatureAndCurrentUser(
    const SignatureSealedData& sealed_data,
    const brillo::Blob& public_key_spki_der,
    const std::vector<SignatureSealingAlgorithm>& key_algorithms) {
  OperationPolicy current_user_policy{
      .device_configs = DeviceConfigs{DeviceConfig::kCurrentUser}};

  return middleware_.CallSync<&Backend::SignatureSealing::Challenge>(
      current_user_policy, sealed_data, public_key_spki_der, key_algorithms);
}

StatusOr<brillo::SecureBlob> CryptohomeFrontendImpl::UnsealWithChallenge(
    ChallengeID challenge, const brillo::Blob& challenge_response) {
  return middleware_.CallSync<&Backend::SignatureSealing::Unseal>(
      challenge, challenge_response);
}

}  // namespace hwsec
