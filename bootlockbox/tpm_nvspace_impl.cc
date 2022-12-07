// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <brillo/secure_blob.h>
#include <libhwsec/frontend/bootlockbox/frontend.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "bootlockbox/tpm_nvspace.h"
#include "bootlockbox/tpm_nvspace_impl.h"
#include "libhwsec/error/tpm_error.h"

namespace bootlockbox {

bool TPMNVSpaceImpl::Initialize() {
  if (!tpm_owner_) {
    scoped_refptr<dbus::Bus> bus = connection_.Connect();
    CHECK(bus) << "Failed to connect to system D-Bus";
    default_tpm_owner_ = std::make_unique<org::chromium::TpmManagerProxy>(bus);
    tpm_owner_ = default_tpm_owner_.get();
  }
  return true;
}

NVSpaceState TPMNVSpaceImpl::DefineNVSpace() {
  ASSIGN_OR_RETURN(hwsec::BootLockboxFrontend::StorageState state,
                   hwsec_->GetSpaceState(),
                   _.WithStatus<hwsec::TPMError>("Failed to get space state")
                       .LogError()
                       .As(NVSpaceState::kNVSpaceNeedPowerwash));

  if (state == hwsec::BootLockboxFrontend::StorageState::kReady) {
    return NVSpaceState::kNVSpaceUninitialized;
  }

  if (state != hwsec::BootLockboxFrontend::StorageState::kPreparable) {
    LOG(ERROR) << "Cannot prepare space with unprepareable state: "
               << static_cast<int>(state);
    return NVSpaceState::kNVSpaceError;
  }

  RETURN_IF_ERROR(hwsec_->PrepareSpace(kNVSpaceSize))
      .WithStatus<hwsec::TPMError>("Failed to prepare space")
      .LogError()
      .As(NVSpaceState::kNVSpaceUndefined);

  return NVSpaceState::kNVSpaceUninitialized;
}

bool TPMNVSpaceImpl::WriteNVSpace(const std::string& digest) {
  if (digest.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "Wrong digest size, expected: " << SHA256_DIGEST_LENGTH
               << " got: " << digest.size();
    return false;
  }

  BootLockboxNVSpace space;
  space.version = kNVSpaceVersion;
  space.flags = 0;
  memcpy(space.digest, digest.data(), SHA256_DIGEST_LENGTH);
  brillo::Blob nvram_data(kNVSpaceSize);
  memcpy(nvram_data.data(), &space, kNVSpaceSize);

  RETURN_IF_ERROR(hwsec_->StoreSpace(nvram_data))
      .WithStatus<hwsec::TPMError>("Failed to store space")
      .LogError()
      .As(false);

  return true;
}

NVSpaceState TPMNVSpaceImpl::ReadNVSpace(std::string* digest) {
  ASSIGN_OR_RETURN(hwsec::BootLockboxFrontend::StorageState state,
                   hwsec_->GetSpaceState(),
                   _.WithStatus<hwsec::TPMError>("Failed to get space state")
                       .LogError()
                       .As(NVSpaceState::kNVSpaceNeedPowerwash));

  if (state == hwsec::BootLockboxFrontend::StorageState::kPreparable) {
    return NVSpaceState::kNVSpaceUndefined;
  }

  ASSIGN_OR_RETURN(brillo::Blob nvram_data, hwsec_->LoadSpace(),
                   _.WithStatus<hwsec::TPMError>("Failed to read space")
                       .LogError()
                       .As(NVSpaceState::kNVSpaceError));

  if (nvram_data.size() != kNVSpaceSize) {
    LOG(ERROR) << "Error reading nvram space, invalid data length, expected:"
               << kNVSpaceSize << ", got " << nvram_data.size();
    return NVSpaceState::kNVSpaceError;
  }

  std::string nvram_data_str = brillo::BlobToString(nvram_data);
  if (nvram_data_str == std::string(kNVSpaceSize, '\0') ||
      nvram_data_str == std::string(kNVSpaceSize, 0xff)) {
    LOG(ERROR) << "Empty nvram data.";
    return NVSpaceState::kNVSpaceUninitialized;
  }

  BootLockboxNVSpace space;
  memcpy(&space, nvram_data.data(), kNVSpaceSize);
  if (space.version != kNVSpaceVersion) {
    LOG(ERROR) << "Error reading nvram space, invalid version";
    return NVSpaceState::kNVSpaceError;
  }
  digest->assign(reinterpret_cast<const char*>(space.digest),
                 SHA256_DIGEST_LENGTH);
  return NVSpaceState::kNVSpaceNormal;
}

bool TPMNVSpaceImpl::LockNVSpace() {
  RETURN_IF_ERROR(hwsec_->LockSpace())
      .WithStatus<hwsec::TPMError>("Failed to lock space")
      .LogError()
      .As(false);

  return true;
}

void TPMNVSpaceImpl::RegisterOwnershipTakenCallback(
    const base::RepeatingClosure& callback) {
  tpm_owner_->RegisterSignalOwnershipTakenSignalHandler(
      base::BindRepeating(&TPMNVSpaceImpl::OnOwnershipTaken,
                          base::Unretained(this), callback),
      base::DoNothing());
}

void TPMNVSpaceImpl::OnOwnershipTaken(
    const base::RepeatingClosure& callback,
    const tpm_manager::OwnershipTakenSignal& signal) {
  LOG(INFO) << __func__ << ": Received |OwnershipTakenSignal|.";
  callback.Run();
}

}  // namespace bootlockbox
