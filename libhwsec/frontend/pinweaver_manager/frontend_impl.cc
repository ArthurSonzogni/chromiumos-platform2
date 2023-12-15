// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/pinweaver_manager/frontend_impl.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using CredentialTreeResult = PinWeaverManagerFrontend::CredentialTreeResult;
using DelaySchedule = PinWeaverManagerFrontend::DelaySchedule;
using PinWeaverEccPoint = Backend::PinWeaver::PinWeaverEccPoint;
using CheckCredentialReply = PinWeaverManager::CheckCredentialReply;
using StartBiometricsAuthReply = PinWeaverManager::StartBiometricsAuthReply;
using ResetType = PinWeaverManager::ResetType;

StatusOr<bool> PinWeaverManagerFrontendImpl::IsEnabled() const {
  return middleware_.CallSync<&Backend::PinWeaver::IsEnabled>();
}

StatusOr<uint8_t> PinWeaverManagerFrontendImpl::GetVersion() const {
  return middleware_.CallSync<&Backend::PinWeaver::GetVersion>();
}

Status PinWeaverManagerFrontendImpl::Initialize() const {
  return middleware_.CallSync<&Backend::PinWeaverManager::StateIsReady>();
}

StatusOr<uint64_t> PinWeaverManagerFrontendImpl::InsertCredential(
    const std::vector<OperationPolicySetting>& policies,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule,
    std::optional<uint32_t> expiration_delay) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::InsertCredential>(
      policies, le_secret, he_secret, reset_secret, delay_schedule,
      expiration_delay);
}

void PinWeaverManagerFrontendImpl::InsertCredentialAsync(
    const std::vector<OperationPolicySetting>& policies,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule,
    std::optional<uint32_t> expiration_delay,
    InsertCredentialCallback callback) const {
  return middleware_.CallAsync<&Backend::PinWeaverManager::InsertCredential>(
      std::move(callback), policies, le_secret, he_secret, reset_secret,
      delay_schedule, expiration_delay);
}

StatusOr<CheckCredentialReply> PinWeaverManagerFrontendImpl::CheckCredential(
    const uint64_t label, const brillo::SecureBlob& le_secret) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::CheckCredential>(
      label, le_secret);
}

void PinWeaverManagerFrontendImpl::CheckCredentialAsync(
    const uint64_t label,
    const brillo::SecureBlob& le_secret,
    CheckCredentialCallback callback) const {
  return middleware_.CallAsync<&Backend::PinWeaverManager::CheckCredential>(
      std::move(callback), label, le_secret);
}

Status PinWeaverManagerFrontendImpl::RemoveCredential(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::RemoveCredential>(
      label);
}

void PinWeaverManagerFrontendImpl::RemoveCredentialAsync(
    const uint64_t label, RemoveCredentialCallback callback) const {
  return middleware_.CallAsync<&Backend::PinWeaverManager::RemoveCredential>(
      std::move(callback), label);
}

Status PinWeaverManagerFrontendImpl::ResetCredential(
    const uint64_t label,
    const brillo::SecureBlob& reset_secret,
    ResetType reset_type) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::ResetCredential>(
      label, reset_secret, reset_type);
}

void PinWeaverManagerFrontendImpl::ResetCredentialAsync(
    const uint64_t label,
    const brillo::SecureBlob& reset_secret,
    ResetType reset_type,
    ResetCredentialCallback callback) const {
  return middleware_.CallAsync<&Backend::PinWeaverManager::ResetCredential>(
      std::move(callback), label, reset_secret, reset_type);
}

StatusOr<uint32_t> PinWeaverManagerFrontendImpl::GetWrongAuthAttempts(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::GetWrongAuthAttempts>(
      label);
}

StatusOr<DelaySchedule> PinWeaverManagerFrontendImpl::GetDelaySchedule(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::GetDelaySchedule>(
      label);
}

StatusOr<uint32_t> PinWeaverManagerFrontendImpl::GetDelayInSeconds(
    const uint64_t label) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::GetDelayInSeconds>(
      label);
}

StatusOr<std::optional<uint32_t>>
PinWeaverManagerFrontendImpl::GetExpirationInSeconds(
    const uint64_t label) const {
  return middleware_
      .CallSync<&Backend::PinWeaverManager::GetExpirationInSeconds>(label);
}

StatusOr<PinWeaverEccPoint> PinWeaverManagerFrontendImpl::GeneratePk(
    AuthChannel auth_channel,
    const PinWeaverEccPoint& client_public_key) const {
  return middleware_.CallSync<&Backend::PinWeaver::GeneratePk>(
      auth_channel, client_public_key);
}

StatusOr<uint64_t> PinWeaverManagerFrontendImpl::InsertRateLimiter(
    AuthChannel auth_channel,
    const std::vector<OperationPolicySetting>& policies,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule,
    std::optional<uint32_t> expiration_delay) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::InsertRateLimiter>(
      auth_channel, policies, reset_secret, delay_schedule, expiration_delay);
}

void PinWeaverManagerFrontendImpl::InsertRateLimiterAsync(
    AuthChannel auth_channel,
    const std::vector<OperationPolicySetting>& policies,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule,
    std::optional<uint32_t> expiration_delay,
    InsertRateLimiterCallback callback) const {
  return middleware_.CallAsync<&Backend::PinWeaverManager::InsertRateLimiter>(
      std::move(callback), auth_channel, policies, reset_secret, delay_schedule,
      expiration_delay);
}

StatusOr<StartBiometricsAuthReply>
PinWeaverManagerFrontendImpl::StartBiometricsAuth(
    AuthChannel auth_channel,
    const uint64_t label,
    const brillo::Blob& client_nonce) const {
  return middleware_.CallSync<&Backend::PinWeaverManager::StartBiometricsAuth>(
      auth_channel, label, client_nonce);
}

void PinWeaverManagerFrontendImpl::StartBiometricsAuthAsync(
    AuthChannel auth_channel,
    const uint64_t label,
    const brillo::Blob& client_nonce,
    StartBiometricsAuthCallback callback) const {
  return middleware_.CallAsync<&Backend::PinWeaverManager::StartBiometricsAuth>(
      std::move(callback), auth_channel, label, client_nonce);
}

Status PinWeaverManagerFrontendImpl::BlockGeneratePk() const {
  return middleware_.CallSync<&Backend::PinWeaver::BlockGeneratePk>();
}

}  // namespace hwsec
