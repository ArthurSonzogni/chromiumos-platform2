// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"

namespace {
constexpr base::TimeDelta kDefaultTimeout = base::TimeDelta::FromMinutes(2);
}  // namespace

using tpm_manager::NvramResult;

namespace cryptohome {

NVSpaceState MapReadNvramError(NvramResult r) {
  switch (r) {
    case NvramResult::NVRAM_RESULT_SUCCESS:
      return NVSpaceState::kNVSpaceNormal;
    case NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST:
      return NVSpaceState::kNVSpaceUndefined;
    // Operation disable includes uninitialized and locked, but we shouldn't see
    // read lock for bootlockboxd.
    case NvramResult::NVRAM_RESULT_OPERATION_DISABLED:
      return NVSpaceState::kNVSpaceUninitialized;
    // There is nothing to do for these errors.
    case NvramResult::NVRAM_RESULT_DEVICE_ERROR:
    case NvramResult::NVRAM_RESULT_ACCESS_DENIED:
    case NvramResult::NVRAM_RESULT_INVALID_PARAMETER:
    case NvramResult::NVRAM_RESULT_SPACE_ALREADY_EXISTS:
    case NvramResult::NVRAM_RESULT_INSUFFICIENT_SPACE:
    case NvramResult::NVRAM_RESULT_IPC_ERROR:
      return NVSpaceState::kNVSpaceError;
  }
}

std::string NvramResult2Str(NvramResult r) {
  switch (r) {
    case NvramResult::NVRAM_RESULT_SUCCESS:
      return "NVRAM_RESULT_SUCCESS";
    case NvramResult::NVRAM_RESULT_DEVICE_ERROR:
      return "NVRAM_RESULT_DEVICE_ERROR";
    case NvramResult::NVRAM_RESULT_ACCESS_DENIED:
      return "NVRAM_RESULT_ACCESS_DENIED";
    case NvramResult::NVRAM_RESULT_INVALID_PARAMETER:
      return "NVRAM_RESULT_INVALID_PARAMETER";
    case NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST:
      return "NVRAM_RESULT_SPACE_DOES_NOT_EXIST";
    case NvramResult::NVRAM_RESULT_SPACE_ALREADY_EXISTS:
      return "NVRAM_RESULT_SPACE_ALREADY_EXISTS";
    case NvramResult::NVRAM_RESULT_OPERATION_DISABLED:
      return "NVRAM_RESULT_OPERATION_DISABLED";
    case NvramResult::NVRAM_RESULT_INSUFFICIENT_SPACE:
      return "NVRAM_RESULT_INSUFFICIENT_SPACE";
    case NvramResult::NVRAM_RESULT_IPC_ERROR:
      return "NVRAM_RESULT_IPC_ERROR";
  }
}

TPM2NVSpaceUtility::TPM2NVSpaceUtility(
    org::chromium::TpmNvramProxyInterface* tpm_nvram) {
  tpm_nvram_ = tpm_nvram;
}

bool TPM2NVSpaceUtility::Initialize() {
  if (!tpm_nvram_) {
    scoped_refptr<dbus::Bus> bus = connection_.Connect();
    CHECK(bus) << "Failed to connect to system D-Bus";
    default_tpm_nvram_ = std::make_unique<org::chromium::TpmNvramProxy>(bus);
    tpm_nvram_ = default_tpm_nvram_.get();
  }
  return true;
}

bool TPM2NVSpaceUtility::DefineNVSpace() {
  tpm_manager::DefineSpaceRequest request;
  request.set_index(kBootLockboxNVRamIndex);
  request.set_size(kNVSpaceSize);
  request.add_attributes(tpm_manager::NVRAM_READ_AUTHORIZATION);
  request.add_attributes(tpm_manager::NVRAM_BOOT_WRITE_LOCK);
  request.add_attributes(tpm_manager::NVRAM_WRITE_AUTHORIZATION);
  request.set_authorization_value(kWellKnownPassword);

  tpm_manager::DefineSpaceReply reply;
  brillo::ErrorPtr error;
  if (!tpm_nvram_->DefineSpace(request, &reply, &error,
                               kDefaultTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to call DefineSpace: " << error->GetMessage();
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to define nvram space: "
               << NvramResult2Str(reply.result());
    return false;
  }
  // TODO(xzhou): notify tpm_managerd ready to drop key.
  return true;
}

bool TPM2NVSpaceUtility::WriteNVSpace(const std::string& digest) {
  if (digest.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "Wrong digest size, expected: " << SHA256_DIGEST_LENGTH
               << " got: " << digest.size();
    return false;
  }

  BootLockboxNVSpace BootLockboxNVSpace;
  BootLockboxNVSpace.version = kNVSpaceVersion;
  BootLockboxNVSpace.flags = 0;
  memcpy(BootLockboxNVSpace.digest, digest.data(), SHA256_DIGEST_LENGTH);
  std::string nvram_data(reinterpret_cast<const char*>(&BootLockboxNVSpace),
                         kNVSpaceSize);

  tpm_manager::WriteSpaceRequest request;
  request.set_index(kBootLockboxNVRamIndex);
  request.set_data(nvram_data);
  request.set_authorization_value(kWellKnownPassword);
  request.set_use_owner_authorization(false);
  tpm_manager::WriteSpaceReply reply;
  brillo::ErrorPtr error;
  if (!tpm_nvram_->WriteSpace(request, &reply, &error,
                              kDefaultTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to call WriteSpace: " << error->GetMessage();
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to write nvram space: "
               << NvramResult2Str(reply.result());
    return false;
  }
  return true;
}

bool TPM2NVSpaceUtility::ReadNVSpace(std::string* digest,
                                     NVSpaceState* result) {
  *result = NVSpaceState::kNVSpaceError;

  tpm_manager::ReadSpaceRequest request;
  request.set_index(kBootLockboxNVRamIndex);
  request.set_authorization_value(kWellKnownPassword);
  request.set_use_owner_authorization(false);
  tpm_manager::ReadSpaceReply reply;

  brillo::ErrorPtr error;
  if (!tpm_nvram_->ReadSpace(request, &reply, &error,
                             kDefaultTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to call ReadSpace: " << error->GetMessage();
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to read nvram space: "
               << NvramResult2Str(reply.result());
    *result = MapReadNvramError(reply.result());
    return false;
  }

  const std::string& nvram_data = reply.data();

  if (nvram_data.size() != kNVSpaceSize) {
    LOG(ERROR) << "Error reading nvram space, invalid data length, expected:"
               << kNVSpaceSize << ", got " << nvram_data.size();
    return false;
  }
  BootLockboxNVSpace BootLockboxNVSpace;
  memcpy(&BootLockboxNVSpace, nvram_data.data(), kNVSpaceSize);
  if (BootLockboxNVSpace.version != kNVSpaceVersion) {
    LOG(ERROR) << "Error reading nvram space, invalid version";
    return false;
  }
  digest->assign(reinterpret_cast<const char*>(BootLockboxNVSpace.digest),
                 SHA256_DIGEST_LENGTH);
  *result = NVSpaceState::kNVSpaceNormal;
  return true;
}

bool TPM2NVSpaceUtility::LockNVSpace() {
  tpm_manager::LockSpaceRequest request;
  request.set_index(kBootLockboxNVRamIndex);
  request.set_lock_read(false);
  request.set_lock_write(true);
  request.set_authorization_value(kWellKnownPassword);
  request.set_use_owner_authorization(false);
  tpm_manager::LockSpaceReply reply;

  brillo::ErrorPtr error;
  if (!tpm_nvram_->LockSpace(request, &reply, &error,
                             kDefaultTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to call LockSpace: " << error->GetMessage();
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to lock nvram space: "
               << NvramResult2Str(reply.result());
    return false;
  }
  return true;
}

}  // namespace cryptohome
