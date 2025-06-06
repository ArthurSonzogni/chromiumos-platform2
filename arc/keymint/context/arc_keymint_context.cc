// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_keymint_context.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <crypto/sha2.h>
#include <keymaster/android_keymaster_utils.h>
#include <keymaster/attestation_context.h>
#include <keymaster/key_blob_utils/integrity_assured_key_blob.h>
#include <keymaster/key_blob_utils/software_keyblobs.h>
#include <libarc-attestation/lib/interface.h>
#include <metrics/metrics_library.h>
#include <mojo/cert_store.mojom.h>
#include <openssl/evp.h>
#include <re2/re2.h>

#include "absl/strings/escaping.h"
#include "arc/keymint/context/chaps_client.h"
#include "arc/keymint/context/openssl_utils.h"

namespace arc::keymint::context {

namespace {

constexpr const char kVbMetaDigestFileDir[] = "/opt/google/vms/android/";
constexpr const char kVbMetaDigestFileName[] = "arcvm_vbmeta_digest.sha256";
constexpr uint32_t kExpectedVbMetaDigestSize = 32;

// Relate cros system property mainfw_type (main firmware type)
// to verified boot state. Devices in normal and recovery mode
// are in verified boot state. Devices in developer mode are in
// an unverified boot state.
const std::map<std::string, VerifiedBootState> kMainfwTypeToBootStateMap = {
    {"normal", VerifiedBootState::kVerifiedBoot},
    {"recovery", VerifiedBootState::kVerifiedBoot},
    {"developer", VerifiedBootState::kUnverifiedBoot}};

// Converts VerifiedBootState to the value expected by Android in DeviceInfo
// for |vb_state|.
// DeviceInfo expected values:
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/DeviceInfoV2.cddl
const std::map<VerifiedBootState, std::string> kVerifiedBootStateToStringMap = {
    {VerifiedBootState::kVerifiedBoot, "green"},
    {VerifiedBootState::kUnverifiedBoot, "orange"}};

// Converts VerifiedBootDeviceState to the value expected by Android in
// DeviceInfo for |bootloader_state|.
const std::map<VerifiedBootDeviceState, std::string> kDeviceStateToStringMap = {
    {VerifiedBootDeviceState::kLockedDevice, "locked"},
    {VerifiedBootDeviceState::kUnlockedDevice, "unlocked"}};

// This debugd boot log name maps to /var/log/debug_vboot_noisy.log. See
// https://source.chromium.org/chromiumos/_/chromium/chromiumos/platform2/+/main:debugd/src/log_tool.cc
constexpr const char kVerifiedBootLogName[] = "verified boot";
constexpr const char kBootKeyRegex[] =
    ".*bios::GBB::root_key::sha1_sum::([^ ].*)";

bool DeserializeKeyMaterialToBlob(const std::string& key_material,
                                  ::keymaster::KeymasterKeyBlob* output) {
  if (!output->Reset(key_material.size())) {
    return false;
  }

  uint8_t* end = std::copy(key_material.begin(), key_material.end(),
                           output->writable_data());
  CHECK_EQ(end - output->key_material, output->key_material_size);
  return true;
}

bool DeserializeKeyDataToBlob(const KeyData& key_data,
                              ::keymaster::KeymasterKeyBlob* output) {
  if (!output->Reset(key_data.ByteSizeLong())) {
    return false;
  }
  uint8_t* end =
      key_data.SerializeWithCachedSizesToArray(output->writable_data());
  return (end - output->key_material) == output->key_material_size;
}

void SerializeAuthorizationSet(const ::keymaster::AuthorizationSet& auth_set,
                               std::string* output) {
  output->resize(auth_set.SerializedSize());
  uint8_t* buffer = reinterpret_cast<uint8_t*>(std::data(*output));
  auth_set.Serialize(buffer, buffer + output->size());
}

bool DeserializeAuthorizationSet(const std::string& serialized_auth_set,
                                 ::keymaster::AuthorizationSet* output) {
  std::string mutable_auth_set(serialized_auth_set);
  const uint8_t* buffer =
      reinterpret_cast<uint8_t*>(std::data(mutable_auth_set));
  return output->Deserialize(&buffer, buffer + serialized_auth_set.size());
}

brillo::Blob SerializeAuthorizationSetToBlob(
    const ::keymaster::AuthorizationSet& authorization_set) {
  brillo::Blob blob(authorization_set.SerializedSize(), 0);
  authorization_set.Serialize(blob.data(), blob.data() + blob.size());
  return blob;
}

bool UpgradeIntegerTag(keymaster_tag_t tag,
                       uint32_t value,
                       ::keymaster::AuthorizationSet* authorization_set,
                       bool* authorization_set_did_change) {
  *authorization_set_did_change = false;
  int tag_index = authorization_set->find(tag);
  if (tag_index == -1) {
    keymaster_key_param_t key_param;
    key_param.tag = tag;
    key_param.integer = value;
    authorization_set->push_back(key_param);
    *authorization_set_did_change = true;
    return true;
  }

  if (authorization_set->params[tag_index].integer > value) {
    return false;
  }

  if (authorization_set->params[tag_index].integer != value) {
    authorization_set->params[tag_index].integer = value;
    *authorization_set_did_change = true;
  }
  return true;
}

KeyData PackToArcKeyData(const ::keymaster::KeymasterKeyBlob& key_material,
                         const ::keymaster::AuthorizationSet& hw_enforced,
                         const ::keymaster::AuthorizationSet& sw_enforced) {
  KeyData key_data;

  // Copy key material.
  key_data.mutable_arc_key()->set_key_material(key_material.key_material,
                                               key_material.key_material_size);

  // Serialize hardware enforced authorization set.
  SerializeAuthorizationSet(hw_enforced, key_data.mutable_hw_enforced_tags());

  // Serialize software enforced authorization set.
  SerializeAuthorizationSet(sw_enforced, key_data.mutable_sw_enforced_tags());

  return key_data;
}

std::optional<KeyData> PackToChromeOsKeyData(
    const arc::keymint::mojom::KeyDataPtr& mojo_key_data,
    const ::keymaster::AuthorizationSet& hw_enforced,
    const ::keymaster::AuthorizationSet& sw_enforced) {
  KeyData key_data;

  // Copy key data.
  if (mojo_key_data->is_chaps_key_data()) {
    key_data.mutable_chaps_key()->set_id(
        mojo_key_data->get_chaps_key_data()->id);
    key_data.mutable_chaps_key()->set_label(
        mojo_key_data->get_chaps_key_data()->label);
    key_data.mutable_chaps_key()->set_slot(
        (ChapsKeyData::Slot)mojo_key_data->get_chaps_key_data()->slot);
  } else {
    return std::nullopt;
  }

  // Serialize hardware enforced authorization set.
  SerializeAuthorizationSet(hw_enforced, key_data.mutable_hw_enforced_tags());

  // Serialize software enforced authorization set.
  SerializeAuthorizationSet(sw_enforced, key_data.mutable_sw_enforced_tags());

  return key_data;
}

bool UnpackFromKeyData(const KeyData& key_data,
                       ::keymaster::KeymasterKeyBlob* key_material,
                       ::keymaster::AuthorizationSet* hw_enforced,
                       ::keymaster::AuthorizationSet* sw_enforced) {
  // For ARC keys, deserialize the actual key material into |key_material|.
  if (key_data.data_case() == KeyData::DataCase::kArcKey &&
      !DeserializeKeyMaterialToBlob(key_data.arc_key().key_material(),
                                    key_material)) {
    return false;
  }
  // For any other key type, store the full |key_data| into |key_material|.
  if (key_data.data_case() != KeyData::DataCase::kArcKey &&
      !DeserializeKeyDataToBlob(key_data, key_material)) {
    return false;
  }

  // Deserialize hardware enforced authorization set.
  if (!DeserializeAuthorizationSet(key_data.hw_enforced_tags(), hw_enforced)) {
    return false;
  }

  // Deserialize software enforced authorization set.
  return DeserializeAuthorizationSet(key_data.sw_enforced_tags(), sw_enforced);
}

std::optional<keymaster_algorithm_t> FindAlgorithmTag(
    const ::keymaster::AuthorizationSet& hw_enforced,
    const ::keymaster::AuthorizationSet& sw_enforced) {
  keymaster_algorithm_t algorithm;
  if (!hw_enforced.GetTagValue(::keymaster::TAG_ALGORITHM, &algorithm) &&
      !sw_enforced.GetTagValue(::keymaster::TAG_ALGORITHM, &algorithm)) {
    return std::nullopt;
  }
  return algorithm;
}

std::optional<std::string> ExtractBase64Spki(
    const ::keymaster::KeymasterKeyBlob& key_material) {
  // Parse key material.
  const uint8_t* material = key_material.key_material;
  EVP_PKEY* pkey = d2i_PrivateKey(EVP_PKEY_RSA, /*pkey=*/nullptr, &material,
                                  key_material.key_material_size);
  if (!pkey) {
    return std::nullopt;
  }
  std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> pkey_deleter(
      pkey, EVP_PKEY_free);

  // Retrieve DER subject public key info.
  int der_spki_length = i2d_PUBKEY(pkey, /*outp=*/nullptr);
  if (der_spki_length <= 0) {
    return std::nullopt;
  }

  brillo::Blob der_spki(der_spki_length, 0);
  uint8_t* der_spki_buffer = std::data(der_spki);
  int written_bytes = i2d_PUBKEY(pkey, &der_spki_buffer);
  if (written_bytes != der_spki_length) {
    return std::nullopt;
  }

  // Encode subject public key info to base 64.
  std::string der_spki_string(der_spki.begin(), der_spki.end());
  return base::Base64Encode(der_spki_string);
}

std::optional<std::vector<uint8_t>> fetchEndorsementPublicKey() {
  // Fetch endorsement public key from libarc-attestation.
  std::vector<uint8_t> ek_public_key;
  arc_attestation::AndroidStatus ek_key_status =
      arc_attestation::GetEndorsementPublicKey(ek_public_key);
  if (!ek_key_status.is_ok()) {
    LOG(ERROR)
        << "Error in fetching endorsement public key from libarc-attestation";
    return std::nullopt;
  }

  if (ek_public_key.empty()) {
    LOG(ERROR) << "Endorsement Public Key from libarc-attestation is empty";
    return std::nullopt;
  }
  return ek_public_key;
}
}  // namespace

ArcKeyMintContext::ArcKeyMintContext(::keymaster::KmVersion version)
    : PureSoftKeymasterContext(version, KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT),
      rsa_key_factory_(context_adaptor_.GetWeakPtr(), KM_ALGORITHM_RSA),
      vbmeta_digest_file_dir_(kVbMetaDigestFileDir) {
  CHECK(version >= ::keymaster::KmVersion::KEYMINT_1);

  arc_keymint_metrics_ = std::make_unique<ArcKeyMintMetrics>();
  cros_system_ = std::make_unique<crossystem::Crossystem>();
  const bool is_dev_mode = IsDevMode();
  const std::string bootloader_state = DeriveBootloaderState(is_dev_mode);
  const std::string boot_state = DeriveVerifiedBootState(is_dev_mode);
  const std::optional<std::vector<uint8_t>> vbmeta_digest_opt =
      GetVbMetaDigestFromFile();

  // vbmeta_digest will an empty vector unless a valid result was
  // returned from GetVbMetaDigestFromFile.
  std::vector<uint8_t> vbmeta_digest = {};
  if (vbmeta_digest_opt.has_value()) {
    vbmeta_digest = vbmeta_digest_opt.value();
  }

  // This is a protected data member in |pure_soft_keymaster_context.cpp|
  pure_soft_remote_provisioning_context_ =
      std::make_unique<ArcRemoteProvisioningContext>(security_level_);

  arc_attestation_context_ =
      std::make_unique<ArcAttestationContext>(version, security_level_);
  arc_enforcement_policy_ = std::make_unique<ArcEnforcementPolicy>(64, 64);

  GetAndSetBootKeyFromLogs(is_dev_mode);
  SetVerifiedBootParams(boot_state, bootloader_state, vbmeta_digest);
}

ArcKeyMintContext::~ArcKeyMintContext() {
  if (bus_ != nullptr) {
    bus_->ShutdownAndBlock();
  }
}

void ArcKeyMintContext::set_placeholder_keys(
    std::vector<arc::keymint::mojom::ChromeOsKeyPtr> keys) {
  placeholder_keys_ = std::move(keys);
}

void ArcKeyMintContext::DeletePlaceholderKey(
    const arc::keymint::mojom::ChromeOsKeyPtr& key) const {
  std::vector<arc::keymint::mojom::ChromeOsKeyPtr>::iterator position =
      std::find(placeholder_keys_.begin(), placeholder_keys_.end(), key);
  if (position != placeholder_keys_.end()) {
    placeholder_keys_.erase(position);
  }
}

std::optional<arc::keymint::mojom::ChromeOsKeyPtr>
ArcKeyMintContext::FindPlaceholderKey(
    const ::keymaster::KeymasterKeyBlob& key_material) const {
  if (placeholder_keys_.empty()) {
    return std::nullopt;
  }

  std::optional<std::string> base64_spki = ExtractBase64Spki(key_material);
  if (!base64_spki.has_value()) {
    return std::nullopt;
  }

  for (const auto& cros_key : placeholder_keys_) {
    if (base64_spki.value() == cros_key->base64_subject_public_key_info) {
      LOG(INFO) << "Found the placeholder key";
      return cros_key->Clone();
    }
  }

  return std::nullopt;
}

std::optional<KeyData> ArcKeyMintContext::PackToKeyData(
    const ::keymaster::KeymasterKeyBlob& key_material,
    const ::keymaster::AuthorizationSet& hw_enforced,
    const ::keymaster::AuthorizationSet& sw_enforced) const {
  std::optional<arc::keymint::mojom::ChromeOsKeyPtr> cros_key =
      FindPlaceholderKey(key_material);
  if (!cros_key.has_value()) {
    return PackToArcKeyData(key_material, hw_enforced, sw_enforced);
  }

  std::optional<KeyData> key_data = PackToChromeOsKeyData(
      cros_key.value()->key_data->Clone(), hw_enforced, sw_enforced);

  // Ensure the placeholder of a Chrome OS key is only used once.
  if (key_data.has_value()) {
    DeletePlaceholderKey(cros_key.value());
  }

  return key_data;
}

keymaster_error_t ArcKeyMintContext::CreateKeyBlob(
    const ::keymaster::AuthorizationSet& key_description,
    const keymaster_key_origin_t origin,
    const ::keymaster::KeymasterKeyBlob& key_material,
    ::keymaster::KeymasterKeyBlob* key_blob,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced) const {
  // Check whether the key blob can be securely stored by pure software secure
  // key storage.
  bool canStoreBySecureKeyStorageIfRequired = false;
  if (GetSecurityLevel() != KM_SECURITY_LEVEL_SOFTWARE &&
      pure_soft_secure_key_storage_ != nullptr) {
    pure_soft_secure_key_storage_->HasSlot(
        &canStoreBySecureKeyStorageIfRequired);
  }

  bool needStoreBySecureKeyStorage = false;
  if (key_description.GetTagValue(::keymaster::TAG_ROLLBACK_RESISTANCE)) {
    needStoreBySecureKeyStorage = true;
    if (!canStoreBySecureKeyStorageIfRequired) {
      return KM_ERROR_ROLLBACK_RESISTANCE_UNAVAILABLE;
    }
  }

  if (GetSecurityLevel() != KM_SECURITY_LEVEL_SOFTWARE) {
    // We're pretending to be some sort of secure hardware.  Put relevant tags
    // in hw_enforced.
    for (auto& entry : key_description) {
      switch (entry.tag) {
        case KM_TAG_PURPOSE:
        case KM_TAG_ALGORITHM:
        case KM_TAG_KEY_SIZE:
        case KM_TAG_RSA_PUBLIC_EXPONENT:
        case KM_TAG_BLOB_USAGE_REQUIREMENTS:
        case KM_TAG_DIGEST:
        case KM_TAG_PADDING:
        case KM_TAG_BLOCK_MODE:
        case KM_TAG_MIN_SECONDS_BETWEEN_OPS:
        case KM_TAG_MAX_USES_PER_BOOT:
        case KM_TAG_USER_SECURE_ID:
        case KM_TAG_NO_AUTH_REQUIRED:
        case KM_TAG_AUTH_TIMEOUT:
        case KM_TAG_CALLER_NONCE:
        case KM_TAG_MIN_MAC_LENGTH:
        case KM_TAG_KDF:
        case KM_TAG_EC_CURVE:
        case KM_TAG_ECIES_SINGLE_HASH_MODE:
        case KM_TAG_USER_AUTH_TYPE:
        case KM_TAG_ORIGIN:
        case KM_TAG_OS_VERSION:
        case KM_TAG_OS_PATCHLEVEL:
        case KM_TAG_VENDOR_PATCHLEVEL:
        case KM_TAG_BOOT_PATCHLEVEL:
        case KM_TAG_EARLY_BOOT_ONLY:
        case KM_TAG_UNLOCKED_DEVICE_REQUIRED:
        case KM_TAG_RSA_OAEP_MGF_DIGEST:
        case KM_TAG_ROLLBACK_RESISTANCE:
          hw_enforced->push_back(entry);
          break;

        case KM_TAG_USAGE_COUNT_LIMIT:
          // Enforce single use key with usage count limit = 1 into secure key
          // storage.
          if (canStoreBySecureKeyStorageIfRequired && entry.integer == 1) {
            needStoreBySecureKeyStorage = true;
            hw_enforced->push_back(entry);
          }
          break;

        default:
          break;
      }
    }
  }

  keymaster_error_t error = SetKeyBlobAuthorizations(
      key_description, origin, os_version_, os_patchlevel_, hw_enforced,
      sw_enforced, GetKmVersion());
  if (error != KM_ERROR_OK) {
    return error;
  }

  error = ExtendKeyBlobAuthorizations(hw_enforced, sw_enforced,
                                      vendor_patchlevel_, boot_patchlevel_);
  if (error != KM_ERROR_OK) {
    LOG(ERROR) << "Failed to extend key blob authorizations with vendor patch "
                  "level and boot patch level";
    return error;
  }

  ::keymaster::AuthorizationSet hidden;
  error = BuildHiddenAuthorizations(key_description, &hidden,
                                    ::keymaster::softwareRootOfTrust);
  if (error != KM_ERROR_OK) {
    return error;
  }

  error = SerializeKeyDataBlob(key_material, hidden, *hw_enforced, *sw_enforced,
                               key_blob);
  if (error != KM_ERROR_OK) {
    return error;
  }

  // Pretend to be some sort of secure hardware that can securely store the key
  // blob.
  if (!needStoreBySecureKeyStorage) {
    return KM_ERROR_OK;
  }
  ::keymaster::km_id_t keyid;
  if (!soft_keymaster_enforcement_.CreateKeyId(*key_blob, &keyid)) {
    return KM_ERROR_UNKNOWN_ERROR;
  }
  assert(needStoreBySecureKeyStorage && canStoreBySecureKeyStorageIfRequired);

  return pure_soft_secure_key_storage_->WriteKey(keyid, *key_blob);
}

keymaster_error_t ArcKeyMintContext::ParseKeyBlob(
    const ::keymaster::KeymasterKeyBlob& key_blob,
    const ::keymaster::AuthorizationSet& additional_params,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  if (!key) {
    return KM_ERROR_OUTPUT_PARAMETER_NULL;
  }

  ::keymaster::AuthorizationSet hw_enforced;
  ::keymaster::AuthorizationSet sw_enforced;
  ::keymaster::KeymasterKeyBlob key_material;

  ::keymaster::AuthorizationSet hidden;
  keymaster_error_t error = BuildHiddenAuthorizations(
      additional_params, &hidden, ::keymaster::softwareRootOfTrust);
  if (error != KM_ERROR_OK) {
    return error;
  }

  error = DeserializeBlob(key_blob, hidden, &key_material, &hw_enforced,
                          &sw_enforced, key);
  if (error != KM_ERROR_OK) {
    return error;
  }
  if (*key) {
    return KM_ERROR_OK;
  }

  std::optional<keymaster_algorithm_t> algorithm =
      FindAlgorithmTag(hw_enforced, sw_enforced);
  if (!algorithm.has_value()) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  // Pretend to be some sort of secure hardware that can securely store
  // the key blob. Check the key blob is still securely stored now.
  if (hw_enforced.Contains(KM_TAG_ROLLBACK_RESISTANCE) ||
      hw_enforced.Contains(KM_TAG_USAGE_COUNT_LIMIT)) {
    if (pure_soft_secure_key_storage_ == nullptr) {
      return KM_ERROR_INVALID_KEY_BLOB;
    }
    ::keymaster::km_id_t keyid = 0;
    bool exists = false;
    if (!soft_keymaster_enforcement_.CreateKeyId(key_blob, &keyid)) {
      return KM_ERROR_INVALID_KEY_BLOB;
    }
    error = pure_soft_secure_key_storage_->KeyExists(keyid, &exists);
    if (error != KM_ERROR_OK || !exists) {
      return KM_ERROR_INVALID_KEY_BLOB;
    }
  }

  ::keymaster::KeyFactory* factory = GetKeyFactory(algorithm.value());

  return factory->LoadKey(std::move(key_material), additional_params,
                          std::move(hw_enforced), std::move(sw_enforced), key);
}

keymaster_error_t ArcKeyMintContext::UpgradeKeyBlob(
    const ::keymaster::KeymasterKeyBlob& key_blob,
    const ::keymaster::AuthorizationSet& upgrade_params,
    ::keymaster::KeymasterKeyBlob* upgraded_key) const {
  // Deserialize |key_blob| so it can be upgraded.
  ::keymaster::AuthorizationSet hidden;
  keymaster_error_t error = BuildHiddenAuthorizations(
      upgrade_params, &hidden, ::keymaster::softwareRootOfTrust);
  if (error != KM_ERROR_OK) {
    return error;
  }

  ::keymaster::AuthorizationSet hw_enforced;
  ::keymaster::AuthorizationSet sw_enforced;
  ::keymaster::KeymasterKeyBlob key_material;
  error = DeserializeBlob(key_blob, hidden, &key_material, &hw_enforced,
                          &sw_enforced, /*key=*/nullptr);
  if (error != KM_ERROR_OK) {
    return error;
  }

  // Try to upgrade system version and patchlevel, return if upgrade fails.
  bool os_version_did_change = false;
  bool os_patchlevel_did_change = false;
  if (!UpgradeIntegerTag(::keymaster::TAG_OS_VERSION, os_version_, &hw_enforced,
                         &os_version_did_change) ||
      !UpgradeIntegerTag(::keymaster::TAG_OS_PATCHLEVEL, os_patchlevel_,
                         &hw_enforced, &os_patchlevel_did_change)) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  bool vendor_patchlevel_did_change = false;
  if (!vendor_patchlevel_.has_value() ||
      !UpgradeIntegerTag(::keymaster::TAG_VENDOR_PATCHLEVEL,
                         vendor_patchlevel_.value(), &hw_enforced,
                         &vendor_patchlevel_did_change)) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  bool boot_patchlevel_did_change = false;
  if (!boot_patchlevel_.has_value() ||
      !UpgradeIntegerTag(::keymaster::TAG_BOOT_PATCHLEVEL,
                         boot_patchlevel_.value(), &hw_enforced,
                         &boot_patchlevel_did_change)) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  // If OS or any patch level has not changed, do not upgrade.
  if (!os_version_did_change && !os_patchlevel_did_change &&
      !vendor_patchlevel_did_change && !boot_patchlevel_did_change) {
    return KM_ERROR_OK;
  }

  // Serialize the new blob into |upgraded_key|.
  return SerializeKeyDataBlob(key_material, hidden, hw_enforced, sw_enforced,
                              upgraded_key);
}

keymaster_error_t ArcKeyMintContext::DeserializeBlob(
    const ::keymaster::KeymasterKeyBlob& key_blob,
    const ::keymaster::AuthorizationSet& hidden,
    ::keymaster::KeymasterKeyBlob* key_material,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  keymaster_error_t error = DeserializeKeyDataBlob(
      key_blob, hidden, key_material, hw_enforced, sw_enforced, key);
  if (error == KM_ERROR_OK) {
    return error;
  }

  // Still need to parse insecure blobs when upgrading to the encrypted format.
  // TODO(b/151146402) drop support for insecure blobs.
  return DeserializeIntegrityAssuredBlob(key_blob, hidden, key_material,
                                         hw_enforced, sw_enforced);
}

keymaster_error_t ArcKeyMintContext::SerializeKeyDataBlob(
    const ::keymaster::KeymasterKeyBlob& key_material,
    const ::keymaster::AuthorizationSet& hidden,
    const ::keymaster::AuthorizationSet& hw_enforced,
    const ::keymaster::AuthorizationSet& sw_enforced,
    ::keymaster::KeymasterKeyBlob* key_blob) const {
  if (!key_blob) {
    return KM_ERROR_OUTPUT_PARAMETER_NULL;
  }

  std::optional<KeyData> key_data =
      PackToKeyData(key_material, hw_enforced, sw_enforced);
  if (!key_data.has_value()) {
    LOG(ERROR) << "Failed to package KeyData.";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  // Serialize key data into the output |key_blob|.
  if (!SerializeKeyData(key_data.value(), hidden, key_blob)) {
    LOG(ERROR) << "Failed to serialize KeyData.";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  return KM_ERROR_OK;
}

keymaster_error_t ArcKeyMintContext::DeserializeKeyDataBlob(
    const ::keymaster::KeymasterKeyBlob& key_blob,
    const ::keymaster::AuthorizationSet& hidden,
    ::keymaster::KeymasterKeyBlob* key_material,
    ::keymaster::AuthorizationSet* hw_enforced,
    ::keymaster::AuthorizationSet* sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  if (!key_material || !hw_enforced || !sw_enforced) {
    return KM_ERROR_OUTPUT_PARAMETER_NULL;
  }

  // Deserialize a KeyData object from the given |key_blob|.
  std::optional<KeyData> key_data = DeserializeKeyData(key_blob, hidden);
  if (!key_data.has_value() || key_data->data_case() == KeyData::DATA_NOT_SET) {
    LOG(ERROR) << "Failed to parse a KeyData from key blob.";
    return KM_ERROR_INVALID_KEY_BLOB;
  }

  // Unpack Keymaster structures from KeyData.
  if (!UnpackFromKeyData(key_data.value(), key_material, hw_enforced,
                         sw_enforced)) {
    LOG(ERROR) << "Failed to unpack key blob.";
    return KM_ERROR_INVALID_KEY_BLOB;
  }

  // Load it here if this is not an ARC key (it is a Chrome OS key).
  if (!key_data->has_arc_key() && key) {
    return LoadKey(std::move(key_data.value()), std::move(*hw_enforced),
                   std::move(*sw_enforced), key);
  }

  // Otherwise, return success and let Keymaster load ARC keys itself.
  return KM_ERROR_OK;
}

keymaster_error_t ArcKeyMintContext::LoadKey(
    KeyData&& key_data,
    ::keymaster::AuthorizationSet&& hw_enforced,
    ::keymaster::AuthorizationSet&& sw_enforced,
    ::keymaster::UniquePtr<::keymaster::Key>* key) const {
  std::optional<keymaster_algorithm_t> algorithm =
      FindAlgorithmTag(hw_enforced, sw_enforced);
  if (!algorithm.has_value()) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  switch (algorithm.value()) {
    case KM_ALGORITHM_RSA:
      return rsa_key_factory_.LoadKey(std::move(key_data),
                                      std::move(hw_enforced),
                                      std::move(sw_enforced), key);
    default:
      return KM_ERROR_UNSUPPORTED_ALGORITHM;
  }
}

bool ArcKeyMintContext::SerializeKeyData(
    const KeyData& key_data,
    const ::keymaster::AuthorizationSet& hidden,
    ::keymaster::KeymasterKeyBlob* key_blob) const {
  // Fetch key.
  ChapsClient chaps(context_adaptor_.GetWeakPtr(), ContextAdaptor::Slot::kUser);
  std::optional<brillo::SecureBlob> encryption_key =
      chaps.ExportOrGenerateEncryptionKey();
  if (!encryption_key.has_value()) {
    return false;
  }

  // Initialize a KeyData blob. Allocated blobs should offer the same guarantees
  // as brillo::SecureBlob (b/151103358).
  brillo::SecureBlob data(key_data.ByteSizeLong(), 0);
  key_data.SerializeWithCachedSizesToArray(data.data());

  // Encrypt the KeyData blob. As of Android R KeyStore's client ID and data
  // used in |auth_data| is empty. We still bind to it to comply with VTS tests.
  brillo::Blob auth_data = SerializeAuthorizationSetToBlob(hidden);
  std::optional<brillo::Blob> encrypted =
      Aes256GcmEncrypt(encryption_key.value(), auth_data, data);
  if (!encrypted.has_value()) {
    return false;
  }

  // Copy |encrypted| to output |key_blob|.
  if (!key_blob->Reset(encrypted->size())) {
    return false;
  }
  std::copy(encrypted->begin(), encrypted->end(), key_blob->writable_data());
  return true;
}

std::optional<KeyData> ArcKeyMintContext::DeserializeKeyData(
    const ::keymaster::KeymasterKeyBlob& key_blob,
    const ::keymaster::AuthorizationSet& hidden) const {
  // Fetch key.
  ChapsClient chaps(context_adaptor_.GetWeakPtr(), ContextAdaptor::Slot::kUser);
  std::optional<brillo::SecureBlob> encryption_key =
      chaps.ExportOrGenerateEncryptionKey();
  if (!encryption_key.has_value()) {
    return std::nullopt;
  }

  // Decrypt the KeyData blob.
  brillo::Blob encrypted(key_blob.begin(), key_blob.end());
  brillo::Blob auth_data = SerializeAuthorizationSetToBlob(hidden);
  std::optional<brillo::SecureBlob> unencrypted =
      Aes256GcmDecrypt(encryption_key.value(), auth_data, encrypted);
  if (!unencrypted.has_value()) {
    return std::nullopt;
  }

  // Parse the |unencrypted| blob into a KeyData object and return it.
  KeyData key_data;
  if (!key_data.ParseFromArray(unencrypted->data(), unencrypted->size())) {
    return std::nullopt;
  }

  return key_data;
}

brillo::Blob ArcKeyMintContext::TestSerializeAuthorizationSetToBlob(
    const ::keymaster::AuthorizationSet& authorization_set) {
  return SerializeAuthorizationSetToBlob(authorization_set);
}

keymaster_error_t ArcKeyMintContext::SetSerialNumber(
    const std::string& serial_number) {
  if (serial_number.empty()) {
    LOG(ERROR) << "Cannot set empty serial number in KeyMint.";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  if (pure_soft_remote_provisioning_context_ == nullptr) {
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  // We also need to set the serial number in Arc Remote Provisioning Context.
  // Hence, dynamic casting a base class pointer to derived class.
  ArcRemoteProvisioningContext* arc_remote_provisioning_context =
      dynamic_cast<ArcRemoteProvisioningContext*>(
          pure_soft_remote_provisioning_context_.get());
  if (arc_remote_provisioning_context == nullptr) {
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  return arc_remote_provisioning_context->SetSerialNumber(serial_number);
}

keymaster_error_t ArcKeyMintContext::SetSystemVersion(uint32_t os_version,
                                                      uint32_t os_patchlevel) {
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
  if (pure_soft_remote_provisioning_context_ != nullptr) {
    pure_soft_remote_provisioning_context_->SetSystemVersion(os_version,
                                                             os_patchlevel);
    // We also need to set the fields in Arc Remote Provisioning Context.
    // Hence, dynamic casting a base class pointer to derived class.
    ArcRemoteProvisioningContext* arc_remote_provisioning_context =
        dynamic_cast<ArcRemoteProvisioningContext*>(
            pure_soft_remote_provisioning_context_.get());
    if (arc_remote_provisioning_context != nullptr) {
      arc_remote_provisioning_context->SetSystemVersion(os_version,
                                                        os_patchlevel);
    }
  }
  return KM_ERROR_OK;
}

keymaster_error_t ArcKeyMintContext::SetChallengeForCertificateRequest(
    std::vector<uint8_t>& challenge) {
  if (challenge.empty()) {
    return KM_ERROR_INVALID_ARGUMENT;
  }
  if (pure_soft_remote_provisioning_context_ == nullptr) {
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  // We also need to set the fields in Arc Remote Provisioning Context.
  // Hence, dynamic casting a base class pointer to derived class.
  ArcRemoteProvisioningContext* arc_remote_provisioning_context =
      dynamic_cast<ArcRemoteProvisioningContext*>(
          pure_soft_remote_provisioning_context_.get());
  if (arc_remote_provisioning_context == nullptr) {
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  arc_remote_provisioning_context->SetChallengeForCertificateRequest(challenge);
  return KM_ERROR_OK;
}

void ArcKeyMintContext::set_cros_system_for_tests(
    std::unique_ptr<crossystem::Crossystem> cros_system) {
  cros_system_ = std::move(cros_system);
}

void ArcKeyMintContext::set_vbmeta_digest_file_dir_for_tests(
    base::FilePath& vbmeta_digest_file_dir) {
  vbmeta_digest_file_dir_ = base::FilePath(vbmeta_digest_file_dir);
}

void ArcKeyMintContext::set_dbus_for_tests(scoped_refptr<dbus::Bus> bus) {
  bus_ = bus;
}

void ArcKeyMintContext::set_arc_keymint_metrics_for_tests(
    std::unique_ptr<ArcKeyMintMetrics> arc_keymint_metrics) {
  arc_keymint_metrics_ = std::move(arc_keymint_metrics);
}

std::optional<std::vector<uint8_t>> ArcKeyMintContext::GetVbMetaDigestFromFile()
    const {
  base::FilePath vbmeta_digest_file_path =
      vbmeta_digest_file_dir_.Append(kVbMetaDigestFileName);
  std::string vbmeta_digest;
  if (!base::PathExists(vbmeta_digest_file_path) ||
      !base::ReadFileToString(base::FilePath(vbmeta_digest_file_path),
                              &vbmeta_digest)) {
    // In case of failure to read vb meta digest into string, return nullopt.
    LOG(ERROR) << "Failed to read vb meta digest file from path "
               << vbmeta_digest_file_path;
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootHashResult(
          ArcVerifiedBootHashResult::kFileError);
    }
    return std::nullopt;
  }
  std::string bytes_string = absl::HexStringToBytes(vbmeta_digest.c_str());
  std::vector<uint8_t> vbmeta_digest_result =
      brillo::BlobFromString(bytes_string);
  if (vbmeta_digest_result.size() != kExpectedVbMetaDigestSize) {
    LOG(ERROR) << "vbmeta digest is not a valid hash. "
               << "Expected size: " << kExpectedVbMetaDigestSize
               << ". Actual size: " << vbmeta_digest_result.size();
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootHashResult(
          ArcVerifiedBootHashResult::kInvalidHash);
    }
    return std::nullopt;
  }

  if (arc_keymint_metrics_ != nullptr) {
    arc_keymint_metrics_->SendVerifiedBootHashResult(
        ArcVerifiedBootHashResult::kSuccess);
  }
  return vbmeta_digest_result;
}

void ArcKeyMintContext::GetAndSetBootKeyFromLogs(const bool is_dev_mode) {
  const std::string empty_boot_key(32, '\0');
  if (is_dev_mode) {
    boot_key_ = brillo::BlobFromString(empty_boot_key);
    LOG(INFO) << "Returning Empty Boot key in Dev Mode";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootKeyResult(
          ArcVerifiedBootKeyResult::kSuccessDevKey);
    }
    return;
  }

  if (bus_ == nullptr) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::Bus(options);
  }
  if (!bus_->Connect()) {
    LOG(ERROR) << "Unable to connect to DBUS. Cannot get verified boot key.";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootKeyResult(
          ArcVerifiedBootKeyResult::kDebugdError);
    }
    return;
  }

  brillo::ErrorPtr error;
  std::string verified_boot_log;
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy =
      std::make_unique<org::chromium::debugdProxy>(bus_.get());

  if (debugd_proxy == nullptr) {
    LOG(ERROR) << "debugd_proxy is null. Cannot get verified boot key.";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootKeyResult(
          ArcVerifiedBootKeyResult::kDebugdError);
    }
    return;
  }

  debugd_proxy->GetLog(kVerifiedBootLogName, &verified_boot_log, &error);
  if (error != nullptr) {
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootKeyResult(
          ArcVerifiedBootKeyResult::kDebugdError);
    }
    LOG(ERROR) << "debugd GetLog call failed with: " << error->GetMessage();
    return;
  }

  if (verified_boot_log.empty()) {
    LOG(ERROR) << "Empty verified boot log was retrieved from debugd";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootKeyResult(
          ArcVerifiedBootKeyResult::kVbLogError);
    }
    return;
  }

