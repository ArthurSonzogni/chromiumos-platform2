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
          param_set_.params[i] = ConvertEnum(data[i]);
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

arc::mojom::keymint::Tag ConvertKeymasterTag(keymaster_tag_t tag) {
  return static_cast<arc::mojom::keymint::Tag>(tag);
}

keymaster_key_format_t ConvertEnum(arc::mojom::keymint::KeyFormat key_format) {
  return static_cast<keymaster_key_format_t>(key_format);
}

keymaster_purpose_t ConvertEnum(arc::mojom::keymint::KeyPurpose key_purpose) {
  return static_cast<keymaster_purpose_t>(key_purpose);
}

keymaster_key_param_t kInvalidKeyParam{.tag = KM_TAG_INVALID, .integer = 0};

keymaster_key_param_t ConvertEnum(
    const arc::mojom::keymint::KeyParameterPtr& param) {
  if (param.is_null() || param->value.is_null()) {
    return kInvalidKeyParam;
  }

  keymaster_tag_t tag = ConvertEnum(param->tag);
  switch (tag) {
    case KM_TAG_PURPOSE:
      if (param->value->is_key_purpose() &&
          param->value->get_key_purpose() !=
              arc::mojom::keymint::KeyPurpose::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_key_purpose()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_ALGORITHM:
      if (param->value->is_algorithm() &&
          param->value->get_algorithm() !=
              arc::mojom::keymint::Algorithm::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_algorithm()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_BLOCK_MODE:
      if (param->value->is_block_mode() &&
          param->value->get_block_mode() !=
              arc::mojom::keymint::BlockMode::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_block_mode()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_DIGEST:
    case KM_TAG_RSA_OAEP_MGF_DIGEST:
      if (param->value->is_digest() &&
          param->value->get_digest() != arc::mojom::keymint::Digest::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_digest()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_PADDING:
      if (param->value->is_padding_mode() &&
          param->value->get_padding_mode() !=
              arc::mojom::keymint::PaddingMode::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_padding_mode()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_EC_CURVE:
      if (param->value->is_ec_curve() &&
          param->value->get_ec_curve() !=
              arc::mojom::keymint::EcCurve::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_ec_curve()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_USER_AUTH_TYPE:
      if (param->value->is_hardware_authenticator_type() &&
          param->value->get_hardware_authenticator_type() !=
              arc::mojom::keymint::HardwareAuthenticatorType::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(
                     param->value->get_hardware_authenticator_type()));
      } else {
        return kInvalidKeyParam;
      }
      break;

    case KM_TAG_ORIGIN:
      if (param->value->is_origin() &&
          param->value->get_origin() !=
              arc::mojom::keymint::KeyOrigin::UNKNOWN) {
        return keymaster_param_enum(
            tag, static_cast<uint32_t>(param->value->get_origin()));
      } else {
        return kInvalidKeyParam;
      }
      break;
    // The 2 Cases below are unused.
    case KM_TAG_BLOB_USAGE_REQUIREMENTS:
    case KM_TAG_KDF:

    default:
      CHECK(false) << "Unknown or unused enum tag: Something is broken";
      LOG(ERROR) << "Unknown or unused enum tag: " << tag;
      return kInvalidKeyParam;
  }
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

arc::mojom::keymint::KeyParameterValuePtr ConvertEnumParamFromKeymasterMessage(
    const keymaster_key_param_t& param) {
  keymaster_tag_t tag = param.tag;
  keymaster_tag_type_t tag_type = keymaster_tag_get_type(tag);

  arc::mojom::keymint::KeyParameterValuePtr out;
  if (tag_type != KM_ENUM && tag_type != KM_ENUM_REP) {
    LOG(ERROR) << "Mismatched Tag type received. Expected ENUM or ENUM_REP";
    return arc::mojom::keymint::KeyParameterValue::NewInvalid(
        static_cast<uint32_t>(param.enumerated));
  }

  switch (tag) {
    case KM_TAG_PURPOSE:
      out = arc::mojom::keymint::KeyParameterValue::NewKeyPurpose(
          static_cast<arc::mojom::keymint::KeyPurpose>(param.enumerated));
      break;
    case KM_TAG_ALGORITHM:
      out = arc::mojom::keymint::KeyParameterValue::NewAlgorithm(
          static_cast<arc::mojom::keymint::Algorithm>(param.enumerated));
      break;
    case KM_TAG_BLOCK_MODE:
      out = arc::mojom::keymint::KeyParameterValue::NewBlockMode(
          static_cast<arc::mojom::keymint::BlockMode>(param.enumerated));
      break;
    case KM_TAG_DIGEST:
    case KM_TAG_RSA_OAEP_MGF_DIGEST:
      out = arc::mojom::keymint::KeyParameterValue::NewDigest(
          static_cast<arc::mojom::keymint::Digest>(param.enumerated));
      break;
    case KM_TAG_PADDING:
      out = arc::mojom::keymint::KeyParameterValue::NewPaddingMode(
          static_cast<arc::mojom::keymint::PaddingMode>(param.enumerated));
      break;
    case KM_TAG_EC_CURVE:
      out = arc::mojom::keymint::KeyParameterValue::NewEcCurve(
          static_cast<arc::mojom::keymint::EcCurve>(param.enumerated));
      break;
    case KM_TAG_USER_AUTH_TYPE:
      out =
          arc::mojom::keymint::KeyParameterValue::NewHardwareAuthenticatorType(
              static_cast<arc::mojom::keymint::HardwareAuthenticatorType>(
                  param.enumerated));
      break;
    case KM_TAG_ORIGIN:
      out = arc::mojom::keymint::KeyParameterValue::NewOrigin(
          static_cast<arc::mojom::keymint::KeyOrigin>(param.enumerated));
      break;

    // The 2 Cases below are unused.
    case KM_TAG_BLOB_USAGE_REQUIREMENTS:
    case KM_TAG_KDF:

    default:
      CHECK(false) << "Unknown or unused enum tag: Something is broken";
      LOG(ERROR) << "Unknown or unused enum tag: " << tag;
      out = arc::mojom::keymint::KeyParameterValue::NewInvalid(
          static_cast<uint32_t>(param.enumerated));
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
        param = ConvertEnumParamFromKeymasterMessage(params[i]);
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
  if (request->auth_token) {
    auto tokenAsVec(authToken2AidlVec(*request->auth_token));

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

std::unique_ptr<::keymaster::FinishOperationRequest> MakeFinishOperationRequest(
    const arc::mojom::keymint::FinishRequestPtr& request,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::FinishOperationRequest>(
      keymint_message_version);

  if (request.is_null()) {
    LOG(ERROR) << "KeyMint Error: Finish Operation Request is null";
    return out;
  }

  out->op_handle = request->op_handle;
  if (request->input.has_value()) {
    ConvertToKeymasterMessage(request->input.value(), &out->input);
  }
  if (request->signature.has_value()) {
    ConvertToKeymasterMessage(request->signature.value(), &out->signature);
  }
  std::vector<arc::mojom::keymint::KeyParameterPtr> key_param_array;
  if (request->auth_token) {
    auto tokenAsVec(authToken2AidlVec(*request->auth_token));

    auto key_param_ptr = arc::mojom::keymint::KeyParameter::New(
        static_cast<arc::mojom::keymint::Tag>(KM_TAG_AUTH_TOKEN),
        arc::mojom::keymint::KeyParameterValue::NewBlob(std::move(tokenAsVec)));

    key_param_array.push_back(std::move(key_param_ptr));
  }
  // TimeStamp Token and Confirmation Token are not used
  // here since they are not passed from the AIDL.
  // If they are added in future, they will be converted here.
  ConvertToKeymasterMessage(std::move(key_param_array),
                            &out->additional_params);
  return out;
}

std::unique_ptr<::keymaster::ComputeSharedHmacRequest>
MakeComputeSharedSecretRequest(
    const std::vector<arc::mojom::keymint::SharedSecretParametersPtr>& request,
    const int32_t keymint_message_version) {
  auto out = std::make_unique<::keymaster::ComputeSharedHmacRequest>(
      keymint_message_version);

  // Allocate memory for HmacSharingParametersArray.
  out->params_array.params_array =
      new (std::nothrow)::keymaster::HmacSharingParameters[request.size()];
  if (out->params_array.params_array == nullptr) {
    LOG(ERROR)
        << "KeyMint Error: Null Pointer received for ComputeSharedHmacRequest";
    return out;
  }
  out->params_array.num_params = request.size();

  // Transform each shared secret's nonce and seed to Keymaster request.
  for (size_t i = 0; i < request.size(); ++i) {
    out->params_array.params_array[i].seed = {request[i]->seed.data(),
                                              request[i]->seed.size()};

    // Only copy memory if the nonce size is same for the Keymaster request
    // and Shared secret parameter.
    if (sizeof(out->params_array.params_array[i].nonce) !=
        request[i]->nonce.size()) {
      LOG(ERROR)
          << "KeyMint Error: Different Nonce Size for Shared Secret Parameter";
      return out;
    }
    std::copy(request[i]->nonce.data(),
              request[i]->nonce.data() + request[i]->nonce.size(),
              out->params_array.params_array[i].nonce);
  }

  return out;
}

// Mojo Result Methods.
arc::mojom::keymint::KeyCharacteristicsArrayOrErrorPtr
MakeGetKeyCharacteristicsResult(
    const ::keymaster::GetKeyCharacteristicsResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::KeyCharacteristicsArrayOrError::NewError(
        km_response.error);
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

  return arc::mojom::keymint::KeyCharacteristicsArrayOrError::
      NewKeyCharacteristics(std::move(output));
}

arc::mojom::keymint::KeyCreationResultOrErrorPtr MakeGenerateKeyResult(
    const ::keymaster::GenerateKeyResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::KeyCreationResultOrError::NewError(
        km_response.error);
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
  std::vector<arc::mojom::keymint::CertificatePtr> cert_array;

  auto key_result = arc::mojom::keymint::KeyCreationResult::New(
      std::move(key_blob), std::move(key_chars_array), std::move(cert_array));

  return arc::mojom::keymint::KeyCreationResultOrError::NewKeyCreationResult(
      std::move(key_result));
}

arc::mojom::keymint::KeyCreationResultOrErrorPtr MakeImportKeyResult(
    const ::keymaster::ImportKeyResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::KeyCreationResultOrError::NewError(
        km_response.error);
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
  std::vector<arc::mojom::keymint::CertificatePtr> cert_array;

  auto key_creation_result = arc::mojom::keymint::KeyCreationResult::New(
      std::move(key_blob), std::move(key_chars_array), std::move(cert_array));

  return arc::mojom::keymint::KeyCreationResultOrError::NewKeyCreationResult(
      std::move(key_creation_result));
}

arc::mojom::keymint::KeyCreationResultOrErrorPtr MakeImportWrappedKeyResult(
    const ::keymaster::ImportWrappedKeyResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::KeyCreationResultOrError::NewError(
        km_response.error);
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
  std::vector<arc::mojom::keymint::CertificatePtr> cert_array;

  auto key_creation_result = arc::mojom::keymint::KeyCreationResult::New(
      std::move(key_blob), std::move(key_chars_array), std::move(cert_array));

  return arc::mojom::keymint::KeyCreationResultOrError::NewKeyCreationResult(
      std::move(key_creation_result));
}

arc::mojom::keymint::ByteArrayOrErrorPtr MakeUpgradeKeyResult(
    const ::keymaster::UpgradeKeyResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::ByteArrayOrError::NewError(km_response.error);
  }
  // Create the Key Blob.
  auto upgraded_key_blob =
      ConvertFromKeymasterMessage(km_response.upgraded_key.key_material,
                                  km_response.upgraded_key.key_material_size);

  return arc::mojom::keymint::ByteArrayOrError::NewOutput(
      std::move(upgraded_key_blob));
}

arc::mojom::keymint::ByteArrayOrErrorPtr MakeUpdateResult(
    const ::keymaster::UpdateOperationResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::ByteArrayOrError::NewError(km_response.error);
  }
  // UpdateOperationResponse also carries a field - |input_consumed|,
  // which is used in keymint_server.cc file.
  // It also carries another field - |output_params|, which is a
  // part of |output| returned from here.
  auto output = ConvertFromKeymasterMessage(
      km_response.output.begin(), km_response.output.available_read());

  return arc::mojom::keymint::ByteArrayOrError::NewOutput(std::move(output));
}

arc::mojom::keymint::BeginResultOrErrorPtr MakeBeginResult(
    const ::keymaster::BeginOperationResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::BeginResultOrError::NewError(km_response.error);
  }

  uint64_t challenge = km_response.op_handle;
  uint64_t op_handle = km_response.op_handle;

  auto begin_result = arc::mojom::keymint::BeginResult::New(
      std::move(challenge),
      ConvertFromKeymasterMessage(km_response.output_params),
      std::move(op_handle));

  return arc::mojom::keymint::BeginResultOrError::NewBeginResult(
      std::move(begin_result));
}

arc::mojom::keymint::ByteArrayOrErrorPtr MakeFinishResult(
    const ::keymaster::FinishOperationResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::ByteArrayOrError::NewError(km_response.error);
  }
  auto output = ConvertFromKeymasterMessage(
      km_response.output.begin(), km_response.output.available_read());

  return arc::mojom::keymint::ByteArrayOrError::NewOutput(std::move(output));
}

arc::mojom::keymint::SharedSecretParametersOrErrorPtr
MakeGetSharedSecretParametersResult(
    const ::keymaster::GetHmacSharingParametersResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::SharedSecretParametersOrError::NewError(
        km_response.error);
  }

  // Create seed and nonce.
  std::vector<uint8_t> seed = ConvertFromKeymasterMessage(
      km_response.params.seed.begin(), km_response.params.seed.size());
  std::vector<uint8_t> nonce(std::begin(km_response.params.nonce),
                             std::end(km_response.params.nonce));

  auto params = arc::mojom::keymint::SharedSecretParameters::New(
      std::move(seed), std::move(nonce));

  return arc::mojom::keymint::SharedSecretParametersOrError::
      NewSecretParameters(std::move(params));
}

arc::mojom::keymint::ByteArrayOrErrorPtr MakeComputeSharedSecretResult(
    const ::keymaster::ComputeSharedHmacResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::ByteArrayOrError::NewError(km_response.error);
  }

  std::vector<uint8_t> result(
      km_response.sharing_check.data,
      km_response.sharing_check.data + km_response.sharing_check.data_length);
  return arc::mojom::keymint::ByteArrayOrError::NewOutput(std::move(result));
}

arc::mojom::keymint::TimeStampTokenOrErrorPtr MakeGenerateTimeStampTokenResult(
    const ::keymaster::GenerateTimestampTokenResponse& km_response) {
  if (km_response.error != KM_ERROR_OK) {
    return arc::mojom::keymint::TimeStampTokenOrError::NewError(
        km_response.error);
  }

  uint64_t challenge = km_response.token.challenge;

  auto time_stamp = arc::mojom::keymint::Timestamp::New(
      base::strict_cast<uint64_t>(km_response.token.timestamp));

  std::vector<uint8_t> mac(
      km_response.token.mac.data,
      km_response.token.mac.data + km_response.token.mac.data_length);

  auto time_stamp_token = arc::mojom::keymint::TimeStampToken::New(
      std::move(challenge), std::move(time_stamp), std::move(mac));

  return arc::mojom::keymint::TimeStampTokenOrError::NewTimestampToken(
      std::move(time_stamp_token));
}

}  // namespace arc::keymint
