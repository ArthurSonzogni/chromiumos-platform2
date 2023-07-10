// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/conversion.h"

#include <algorithm>
#include <utility>

namespace arc::keymint {

namespace {

class KmParamSet {
 public:
  explicit KmParamSet(
      const std::vector<arc::mojom::keymint::KeyParameterPtr>& data) {
    param_set_.params = new keymaster_key_param_t[data.size()];
    param_set_.length = data.size();
    for (size_t i = 0; i < data.size(); ++i) {
      keymaster_tag_t tag = ConvertEnum(data[i]->tag);
      switch (keymaster_tag_get_type(tag)) {
        case KM_ENUM:
        case KM_ENUM_REP:
          if (data[i]->value->is_integer()) {
            param_set_.params[i] =
                keymaster_param_enum(tag, data[i]->value->get_integer());
          } else {
            param_set_.params[i].tag = KM_TAG_INVALID;
          }
          break;
        case KM_UINT:
        case KM_UINT_REP:
          if (data[i]->value->is_integer()) {
            param_set_.params[i] =
                keymaster_param_int(tag, data[i]->value->get_integer());
          } else {
            param_set_.params[i].tag = KM_TAG_INVALID;
          }
          break;
        case KM_ULONG:
        case KM_ULONG_REP:
          if (data[i]->value->is_long_integer()) {
            param_set_.params[i] =
                keymaster_param_long(tag, data[i]->value->get_long_integer());
          } else {
            param_set_.params[i].tag = KM_TAG_INVALID;
          }
          break;
        case KM_DATE:
          if (data[i]->value->is_date_time()) {
            param_set_.params[i] =
                keymaster_param_date(tag, data[i]->value->get_date_time());
          } else {
            param_set_.params[i].tag = KM_TAG_INVALID;
          }
          break;
        case KM_BOOL:
          if (data[i]->value->is_bool_value() &&
              data[i]->value->get_bool_value()) {
            // This function takes a single argument. Default value is TRUE.
            param_set_.params[i] = keymaster_param_bool(tag);
          } else {
            param_set_.params[i].tag = KM_TAG_INVALID;
          }
          break;
        case KM_BIGNUM:
        case KM_BYTES:
          if (data[i]->value->is_blob()) {
            param_set_.params[i] =
                keymaster_param_blob(tag, data[i]->value->get_blob().data(),
                                     data[i]->value->get_blob().size());
          } else {
            param_set_.params[i].tag = KM_TAG_INVALID;
          }
          break;
        case KM_INVALID:
        default:
          param_set_.params[i].tag = KM_TAG_INVALID;
          // just skip
          break;
      }
    }
  }

  KmParamSet(KmParamSet&& other)
      : param_set_{other.param_set_.params, other.param_set_.length} {
    other.param_set_.length = 0;
    other.param_set_.params = nullptr;
  }
  KmParamSet(const KmParamSet&) = delete;
  KmParamSet& operator=(const KmParamSet&) = delete;

  ~KmParamSet() { delete[] param_set_.params; }

  inline const keymaster_key_param_set_t& param_set() const {
    return param_set_;
  }