  std::string boot_key;
  if (!RE2::PartialMatch(verified_boot_log, kBootKeyRegex, &boot_key)) {
    LOG(ERROR) << "Did not find boot key info in verified boot log";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootKeyResult(
          ArcVerifiedBootKeyResult::kVbLogError);
    }
    return;
  }
  if (arc_keymint_metrics_ != nullptr) {
    arc_keymint_metrics_->SendVerifiedBootKeyResult(
        ArcVerifiedBootKeyResult::kSuccessProdKey);
  }
  boot_key_ = brillo::BlobFromString(boot_key);
}

const bool ArcKeyMintContext::IsDevMode() const {
  if (!cros_system_) {
    LOG(ERROR) << "cros_system_ is null. Hence, assuming device is in dev mode";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootStateResult(
          ArcVerifiedBootStateResult::kNullCrosSystem);
    }
    return true;
  }

  // Get the value of cros_debug using cros_system.
  std::optional<int> cros_debug =
      cros_system_->VbGetSystemPropertyInt("cros_debug");

  // If cros_debug cannot be read, assume the device is in dev mode.
  if (!cros_debug.has_value() || cros_debug < 0) {
    LOG(ERROR) << "Error while trying to read cros_debug. Assuming dev mode";
    if (arc_keymint_metrics_ != nullptr) {
      arc_keymint_metrics_->SendVerifiedBootStateResult(
          ArcVerifiedBootStateResult::kInvalidCrosDebug);
    }
    return true;
  }

  if (arc_keymint_metrics_ != nullptr) {
    arc_keymint_metrics_->SendVerifiedBootStateResult(
        ArcVerifiedBootStateResult::kSuccess);
  }
  // Device is in dev mode as flag is explicitly set.
  if (cros_debug == 1) {
    return true;
  }
  // Device is not in dev mode.
  return false;
}

