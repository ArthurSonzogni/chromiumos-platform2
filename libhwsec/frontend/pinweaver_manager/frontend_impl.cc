// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/pinweaver_manager/frontend_impl.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using CredentialTreeResult = LECredentialManagerFrontend::CredentialTreeResult;
using DelaySchedule = LECredentialManagerFrontend::DelaySchedule;
using PinWeaverEccPoint = Backend::PinWeaver::PinWeaverEccPoint;
using CheckCredentialReply = LECredentialManager::CheckCredentialReply;
using StartBiometricsAuthReply = LECredentialManager::StartBiometricsAuthReply;
using ResetType = LECredentialManager::ResetType;

StatusOr<bool> LECredentialManagerFrontendImpl::IsEnabled() const {
  return middleware_.CallSync<&Backend::PinWeaver::IsEnabled>();
}

StatusOr<uint8_t> LECredentialManagerFrontendImpl::GetVersion() const {
  return middleware_.CallSync<&Backend::PinWeaver::GetVersion>();
}

StatusOr<uint64_t> LECredentialManagerFrontendImpl::InsertCredential(
    const std::vector<OperationPolicySetting>& policies,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule,
    std::optional<uint32_t> expiration_delay) const {
  return middleware_.CallSync<&Backend::LECredentialManager::InsertCredential>(
      policies, le_secret, he_secret, reset_secret, delay_schedule,
      expiration_delay);
}

StatusOr<CheckCredentialReply> LECredentialManagerFrontendImpl::CheckCredential(
    const uint64_t label, const brillo::SecureBlob& le_secret) const {
  return middleware_.CallSync<&Backend::LECredentialManager::CheckCredential>(
      label, le_secret);
}

Status LECredentialManagerFrontendImpl::RemoveCredential(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::LECredentialManager::RemoveCredential>(
      label);
}

Status LECredentialManagerFrontendImpl::ResetCredential(
    const uint64_t label,
    const brillo::SecureBlob& reset_secret,
    ResetType reset_type) const {
  return middleware_.CallSync<&Backend::LECredentialManager::ResetCredential>(
      label, reset_secret, reset_type);
}

StatusOr<uint32_t> LECredentialManagerFrontendImpl::GetWrongAuthAttempts(
    const uint64_t label) const {
  return middleware_
      .CallSync<&Backend::LECredentialManager::GetWrongAuthAttempts>(label);
}

StatusOr<DelaySchedule> LECredentialManagerFrontendImpl::GetDelaySchedule(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::LECredentialManager::GetDelaySchedule>(
      label);
}

StatusOr<uint32_t> LECredentialManagerFrontendImpl::GetDelayInSeconds(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::LECredentialManager::GetDelayInSeconds>(
      label);
}

StatusOr<std::optional<uint32_t>>
LECredentialManagerFrontendImpl::GetExpirationInSeconds(
    const uint64_t label) const {
  return middleware_
      .CallSync<&Backend::LECredentialManager::GetExpirationInSeconds>(label);
}

StatusOr<PinWeaverEccPoint> LECredentialManagerFrontendImpl::GeneratePk(
    uint8_t auth_channel, const PinWeaverEccPoint& client_public_key) const {
  return middleware_.CallSync<&Backend::PinWeaver::GeneratePk>(
      auth_channel, client_public_key);
}

StatusOr<uint64_t> LECredentialManagerFrontendImpl::InsertRateLimiter(
    uint8_t auth_channel,
    const std::vector<OperationPolicySetting>& policies,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule,
    std::optional<uint32_t> expiration_delay) const {
  return middleware_.CallSync<&Backend::LECredentialManager::InsertRateLimiter>(
      auth_channel, policies, reset_secret, delay_schedule, expiration_delay);
}

StatusOr<StartBiometricsAuthReply>
LECredentialManagerFrontendImpl::StartBiometricsAuth(
    uint8_t auth_channel,
    const uint64_t label,
    const brillo::Blob& client_nonce) const {
  return middleware_
      .CallSync<&Backend::LECredentialManager::StartBiometricsAuth>(
          auth_channel, label, client_nonce);
}

Status LECredentialManagerFrontendImpl::BlockGeneratePk() const {
  return middleware_.CallSync<&Backend::PinWeaver::BlockGeneratePk>();
}

}  // namespace hwsec