 private:
  keymaster_key_param_set_t param_set_;
};

}  // namespace

std::vector<uint8_t> authToken2AidlVec(
    const arc::mojom::keymint::HardwareAuthToken& token) {
  static_assert(
      1 /* version size */ + sizeof(token.challenge) + sizeof(token.user_id) +
              sizeof(token.authenticator_id) +
              sizeof(token.authenticator_type) + sizeof(*token.timestamp) +
              32 /* HMAC size */
          == sizeof(hw_auth_token_t),
      "HardwareAuthToken content size does not match hw_auth_token_t size");

  std::vector<uint8_t> result;

  if (token.mac.size() != 32) {
    return result;
  }

  result.resize(sizeof(hw_auth_token_t));
  auto pos = result.begin();
  *pos++ = 0;  // Version byte
  pos = copy_bytes_to_iterator(token.challenge, pos);
  pos = copy_bytes_to_iterator(token.user_id, pos);
  pos = copy_bytes_to_iterator(token.authenticator_id, pos);
  pos = copy_bytes_to_iterator(
      ::keymaster::hton(static_cast<uint32_t>(token.authenticator_type)), pos);
  pos = copy_bytes_to_iterator(
      ::keymaster::hton(token.timestamp->milli_seconds), pos);
  pos = std::copy(token.mac.data(), token.mac.data() + token.mac.size(), pos);

  return result;
}

// TODO(b/274723521) : Add more required ConvertEnum functions for KeyMint
// Server.
keymaster_tag_t ConvertEnum(arc::mojom::keymint::Tag tag) {
  return static_cast<keymaster_tag_t>(tag);
}

keymaster_key_format_t ConvertEnum(arc::mojom::keymint::KeyFormat key_format) {
  return static_cast<keymaster_key_format_t>(key_format);
}

keymaster_purpose_t ConvertEnum(arc::mojom::keymint::KeyPurpose key_purpose) {
  return static_cast<keymaster_purpose_t>(key_purpose);
}

std::vector<uint8_t> ConvertFromKeymasterMessage(const uint8_t* data,
                                                 const size_t size) {
  return std::vector<uint8_t>(data, data + size);
}

std::vector<std::vector<uint8_t>> ConvertFromKeymasterMessage(
    const keymaster_cert_chain_t& cert) {
  std::vector<std::vector<uint8_t>> out(cert.entry_count);
  for (size_t i = 0; i < cert.entry_count; ++i) {
    const auto& entry = cert.entries[i];
    out[i] = ConvertFromKeymasterMessage(entry.data, entry.data_length);
  }
  return out;
}

std::vector<arc::mojom::keymint::KeyParameterPtr> ConvertFromKeymasterMessage(
    const keymaster_key_param_set_t& param_set) {
  if (param_set.length == 0 || !param_set.params) {
    return std::vector<arc::mojom::keymint::KeyParameterPtr>();
  }

  std::vector<arc::mojom::keymint::KeyParameterPtr> out(param_set.length);
  const keymaster_key_param_t* params = param_set.params;

  for (size_t i = 0; i < param_set.length; ++i) {
    keymaster_tag_t tag = params[i].tag;
    arc::mojom::keymint::KeyParameterValuePtr param;
    switch (keymaster_tag_get_type(tag)) {
      case KM_ENUM:
      case KM_ENUM_REP:
        param = arc::mojom::keymint::KeyParameterValue::NewInteger(
            params[i].enumerated);
        break;
      case KM_UINT:
      case KM_UINT_REP:
        param = arc::mojom::keymint::KeyParameterValue::NewInteger(
            params[i].integer);
        break;
      case KM_ULONG:
      case KM_ULONG_REP:
        param = arc::mojom::keymint::KeyParameterValue::NewLongInteger(
            params[i].long_integer);
        break;
      case KM_DATE:
        param = arc::mojom::keymint::KeyParameterValue::NewDateTime(
            params[i].date_time);
        break;
      case KM_BOOL:
        param = arc::mojom::keymint::KeyParameterValue::NewBoolValue(
            params[i].boolean);
        break;
      case KM_BIGNUM:
      case KM_BYTES:
        param = arc::mojom::keymint::KeyParameterValue::NewBlob(
            ConvertFromKeymasterMessage(params[i].blob.data,
                                        params[i].blob.data_length));
        break;
      case KM_INVALID:
        tag = KM_TAG_INVALID;
        // just skip
        break;
    }

    out[i] = arc::mojom::keymint::KeyParameter::New(
        static_cast<arc::mojom::keymint::Tag>(tag), std::move(param));
  }

  return out;
}

void ConvertToKeymasterMessage(const std::vector<uint8_t>& data,
                               ::keymaster::Buffer* out) {
  out->Reinitialize(data.data(), data.size());
}

void ConvertToKeymasterMessage(const std::vector<uint8_t>& clientId,
                               const std::vector<uint8_t>& appData,
                               ::keymaster::AuthorizationSet* params) {
  params->Clear();
  if (!clientId.empty()) {
    params->push_back(::keymaster::TAG_APPLICATION_ID, clientId.data(),
                      clientId.size());
  }
  if (!appData.empty()) {
    params->push_back(::keymaster::TAG_APPLICATION_DATA, appData.data(),
                      appData.size());
  }
}

void ConvertToKeymasterMessage(
    const std::vector<arc::mojom::keymint::KeyParameterPtr>& data,
    ::keymaster::AuthorizationSet* out) {
  KmParamSet param_set(data);
  out->Reinitialize(param_set.param_set());
}

// Request Methods.
std::unique_ptr<::keymaster::GetKeyCharacteristicsRequest>
MakeGetKeyCharacteristicsRequest(
    const ::arc::mojom::keymint::GetKeyCharacteristicsRequestPtr& value,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::GetKeyCharacteristicsRequest>(
      keymint_message_version);
  out->SetKeyMaterial(value->key_blob.data(), value->key_blob.size());
  ConvertToKeymasterMessage(value->app_id, value->app_data,
                            &out->additional_params);
  return out;
}

std::unique_ptr<::keymaster::GenerateKeyRequest> MakeGenerateKeyRequest(
    const std::vector<arc::mojom::keymint::KeyParameterPtr>& data,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::GenerateKeyRequest>(
      keymint_message_version);
  ConvertToKeymasterMessage(data, &out->key_description);
  return out;
}

std::unique_ptr<::keymaster::ImportKeyRequest> MakeImportKeyRequest(
    const arc::mojom::keymint::ImportKeyRequestPtr& request,
    const int32_t keymint_message_version) {
  auto out =
      std::make_unique<::keymaster::ImportKeyRequest>(keymint_message_version);
  ConvertToKeymasterMessage(request->key_params, &out->key_description);

  out->key_format = ConvertEnum(request->key_format);
  out->key_data = ::keymaster::KeymasterKeyBlob(request->key_data.data(),
                                                request->key_data.size());

  // TODO(b/289173356): Add Attestation Key in MakeImportKeyRequest.
  return out;
}

std::unique_ptr<::keymaster::ImportWrappedKeyRequest>
MakeImportWrappedKeyRequest(
    const arc::mojom::keymint::ImportWrappedKeyRequestPtr& request,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::ImportWrappedKeyRequest>(
      keymint_message_version);

  out->SetWrappedMaterial(request->wrapped_key_data.data(),
                          request->wrapped_key_data.size());
  out->SetWrappingMaterial(request->wrapping_key_blob.data(),
                           request->wrapping_key_blob.size());
  out->SetMaskingKeyMaterial(request->masking_key.data(),
                             request->masking_key.size());
  ConvertToKeymasterMessage(request->unwrapping_params,
                            &out->additional_params);
  out->password_sid = request->password_sid;
  out->biometric_sid = request->biometric_sid;
  return out;
}

std::unique_ptr<::keymaster::UpgradeKeyRequest> MakeUpgradeKeyRequest(
    const arc::mojom::keymint::UpgradeKeyRequestPtr& request,
    const int32_t keymint_message_version) {
  auto out =
      std::make_unique<::keymaster::UpgradeKeyRequest>(keymint_message_version);
  ConvertToKeymasterMessage(request->upgrade_params, &out->upgrade_params);
  out->SetKeyMaterial(request->key_blob_to_upgrade.data(),
                      request->key_blob_to_upgrade.size());
  return out;
}

std::unique_ptr<::keymaster::UpdateOperationRequest> MakeUpdateOperationRequest(
    const arc::mojom::keymint::UpdateRequestPtr& request,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::UpdateOperationRequest>(
      keymint_message_version);

  out->op_handle = request->op_handle;
  ConvertToKeymasterMessage(request->input, &out->input);

  std::vector<arc::mojom::keymint::KeyParameterPtr> key_param_array;
  // UpdateOperationRequest also carries TimeStampTokenPtr, which is
  // unused yet and hence not converted. However, if it is used
  // in future by the reference implementation and the AIDL interface,
  // it will be added here.
  if (request->auto_token) {
    auto tokenAsVec(authToken2AidlVec(*request->auto_token));

    auto key_param_ptr = arc::mojom::keymint::KeyParameter::New(
        static_cast<arc::mojom::keymint::Tag>(KM_TAG_AUTH_TOKEN),
        arc::mojom::keymint::KeyParameterValue::NewBlob(std::move(tokenAsVec)));

    key_param_array.push_back(std::move(key_param_ptr));
  }
  ConvertToKeymasterMessage(std::move(key_param_array),
                            &out->additional_params);
  return out;
}

std::unique_ptr<::keymaster::BeginOperationRequest> MakeBeginOperationRequest(
    const arc::mojom::keymint::BeginRequestPtr& request,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::BeginOperationRequest>(
      keymint_message_version);
  out->purpose = ConvertEnum(request->key_purpose);
  out->SetKeyMaterial(request->key_blob.data(), request->key_blob.size());

  if (request->auth_token) {
    auto tokenAsVec(authToken2AidlVec(*request->auth_token));
    auto key_param_ptr = arc::mojom::keymint::KeyParameter::New(
        static_cast<arc::mojom::keymint::Tag>(KM_TAG_AUTH_TOKEN),
        arc::mojom::keymint::KeyParameterValue::NewBlob(std::move(tokenAsVec)));
    request->params.push_back(std::move(key_param_ptr));
  }
  ConvertToKeymasterMessage(request->params, &out->additional_params);
  return out;
}

std::unique_ptr<::keymaster::DeviceLockedRequest> MakeDeviceLockedRequest(
    bool password_only,
    const arc::mojom::keymint::TimeStampTokenPtr& timestamp_token,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::DeviceLockedRequest>(
      keymint_message_version);

  out->passwordOnly = password_only;
  if (timestamp_token) {
    out->token.challenge = timestamp_token->challenge;
    out->token.mac = {timestamp_token->mac.data(), timestamp_token->mac.size()};

    if (!timestamp_token->timestamp) {
      LOG(ERROR) << "Timestamp token should have a valid timestamp.";
      return out;
    }
    out->token.timestamp = timestamp_token->timestamp->milli_seconds;
  }
  return out;
}

// Mojo Result Methods.
std::optional<std::vector<arc::mojom::keymint::KeyCharacteristicsPtr>>
MakeGetKeyCharacteristicsResult(
    const ::keymaster::GetKeyCharacteristicsResponse& km_response,
    uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::nullopt;
  }