/* Derive the boot loader state depending upon if the device is in developer
 * mode or not.
 */
std::string ArcKeyMintContext::DeriveBootloaderState(
    const bool is_dev_mode) const {
  if (!is_dev_mode) {
    return kDeviceStateToStringMap.at(VerifiedBootDeviceState::kLockedDevice);
  }
  return kDeviceStateToStringMap.at(VerifiedBootDeviceState::kUnlockedDevice);
}

/* Derive the verified boot state depending upon if the device is in developer
 * mode or not.
 */
std::string ArcKeyMintContext::DeriveVerifiedBootState(
    const bool is_dev_mode) const {
  if (!is_dev_mode) {
    return kVerifiedBootStateToStringMap.at(VerifiedBootState::kVerifiedBoot);
  }
  return kVerifiedBootStateToStringMap.at(VerifiedBootState::kUnverifiedBoot);
}

keymaster::Buffer ArcKeyMintContext::GenerateUniqueId(
    uint64_t creation_date_time,
    const keymaster_blob_t& application_id,
    bool reset_since_rotation,
    keymaster_error_t* error) const {
  auto ek_public_key = fetchEndorsementPublicKey();
  if (!ek_public_key.has_value() || ek_public_key.value().empty()) {
    LOG(ERROR)
        << "Failed to get Endorsement Public Key from lib arc-attestation";
    *error = KM_ERROR_INVALID_KEY_BLOB;
    return keymaster::generate_unique_id({}, creation_date_time, application_id,
                                         reset_since_rotation);
  }
  const std::string ek_pub_key_hash =
      crypto::SHA256HashString(brillo::BlobToString(ek_public_key.value()));
  auto ek_pub_key_hash_vector = brillo::BlobFromString(ek_pub_key_hash);
  *error = KM_ERROR_OK;
  return keymaster::generate_unique_id(ek_pub_key_hash_vector,
                                       creation_date_time, application_id,
                                       reset_since_rotation);
}

