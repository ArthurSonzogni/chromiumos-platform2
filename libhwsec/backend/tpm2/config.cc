// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/config.h"

#include <cstdint>
#include <string>
#include <vector>

#include <base/hash/sha1.h>
#include <crypto/sha2.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <openssl/sha.h>
#include <trunks/openssl_utility.h>
#include <trunks/tpm_utility.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

constexpr int kBootModePcr = 0;
constexpr int kDeviceModelPcr = 1;
constexpr int kCurrentUserPcr = USE_TPM_DYNAMIC ? 11 : 4;

constexpr DeviceConfig kSupportConfigs[] = {
    DeviceConfig::kBootMode,
    DeviceConfig::kDeviceModel,
    DeviceConfig::kCurrentUser,
};

StatusOr<int> DeviceConfigToPcr(DeviceConfig config) {
  switch (config) {
    case DeviceConfig::kBootMode:
      return kBootModePcr;
    case DeviceConfig::kDeviceModel:
      return kDeviceModelPcr;
    case DeviceConfig::kCurrentUser:
      return kCurrentUserPcr;
  }
  return MakeStatus<TPMError>("Unknown device config",
                              TPMRetryAction::kNoRetry);
}

Status AddToPolicySession(trunks::PolicySession& policy_session,
                          const ConfigTpm2::PcrMap& pcr_map,
                          const Permission& permission) {
  if (permission.auth_value.has_value()) {
    if (permission.type == PermissionType::kAuthValue) {
      RETURN_IF_ERROR(MakeStatus<TPM2Error>(policy_session.PolicyAuthValue()))
          .WithStatus<TPMError>("Failed to create auth value policy");
    } else if (permission.type == PermissionType::kPolicyOR) {
      // Doing the PolicyOR with the zero digest and auth_value.
      // The initaial policy digest is zero, so we will always match the first
      // section. But we still need the correct auth_value to generate correct
      // the final policy digest.
      std::vector<std::string> policy_or_digests = {
          std::string(SHA256_DIGEST_LENGTH, 0),
          Sha256(permission.auth_value.value()).to_string()};
      RETURN_IF_ERROR(
          MakeStatus<TPM2Error>(policy_session.PolicyOR(policy_or_digests)))
          .WithStatus<TPMError>("Failed to call PolicyOR");
    } else {
      return MakeStatus<TPMError>("Unknown policy permission type",
                                  TPMRetryAction::kNoRetry);
    }
  }

  if (!pcr_map.empty()) {
    RETURN_IF_ERROR(MakeStatus<TPM2Error>(policy_session.PolicyPCR(pcr_map)))
        .WithStatus<TPMError>("Failed to create PCR policy");
  }

  return OkStatus();
}

}  // namespace

StatusOr<OperationPolicy> ConfigTpm2::ToOperationPolicy(
    const OperationPolicySetting& policy) {
  DeviceConfigs configs;
  const DeviceConfigSettings& settings = policy.device_config_settings;
  if (settings.boot_mode.has_value()) {
    configs[DeviceConfig::kBootMode] = true;
  }

  if (settings.device_model.has_value()) {
    configs[DeviceConfig::kDeviceModel] = true;
  }

  if (settings.current_user.has_value()) {
    configs[DeviceConfig::kCurrentUser] = true;
  }

  return OperationPolicy{
      .device_configs = configs,
      .permission = policy.permission,
  };
}

Status ConfigTpm2::SetCurrentUser(const std::string& current_user) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->ExtendPCR(
                      kCurrentUserPcr, current_user, delegate.get())))
      .WithStatus<TPMError>("Failed to extend current user PCR");

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->ExtendPCRForCSME(
                      kCurrentUserPcr, current_user)))
      .WithStatus<TPMError>("Failed to extend current user PCR for CSME");

  return OkStatus();
}