  // Enforced response corresponds to Trusted Execution
  // Environment(TEE) security level.
  auto teeChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::TRUSTED_ENVIRONMENT,
      ConvertFromKeymasterMessage(km_response.enforced));
  // Unenforced response corresponds to Software security level.
  auto softwareChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::SOFTWARE,
      ConvertFromKeymasterMessage(km_response.unenforced));

  std::vector<arc::mojom::keymint::KeyCharacteristicsPtr> output;
  output.push_back(std::move(teeChars));
  output.push_back(std::move(softwareChars));
  return output;
}

std::optional<arc::mojom::keymint::KeyCreationResultPtr> MakeGenerateKeyResult(
    const ::keymaster::GenerateKeyResponse& km_response, uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::nullopt;
  }

  // Create the Key Blob.
  auto key_blob =
      ConvertFromKeymasterMessage(km_response.key_blob.key_material,
                                  km_response.key_blob.key_material_size);

  // Create the Key Characteristics Array.
  // Enforced response corresponds to Trusted Execution
  // Environment(TEE) security level.
  auto teeChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::TRUSTED_ENVIRONMENT,
      ConvertFromKeymasterMessage(km_response.enforced));
  // Unenforced response corresponds to Software security level.
  auto softwareChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::SOFTWARE,
      ConvertFromKeymasterMessage(km_response.unenforced));
  std::vector<arc::mojom::keymint::KeyCharacteristicsPtr> key_chars_array;
  key_chars_array.push_back(std::move(teeChars));
  key_chars_array.push_back(std::move(softwareChars));

  // Create the Certificate Array.
  // TODO(b/286944450): Add certificates for Attestation.
  auto cert = arc::mojom::keymint::Certificate::New();
  std::vector<arc::mojom::keymint::CertificatePtr> cert_array;
  cert_array.push_back(std::move(cert));

  return arc::mojom::keymint::KeyCreationResult::New(
      std::move(key_blob), std::move(key_chars_array), std::move(cert_array));
}

