// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/device_management_service.h"

#include <ios>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/structures/threading_mode.h>
#include <libstorage/platform/platform.h>

#include "device_management/proto_bindings/device_management_interface.pb.h"
#include "fwmp/firmware_management_parameters.h"

namespace device_management {

namespace {
// Developer-disable flag bits from FirmwareManagementParametersFlags in
// system_api/dbus/cryptohome/rpc.proto (corresponding to
// VB2_SECDATA_FWMP_DEV_DISABLE_BOOT and
// VB2_SECDATA_FWMP_DEV_DISABLE_CCD_UNLOCK in
// vboot_reference/firmware/2lib/include/2secdata.h) that Chrome sets to enforce
// block_devmode during enterprise enrollment. Once set during OOBE, these flags
// cannot be cleared by subsequent SetFirmwareManagementParameters or
// RemoveFirmwareManagementParameters calls.
constexpr uint32_t kFwmpDeveloperDisableFlagsMask =
    cryptohome::DEVELOPER_DISABLE_BOOT |
    cryptohome::DEVELOPER_DISABLE_CASE_CLOSED_DEBUGGING_UNLOCK;
}  // namespace

const char DeviceManagementService::kDefaultFwmpLockedFlagsFile[] =
    "/run/lockbox/fwmp_locked_flags";
const mode_t DeviceManagementService::kFwmpLockedFlagsFilePermissions = 0600;

DeviceManagementService::DeviceManagementService()
    : firmware_management_parameters_(nullptr),
      install_attrs_(nullptr),
      enterprise_owned_(false),
      metrics_(std::make_unique<Metrics>()) {}
DeviceManagementService::~DeviceManagementService() {}

void DeviceManagementService::Initialize(
    const hwsec::CryptohomeFrontend& hwsec_, libstorage::Platform& platform_) {
  this->platform_ = &platform_;
  if (!firmware_management_parameters_) {
    firmware_management_parameters_ =
        fwmp::FirmwareManagementParameters::CreateInstance(&hwsec_);
  }

  if (!install_attrs_) {
    install_attrs_ = std::make_unique<InstallAttributes>(&platform_, &hwsec_);
  }

  const base::FilePath locked_flags_file(kDefaultFwmpLockedFlagsFile);
  if (platform_.FileExists(locked_flags_file)) {
    brillo::Blob blob;
    uint32_t persisted_flags = 0;
    if (platform_.ReadFile(locked_flags_file, &blob) &&
        base::StringToUint(base::TrimWhitespaceASCII(brillo::BlobToString(blob),
                                                     base::TRIM_ALL),
                           &persisted_flags)) {
      locked_fwmp_flags_ = persisted_flags & kFwmpDeveloperDisableFlagsMask;
    } else {
      LOG(WARNING) << "Failed to parse " << kDefaultFwmpLockedFlagsFile
                   << "; latching all developer-disable flags.";
      locked_fwmp_flags_ = kFwmpDeveloperDisableFlagsMask;
    }
  }

  // Initialize the install-time locked attributes since we can't do it prior
  // to ownership.
  hwsec_.RegisterOnReadyCallback(base::BindOnce(
      &DeviceManagementService::InitializeInstallAttributesCallback,
      base::Unretained(this)));

  // Always try to init the install attributes even if the TPM is not ready.
  InitializeInstallAttributesCallback(hwsec::OkStatus());

  // Report the current status of install-attributes to UMA.
  metrics_->ReportInstallAttributesStatus(install_attrs_->status());
}

device_management::DeviceManagementErrorCode
DeviceManagementService::GetFirmwareManagementParameters(
    device_management::FirmwareManagementParameters* fwmp) {
  if (!firmware_management_parameters_->Load()) {
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  uint32_t flags;
  if (firmware_management_parameters_->GetFlags(&flags)) {
    fwmp->set_flags(flags);
  } else {
    LOG(WARNING)
        << "Failed to GetFlags() for GetFirmwareManagementParameters().";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  std::vector<uint8_t> hash;
  if (firmware_management_parameters_->GetDeveloperKeyHash(&hash)) {
    *fwmp->mutable_developer_key_hash() = {hash.begin(), hash.end()};
  } else {
    LOG(WARNING) << "Failed to GetDeveloperKeyHash() for "
                    "GetFirmwareManagementParameters().";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  return device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET;
}

device_management::DeviceManagementErrorCode
DeviceManagementService::SetFirmwareManagementParameters(
    const device_management::FirmwareManagementParameters& fwmp) {
  // Firmware management parameters may only be modified during OOBE before
  // install attributes are finalized/locked (matching InstallAttributes::Set;
  // see also SessionManagerImpl::ClearBlockDevmodeVpd in b/524007151, which
  // gates on !install_attributes_reader_->IsLocked()).
  if (!install_attrs_) {
    LOG(ERROR) << "SetFirmwareManagementParameters() called with null "
                  "install_attrs_.";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  InstallAttributes::Status status = install_attrs_->status();
  if (status == InstallAttributes::Status::kTpmNotOwned) {
    InitializeInstallAttributesCallback(hwsec::OkStatus());
    status = install_attrs_->status();
  }
  if (status != InstallAttributes::Status::kFirstInstall) {
    LOG(ERROR) << "SetFirmwareManagementParameters() called outside of first "
                  "install (status="
               << static_cast<int>(status) << ").";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  uint32_t flags = fwmp.flags();
  if ((flags & locked_fwmp_flags_) != locked_fwmp_flags_) {
    LOG(ERROR) << "SetFirmwareManagementParameters() cannot clear previously "
                  "set developer-disable flags (flags=0x"
               << std::hex << flags << ", locked_fwmp_flags=0x"
               << locked_fwmp_flags_ << ").";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  if (!firmware_management_parameters_->Create()) {
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  std::unique_ptr<std::vector<uint8_t>> hash;

  if (!fwmp.developer_key_hash().empty()) {
    hash.reset(new std::vector<uint8_t>(fwmp.developer_key_hash().begin(),
                                        fwmp.developer_key_hash().end()));
  }

  if (!firmware_management_parameters_->Store(flags, hash.get())) {
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  uint32_t new_locked_flags =
      locked_fwmp_flags_ | (flags & kFwmpDeveloperDisableFlagsMask);
  if (new_locked_flags != locked_fwmp_flags_) {
    locked_fwmp_flags_ = new_locked_flags;
    if (platform_ &&
        !platform_->WriteFileAtomic(
            base::FilePath(kDefaultFwmpLockedFlagsFile),
            brillo::BlobFromString(base::NumberToString(locked_fwmp_flags_)),
            kFwmpLockedFlagsFilePermissions)) {
      LOG(WARNING) << "Failed to persist locked FWMP flags to "
                   << kDefaultFwmpLockedFlagsFile;
    }
  }

  return device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET;
}

bool DeviceManagementService::RemoveFirmwareManagementParameters() {
  // Firmware management parameters may only be removed during OOBE before
  // install attributes are finalized/locked (matching InstallAttributes::Set;
  // see also SessionManagerImpl::ClearBlockDevmodeVpd in b/524007151, which
  // gates on !install_attributes_reader_->IsLocked()).
  if (!install_attrs_) {
    LOG(ERROR) << "RemoveFirmwareManagementParameters() called with null "
                  "install_attrs_.";
    return false;
  }

  InstallAttributes::Status status = install_attrs_->status();
  if (status == InstallAttributes::Status::kTpmNotOwned) {
    InitializeInstallAttributesCallback(hwsec::OkStatus());
    status = install_attrs_->status();
  }
  if (status != InstallAttributes::Status::kFirstInstall) {
    LOG(ERROR) << "RemoveFirmwareManagementParameters() called outside of "
                  "first install (status="
               << static_cast<int>(status) << ").";
    return false;
  }

  if (locked_fwmp_flags_ != 0) {
    LOG(ERROR) << "RemoveFirmwareManagementParameters() called after "
                  "developer-disable flags have been set (locked_fwmp_flags=0x"
               << std::hex << locked_fwmp_flags_ << ").";
    return false;
  }

  return firmware_management_parameters_->Destroy();
}

bool DeviceManagementService::InstallAttributesGet(
    const std::string& name, std::vector<uint8_t>* data_out) {
  return install_attrs_->Get(name, data_out);
}
bool DeviceManagementService::InstallAttributesSet(
    const std::string& name, const std::vector<uint8_t>& data) {
  return install_attrs_->Set(name, data);
}

bool DeviceManagementService::InstallAttributesFinalize() {
  bool result = install_attrs_->Finalize();
  DetectEnterpriseOwnership();
  return result;
}

int DeviceManagementService::InstallAttributesCount() {
  return install_attrs_->Count();
}

bool DeviceManagementService::InstallAttributesIsSecure() {
  return install_attrs_->IsSecure();
}

InstallAttributes::Status
DeviceManagementService::InstallAttributesGetStatus() {
  return install_attrs_->status();
}

// static
device_management::InstallAttributesState
DeviceManagementService::InstallAttributesStatusToProtoEnum(
    InstallAttributes::Status status) {
  static const std::unordered_map<InstallAttributes::Status,
                                  device_management::InstallAttributesState>
      state_map = {{InstallAttributes::Status::kUnknown,
                    device_management::InstallAttributesState::UNKNOWN},
                   {InstallAttributes::Status::kTpmNotOwned,
                    device_management::InstallAttributesState::TPM_NOT_OWNED},
                   {InstallAttributes::Status::kFirstInstall,
                    device_management::InstallAttributesState::FIRST_INSTALL},
                   {InstallAttributes::Status::kValid,
                    device_management::InstallAttributesState::VALID},
                   {InstallAttributes::Status::kInvalid,
                    device_management::InstallAttributesState::INVALID}};
  if (state_map.count(status) != 0) {
    return state_map.at(status);
  }

  return device_management::InstallAttributesState::INVALID;
}

void DeviceManagementService::DetectEnterpriseOwnership() {
  static const std::string true_str = "true";
  brillo::Blob true_value(true_str.begin(), true_str.end());
  true_value.push_back(0);

  brillo::Blob value;
  if (install_attrs_->Get("enterprise.owned", &value) && value == true_value) {
    enterprise_owned_ = true;
  }
  // Note: Right now there's no way to convert an enterprise owned machine to a
  // non-enterprise owned machine without clearing the TPM, so we don't try
  // set `enterprise_owned_` to false.
}

void DeviceManagementService::InitializeInstallAttributesCallback(
    hwsec::Status status) {
  // Don't reinitialize when install attributes are valid or first install.
  if (install_attrs_->status() == InstallAttributes::Status::kValid ||
      install_attrs_->status() == InstallAttributes::Status::kFirstInstall) {
    return;
  }

  if (!status.ok()) {
    LOG(ERROR) << "InitializeInstallAttributesCallback failed: " << status;
    return;
  }

  // The TPM owning instance may have changed since initialization.
  // InstallAttributes can handle a NULL or !IsEnabled Tpm object.
  std::ignore = install_attrs_->Init();

  // Check if the machine is enterprise owned and report to mount_ then.
  DetectEnterpriseOwnership();
}

}  // namespace device_management