StatusOr<bool> ConfigTpm2::IsCurrentUserSet() {
  ASSIGN_OR_RETURN(std::string && value, ReadPcr(kCurrentUserPcr),
                   _.WithStatus<TPMError>("Failed to read boot mode PCR"));

  return value != std::string(SHA256_DIGEST_LENGTH, 0);
}

StatusOr<ConfigTpm2::QuoteResult> ConfigTpm2::Quote(DeviceConfigs device_config,
                                                    Key key) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

StatusOr<ConfigTpm2::PcrMap> ConfigTpm2::ToPcrMap(
    const DeviceConfigs& device_config) {
  PcrMap result;
  for (DeviceConfig config : kSupportConfigs) {
    if (device_config[config]) {
      ASSIGN_OR_RETURN(int pcr, DeviceConfigToPcr(config),
                       _.WithStatus<TPMError>("Failed to convert to PCR"));
      result[pcr] = std::string();
    }
  }
  return result;
}

StatusOr<ConfigTpm2::PcrMap> ConfigTpm2::ToSettingsPcrMap(
    const DeviceConfigSettings& settings) {
  PcrMap result;

  if (settings.boot_mode.has_value()) {
    const auto& mode = settings.boot_mode->mode;
    if (mode.has_value()) {
      char boot_modes[3] = {mode->developer_mode, mode->recovery_mode,
                            mode->recovery_mode};
      std::string mode_str(std::begin(boot_modes), std::end(boot_modes));
      std::string mode_digest = base::SHA1HashString(mode_str);
      mode_digest.resize(SHA256_DIGEST_LENGTH);
      const std::string pcr_initial_value(SHA256_DIGEST_LENGTH, 0);
      result[kBootModePcr] =
          crypto::SHA256HashString(pcr_initial_value + mode_digest);
    } else {
      ASSIGN_OR_RETURN(std::string && value, ReadPcr(kBootModePcr),
                       _.WithStatus<TPMError>("Failed to read boot mode PCR"));
      result[kBootModePcr] = std::move(value);
    }
  }

  if (settings.device_model.has_value()) {
    const auto& hardware_id = settings.device_model->hardware_id;
    if (hardware_id.has_value()) {
      return MakeStatus<TPMError>("Unsupported settings",
                                  TPMRetryAction::kNoRetry);
    } else {
      ASSIGN_OR_RETURN(
          std::string && value, ReadPcr(kDeviceModelPcr),
          _.WithStatus<TPMError>("Failed to read device model PCR"));
      result[kDeviceModelPcr] = std::move(value);
    }
  }

  if (settings.current_user.has_value()) {
    const auto& username = settings.current_user->username;
    brillo::Blob digest_value(SHA256_DIGEST_LENGTH, 0);
    if (username.has_value()) {
      digest_value = Sha256(brillo::CombineBlobs(
          {digest_value, Sha256(BlobFromString(username.value()))}));
    }
    result[kCurrentUserPcr] = BlobToString(digest_value);
  }

  return result;
}

StatusOr<std::unique_ptr<trunks::PolicySession>>
ConfigTpm2::GetTrunksPolicySession(
    const OperationPolicy& policy,
    const std::vector<std::string>& extra_policy_digests,
    bool salted,
    bool enable_encryption) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::unique_ptr<trunks::PolicySession> policy_session =
      context.factory.GetPolicySession();

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(policy_session->StartUnboundSession(
                      salted, enable_encryption)))
      .WithStatus<TPMError>("Failed to start policy session");

  ASSIGN_OR_RETURN(const PcrMap& pcr_map, ToPcrMap(policy.device_configs),
                   _.WithStatus<TPMError>("Failed to get PCR map"));

  RETURN_IF_ERROR(
      AddToPolicySession(*policy_session, pcr_map, policy.permission))
      .WithStatus<TPMError>("Failed to add policy to policy session");

  if (!extra_policy_digests.empty()) {
    RETURN_IF_ERROR(
        MakeStatus<TPM2Error>(policy_session->PolicyOR(extra_policy_digests)))
        .WithStatus<TPMError>("Failed to call PolicyOR");
  }

  if (policy.permission.auth_value.has_value() &&
      policy.permission.type == PermissionType::kAuthValue) {
    std::string auth_value = policy.permission.auth_value.value().to_string();
    policy_session->SetEntityAuthorizationValue(auth_value);
    brillo::SecureClearContainer(auth_value);
  }

  return policy_session;
}