std::optional<arc::mojom::keymint::KeyCreationResultPtr> MakeImportKeyResult(
    const ::keymaster::ImportKeyResponse& km_response, uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::nullopt;
  }

  // Create the Key Blob.
  auto key_blob =
      ConvertFromKeymasterMessage(km_response.key_blob.key_material,
                                  km_response.key_blob.key_material_size);

  // Create the Key Characteristics Array.
  // Enforced response corresponds to Trusted Execution
  // Environment(TEE) security level.
  auto teeChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::TRUSTED_ENVIRONMENT,
      ConvertFromKeymasterMessage(km_response.enforced));
  // Unenforced response corresponds to Software security level.
  auto softwareChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::SOFTWARE,
      ConvertFromKeymasterMessage(km_response.unenforced));
  std::vector<arc::mojom::keymint::KeyCharacteristicsPtr> key_chars_array;
  key_chars_array.push_back(std::move(teeChars));
  key_chars_array.push_back(std::move(softwareChars));

  // Create the Certificate Array.
  // TODO(b/286944450): Add certificates for Attestation.
  auto cert = arc::mojom::keymint::Certificate::New();
  std::vector<arc::mojom::keymint::CertificatePtr> cert_array;
  cert_array.push_back(std::move(cert));

  return arc::mojom::keymint::KeyCreationResult::New(
      std::move(key_blob), std::move(key_chars_array), std::move(cert_array));
}

