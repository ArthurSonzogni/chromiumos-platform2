// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/dmsetup_container.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <absl/cleanup/cleanup.h>
#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/keyring/utils.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"
#include "libstorage/storage_container/storage_container.h"

using hwsec_foundation::SecureBlobToHex;

namespace libstorage {

namespace {

constexpr uint64_t kSectorSize = 512;
constexpr uint64_t kExt4BlockSize = 4096;
constexpr char kDeviceMapperPathPrefix[] = "/dev/mapper";

}  // namespace

DmsetupContainer::DmsetupContainer(
    StorageContainerType type,
    const DmsetupConfig& config,
    std::unique_ptr<BackingDevice> backing_device,
    const FileSystemKeyReference& key_reference,
    Platform* platform,
    Keyring* keyring,
    std::unique_ptr<brillo::DeviceMapper> device_mapper)
    : dmsetup_device_name_(config.dmsetup_device_name),
      dmsetup_cipher_(config.dmsetup_cipher),
      dmsetup_type_(type),
      iv_offset_(config.iv_offset),
      backing_device_(std::move(backing_device)),
      key_reference_(
          dmcrypt::GenerateKeyringDescription(key_reference.fek_sig)),
      platform_(platform),
      keyring_(keyring),
      device_mapper_(std::move(device_mapper)) {}

DmsetupContainer::DmsetupContainer(
    StorageContainerType type,
    const DmsetupConfig& config,
    std::unique_ptr<BackingDevice> backing_device,
    const FileSystemKeyReference& key_reference,
    Platform* platform,
    Keyring* keyring)
    : DmsetupContainer(type,
                       config,
                       std::move(backing_device),
                       key_reference,
                       platform,
                       keyring,
                       std::make_unique<brillo::DeviceMapper>()) {}

bool DmsetupContainer::Purge() {
  // Stale dm-crypt containers may need an extra teardown before purging the
  // device.
  std::ignore = Teardown();

  return backing_device_->Purge();
}

bool DmsetupContainer::Exists() {
  return backing_device_->Exists();
}

bool DmsetupContainer::IsDeviceKeyValid() {
  // Considered valid if the keys are anything other than repeating 0's.
  return device_mapper_->GetTable(dmsetup_device_name_)
             .CryptGetKey()
             .to_string()
             .find_first_not_of("0") != std::string::npos;
}

bool DmsetupContainer::Setup(const FileSystemKey& encryption_key) {
  // Check whether the kernel keyring provisioning is supported by the current
  // kernel.
  std::optional<std::string> type = GetDmsetupType(dmsetup_type_);
  if (!type) {
    LOG(ERROR) << "Invalid configuration";
    return false;
  }

  brillo::DeviceMapperVersion version = device_mapper_->GetTargetVersion(*type);
  if (version.major == 0) {
    LOG(ERROR) << "dm-" << *type << " not supported.";
  }

  bool created = false;
  if (!backing_device_->Exists()) {
    LOG(INFO) << "Creating backing device for " << dmsetup_device_name_
              << " type: dm-" << *type << "(" << version.major << ", "
              << version.minor << ", " << version.patchlevel << ")";
    if (!backing_device_->Create()) {
      LOG(ERROR) << "Failed to create backing device";
      return false;
    }
    created = true;
  }

  // Ensure that the dm-crypt device or the underlying backing device are
  // not left attached on the failure paths. If the backing device was created
  // during setup, purge it as well.
  absl::Cleanup device_cleanup_runner = [this, created]() {
    if (created) {
      Purge();
    } else {
      Teardown();
    }
  };

  LOG(INFO) << "Setting up backing device";
  if (!backing_device_->Setup()) {
    LOG(ERROR) << "Failed to setup backing device";
    return false;
  }

  std::optional<base::FilePath> backing_device_path =
      backing_device_->GetPath();
  if (!backing_device_path) {
    LOG(ERROR) << "Failed to get backing device path";
    backing_device_->Teardown();
    return false;
  }

  uint64_t blkdev_size;
  if (!platform_->GetBlkSize(*backing_device_path, &blkdev_size) ||
      blkdev_size < kExt4BlockSize) {
    PLOG(ERROR) << "Failed to get block device size";
    backing_device_->Teardown();
    return false;
  }

  brillo::SecureBlob key_descriptor;
  if (dmsetup_type_ == StorageContainerType::kDmcrypt) {
    if (!keyring_->AddKey(Keyring::KeyType::kDmcryptKey, encryption_key,
                          &key_reference_)) {
      LOG(ERROR) << "Failed to insert logon key to session keyring.";
      return false;
    }

    // Once the key is inserted, update the key descriptor.
    key_descriptor = dmcrypt::GenerateDmcryptKeyDescriptor(
        key_reference_.fek_sig, encryption_key.fek.size());
  } else {
    // default-key does not support keyring, send the key on the command line.
    key_descriptor = brillo::SecureBlob(SecureBlobToHex(encryption_key.fek));
  }

  // Ensure that once the key has been used by dmsetup or failed,
  // remove it from the keyring when inserted.
  absl::Cleanup keyring_cleanup_runner = [this]() {
    if (dmsetup_type_ == StorageContainerType::kDmcrypt) {
      LOG(INFO) << "Removing provisioned dmsetup key from kernel keyring.";
      if (!keyring_->RemoveKey(Keyring::KeyType::kDmcryptKey, key_reference_)) {
        LOG(ERROR) << "Failed to remove key from keyring";
      }
    }
  };

  uint64_t sectors = blkdev_size / kSectorSize;
  brillo::SecureBlob dm_parameters =
      brillo::DevmapperTable::CryptCreateParameters(
          // cipher.
          dmsetup_cipher_,
          // encryption key descriptor.
          key_descriptor,
          // iv offset.
          iv_offset_,
          // device path.
          *backing_device_path,
          // device offset.
          0,
          // allow discards.
          true);
  brillo::DevmapperTable dm_table(0, sectors, *GetDmsetupType(dmsetup_type_),
                                  dm_parameters);
  if (!device_mapper_->Setup(dmsetup_device_name_, dm_table)) {
    backing_device_->Teardown();
    LOG(ERROR) << "dm_setup failed";
    return false;
  }

  // Wait for the dmsetup device path to show up before continuing to setting
  // up the filesystem.
  LOG(INFO) << "Waiting for dm-" << *GetDmsetupType(dmsetup_type_)
            << " device to appear";
  if (!platform_->UdevAdmSettle(GetPath(), true)) {
    LOG(ERROR) << "udevadm settle failed.";
    return false;
  }

  std::move(device_cleanup_runner).Cancel();
  return true;
}

bool DmsetupContainer::EvictKey() {
  if (!IsDeviceKeyValid()) {
    LOG(INFO) << "Dm-" << *GetDmsetupType(dmsetup_type_) << " device EvictKey("
              << dmsetup_device_name_ << ") isn't valid.";
    return true;
  }

  // Suspend device to properly freeze block IO and flush data in cache.
  if (!device_mapper_->Suspend(dmsetup_device_name_)) {
    LOG(ERROR) << "Dm-" << *GetDmsetupType(dmsetup_type_) << " device EvictKey("
               << dmsetup_device_name_ << ") Suspend failed.";
    return false;
  }

  // Remove the dmsetup device key only, keeps the backing device
  // attached and dmsetup table.
  if (!device_mapper_->Message(dmsetup_device_name_, "key wipe")) {
    LOG(ERROR) << "Dm-" << *GetDmsetupType(dmsetup_type_) << " device EvictKey("
               << dmsetup_device_name_ << ") failed.";
    return false;
  }
  return true;
}

bool DmsetupContainer::Reset() {
  // Discard the entire device.
  if (!platform_->DiscardDevice(GetPath())) {
    LOG(ERROR) << "Failed to discard device";
    return false;
  }

  return true;
}

bool DmsetupContainer::SetLazyTeardownWhenUnused() {
  if (!device_mapper_->Remove(dmsetup_device_name_, true /* deferred */)) {
    LOG(ERROR) << "Failed to mark the device mapper target for deferred remove";
    return false;
  }

  if (backing_device_->GetType() != BackingDeviceType::kLoopbackDevice) {
    LOG(WARNING) << "Backing device does not support lazy teardown";
    return false;
  }

  if (!backing_device_->Teardown()) {
    LOG(ERROR) << "Failed to lazy teardown backing device";
    return false;
  }

  return true;
}

bool DmsetupContainer::Teardown() {
  if (!(device_mapper_->GetTable(dmsetup_device_name_).GetType() == "") &&
      !IsDeviceKeyValid()) {
    // To force remove the block device, replace device with an error, read-only
    // target. It should stop processes from reading it and also removed
    // underlying device from mapping, so it is usable again. If some process
    // try to read temporary cryptsetup device, it is bug - no other process
    // should try touch it (e.g. udev).
    if (!device_mapper_->WipeTable(dmsetup_device_name_)) {
      LOG(ERROR) << "Failed to wipe device mapper table.";
      return false;
    }
    // Move error from inactive device mapper table to live one.
    if (!device_mapper_->Resume(dmsetup_device_name_)) {
      LOG(ERROR) << "Failed to teardown device mapper device.";
      return false;
    }

    LOG(INFO) << "Dm-" << *GetDmsetupType(dmsetup_type_)
              << " device remapped to error target.";
  }

  if (!device_mapper_->Remove(dmsetup_device_name_)) {
    LOG(ERROR) << "Failed to teardown device mapper device.";
    // If we are unable to remove the device from the mapper, it could
    // have a running process still tied to it i.e. Chrome, even if remapped
    // to an error target.
    return false;
  }

  if (!backing_device_->Teardown()) {
    LOG(ERROR) << "Failed to teardown backing device";
    return false;
  }

  return true;
}

base::FilePath DmsetupContainer::GetPath() const {
  return base::FilePath(kDeviceMapperPathPrefix).Append(dmsetup_device_name_);
}

base::FilePath DmsetupContainer::GetBackingLocation() const {
  if (backing_device_ != nullptr && backing_device_->GetPath().has_value()) {
    return *(backing_device_->GetPath());
  }
  return base::FilePath();
}

}  // namespace libstorage
