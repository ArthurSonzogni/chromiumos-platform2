// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/dmcrypt_container.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

namespace {

constexpr uint64_t kSectorSize = 512;
constexpr uint64_t kExt4BlockSize = 4096;
constexpr char kKeyring[] = "logon";
constexpr char kDmcryptKeyDescriptor[] = "dmcrypt:";

// Generate the keyring description.
brillo::SecureBlob GenerateKeyringDescription(
    const brillo::SecureBlob& key_reference) {
  return brillo::SecureBlob::Combine(
      brillo::SecureBlob(kDmcryptKeyDescriptor),
      brillo::SecureBlob(base::ToLowerASCII(
          base::HexEncode(key_reference.data(), key_reference.size()))));
}

// Generates the key descriptor to be used in the device mapper table if the
// kernel keyring is supported.
brillo::SecureBlob GenerateDmcryptKeyDescriptor(
    const brillo::SecureBlob key_reference, uint64_t key_size) {
  brillo::SecureBlob key_desc(
      base::StringPrintf(":%" PRIu64 ":%s:", key_size, kKeyring));
  return brillo::SecureBlob::Combine(key_desc, key_reference);
}

// Check whether the dm-crypt driver version is greater than 1.15.0 and can
// support key provisioning via the kernel keyring.
bool IsKernelKeyringSupported(const brillo::DeviceMapperVersion& version) {
  const brillo::DeviceMapperVersion reference_version({1, 15, 0});

  if (version < reference_version) {
    return false;
  }

  return true;
}

// For dm-crypt, we use the process keyring to ensure that the key is unlinked
// if the process exits/crashes before it is cleared.
bool AddLogonKey(const brillo::SecureBlob& key,
                 const brillo::SecureBlob& key_reference) {
  if (add_key(kKeyring, key_reference.char_data(), key.char_data(), key.size(),
              KEY_SPEC_THREAD_KEYRING) == -1) {
    PLOG(ERROR) << "add_key failed";
    return false;
  }

  return true;
}

bool UnlinkLogonKey(const brillo::SecureBlob& key_reference) {
  key_serial_t key = keyctl_search(KEY_SPEC_THREAD_KEYRING, kKeyring,
                                   key_reference.char_data(), 0);

  if (key == -1) {
    PLOG(ERROR) << "keyctl_search failed";
    return false;
  }

  if (keyctl_invalidate(key) != 0) {
    LOG(ERROR) << "Failed to invalidate key " << key;
    return false;
  }

  return true;
}

}  // namespace

DmcryptContainer::DmcryptContainer(
    const DmcryptConfig& config,
    std::unique_ptr<BackingDevice> backing_device,
    const FileSystemKeyReference& key_reference,
    Platform* platform,
    std::unique_ptr<brillo::DeviceMapper> device_mapper)
    : dmcrypt_device_name_(config.dmcrypt_device_name),
      dmcrypt_cipher_(config.dmcrypt_cipher),
      mkfs_opts_(config.mkfs_opts),
      tune2fs_opts_(config.tune2fs_opts),
      backing_device_(std::move(backing_device)),
      key_reference_(key_reference),
      platform_(platform),
      device_mapper_(std::move(device_mapper)) {}

DmcryptContainer::DmcryptContainer(
    const DmcryptConfig& config,
    std::unique_ptr<BackingDevice> backing_device,
    const FileSystemKeyReference& key_reference,
    Platform* platform)
    : DmcryptContainer(config,
                       std::move(backing_device),
                       key_reference,
                       platform,
                       std::make_unique<brillo::DeviceMapper>()) {}

bool DmcryptContainer::Purge() {
  // Stale dm-crypt containers may need an extra teardown before purging the
  // device.
  ignore_result(Teardown());

  return backing_device_->Purge();
}

bool DmcryptContainer::Exists() {
  return backing_device_->Exists();
}