keymaster_error_t ArcKeyMintContext::SetVerifiedBootParams(
    std::string_view boot_state,
    std::string_view bootloader_state,
    const std::vector<uint8_t>& vbmeta_digest) {
  // These are protected data members in |pure_soft_keymaster_context.cpp|
  bootloader_state_ = bootloader_state;
  verified_boot_state_ = boot_state;
  if (!vbmeta_digest.empty()) {
    vbmeta_digest_ = vbmeta_digest;
  } else {
    LOG(ERROR) << "vbmeta_digest is empty when trying to set vb boot params";
  }

  if (arc_attestation_context_ == nullptr) {
    LOG(ERROR) << "arc_attestation_context_ is null. Cannot set "
                  "verified boot info.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }

  auto error = arc_attestation_context_->SetVerifiedBootParams(
      boot_state, bootloader_state, vbmeta_digest, boot_key_);
  if (error != KM_ERROR_OK) {
    LOG(ERROR)
        << "Cannot set Verified Boot parameters in ARC Attestation Context";
    return KM_ERROR_INVALID_ARGUMENT;
  }

  // We also need to set the fields in Arc Remote Provisioning Context.
  // Hence, dynamic casting a base class pointer to derived class.
  if (pure_soft_remote_provisioning_context_ == nullptr) {
    LOG(ERROR) << "pure_soft_remote_provisioning_context_ is null. Cannot set "
                  "verified boot info.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  ArcRemoteProvisioningContext* arc_remote_provisioning_context =
      dynamic_cast<ArcRemoteProvisioningContext*>(
          pure_soft_remote_provisioning_context_.get());

  if (arc_remote_provisioning_context == nullptr) {
    LOG(ERROR) << "arc_remote_provisioning_context is null. Cannot set "
                  "verified boot info.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  arc_remote_provisioning_context->SetVerifiedBootInfo(
      boot_state, bootloader_state, vbmeta_digest);
  return KM_ERROR_OK;
}

std::optional<uint32_t> ArcKeyMintContext::GetVendorPatchlevel() const {
  return vendor_patchlevel_;
}

std::optional<uint32_t> ArcKeyMintContext::GetBootPatchlevel() const {
  return boot_patchlevel_;
}

keymaster_error_t ArcKeyMintContext::SetVendorPatchlevel(
    uint32_t vendor_patchlevel) {
  if (vendor_patchlevel_.has_value() &&
      vendor_patchlevel != vendor_patchlevel_.value()) {
    // Can't set patchlevel to a different value.
    LOG(ERROR) << "Vendor Patch level was already set. Can't set it to a "
                  "different value.";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  vendor_patchlevel_ = vendor_patchlevel;

  // We also need to set the fields in Arc Remote Provisioning Context.
  // Hence, dynamic casting a base class pointer to derived class.
  if (pure_soft_remote_provisioning_context_ == nullptr) {
    LOG(ERROR) << "pure_soft_remote_provisioning_context_ is null. Cannot set "
                  "vendor patch level.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  ArcRemoteProvisioningContext* arc_remote_provisioning_context =
      dynamic_cast<ArcRemoteProvisioningContext*>(
          pure_soft_remote_provisioning_context_.get());

  if (arc_remote_provisioning_context == nullptr) {
    LOG(ERROR) << "arc_remote_provisioning_context is null. Cannot set "
                  "vendor patch level.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  arc_remote_provisioning_context->SetVendorPatchlevel(vendor_patchlevel);
  return KM_ERROR_OK;
}

keymaster_error_t ArcKeyMintContext::SetBootPatchlevel(
    uint32_t boot_patchlevel) {
  if (boot_patchlevel_.has_value() &&
      boot_patchlevel != boot_patchlevel_.value()) {
    // Can't set patchlevel to a different value.
    LOG(ERROR) << "Boot Patch level was already set. Can't set it to a "
                  "different value.";
    return KM_ERROR_INVALID_ARGUMENT;
  }
  boot_patchlevel_ = boot_patchlevel;

  // We also need to set the fields in Arc Remote Provisioning Context.
  // Hence, dynamic casting a base class pointer to derived class.
  if (pure_soft_remote_provisioning_context_ == nullptr) {
    LOG(ERROR) << "pure_soft_remote_provisioning_context_ is null. Cannot set "
                  "boot patch level.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  ArcRemoteProvisioningContext* arc_remote_provisioning_context =
      dynamic_cast<ArcRemoteProvisioningContext*>(
          pure_soft_remote_provisioning_context_.get());

  if (arc_remote_provisioning_context == nullptr) {
    LOG(ERROR) << "arc_remote_provisioning_context is null. Cannot set "
                  "boot patch level.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  arc_remote_provisioning_context->SetBootPatchlevel(boot_patchlevel);
  return KM_ERROR_OK;
}

keymaster_error_t ArcKeyMintContext::VerifyAndCopyDeviceIds(
    const ::keymaster::AuthorizationSet& attestation_params,
    ::keymaster::AuthorizationSet* attestation) const {
  // Dynamic casting a base class pointer to derived class.
  if (pure_soft_remote_provisioning_context_ == nullptr) {
    LOG(ERROR) << "pure_soft_remote_provisioning_context_ is null. Cannot "
                  "verify and copy device IDs.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }
  ArcRemoteProvisioningContext* arc_remote_provisioning_context =
      dynamic_cast<ArcRemoteProvisioningContext*>(
          pure_soft_remote_provisioning_context_.get());

  if (arc_remote_provisioning_context == nullptr) {
    LOG(ERROR) << "Failure to dynamically cast the pointer. Cannot "
                  "verify and copy device IDs.";
    return KM_ERROR_UNEXPECTED_NULL_POINTER;
  }

  keymaster_error_t error =
      arc_remote_provisioning_context->VerifyAndCopyDeviceIds(
          attestation_params, attestation);
  return error;
}

::keymaster::AttestationContext* ArcKeyMintContext::attestation_context() {
  return reinterpret_cast<::keymaster::AttestationContext*>(
      arc_attestation_context_.get());
}

const ::keymaster::AttestationContext::VerifiedBootParams*
ArcKeyMintContext::GetVerifiedBootParams(keymaster_error_t* error) const {
  static VerifiedBootParams params;
  if (error == nullptr) {
    LOG(ERROR) << "Cannot return an error via nullptr";
    return &params;
  }
  if (arc_attestation_context_ == nullptr) {
    LOG(ERROR)
        << "Arc Attestation Context is null. Cannot get Verified Boot Params";
    // We still need to return KM_ERROR_OK to pass the CTS.
    *error = KM_ERROR_OK;
    return &params;
  }
  auto derived_params = arc_attestation_context_->GetVerifiedBootParams(error);
  if (derived_params != nullptr) {
    params = *derived_params;
  }

  return &params;
}

::keymaster::KeymasterEnforcement* ArcKeyMintContext::enforcement_policy() {
  return reinterpret_cast<::keymaster::KeymasterEnforcement*>(
      arc_enforcement_policy_.get());
}

void ArcKeyMintContext::SetVendorPatchlevelForTesting(
    uint32_t vendor_patchlevel) {
  vendor_patchlevel_ = vendor_patchlevel;
}

void ArcKeyMintContext::SetBootPatchlevelForTesting(uint32_t boot_patchlevel) {
  boot_patchlevel_ = boot_patchlevel;
}
}  // namespace arc::keymint::context
