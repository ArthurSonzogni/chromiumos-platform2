// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONVERSION_H_
#define ARC_KEYMINT_CONVERSION_H_

#include <algorithm>
#include <memory>
#include <vector>

#include <keymaster/android_keymaster.h>
#include <mojo/keymint.mojom.h>

namespace arc::keymint {

// Convenience helper methods.
keymaster_key_format_t ConvertEnum(arc::mojom::keymint::KeyFormat key_format);

keymaster_tag_t ConvertEnum(arc::mojom::keymint::Tag tag);

keymaster_purpose_t ConvertEnum(arc::mojom::keymint::KeyPurpose key_purpose);

template <typename T, typename OutIter>
inline OutIter copy_bytes_to_iterator(const T& value, OutIter dest) {
  const uint8_t* value_ptr = reinterpret_cast<const uint8_t*>(&value);
  return std::copy(value_ptr, value_ptr + sizeof(value), dest);
}

std::vector<uint8_t> authToken2AidlVec(
    const arc::mojom::keymint::HardwareAuthToken& token);

std::vector<uint8_t> ConvertFromKeymasterMessage(const uint8_t* data,
                                                 const size_t size);

std::vector<std::vector<uint8_t>> ConvertFromKeymasterMessage(
    const keymaster_cert_chain_t& cert);

std::vector<::arc::mojom::keymint::KeyParameterPtr> ConvertFromKeymasterMessage(
    const keymaster_key_param_set_t& set);

void ConvertToKeymasterMessage(const std::vector<uint8_t>& data,
                               ::keymaster::Buffer* out);

void ConvertToKeymasterMessage(const std::vector<uint8_t>& clientId,
                               const std::vector<uint8_t>& appData,
                               ::keymaster::AuthorizationSet* params);

void ConvertToKeymasterMessage(
    const std::vector<arc::mojom::keymint::KeyParameterPtr>& data,
    ::keymaster::AuthorizationSet* out);

// Request Methods.
std::unique_ptr<::keymaster::GetKeyCharacteristicsRequest>
MakeGetKeyCharacteristicsRequest(
    const ::arc::mojom::keymint::GetKeyCharacteristicsRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::GenerateKeyRequest> MakeGenerateKeyRequest(
    const std::vector<arc::mojom::keymint::KeyParameterPtr>& data,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::ImportKeyRequest> MakeImportKeyRequest(
    const arc::mojom::keymint::ImportKeyRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::ImportWrappedKeyRequest>
MakeImportWrappedKeyRequest(
    const arc::mojom::keymint::ImportWrappedKeyRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::UpgradeKeyRequest> MakeUpgradeKeyRequest(
    const arc::mojom::keymint::UpgradeKeyRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::UpdateOperationRequest> MakeUpdateOperationRequest(
    const arc::mojom::keymint::UpdateRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::BeginOperationRequest> MakeBeginOperationRequest(
    const arc::mojom::keymint::BeginRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::DeviceLockedRequest> MakeDeviceLockedRequest(
    bool password_only,
    const arc::mojom::keymint::TimeStampTokenPtr& timestamp_token,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::FinishOperationRequest> MakeFinishOperationRequest(
    const arc::mojom::keymint::FinishRequestPtr& request,
    const int32_t keymint_message_version);

std::unique_ptr<::keymaster::ComputeSharedHmacRequest>
MakeComputeSharedSecretRequest(
    const std::vector<arc::mojom::keymint::SharedSecretParametersPtr>& request,
    const int32_t keymint_message_version);

// Mojo Result Methods.
arc::mojom::keymint::KeyCharacteristicsArrayOrErrorPtr
MakeGetKeyCharacteristicsResult(
    const ::keymaster::GetKeyCharacteristicsResponse& km_response);

arc::mojom::keymint::KeyCreationResultOrErrorPtr MakeGenerateKeyResult(
    const ::keymaster::GenerateKeyResponse& km_response);

arc::mojom::keymint::KeyCreationResultOrErrorPtr MakeImportKeyResult(
    const ::keymaster::ImportKeyResponse& km_response);

arc::mojom::keymint::KeyCreationResultOrErrorPtr MakeImportWrappedKeyResult(
    const ::keymaster::ImportWrappedKeyResponse& km_response);

arc::mojom::keymint::ByteArrayOrErrorPtr MakeUpgradeKeyResult(
    const ::keymaster::UpgradeKeyResponse& km_response);

arc::mojom::keymint::ByteArrayOrErrorPtr MakeUpdateResult(
    const ::keymaster::UpdateOperationResponse& km_response);

arc::mojom::keymint::BeginResultOrErrorPtr MakeBeginResult(
    const ::keymaster::BeginOperationResponse& km_response);

arc::mojom::keymint::ByteArrayOrErrorPtr MakeFinishResult(
    const ::keymaster::FinishOperationResponse& km_response);

arc::mojom::keymint::SharedSecretParametersOrErrorPtr
MakeGetSharedSecretParametersResult(
    const ::keymaster::GetHmacSharingParametersResponse& km_response);

arc::mojom::keymint::ByteArrayOrErrorPtr MakeComputeSharedSecretResult(
    const ::keymaster::ComputeSharedHmacResponse& km_response);

arc::mojom::keymint::TimeStampTokenOrErrorPtr MakeGenerateTimeStampTokenResult(
    const ::keymaster::GenerateTimestampTokenResponse& km_response);
}  // namespace arc::keymint

#endif  // ARC_KEYMINT_CONVERSION_H_
