// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/mount_encrypted/encrypted_fs.h"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include <memory>
#include <optional>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/backing_device.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>
#include <libstorage/storage_container/storage_container_factory.h>

namespace mount_encrypted {

namespace {

constexpr char kEncryptedFSType[] = "ext4";
constexpr char kCryptDevName[] = "encstateful";
constexpr char kDevMapperPath[] = "/dev/mapper";
constexpr char kDumpe2fsLogPath[] = "/run/mount_encrypted/dumpe2fs.log";
constexpr char kProcDirtyExpirePath[] = "/proc/sys/vm/dirty_expire_centisecs";
constexpr float kSizePercent = 0.3;
constexpr uint64_t kExt4BlockSize = 4096;
// Block size is 4k => Minimum free space available to try resizing is 400MB.
constexpr int64_t kMinBlocksAvailForResize = 102400;
constexpr char kExt4ExtendedOptions[] = "discard";
constexpr char kDmCryptDefaultCipher[] = "aes-cbc-essiv:sha256";

bool CheckBind(libstorage::Platform* platform, const BindMount& bind) {
  if (platform->Access(bind.src, R_OK) &&
      !platform->CreateDirectory(bind.src)) {
    PLOG(ERROR) << "mkdir " << bind.src;
    return false;
  }

  if (platform->Access(bind.dst, R_OK) &&
      !(platform->CreateDirectory(bind.dst) &&
        platform->SetPermissions(bind.dst, bind.mode))) {
    PLOG(ERROR) << "mkdir " << bind.dst;
    return false;
  }

  // Destination may be on read-only filesystem, so skip tweaks.
  // Must do explicit chmod since mkdir()'s mode respects umask.
  if (!platform->SetPermissions(bind.src, bind.mode)) {
    PLOG(ERROR) << "chmod " << bind.src;
    return false;
  }
  if (!platform->SetOwnership(bind.src, bind.owner, bind.group, true)) {
    PLOG(ERROR) << "chown " << bind.src;
    return false;
  }

  return true;
}

std::string GetMountOpts() {
  // Use vm.dirty_expire_centisecs / 100 as the commit interval.
  std::string dirty_expire;
  uint64_t dirty_expire_centisecs;
  uint64_t commit_interval = 600;

  if (base::ReadFileToString(base::FilePath(kProcDirtyExpirePath),
                             &dirty_expire)) {
    base::TrimWhitespaceASCII(dirty_expire, base::TRIM_ALL, &dirty_expire);
    if (!base::StringToUint64(dirty_expire, &dirty_expire_centisecs)) {
      LOG(INFO) << "Failed to parse contents of " << dirty_expire;
    }
    LOG(INFO) << "Using vm.dirty_expire_centisecs/100 as the commit interval";

    // Keep commit interval as 5 seconds (default for ext4) for smaller
    // values of dirty_expire_centisecs.
    if (dirty_expire_centisecs < 600) {
      commit_interval = 5;
    } else {
      commit_interval = dirty_expire_centisecs / 100;
    }
  }
  return "discard,commit=" + std::to_string(commit_interval);
}

}  // namespace

EncryptedFs::EncryptedFs(
    const base::FilePath& rootdir,
    const base::FilePath& statefulmnt,
    uint64_t fs_size,
    const std::string& dmcrypt_name,
    std::unique_ptr<libstorage::StorageContainer> container,
    libstorage::Platform* platform)
    : rootdir_(rootdir),
      fs_size_(fs_size),
      dmcrypt_name_(dmcrypt_name),
      stateful_mount_(statefulmnt),
      dmcrypt_dev_(base::FilePath(kDevMapperPath).Append(dmcrypt_name_)),
      encrypted_mount_(stateful_mount_.Append("encrypted")),
      platform_(platform),
      container_(std::move(container)),
      bind_mounts_(
          {{encrypted_mount_.Append("var"), rootdir_.Append("var"),
            libstorage::kRootUid, libstorage::kRootGid,
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, false},
           {encrypted_mount_.Append("chronos"), rootdir_.Append("home/chronos"),
            libstorage::kChronosUid, libstorage::kChronosGid,
            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH, true}}) {}

// static
std::unique_ptr<EncryptedFs> EncryptedFs::Generate(
    const base::FilePath& rootdir,
    const base::FilePath& statefulmnt,
    libstorage::Platform* platform,
    libstorage::StorageContainerFactory* storage_container_factory) {
  // Calculate the maximum size of the encrypted stateful partition.
  // truncate()/ftruncate() use int64_t for file size.
  struct statvfs stateful_statbuf;
  if (!platform->StatVFS(statefulmnt, &stateful_statbuf)) {
    PLOG(ERROR) << "stat() failed on: " << statefulmnt;
    return nullptr;
  }

  int64_t fs_bytes_max = static_cast<int64_t>(stateful_statbuf.f_blocks);
  fs_bytes_max *= kSizePercent;
  fs_bytes_max *= stateful_statbuf.f_frsize;

  std::string dmcrypt_name = std::string(kCryptDevName);
  if (rootdir != base::FilePath("/")) {
    brillo::SecureBlob digest =
        hwsec_foundation::Sha256(brillo::SecureBlob(rootdir.value()));
    std::string hex = hwsec_foundation::SecureBlobToHex(digest);
    dmcrypt_name += "_" + hex.substr(0, 16);
  }

  // Initialize the encrypted container.
  libstorage::BackingDeviceConfig backing_device_config;

  base::FilePath sparse_backing_file = statefulmnt.Append("encrypted.block");

  base::FilePath stateful_device = platform->GetStatefulDevice();

  // Use the loopback sparse file in 2 cases:
  // 1. If the device is set up using an ext4 stateful partition.
  // 2. If the device already has an existing sparse loopback file: this
  //    situation can occur during migration of a device to an LVM stateful
  //    stateful partition.
  // TODO(sarthakkukreti@): Loopback backing devices use size in bytes whereas
  // logical volume backing devices use size in megabytes. Fix this
  // inconsistency.
  if (!platform->IsStatefulLogicalVolumeSupported() ||
      platform->FileExists(sparse_backing_file)) {
    backing_device_config = {
        .type = libstorage::BackingDeviceType::kLoopbackDevice,
        .name = dmcrypt_name,
        .size = fs_bytes_max,
        .loopback = {.backing_file_path = sparse_backing_file}};
  } else {
    brillo::LogicalVolumeManager* lvm = platform->GetLogicalVolumeManager();
    brillo::PhysicalVolume pv(stateful_device,
                              std::make_shared<brillo::LvmCommandRunner>());
    std::optional<brillo::VolumeGroup> vg = lvm->GetVolumeGroup(pv);
    if (!vg || !vg->IsValid()) {
      LOG(WARNING) << "Failed to get volume group.";
      return nullptr;
    }

    std::optional<brillo::Thinpool> thinpool =
        lvm->GetThinpool(*vg, "thinpool");
    if (!thinpool || !thinpool->IsValid()) {
      LOG(WARNING) << "Failed to get thinpool.";
      return nullptr;
    }

    backing_device_config = {
        .type = libstorage::BackingDeviceType::kLogicalVolumeBackingDevice,
        .name = dmcrypt_name,
        .size = fs_bytes_max / (1024 * 1024),
        .logical_volume = {
            .vg = std::make_shared<brillo::VolumeGroup>(*vg),
            .thinpool = std::make_shared<brillo::Thinpool>(*thinpool)}};
  }

  libstorage::StorageContainerConfig container_config(
      {.filesystem_config =
           {.mkfs_opts = {"-T", "default", "-b", std::to_string(kExt4BlockSize),
                          "-m", "0", "-O", "^huge_file,^flex_bg", "-E",
                          kExt4ExtendedOptions},
            .tune2fs_opts = {},
            .backend_type = libstorage::StorageContainerType::kDmcrypt,
            .recovery = libstorage::RecoveryType::kEnforceCleaning},
       .dmcrypt_config = {
           .backing_device_config = backing_device_config,
           .dmcrypt_device_name = dmcrypt_name,
           .dmcrypt_cipher = std::string(kDmCryptDefaultCipher)}});

  libstorage::FileSystemKeyReference key_reference;
  key_reference.fek_sig = brillo::SecureBlob("encstateful");

  std::unique_ptr<libstorage::StorageContainer> container =
      storage_container_factory->Generate(
          container_config, libstorage::StorageContainerType::kExt4,
          key_reference);

  return std::make_unique<EncryptedFs>(rootdir, statefulmnt, fs_bytes_max,
                                       dmcrypt_name, std::move(container),
                                       platform);
}

bool EncryptedFs::Purge() {
  LOG(INFO) << "Purging block device";
  return container_->Purge();
}

// Do all the work needed to actually set up the encrypted partition.
bool EncryptedFs::Setup(const libstorage::FileSystemKey& encryption_key,
                        bool rebuild) {
  // Get stateful partition statistics. This acts as an indicator of how large
  // we want the encrypted stateful partition to be.
  struct statvfs stateful_statbuf;
  if (!platform_->StatVFS(stateful_mount_, &stateful_statbuf)) {
    PLOG(ERROR) << "stat() failed on: " << stateful_mount_;
    return false;
  }

  if (rebuild) {
    // Wipe out the old files, and ignore errors.
    Purge();

    // Create new sparse file.
    LOG(INFO) << "Creating sparse backing file with size " << fs_size_;
  } else if (!container_->Exists()) {
    // If not rebuilding, we expect the container to be present.
    LOG(ERROR) << "Encrypted container doesn't exist";
    return false;
  }

  if (!container_->Setup(encryption_key)) {
    LOG(ERROR) << "Failed to set up encrypted container";
    TeardownByStage(TeardownStage::kTeardownContainer, true);
    return false;
  }

  // Trigger filesystem resizer, in case growth was interrupted.
  // TODO(keescook): if already full size, don't resize.
  // If there aren't enough blocks
  // available, we might succeed here but eventually fail to resize and corrupt
  // the encrypted stateful file system. Check if there are at least a few
  // blocks available on the stateful partition.
  if (stateful_statbuf.f_bfree > kMinBlocksAvailForResize &&
      !container_->Resize(0)) {
    LOG(ERROR) << "Failed to resize stateful";
    return false;
  }

  // Mount the dm-crypt partition finally.
  LOG(INFO) << "Mounting " << dmcrypt_dev_ << " onto " << encrypted_mount_;
  if (platform_->Access(encrypted_mount_, R_OK) &&
      !(platform_->CreateDirectory(encrypted_mount_) &&
        platform_->SetPermissions(encrypted_mount_,
                                  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))) {
    PLOG(ERROR) << dmcrypt_dev_;
    TeardownByStage(TeardownStage::kTeardownContainer, true);
    return false;
  }
  if (!platform_->Mount(dmcrypt_dev_, encrypted_mount_, kEncryptedFSType,
                        MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_NOATIME,
                        GetMountOpts().c_str())) {
    PLOG(ERROR) << "mount: " << dmcrypt_dev_ << ", " << encrypted_mount_;
    // On failure to mount, use dumpe2fs to collect debugging data about
    // the unencrypted block device that failed to mount. Since mount-encrypted
    // cleans up afterwards, this is the only point where this data can be
    // collected.
    platform_->ReportFilesystemDetails(dmcrypt_dev_,
                                       base::FilePath(kDumpe2fsLogPath));
    TeardownByStage(TeardownStage::kTeardownContainer, true);
    return false;
  }

  // Perform bind mounts.
  for (auto& bind : bind_mounts_) {
    LOG(INFO) << "Bind mounting " << bind.src << " onto " << bind.dst;
    if (!CheckBind(platform_, bind)) {
      TeardownByStage(TeardownStage::kTeardownUnbind, true);
      return false;
    }
    if (!platform_->Bind(bind.src, bind.dst)) {
      PLOG(ERROR) << "mount: " << bind.src << ", " << bind.dst;
      TeardownByStage(TeardownStage::kTeardownUnbind, true);
      return false;
    }
  }

  // Everything completed without error.
  return true;
}

// Clean up all bind mounts, mounts, attaches, etc. Only the final
// action informs the return value. This makes it so that failures
// can be cleaned up from, and continue the shutdown process on a
// second call. If the loopback cannot be found, claim success.
bool EncryptedFs::Teardown() {
  return TeardownByStage(TeardownStage::kTeardownUnbind, false);
}

bool EncryptedFs::TeardownByStage(TeardownStage stage, bool ignore_errors) {
  switch (stage) {
    case TeardownStage::kTeardownUnbind:
      for (auto& bind : bind_mounts_) {
        LOG(INFO) << "Unmounting " << bind.dst;
        errno = 0;
        // Allow either success or a "not mounted" failure.
        if (!platform_->Unmount(bind.dst, false, nullptr) && !ignore_errors) {
          if (errno != EINVAL) {
            PLOG(ERROR) << "umount " << bind.dst;
            return false;
          }
        }
      }

      LOG(INFO) << "Unmounting " << encrypted_mount_;
      errno = 0;
      // Allow either success or a "not mounted" failure.
      if (!platform_->Unmount(encrypted_mount_, false, nullptr) &&
          !ignore_errors) {
        if (errno != EINVAL) {
          PLOG(ERROR) << "umount " << encrypted_mount_;
          return false;
        }
      }

      // Force syncs to make sure we don't tickle racey/buggy kernel
      // routines that might be causing crosbug.com/p/17610.
      platform_->Sync();

      // Intentionally fall through here to teardown the lower dmcrypt device.
      [[fallthrough]];
    case TeardownStage::kTeardownContainer:
      LOG(INFO) << "Removing " << dmcrypt_dev_;
      if (!container_->Teardown() && !ignore_errors) {
        LOG(ERROR) << "Failed to teardown encrypted container";
        return false;
      }
      platform_->Sync();
      return true;
  }

  LOG(ERROR) << "Teardown failed.";
  return false;
}

bool EncryptedFs::CheckStates() {
  // Verify stateful partition exists.
  if (platform_->Access(stateful_mount_, R_OK)) {
    LOG(INFO) << stateful_mount_ << "does not exist.";
    return false;
  }
  // Verify stateful is either a separate mount, or that the
  // root directory is writable (i.e. a factory install, dev mode
  // where root remounted rw, etc).
  if (platform_->SameVFS(stateful_mount_, rootdir_) &&
      platform_->Access(rootdir_, W_OK)) {
    LOG(INFO) << stateful_mount_ << " is not mounted.";
    return false;
  }

  // Verify encrypted partition is missing or not already mounted.
  if (platform_->Access(encrypted_mount_, R_OK) == 0 &&
      !platform_->SameVFS(encrypted_mount_, stateful_mount_)) {
    LOG(INFO) << encrypted_mount_ << " already appears to be mounted.";
    return true;
  }

  // Verify that bind mount targets exist.
  for (auto& bind : bind_mounts_) {
    if (platform_->Access(bind.dst, R_OK)) {
      PLOG(ERROR) << bind.dst << " mount point is missing.";
      return false;
    }
  }

  // Verify that old bind mounts on stateful haven't happened yet.
  for (auto& bind : bind_mounts_) {
    if (bind.submount) {
      continue;
    }

    if (platform_->SameVFS(bind.dst, stateful_mount_)) {
      LOG(INFO) << bind.dst << " already bind mounted.";
      return false;
    }
  }

  LOG(INFO) << "VFS mount state validity check ok.";
  return true;
}

bool EncryptedFs::ReportInfo() const {
  printf("rootdir: %s\n", rootdir_.value().c_str());
  printf("stateful_mount: %s\n", stateful_mount_.value().c_str());
  printf("encrypted_mount: %s\n", encrypted_mount_.value().c_str());
  printf("dmcrypt_name: %s\n", dmcrypt_name_.c_str());
  printf("dmcrypt_dev: %s\n", dmcrypt_dev_.value().c_str());
  printf("bind mounts:\n");
  for (auto& mnt : bind_mounts_) {
    printf("\tsrc:%s\n", mnt.src.value().c_str());
    printf("\tdst:%s\n", mnt.dst.value().c_str());
    printf("\towner:%d\n", mnt.owner);
    printf("\tmode:%o\n", mnt.mode);
    printf("\tsubmount:%d\n", mnt.submount);
    printf("\n");
  }
  return true;
}
}  // namespace mount_encrypted
