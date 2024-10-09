// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_attestation_context.h"

#include <base/containers/flat_map.h>
#include <base/logging.h>

namespace arc::keymint::context {

namespace {
// Converts VerifiedBootState to the value expected by Android in DeviceInfo
// for |vb_state|.
// DeviceInfo expected values:
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/DeviceInfoV2.cddl
const base::flat_map<VerifiedBootState, std::string>
    kVerifiedBootStateToStringMap = {
        {VerifiedBootState::kVerifiedBoot, "green"},
        {VerifiedBootState::kUnverifiedBoot, "orange"}};

// Converts VerifiedBootDeviceState to the value expected by Android in
// DeviceInfo for |bootloader_state|.
const base::flat_map<VerifiedBootDeviceState, std::string>
    kDeviceStateToStringMap = {
        {VerifiedBootDeviceState::kLockedDevice, "locked"},
        {VerifiedBootDeviceState::kUnlockedDevice, "unlocked"}};
}  // namespace

ArcAttestationContext::ArcAttestationContext(
    ::keymaster::KmVersion km_version,
    keymaster_security_level_t security_level)
    : SoftAttestationContext(km_version), security_level_(security_level) {}

ArcAttestationContext::~ArcAttestationContext() = default;

keymaster_security_level_t ArcAttestationContext::GetSecurityLevel() const {
  return security_level_;
}

const ArcAttestationContext::VerifiedBootParams*
ArcAttestationContext::GetVerifiedBootParams(keymaster_error_t* error) const {
  static VerifiedBootParams params;
  if (error == nullptr) {
    LOG(ERROR) << "Cannot return an error in GetVerifiedBootParams due to null "
                  "pointer.";
    return &params;
  }

  const std::string locked_device =
      kDeviceStateToStringMap.at(VerifiedBootDeviceState::kLockedDevice);
  if (bootloader_state_.has_value()) {
    params.device_locked = (bootloader_state_.value() == locked_device);
  } else {
    LOG(ERROR) << "Device Locked State could not be read from Bootloader state "
                  "while fetching Verified Boot parameters";
  }

  const std::string verified_state =
      kVerifiedBootStateToStringMap.at(VerifiedBootState::kVerifiedBoot);
  if (verified_boot_state_.has_value()) {
    params.verified_boot_state = verified_boot_state_.value() == verified_state
                                     ? KM_VERIFIED_BOOT_VERIFIED
                                     : KM_VERIFIED_BOOT_UNVERIFIED;
  } else {
    LOG(ERROR) << "Verified Boot State could not be read while fetching "
                  "Verified Boot Parameters";
  }

  if (vbmeta_digest_.has_value()) {
    params.verified_boot_hash = {vbmeta_digest_.value().data(),
                                 vbmeta_digest_.value().size()};
  } else {
    LOG(ERROR) << "Verified Boot hash could not be read from VB Meta digest "
                  "while fetching Verified Boot Parameters";
  }

  if (boot_key_.has_value()) {
    params.verified_boot_key = {boot_key_.value().data(),
                                boot_key_.value().size()};
  } else {
    LOG(ERROR) << "Verified Boot Key could not be read while fetching Verified "
                  "Boot Parameters";
  }

  *error = KM_ERROR_OK;
  return &params;
}

keymaster_error_t ArcAttestationContext::SetVerifiedBootParams(
    std::string_view boot_state,
    std::string_view bootloader_state,
    const std::vector<uint8_t>& vbmeta_digest) {
  if (!bootloader_state.empty()) {
    bootloader_state_ = bootloader_state;
  } else {
    LOG(ERROR)
        << "bootloader_state is empty when trying to set Verified Boot params";
  }

  if (!boot_state.empty()) {
    verified_boot_state_ = boot_state;
  } else {
    LOG(ERROR) << "boot_state is empty when trying to set Verified Boot params";
  }

  if (!vbmeta_digest.empty()) {
    vbmeta_digest_ = vbmeta_digest;
  } else {
    LOG(ERROR)
        << "vbmeta_digest is empty when trying to set Verified Boot params";
  }

  return KM_ERROR_OK;
}

}  // namespace arc::keymint::context
