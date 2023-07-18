// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZERS_BACKEND_COMMAND_LIST_H_
#define LIBHWSEC_FUZZERS_BACKEND_COMMAND_LIST_H_

#include "libhwsec/backend/backend.h"

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

namespace hwsec {

template <auto... Commands>
class CommandList {
 public:
  inline constexpr static size_t size = sizeof...(Commands);

  template <size_t N>
  static constexpr auto Get() {
    static_assert(N < size);
    return Helper<N, Commands...>::Get();
  }

 private:
  template <size_t N, auto Head, auto... Tails>
  struct Helper {
    static constexpr auto Get() { return Helper<N - 1, Tails...>::Get(); }
  };

  template <auto Head, auto... Tails>
  struct Helper<0, Head, Tails...> {
    static constexpr auto Get() { return Head; }
  };
};

using FuzzCommandList =
    CommandList<&Backend::Attestation::Quote,
                &Backend::Attestation::IsQuoted,
                &Backend::Attestation::CreateCertifiedKey,
                &Backend::Attestation::CreateIdentity,
                &Backend::Config::ToOperationPolicy,
                &Backend::Config::SetCurrentUser,
                &Backend::Config::IsCurrentUserSet,
                &Backend::Config::GetCurrentBootMode,
                &Backend::DAMitigation::IsReady,
                &Backend::DAMitigation::GetStatus,
                &Backend::DAMitigation::Mitigate,
                &Backend::Deriving::Derive,
                &Backend::Deriving::SecureDerive,
                &Backend::Encryption::Encrypt,
                &Backend::Encryption::Decrypt,
                &Backend::KeyManagement::GetSupportedAlgo,
                &Backend::KeyManagement::IsSupported,
                &Backend::KeyManagement::CreateKey,
                &Backend::KeyManagement::LoadKey,
                &Backend::KeyManagement::GetPolicyEndorsementKey,
                &Backend::KeyManagement::GetPubkeyHash,
                &Backend::KeyManagement::Flush,
                &Backend::KeyManagement::ReloadIfPossible,
                &Backend::KeyManagement::SideLoadKey,
                &Backend::KeyManagement::GetKeyHandle,
                &Backend::KeyManagement::WrapRSAKey,
                &Backend::KeyManagement::WrapECCKey,
                &Backend::KeyManagement::GetRSAPublicInfo,
                &Backend::KeyManagement::GetECCPublicInfo,
                &Backend::PinWeaver::IsEnabled,
                &Backend::PinWeaver::GetVersion,
                &Backend::PinWeaver::Reset,
                &Backend::PinWeaver::InsertCredential,
                &Backend::PinWeaver::CheckCredential,
                &Backend::PinWeaver::RemoveCredential,
                &Backend::PinWeaver::ResetCredential,
                &Backend::PinWeaver::GetLog,
                &Backend::PinWeaver::ReplayLogOperation,
                &Backend::PinWeaver::GetWrongAuthAttempts,
                &Backend::PinWeaver::GetDelaySchedule,
                &Backend::PinWeaver::GetDelayInSeconds,
                &Backend::PinWeaver::GetExpirationInSeconds,
                &Backend::PinWeaver::GeneratePk,
                &Backend::PinWeaver::InsertRateLimiter,
                &Backend::PinWeaver::StartBiometricsAuth,
                &Backend::PinWeaver::BlockGeneratePk,
                &Backend::Random::RandomBlob,
                &Backend::Random::RandomSecureBlob,
                &Backend::RecoveryCrypto::GenerateKeyAuthValue,
                &Backend::RecoveryCrypto::EncryptEccPrivateKey,
                &Backend::RecoveryCrypto::GenerateDiffieHellmanSharedSecret,
                &Backend::RecoveryCrypto::GenerateRsaKeyPair,
                &Backend::RecoveryCrypto::SignRequestPayload,
                &Backend::RoData::IsReady,
                &Backend::RoData::Read,
                &Backend::RoData::Certify,
                &Backend::RoData::CertifyWithSize,
                &Backend::Sealing::IsSupported,
                &Backend::Sealing::Seal,
                &Backend::Sealing::PreloadSealedData,
                &Backend::Sealing::Unseal,
                &Backend::SessionManagement::FlushInvalidSessions,
                &Backend::SignatureSealing::Seal,
                &Backend::SignatureSealing::Challenge,
                &Backend::SignatureSealing::Unseal,
                &Backend::Signing::Sign,
                &Backend::Signing::RawSign,
                &Backend::Signing::Verify,
                &Backend::State::IsEnabled,
                &Backend::State::IsReady,
                &Backend::State::Prepare,
                &Backend::Storage::IsReady,
                &Backend::Storage::Prepare,
                &Backend::Storage::Load,
                &Backend::Storage::Store,
                &Backend::Storage::Lock,
                &Backend::Storage::Destroy,
                &Backend::U2f::IsEnabled,
                &Backend::U2f::GenerateUserPresenceOnly,
                &Backend::U2f::Generate,
                &Backend::U2f::SignUserPresenceOnly,
                &Backend::U2f::Sign,
                &Backend::U2f::CheckUserPresenceOnly,
                &Backend::U2f::Check,
                &Backend::U2f::G2fAttest,
                &Backend::U2f::GetG2fAttestData,
                &Backend::U2f::CorpAttest,
                &Backend::U2f::GetConfig,
                &Backend::Vendor::GetFamily,
                &Backend::Vendor::GetSpecLevel,
                &Backend::Vendor::GetManufacturer,
                &Backend::Vendor::GetTpmModel,
                &Backend::Vendor::GetFirmwareVersion,
                &Backend::Vendor::GetVendorSpecific,
                &Backend::Vendor::GetFingerprint,
                &Backend::Vendor::IsSrkRocaVulnerable,
                &Backend::Vendor::GetRsuDeviceId,
                &Backend::Vendor::GetIFXFieldUpgradeInfo,
                &Backend::Vendor::DeclareTpmFirmwareStable,
                &Backend::Vendor::GetRwVersion,
                &Backend::Vendor::SendRawCommand,
                &Backend::VersionAttestation::AttestVersion>;

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZERS_BACKEND_COMMAND_LIST_H_