bool DmcryptContainer::Setup(const FileSystemKey& encryption_key) {
  // Check whether the kernel keyring provisioning is supported by the current
  // kernel.
  bool keyring_support =
      IsKernelKeyringSupported(device_mapper_->GetTargetVersion("crypt"));
  bool created = false;
  if (!backing_device_->Exists()) {
    if (!backing_device_->Create()) {
      LOG(ERROR) << "Failed to create backing device";
      return false;
    }
    created = true;
  }

  // Ensure that the dm-crypt device or the underlying backing device are
  // not left attached on the failure paths. If the backing device was created
  // during setup, purge it as well.
  base::ScopedClosureRunner device_cleanup_runner;

  if (created) {
    device_cleanup_runner = base::ScopedClosureRunner(base::BindOnce(
        base::IgnoreResult(&DmcryptContainer::Purge), base::Unretained(this)));
  } else {
    device_cleanup_runner = base::ScopedClosureRunner(
        base::BindOnce(base::IgnoreResult(&DmcryptContainer::Teardown),
                       base::Unretained(this)));
  }

  if (!backing_device_->Setup()) {
    LOG(ERROR) << "Failed to setup backing device";
    return false;
  }

  base::Optional<base::FilePath> backing_device_path =
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

  brillo::SecureBlob key_descriptor =
      brillo::SecureBlobToSecureHex(encryption_key.fek);

  // If the dm-crypt kernel driver supports using the keyring, use it.
  if (keyring_support) {
    LOG(INFO) << "Using kernel keyring to provision key to dm-crypt.";
    brillo::SecureBlob keyring_description =
        GenerateKeyringDescription(key_reference_.fek_sig);

    if (!AddLogonKey(encryption_key.fek, keyring_description)) {
      LOG(ERROR) << "Failed to insert logon key to session keyring.";
      return false;
    }

    // Once the key is inserted, update the key descriptor.
    key_descriptor = GenerateDmcryptKeyDescriptor(keyring_description,
                                                  encryption_key.fek.size());
  }

  base::FilePath dmcrypt_device_path =
      base::FilePath("/dev/mapper").Append(dmcrypt_device_name_);
  uint64_t sectors = blkdev_size / kSectorSize;
  brillo::SecureBlob dm_parameters =
      brillo::DevmapperTable::CryptCreateParameters(
          // cipher.
          dmcrypt_cipher_,
          // encryption key descriptor.
          key_descriptor,
          // iv offset.
          0,
          // device path.
          *backing_device_path,
          // device offset.
          0,
          // allow discards.
          true);
  brillo::DevmapperTable dm_table(0, sectors, "crypt", dm_parameters);
  if (!device_mapper_->Setup(dmcrypt_device_name_, dm_table)) {
    backing_device_->Teardown();
    LOG(ERROR) << "dm_setup failed";
    return false;
  }

  // Once the key has been used by dm-crypt, remove it from the keyring.
  if (keyring_support) {
    LOG(INFO) << "Removing provisioned dm-crypt key from kernel keyring.";
    brillo::SecureBlob keyring_description =
        GenerateKeyringDescription(key_reference_.fek_sig);
    if (!UnlinkLogonKey(keyring_description)) {
      LOG(ERROR) << "Failed to remove key";
      return false;
    }
  }

  // Wait for the dmcrypt device path to show up before continuing to setting
  // up the filesystem.
  if (!platform_->UdevAdmSettle(dmcrypt_device_path, true)) {
    LOG(ERROR) << "udevadm settle failed.";
    return false;
  }

  // Create filesystem.
  if (created && !platform_->FormatExt4(dmcrypt_device_path, mkfs_opts_, 0)) {
    PLOG(ERROR) << "Failed to format ext4 filesystem";
    return false;
  }

  // Modify features depending on whether we already have the following enabled.
  if (!tune2fs_opts_.empty() &&
      !platform_->Tune2Fs(dmcrypt_device_path, tune2fs_opts_)) {
    PLOG(ERROR) << "Failed to tune ext4 filesystem";
    return false;
  }

  ignore_result(device_cleanup_runner.Release());
  return true;
}

bool DmcryptContainer::SetLazyTeardownWhenUnused() {
  if (!device_mapper_->Remove(dmcrypt_device_name_, true /* deferred */)) {
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

bool DmcryptContainer::Teardown() {
  if (!device_mapper_->Remove(dmcrypt_device_name_)) {
    LOG(ERROR) << "Failed to teardown device mapper device.";
    return false;
  }

  if (!backing_device_->Teardown()) {
    LOG(ERROR) << "Failed to teardown backing device";
    return false;
  }

  return true;
}

base::FilePath DmcryptContainer::GetBackingLocation() const {
  if (backing_device_ != nullptr && backing_device_->GetPath().has_value()) {
    return *(backing_device_->GetPath());
  }
  return base::FilePath();
}

}  // namespace cryptohome