StatusOr<ConfigTpm2::TrunksSession> ConfigTpm2::GetTrunksSession(
    const OperationPolicy& policy, SessionSecuritySetting setting) {
  if (policy.device_configs.any() ||
      policy.permission.type != PermissionType::kAuthValue) {
    SessionSecurityDetail detail = ToSessionSecurityDetail(setting);
    std::vector<std::string> no_extra_policy_digest = {};
    ASSIGN_OR_RETURN(
        std::unique_ptr<trunks::PolicySession> session,
        GetTrunksPolicySession(policy, no_extra_policy_digest, detail.salted,
                               detail.enable_encryption),
        _.WithStatus<TPMError>("Failed to get policy session"));

    trunks::AuthorizationDelegate* delegate = session->GetDelegate();
    return TrunksSession{
        .session = std::move(session),
        .delegate = delegate,
    };
  } else {
    ASSIGN_OR_RETURN(
        trunks::HmacSession & hmac_session,
        backend_.GetSessionManagementTpm2().GetOrCreateHmacSession(setting),
        _.WithStatus<TPMError>("Failed to get hmac session"));

    if (policy.permission.auth_value.has_value()) {
      std::string auth_value = policy.permission.auth_value.value().to_string();
      hmac_session.SetEntityAuthorizationValue(auth_value);
      brillo::SecureClearContainer(auth_value);
    }

    return TrunksSession{
        // The hmac session doesn't owned by the return value.
        .session = nullptr,
        .delegate = hmac_session.GetDelegate(),
    };
  }
}

StatusOr<std::string> ConfigTpm2::ReadPcr(uint32_t pcr_index) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string pcr_digest;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(
                      context.tpm_utility->ReadPCR(pcr_index, &pcr_digest)))
      .WithStatus<TPMError>("Failed to read PCR");

  return pcr_digest;
}

StatusOr<ConfigTpm2::PcrValue> ConfigTpm2::ToPcrValue(
    const DeviceConfigSettings& settings) {
  ASSIGN_OR_RETURN(const PcrMap& pcr_map, ToSettingsPcrMap(settings));

  // Zero initialize.
  ConfigTpm2::PcrValue pcr_value = {};
  std::string digest;

  for (const PcrMap::value_type& pcr : pcr_map) {
    pcr_value.bitmask[pcr.first / 8] = 1u << (pcr.first % 8);
    digest += pcr.second;
  }

  pcr_value.digest = crypto::SHA256HashString(digest);

  return pcr_value;
}

StatusOr<std::string> ConfigTpm2::GetPolicyDigest(
    const OperationPolicySetting& policy) {
  ASSIGN_OR_RETURN(const PcrMap& pcr_map,
                   ToSettingsPcrMap(policy.device_config_settings),
                   _.WithStatus<TPMError>("Failed to get PCR map"));

  if (pcr_map.empty() && policy.permission.type == PermissionType::kAuthValue) {
    // We will use hmac session, no policy digest for this case.
    return std::string();
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  // Start a trial policy session.
  std::unique_ptr<trunks::PolicySession> policy_session =
      context.factory.GetTrialSession();

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(policy_session->StartUnboundSession(false, false)))
      .WithStatus<TPMError>("Failed to start trial session");

  RETURN_IF_ERROR(
      AddToPolicySession(*policy_session, pcr_map, policy.permission))
      .WithStatus<TPMError>("Failed to add policy to policy session");

  std::string policy_digest;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(policy_session->GetDigest(&policy_digest)))
      .WithStatus<TPMError>("Failed to get policy digest");

  return policy_digest;
}

}  // namespace hwsec
