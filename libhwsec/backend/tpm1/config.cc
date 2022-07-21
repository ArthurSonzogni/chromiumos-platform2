// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"

#include <string>

#include <base/hash/sha1.h>
#include <crypto/sha2.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <openssl/sha.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::Sha1;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using ConfigTpm1 = BackendTpm1::ConfigTpm1;

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

}  // namespace

StatusOr<OperationPolicy> ConfigTpm1::ToOperationPolicy(
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

Status ConfigTpm1::SetCurrentUser(const std::string& current_user) {
  ASSIGN_OR_RETURN(TSS_HCONTEXT context, backend_.GetTssContext());

  ASSIGN_OR_RETURN(TSS_HTPM tpm_handle, backend_.GetUserTpmHandle());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  brillo::Blob extention = Sha1(brillo::BlobFromString(current_user));

  uint32_t new_pcr_value_length = 0;
  ScopedTssMemory new_pcr_value(overalls, context);

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Ospi_TPM_PcrExtend(
          tpm_handle, kCurrentUserPcr, extention.size(), extention.data(),
          nullptr, &new_pcr_value_length, new_pcr_value.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_TPM_PcrExtend");

  return OkStatus();
}

StatusOr<bool> ConfigTpm1::IsCurrentUserSet() {
  ASSIGN_OR_RETURN(brillo::Blob && value, ReadPcr(kCurrentUserPcr),
                   _.WithStatus<TPMError>("Failed to read boot mode PCR"));

  return value != brillo::Blob(SHA_DIGEST_LENGTH, 0);
}

StatusOr<ConfigTpm1::QuoteResult> ConfigTpm1::Quote(DeviceConfigs device_config,
                                                    Key key) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

StatusOr<ConfigTpm1::PcrMap> ConfigTpm1::ToPcrMap(
    const DeviceConfigs& device_config) {
  PcrMap result;
  for (DeviceConfig config : kSupportConfigs) {
    if (device_config[config]) {
      ASSIGN_OR_RETURN(int pcr, DeviceConfigToPcr(config),
                       _.WithStatus<TPMError>("Failed to convert to PCR"));
      result[pcr] = brillo::Blob();
    }
  }
  return result;
}

StatusOr<brillo::Blob> ConfigTpm1::ReadPcr(uint32_t pcr_index) {
  ASSIGN_OR_RETURN(TSS_HCONTEXT context, backend_.GetTssContext());

  ASSIGN_OR_RETURN(TSS_HTPM tpm_handle, backend_.GetUserTpmHandle());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  uint32_t length = 0;
  ScopedTssMemory buffer(overalls, context);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_TPM_PcrRead(
                      tpm_handle, pcr_index, &length, buffer.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_TPM_PcrRead");

  return brillo::Blob(buffer.value(), buffer.value() + length);
}

StatusOr<ConfigTpm1::PcrMap> ConfigTpm1::ToSettingsPcrMap(
    const DeviceConfigSettings& settings) {
  PcrMap result;

  if (settings.boot_mode.has_value()) {
    const auto& mode = settings.boot_mode->mode;
    if (mode.has_value()) {
      return MakeStatus<TPMError>("Unsupported settings",
                                  TPMRetryAction::kNoRetry);
    } else {
      ASSIGN_OR_RETURN(brillo::Blob && value, ReadPcr(kBootModePcr),
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
          brillo::Blob && value, ReadPcr(kDeviceModelPcr),
          _.WithStatus<TPMError>("Failed to read device model PCR"));
      result[kDeviceModelPcr] = std::move(value);
    }
  }

  if (settings.current_user.has_value()) {
    const auto& username = settings.current_user->username;
    brillo::Blob digest_value(SHA_DIGEST_LENGTH, 0);
    if (username.has_value()) {
      digest_value = Sha1(brillo::CombineBlobs(
          {digest_value, Sha1(BlobFromString(username.value()))}));
    }
    result[kCurrentUserPcr] = digest_value;
  }

  return result;
}

}  // namespace hwsec