std::optional<arc::mojom::keymint::KeyCreationResultPtr>
MakeImportWrappedKeyResult(
    const ::keymaster::ImportWrappedKeyResponse& km_response, uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::nullopt;
  }

  // Create the Key Blob.
  auto key_blob =
      ConvertFromKeymasterMessage(km_response.key_blob.key_material,
                                  km_response.key_blob.key_material_size);

  // Create the Key Characteristics Array.
  // Enforced response corresponds to Trusted Execution
  // Environment(TEE) security level.
  auto teeChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::TRUSTED_ENVIRONMENT,
      ConvertFromKeymasterMessage(km_response.enforced));
  // Unenforced response corresponds to Software security level.
  auto softwareChars = arc::mojom::keymint::KeyCharacteristics::New(
      arc::mojom::keymint::SecurityLevel::SOFTWARE,
      ConvertFromKeymasterMessage(km_response.unenforced));
  std::vector<arc::mojom::keymint::KeyCharacteristicsPtr> key_chars_array;
  key_chars_array.push_back(std::move(teeChars));
  key_chars_array.push_back(std::move(softwareChars));

  // Create the Certificate Array.
  // TODO(b/286944450): Add certificates for Attestation.
  auto cert = arc::mojom::keymint::Certificate::New();
  std::vector<arc::mojom::keymint::CertificatePtr> cert_array;
  cert_array.push_back(std::move(cert));

  return arc::mojom::keymint::KeyCreationResult::New(
      std::move(key_blob), std::move(key_chars_array), std::move(cert_array));
}

std::vector<uint8_t> MakeUpgradeKeyResult(
    const ::keymaster::UpgradeKeyResponse& km_response, uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::vector<uint8_t>();
  }
  // Create the Key Blob.
  auto key_blob =
      ConvertFromKeymasterMessage(km_response.upgraded_key.key_material,
                                  km_response.upgraded_key.key_material_size);
  return key_blob;
}

std::vector<uint8_t> MakeUpdateResult(
    const ::keymaster::UpdateOperationResponse& km_response, uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::vector<uint8_t>();
  }
  // UpdateOperationResponse also carries a field - |input_consumed|,
  // which is used in keymint_server.cc file.
  // It also carries another field - |output_params|, which is a
  // part of |output| returned from here.
  return ConvertFromKeymasterMessage(km_response.output.begin(),
                                     km_response.output.available_read());
}

std::optional<arc::mojom::keymint::BeginResultPtr> MakeBeginResult(
    const ::keymaster::BeginOperationResponse& km_response, uint32_t& error) {
  error = km_response.error;
  if (km_response.error != KM_ERROR_OK) {
    return std::nullopt;
  }

  uint64_t challenge = km_response.op_handle;
  uint64_t op_handle = km_response.op_handle;

  return arc::mojom::keymint::BeginResult::New(
      std::move(challenge),
      ConvertFromKeymasterMessage(km_response.output_params),
      std::move(op_handle));
}

}  // namespace arc::keymint
