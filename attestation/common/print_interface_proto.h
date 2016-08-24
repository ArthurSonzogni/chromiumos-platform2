//
// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// THIS CODE IS GENERATED.

#ifndef ATTESTATION_COMMON_PRINT_INTERFACE_PROTO_H_
#define ATTESTATION_COMMON_PRINT_INTERFACE_PROTO_H_

#include <string>

#include "attestation/common/interface.pb.h"

namespace attestation {

std::string GetProtoDebugStringWithIndent(AttestationStatus value,
                                          int indent_size);
std::string GetProtoDebugString(AttestationStatus value);
std::string GetProtoDebugStringWithIndent(
    const CreateGoogleAttestedKeyRequest& value,
    int indent_size);
std::string GetProtoDebugString(const CreateGoogleAttestedKeyRequest& value);
std::string GetProtoDebugStringWithIndent(
    const CreateGoogleAttestedKeyReply& value,
    int indent_size);
std::string GetProtoDebugString(const CreateGoogleAttestedKeyReply& value);
std::string GetProtoDebugStringWithIndent(const GetKeyInfoRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetKeyInfoRequest& value);
std::string GetProtoDebugStringWithIndent(const GetKeyInfoReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetKeyInfoReply& value);
std::string GetProtoDebugStringWithIndent(
    const GetEndorsementInfoRequest& value,
    int indent_size);
std::string GetProtoDebugString(const GetEndorsementInfoRequest& value);
std::string GetProtoDebugStringWithIndent(const GetEndorsementInfoReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetEndorsementInfoReply& value);
std::string GetProtoDebugStringWithIndent(
    const GetAttestationKeyInfoRequest& value,
    int indent_size);
std::string GetProtoDebugString(const GetAttestationKeyInfoRequest& value);
std::string GetProtoDebugStringWithIndent(
    const GetAttestationKeyInfoReply& value,
    int indent_size);
std::string GetProtoDebugString(const GetAttestationKeyInfoReply& value);
std::string GetProtoDebugStringWithIndent(
    const ActivateAttestationKeyRequest& value,
    int indent_size);
std::string GetProtoDebugString(const ActivateAttestationKeyRequest& value);
std::string GetProtoDebugStringWithIndent(
    const ActivateAttestationKeyReply& value,
    int indent_size);
std::string GetProtoDebugString(const ActivateAttestationKeyReply& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertifiableKeyRequest& value,
    int indent_size);
std::string GetProtoDebugString(const CreateCertifiableKeyRequest& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertifiableKeyReply& value,
    int indent_size);
std::string GetProtoDebugString(const CreateCertifiableKeyReply& value);
std::string GetProtoDebugStringWithIndent(const DecryptRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const DecryptRequest& value);
std::string GetProtoDebugStringWithIndent(const DecryptReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const DecryptReply& value);
std::string GetProtoDebugStringWithIndent(const SignRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const SignRequest& value);
std::string GetProtoDebugStringWithIndent(const SignReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const SignReply& value);
std::string GetProtoDebugStringWithIndent(
    const RegisterKeyWithChapsTokenRequest& value,
    int indent_size);
std::string GetProtoDebugString(const RegisterKeyWithChapsTokenRequest& value);
std::string GetProtoDebugStringWithIndent(
    const RegisterKeyWithChapsTokenReply& value,
    int indent_size);
std::string GetProtoDebugString(const RegisterKeyWithChapsTokenReply& value);
std::string GetProtoDebugStringWithIndent(const GetStatusRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetStatusRequest& value);
std::string GetProtoDebugStringWithIndent(const GetStatusReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const GetStatusReply& value);
std::string GetProtoDebugStringWithIndent(const VerifyRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const VerifyRequest& value);
std::string GetProtoDebugStringWithIndent(const VerifyReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const VerifyReply& value);
std::string GetProtoDebugStringWithIndent(
    const CreateEnrollRequestRequest& value,
    int indent_size);
std::string GetProtoDebugString(const CreateEnrollRequestRequest& value);
std::string GetProtoDebugStringWithIndent(const CreateEnrollRequestReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const CreateEnrollRequestReply& value);
std::string GetProtoDebugStringWithIndent(const FinishEnrollRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const FinishEnrollRequest& value);
std::string GetProtoDebugStringWithIndent(const FinishEnrollReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const FinishEnrollReply& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertificateRequestRequest& value,
    int indent_size);
std::string GetProtoDebugString(const CreateCertificateRequestRequest& value);
std::string GetProtoDebugStringWithIndent(
    const CreateCertificateRequestReply& value,
    int indent_size);
std::string GetProtoDebugString(const CreateCertificateRequestReply& value);
std::string GetProtoDebugStringWithIndent(
    const FinishCertificateRequestRequest& value,
    int indent_size);
std::string GetProtoDebugString(const FinishCertificateRequestRequest& value);
std::string GetProtoDebugStringWithIndent(
    const FinishCertificateRequestReply& value,
    int indent_size);
std::string GetProtoDebugString(const FinishCertificateRequestReply& value);
std::string GetProtoDebugStringWithIndent(
    const SignEnterpriseChallengeRequest& value,
    int indent_size);
std::string GetProtoDebugString(const SignEnterpriseChallengeRequest& value);
std::string GetProtoDebugStringWithIndent(
    const SignEnterpriseChallengeReply& value,
    int indent_size);
std::string GetProtoDebugString(const SignEnterpriseChallengeReply& value);
std::string GetProtoDebugStringWithIndent(
    const SignSimpleChallengeRequest& value,
    int indent_size);
std::string GetProtoDebugString(const SignSimpleChallengeRequest& value);
std::string GetProtoDebugStringWithIndent(const SignSimpleChallengeReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const SignSimpleChallengeReply& value);
std::string GetProtoDebugStringWithIndent(const SetKeyPayloadRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const SetKeyPayloadRequest& value);
std::string GetProtoDebugStringWithIndent(const SetKeyPayloadReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const SetKeyPayloadReply& value);
std::string GetProtoDebugStringWithIndent(const DeleteKeysRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const DeleteKeysRequest& value);
std::string GetProtoDebugStringWithIndent(const DeleteKeysReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const DeleteKeysReply& value);
std::string GetProtoDebugStringWithIndent(const ResetIdentityRequest& value,
                                          int indent_size);
std::string GetProtoDebugString(const ResetIdentityRequest& value);
std::string GetProtoDebugStringWithIndent(const ResetIdentityReply& value,
                                          int indent_size);
std::string GetProtoDebugString(const ResetIdentityReply& value);

}  // namespace attestation

#endif  // ATTESTATION_COMMON_PRINT_INTERFACE_PROTO_H_
