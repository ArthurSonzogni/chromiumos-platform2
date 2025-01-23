// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/stateful_mount.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/key_value_store.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/hkdf.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>
#include <metrics/bootstat.h>
#include <rootdev/rootdev.h>

#include "base/strings/stringprintf.h"
#include "init/startup/constants.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/security_manager.h"
#include "init/startup/startup_dep_impl.h"
#include "init/utils.h"

namespace {

constexpr char kExt4Features[] = "sys/fs/ext4/features";
constexpr char kReservedBlocksGID[] = "20119";
constexpr char kQuotaOpt[] = "quota";
constexpr char kDumpe2fsStatefulLog[] =
    "run/chromeos_startup/dumpe2fs_stateful.log";
constexpr char kDirtyExpireCentisecs[] = "proc/sys/vm/dirty_expire_centisecs";

constexpr char kUpdateAvailable[] = ".update_available";
constexpr char kLabMachine[] = ".labmachine";
constexpr char kDevModeFile[] = ".developer_mode";

constexpr char kVar[] = "var";
constexpr char kVarNew[] = "var_new";
constexpr char kVarOverlay[] = "var_overlay";
constexpr char kChronos[] = "chronos";
constexpr char kUnencrypted[] = "unencrypted";

constexpr char kDevImageBlockFile[] = "dev_image.block";
constexpr char kNewDevImageBlockFile[] = "dev_image_new.block";
constexpr char kDeveloperToolsMount[] = "developer_tools";
constexpr char kEncrypted[] = "defaultkey_encrypted";

constexpr char kVarLogAsan[] = "var/log/asan";
constexpr char kStatefulDevImage[] = "dev_image";
constexpr char kStatefulDevImageNew[] = "dev_image_new";
constexpr char kUsrLocal[] = "usr/local";
constexpr char kTmpPortage[] = "var/tmp/portage";
constexpr char kProcMounts[] = "proc/mounts";
constexpr char kMountOptionsLog[] = "var/log/mount_options.log";
constexpr char kPreserve[] = "preserve";
const std::vector<const char*> kMountDirs = {"db/pkg", "lib/portage",
                                             "cache/dlc-images"};

constexpr float kSizePercent = 0.9;

uint64_t GetDirtyExpireCentisecs(libstorage::Platform* platform,
                                 const base::FilePath& root) {
  std::string dirty_expire;
  uint64_t dirty_expire_centisecs = 0;
  base::FilePath centisecs_path = root.Append(kDirtyExpireCentisecs);
  if (!platform->ReadFileToString(centisecs_path, &dirty_expire)) {
    PLOG(WARNING) << "Failed to read " << centisecs_path.value();
    return 0;
  }

  base::TrimWhitespaceASCII(dirty_expire, base::TRIM_ALL, &dirty_expire);
  if (!base::StringToUint64(dirty_expire, &dirty_expire_centisecs)) {
    PLOG(WARNING) << "Failed to parse contents of " << centisecs_path.value();
  }
  return dirty_expire_centisecs;
}
}  // namespace

namespace startup {

using ::hwsec_foundation::Hkdf;
using ::hwsec_foundation::HkdfHash;

StatefulMount::StatefulMount(const base::FilePath& root,
                             const base::FilePath& stateful,
                             libstorage::Platform* platform,
                             StartupDep* startup_dep)
    : root_(root),
      stateful_(stateful),
      platform_(platform),
      startup_dep_(startup_dep) {}

void StatefulMount::AppendQuotaFeaturesAndOptions(
    const Flags* flags,
    std::vector<std::string>* sb_options,
    std::vector<std::string>* sb_features) {
  // Enable/disable quota feature.
  // Add Android's AID_RESERVED_DISK to resgid.
  sb_features->push_back("-g");
  sb_features->push_back(kReservedBlocksGID);

  // Quota is enabled in the kernel, make sure that quota is enabled in
  // the filesystem
  sb_options->push_back(kQuotaOpt);
  sb_features->push_back("-Qusrquota,grpquota");
  if (flags->prjquota) {
    sb_features->push_back("-Qprjquota");
  } else {
    sb_features->push_back("-Q^prjquota");
  }
}

std::vector<std::string> StatefulMount::GenerateExt4Features(
    const Flags* flags) {
  std::vector<std::string> sb_features;
  std::vector<std::string> sb_options;

  base::FilePath encryption = root_.Append(kExt4Features).Append("encryption");
  if (flags->direncryption && platform_->FileExists(encryption)) {
    sb_options.push_back("encrypt");
  }

  base::FilePath verity_file = root_.Append(kExt4Features).Append("verity");
  if (flags->fsverity && platform_->FileExists(verity_file)) {
    sb_options.push_back("verity");
  }

  AppendQuotaFeaturesAndOptions(flags, &sb_options, &sb_features);

  if (!sb_features.empty() || !sb_options.empty()) {
    if (!sb_options.empty()) {
      std::string opts = base::JoinString(sb_options, ",");
      sb_features.push_back("-O");
      sb_features.push_back(opts);
    }
  }

  return sb_features;
}

// Only called in MountStateful(), trigger a clobber.
void StatefulMount::ClobberStateful(
    const base::FilePath& stateful_device,
    const std::vector<std::string>& clobber_args,
    const std::string& clobber_message) {
  startup_dep_->BootAlert("self_repair");
  startup_dep_->ClobberLogRepair(stateful_device, clobber_message);
  startup_dep_->AddClobberCrashReport(
      {"--mount_failure", "--mount_device=stateful"});
  startup_dep_->Clobber(clobber_args);
}

bool StatefulMount::AttemptStatefulMigration(
    const base::FilePath& stateful_device) {
  std::unique_ptr<brillo::Process> thinpool_migrator =
      platform_->CreateProcessInstance();
  thinpool_migrator->AddArg("/usr/sbin/thinpool_migrator");
  thinpool_migrator->AddArg(
      base::StringPrintf("--device=%s", stateful_device.value().c_str()));

  if (thinpool_migrator->Run() != 0) {
    LOG(ERROR) << "Failed to run thinpool migrator";
    return false;
  }

  return true;
}

void StatefulMount::MountStateful(
    const base::FilePath& root_dev,
    const Flags* flags,
    MountHelper* mount_helper,
    const base::Value& image_vars,
    std::optional<encryption::EncryptionKey> key) {
  const auto& image_vars_dict = image_vars.GetDict();
  bool status;
  int32_t stateful_mount_flags;
  std::string stateful_mount_opts;
  libstorage::StorageContainerConfig config;

  // Find our stateful partition mount point.
  stateful_mount_flags = kCommonMountFlags | MS_NOATIME;
  const int part_num_state = utils::GetPartitionNumFromImageVars(
      image_vars_dict, "PARTITION_NUM_STATE");
  const std::string* fs_form_state =
      image_vars_dict.FindString("FS_FORMAT_STATE");
  base::FilePath backing_device =
      brillo::AppendPartition(root_dev, part_num_state);
  if (fs_form_state != nullptr && fs_form_state->compare("ext4") == 0) {
    int dirty_expire_centisecs = GetDirtyExpireCentisecs(platform_, root_);
    int commit_interval = dirty_expire_centisecs / 100;
    if (commit_interval != 0) {
      stateful_mount_opts = "commit=" + std::to_string(commit_interval);
      stateful_mount_opts.append(",discard");
    } else {
      LOG(INFO) << "Using default value for commit interval";
      stateful_mount_opts = "discard";
    }
  }

  bool should_mount_lvm = false;
  std::optional<brillo::Thinpool> thinpool;
  if (USE_LVM_STATEFUL_PARTITION && flags->lvm_stateful) {
    brillo::LogicalVolumeManager* lvm = platform_->GetLogicalVolumeManager();

    // Attempt to get a valid volume group name.
    bootstat_.LogEvent("pre-lvm-activation");
    std::optional<brillo::PhysicalVolume> pv =
        lvm->GetPhysicalVolume(backing_device);

    if (!pv && flags->lvm_migration) {
      // Attempt to migrate to thinpool if migration is enabled: if the
      // migration fails, then we expect the stateful partition to be back to
      // its original state.

      if (!AttemptStatefulMigration(backing_device)) {
        LOG(ERROR) << "Failed to migrate stateful partition to a thinpool";
      } else {
        // Reset the physical volume on success from thinpool migration.
        pv = lvm->GetPhysicalVolume(backing_device);
      }
    }

    if (pv && pv->IsValid()) {
      volume_group_ = lvm->GetVolumeGroup(*pv);
      if (volume_group_ && volume_group_->IsValid()) {
        // First attempt to activate the thinpool. If the activation of the
        // thinpool fails, run thin_check to check all mappings.
        thinpool = lvm->GetThinpool(*volume_group_, "thinpool");

        if (!thinpool) {
          LOG(ERROR) << "Thinpool does not exist";
          ClobberStateful(backing_device, {"fast", "keepimg"},
                          "Invalid thinpool");
          // Not reached, except during unit tests.
          return;
        }

        if (!thinpool->Activate()) {
          LOG(WARNING) << "Failed to activate thinpool, attempting repair";
          if (!thinpool->Activate(/*check=*/true)) {
            LOG(ERROR) << "Failed to repair and activate thinpool";
            ClobberStateful(backing_device, {"fast", "keepimg"},
                            "Corrupt thinpool");
            // Not reached, except during unit tests.
            return;
          }
        }
        should_mount_lvm = true;
      }
    }
    bootstat_.LogEvent("lvm-activation-complete");
  }

  libstorage::StorageContainerType backend_type;
  libstorage::FileSystemKeyReference key_reference;
  libstorage::FileSystemKey encryption_key;
  if (key) {
    config.dmsetup_config = {
        .backing_device_config = {.type =
                                      libstorage::BackingDeviceType::kPartition,
                                  .name = backing_device.value()},
        .dmsetup_device_name = kEncrypted,
        .dmsetup_cipher = std::string("aes-xts-plain64")};
    backend_type = libstorage::StorageContainerType::kDmDefaultKey;
    // Not really needed, default_key does not use the keyring.
    key_reference.fek_sig = brillo::SecureBlob(kEncrypted);
    Hkdf(HkdfHash::kSha512, key->encryption_key(),
         /*info=*/brillo::BlobFromString(kEncrypted),
         /*salt=*/brillo::Blob(),
         /*result_len=*/0, &encryption_key.fek);
    stateful_mount_opts.append(",inlinecrypt");
  } else {
    backend_type = libstorage::StorageContainerType::kUnencrypted;
    key_reference = libstorage::FileSystemKeyReference();
    encryption_key = libstorage::FileSystemKey();
    if (should_mount_lvm) {
      config.unencrypted_config = {
          .backing_device_config = {
              .type =
                  libstorage::BackingDeviceType::kLogicalVolumeBackingDevice,
              .name = kUnencrypted,
              .logical_volume = {
                  .vg = std::make_shared<brillo::VolumeGroup>(*volume_group_),
                  .thinpool = std::make_shared<brillo::Thinpool>(*thinpool)}}};
    } else {
      config.unencrypted_config = {
          .backing_device_config = {
              .type = libstorage::BackingDeviceType::kPartition,
              .name = backing_device.value()}};
    }
  }
  config.filesystem_config = {.tune2fs_opts = GenerateExt4Features(flags),
                              .backend_type = backend_type,
                              .recovery = libstorage::RecoveryType::kDoNothing,
                              .metrics_prefix = "Platform.FileSystem.Stateful"};

  if (key && key->is_fresh()) {
    // Need to reformat the container first. But since the partition already
    // exists, the Ext4 storage container will try to use the current
    // filesystem, since the dmsetup storage container also base its existence
    // logic to the presence of the backup device. Force a purge on fsck failure
    // which will happen.
    config.filesystem_config.recovery = libstorage::RecoveryType::kPurge;
    // Do not discard to preserve the pass-through files.
    config.filesystem_config.mkfs_opts = {
        "-E",
        "nodiscard",
        "-O",
        "stable_inodes,encrypt",
    };
  }

  std::unique_ptr<libstorage::StorageContainer> container =
      mount_helper->GetStorageContainerFactory()->Generate(
          config, libstorage::StorageContainerType::kExt4, key_reference);

  if (!container) {
    LOG(ERROR) << "Failed to create stateful container";

    ClobberStateful(backing_device, {"fast", "keepimg", "preserve_lvs"},
                    "Self-repair corrupted stateful partition");
    // Not reached, except during unit tests.
    return;
  }

  if (!container->Setup(encryption_key)) {
    LOG(ERROR) << "Failed to setup stateful";

    ClobberStateful(backing_device, {"fast", "keepimg", "preserve_lvs"},
                    "Self-repair corrupted stateful partition");
    // Not reached, except during unit tests.
    return;
  }

  state_dev_ = container->GetPath();
  // Mount stateful partition from state_dev.
  if (!platform_->Mount(state_dev_, stateful_, *fs_form_state,
                        stateful_mount_flags, stateful_mount_opts)) {
    // Try to rebuild the stateful partition by clobber-state. (Not using fast
    // mode out of security consideration: the device might have gotten into
    // this state through power loss during dev mode transition).
    platform_->ReportFilesystemDetails(state_dev_,
                                       root_.Append(kDumpe2fsStatefulLog));
    ClobberStateful(state_dev_, {"keepimg", "preserve_lvs"},
                    "Self-repair corrupted stateful partition");
    // Not reached, except during unit tests.
    return;
  }

  // Mount the OEM partition.
  // mount_or_fail isn't used since this partition only has a filesystem
  // on some boards.
  int32_t oem_flags = MS_RDONLY | kCommonMountFlags;
  const int part_num_oem =
      utils::GetPartitionNumFromImageVars(image_vars_dict, "PARTITION_NUM_OEM");
  const std::string* fs_form_oem = image_vars_dict.FindString("FS_FORMAT_OEM");
  const base::FilePath oem_dev =
      brillo::AppendPartition(root_dev, part_num_oem);
  status = platform_->Mount(oem_dev, base::FilePath("/usr/share/oem"),
                            *fs_form_oem, oem_flags, "");
  if (!status) {
    PLOG(WARNING) << "mount of /usr/share/oem failed with code " << status;
  }
}

base::FilePath StatefulMount::GetStateDev() {
  return state_dev_;
}

// Remove empty directories that should not be preserved.
void StatefulMount::RemoveEmptyDirectory(
    std::vector<base::FilePath> preserved_paths, base::FilePath directory) {
  std::unique_ptr<libstorage::FileEnumerator> enumerator(
      platform_->GetFileEnumerator(directory, false /* recursive */,
                                   base::FileEnumerator::DIRECTORIES));
  for (auto path = enumerator->Next(); !path.empty();
       path = enumerator->Next()) {
    if (platform_->IsLink(path)) {
      continue;
    }
    bool preserve = false;
    for (auto& preserved_path : preserved_paths) {
      if (path == preserved_path || preserved_path.IsParent(path)) {
        preserve = true;
        break;
      }
    }
    if (!preserve) {
      RemoveEmptyDirectory(preserved_paths, path);
      // Do not remove mounts for /var and /home/chronos,
      // they have been already created during the mount of stateful.
      std::vector<const char*> mounts = {kVar, kChronos};
      if (platform_->IsDirectoryEmpty(path) &&
          (std::find(mounts.begin(), mounts.end(), path.BaseName().value()) ==
           mounts.end())) {
        platform_->DeleteFile(path);
      }
    }
  }
}

void StatefulMount::DevPerformStatefulUpdate() {
  // Perform update.
  std::vector<std::pair<base::FilePath, base::FilePath>> update_targets = {
      {stateful_.Append(kVarNew), stateful_.Append(kVarOverlay)},
      {stateful_.Append(kStatefulDevImageNew),
       stateful_.Append(kStatefulDevImage)},
      {stateful_.Append(kUnencrypted).Append(kNewDevImageBlockFile),
       stateful_.Append(kUnencrypted).Append(kDevImageBlockFile)}};

  for (auto& [src, dst] : update_targets) {
    // Cleanup old target directories.
    if (!platform_->DeletePathRecursively(dst)) {
      PLOG(WARNING) << "Failed to delete " << dst;
    }

    if (!platform_->Rename(src, dst, true)) {
      LOG(WARNING) << "Failed to rename " << src;
      continue;
    }

    if (!platform_->SetPermissions(dst, 0755)) {
      PLOG(WARNING) << "chmod failed for " << dst.value();
    }
  }
}

// Updates stateful partition if pending
// update is available.
// Returns true if there is no need to update or successful update.
bool StatefulMount::DevUpdateStatefulPartition(
    const std::string& args, bool enable_stateful_security_hardening) {
  base::FilePath stateful_update_file = stateful_.Append(kUpdateAvailable);
  std::string stateful_update_args = args;
  if (stateful_update_args.empty()) {
    if (!platform_->ReadFileToString(stateful_update_file,
                                     &stateful_update_args)) {
      PLOG(WARNING) << "Failed to read from " << stateful_update_file.value();
      return true;
    }
    // The file often ends with a new line.
    base::TrimString(stateful_update_args, "\n", &stateful_update_args);
  }

  // To remain compatible with the prior update_stateful tarballs, expect
  // the "var_new" unpack location, but move it into the new "var_overlay"
  // target location.
  base::FilePath var_new = stateful_.Append(kVarNew);
  base::FilePath developer_new = stateful_.Append(kStatefulDevImageNew);
  base::FilePath stateful_dev_image = stateful_.Append(kStatefulDevImage);
  base::FilePath var_target = stateful_.Append(kVarOverlay);
  base::FilePath dev_image_block_new =
      stateful_.Append(kUnencrypted).Append(kNewDevImageBlockFile);

  // Only replace the developer and var_overlay directories if new replacements
  // are available.
  if ((platform_->DirectoryExists(developer_new) &&
       platform_->DirectoryExists(var_new)) ||
      platform_->FileExists(dev_image_block_new)) {
    std::string update = "Updating from " + developer_new.value() + " && " +
                         var_new.value() + ".";
    startup_dep_->ClobberLog(update);
    DevPerformStatefulUpdate();
  } else {
    std::string update = "Stateful update did not find " +
                         developer_new.value() + " & " + var_new.value() +
                         ".'\n'Keeping old development tools.";
    startup_dep_->ClobberLog(update);
  }

  // Check for clobber.
  if (stateful_update_args.compare("clobber") == 0) {
    // Because we want to preserve the testing tools under the /usr/local for
    // test image, we only delete the cryptohome related LVs here.
    // volume_group_ may be null if it was not found in MountStateful.
    if (USE_LVM_STATEFUL_PARTITION && volume_group_ &&
        volume_group_->IsValid()) {
      volume_group_->Activate();
      brillo::LogicalVolumeManager* lvm = platform_->GetLogicalVolumeManager();
      std::vector<brillo::LogicalVolume> lvs =
          lvm->ListLogicalVolumes(volume_group_.value(), "cryptohome*");
      for (auto& lv : lvs) {
        if (!lv.Remove()) {
          LOG(WARNING) << "Failed to remove logical volume: " << lv.GetName();
        }
      }
    }

    base::FilePath preserve_dir =
        stateful_.Append(kUnencrypted).Append(kPreserve);
    base::FilePath dlc_factory_dir =
        stateful_.Append("unencrypted/dlc-factory-images");

    // Find everything in stateful and delete it, except for protected paths,
    // and non-empty directories. The non-empty directories contain protected
    // content or they would already be empty from depth first traversal.
    std::vector<base::FilePath> preserved_paths = {
        stateful_.Append(kLabMachine),
        stateful_.Append(kDevModeFile),
        stateful_.Append("encrypted.block"),
        stateful_.Append("encrypted.key"),
        stateful_.Append(kUnencrypted).Append(kDevImageBlockFile),
        stateful_.Append(kDeveloperToolsMount),
        stateful_dev_image,
        var_target,
        preserve_dir,
        dlc_factory_dir};
    if (enable_stateful_security_hardening) {
      // Allow traversal of preserve_dir, it contains link for /var/log
      // Allow traversal of /var and dev_image: they may have been just created,
      // and are usually allowed later.
      for (auto& preserved_path : preserved_paths) {
        if (platform_->DirectoryExists(preserved_path)) {
          AllowSymlink(platform_, root_, preserved_path.value());
        }
      }
    }

    std::unique_ptr<libstorage::FileEnumerator> enumerator(
        platform_->GetFileEnumerator(stateful_, true,
                                     base::FileEnumerator::FILES));
    for (auto path = enumerator->Next(); !path.empty();
         path = enumerator->Next()) {
      bool preserve = false;
      for (auto& preserved_path : preserved_paths) {
        if (path == preserved_path || preserved_path.IsParent(path)) {
          preserve = true;
          break;
        }
      }
      if (!preserve) {
        platform_->DeleteFile(path);
      }
    }

    // Remove the empty directories
    RemoveEmptyDirectory(preserved_paths, stateful_);

    // Let's really be done before coming back.
    sync();

    if (enable_stateful_security_hardening) {
      // Reapply base symlink exemption if needed.
      SymlinkExceptions(platform_, root_);
    }
  }

  platform_->DeleteFile(stateful_update_file);
  return true;
}

// Gather logs.
void StatefulMount::DevGatherLogs(const base::FilePath& base_dir) {
  // For dev/test images, if .gatherme presents, copy files listed in .gatherme
  // to /mnt/stateful_partition/unencrypted/prior_logs.
  base::FilePath lab_preserve_logs = stateful_.Append(".gatherme");
  base::FilePath prior_log_dir =
      stateful_.Append(kUnencrypted).Append("prior_logs");
  std::string log_path;

  if (!platform_->FileExists(lab_preserve_logs)) {
    return;
  }

  std::string files;
  platform_->ReadFileToString(lab_preserve_logs, &files);
  std::vector<std::string> split_files = base::SplitString(
      files, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (std::string log_path : split_files) {
    if (log_path.find("#") != std::string::npos) {
      continue;
    }
    base::FilePath log(log_path);
    if (platform_->DirectoryExists(log)) {
      if (!platform_->Copy(log, prior_log_dir)) {
        PLOG(WARNING) << "Failed to copy directory " << log_path;
      }
    } else {
      if (!platform_->Copy(log, prior_log_dir.Append(log.BaseName()))) {
        PLOG(WARNING) << "Failed to copy file " << log_path;
      }
    }
  }

  if (!platform_->DeleteFile(lab_preserve_logs)) {
    PLOG(WARNING) << "Failed to delete file: " << lab_preserve_logs;
  }
}

void StatefulMount::SetUpDirectory(const base::FilePath& path) {
  if (!platform_->DirectoryExists(path)) {
    if (!platform_->CreateDirectory(path)) {
      PLOG(ERROR) << "Failed to create " << path.value();
      return;
    }
    if (!platform_->SetPermissions(path, 0755)) {
      PLOG(ERROR) << "Failed to set permissions for " << path.value();
    }
  }
}

void StatefulMount::DevMountDevImage(MountHelper* mount_helper) {
  base::FilePath dev_image_block(
      stateful_.Append(kUnencrypted).Append(kDevImageBlockFile));

  if (!base::PathExists(dev_image_block)) {
    return;
  }

  auto file_size = base::GetFileSize(dev_image_block);
  if (!file_size) {
    LOG(ERROR) << "Failed to get dev_image.block size";
    return;
  }

  // Check if we need to expand the dev_image.block file.
  struct statvfs stateful_statbuf;
  if (!platform_->StatVFS(stateful_, &stateful_statbuf)) {
    PLOG(ERROR) << "stat() failed on: " << stateful_;
    return;
  }

  int64_t expected_file_size = static_cast<int64_t>(stateful_statbuf.f_blocks);
  expected_file_size *= kSizePercent;
  expected_file_size *= stateful_statbuf.f_frsize;

  if (expected_file_size > file_size) {
    base::File file;
    platform_->InitializeFile(&file, dev_image_block,
                              base::File::FLAG_OPEN | base::File::FLAG_WRITE);

    if (!file.IsValid()) {
      LOG(ERROR) << "Unable to open backing device";
      return;
    }

    LOG(INFO) << "Expanding underlying sparse file to " << expected_file_size;
    file.SetLength(expected_file_size);
  }

  libstorage::StorageContainerConfig container_config = {
      .filesystem_config =
          {.tune2fs_opts = {},
           .backend_type = libstorage::StorageContainerType::kUnencrypted,
           .recovery = libstorage::RecoveryType::kEnforceCleaning,
           .metrics_prefix = "Platform.FileSystem.DeveloperTools"},

      .unencrypted_config =
          {.backing_device_config =
               {.type = libstorage::BackingDeviceType::kLoopbackDevice,
                .name = "developer_tools",
                .size = *file_size,
                .loopback = {.backing_file_path = dev_image_block}}},
  };

  std::unique_ptr<libstorage::StorageContainer> container =
      mount_helper->GetStorageContainerFactory()->Generate(
          container_config, libstorage::StorageContainerType::kExt4,
          libstorage::FileSystemKeyReference());
  if (!container) {
    LOG(ERROR) << "Failed to create ext4 container for developer tools";
    return;
  }

  if (!container->Setup(libstorage::FileSystemKey())) {
    LOG(ERROR) << "Failed to set up developer tools container.";
    return;
  }

  if (expected_file_size > file_size && !container->Resize(0)) {
    LOG(ERROR) << "Failed to resize the developer tools container";
  }

  base::FilePath developer_tools_mount = stateful_.Append(kDeveloperToolsMount);
  // Create developer_tools directory in base images in developer mode.
  SetUpDirectory(developer_tools_mount);
  if (!platform_->Mount(container->GetPath(), developer_tools_mount, "ext4",
                        kCommonMountFlags, "commit=600,discard")) {
    LOG(WARNING) << "Failed to mount developer tools filesystem";
    return;
  }

  SetUpDirectory(developer_tools_mount.Append(kStatefulDevImage));
  SetUpDirectory(developer_tools_mount.Append(kVarOverlay));

  mount_helper->BindMountOrFail(developer_tools_mount.Append(kStatefulDevImage),
                                stateful_.Append(kStatefulDevImage));
  mount_helper->BindMountOrFail(developer_tools_mount.Append(kVarOverlay),
                                stateful_.Append(kVarOverlay));
}

void StatefulMount::DevMountPackages(MountHelper* mount_helper,
                                     bool enable_stateful_security_hardening) {
  // Set up the logging dir that ASAN compiled programs will write to. We want
  // any privileged account to be able to write here so unittests need not worry
  // about setting things up ahead of time. See crbug.com/453579 for details.
  base::FilePath asan_dir = root_.Append(kVarLogAsan);
  if (!platform_->CreateDirectory(asan_dir)) {
    PLOG(WARNING) << "Unable to create /var/log/asan directory, error code.";
  }
  if (!platform_->SetPermissions(asan_dir, 01777)) {
    PLOG(WARNING) << "Set permissions failed for /var/log/asan";
  }

  // Capture a snapshot of "normal" mount state here, for auditability,
  // before we start applying devmode-specific changes.
  std::string mount_contents;
  base::FilePath proc_mounts = root_.Append(kProcMounts);
  if (!platform_->ReadFileToString(proc_mounts, &mount_contents)) {
    PLOG(ERROR) << "Reading from " << proc_mounts.value() << " failed.";
  }

  base::FilePath mount_options = root_.Append(kMountOptionsLog);
  if (!platform_->WriteStringToFile(mount_options, mount_contents)) {
    PLOG(ERROR) << "Writing " << proc_mounts.value()
                << "to mount_options.log failed.";
  }

  // Create dev_image directory in base images in developer mode.
  base::FilePath stateful_dev_image = stateful_.Append(kStatefulDevImage);
  SetUpDirectory(stateful_dev_image);

  // Checks and updates stateful partition.
  DevUpdateStatefulPartition("", enable_stateful_security_hardening);

  // Checks for dev_image.block and mounts it in place.
  DevMountDevImage(mount_helper);

  // Mount and then remount to enable exec/suid.
  base::FilePath usrlocal = root_.Append(kUsrLocal);
  mount_helper->BindMountOrFail(stateful_dev_image, usrlocal);
  if (!platform_->Mount(base::FilePath(), usrlocal, "", MS_REMOUNT, "")) {
    PLOG(WARNING) << "Failed to remount " << usrlocal.value();
  }

  if (enable_stateful_security_hardening) {
    // Add exceptions to allow symlink traversal and opening of FIFOs in the
    // dev_image subtree.
    for (const auto& path : {root_.Append(kTmpPortage), stateful_dev_image}) {
      if (!platform_->DirectoryExists(path)) {
        if (!platform_->CreateDirectory(path)) {
          PLOG(ERROR) << "Failed to create " << path.value();
        }
        if (!platform_->SetPermissions(path, 0755)) {
          PLOG(ERROR) << "Failed to set permissions for " << path.value();
        }
      }
      AllowSymlink(platform_, root_, path.value());
      AllowFifo(platform_, root_, path.value());
    }
  }

  // Set up /var elements needed for deploying packages.
  base::FilePath base = stateful_.Append("var_overlay");
  if (platform_->DirectoryExists(base)) {
    for (const auto dir : kMountDirs) {
      base::FilePath full = base.Append(dir);
      if (!platform_->DirectoryExists(full)) {
        continue;
      }
      base::FilePath dest = root_.Append(kVar).Append(dir);
      if (!platform_->DirectoryExists(dest)) {
        if (!platform_->CreateDirectory(dest)) {
          LOG(WARNING) << "Path does not exists, can not create: "
                       << dest.value();
          continue;
        }
        if (!platform_->SetPermissions(dest, 0755)) {
          LOG(WARNING) << "Path does not exists, can not set permissions: "
                       << dest.value();
          continue;
        }
      }
      mount_helper->BindMountOrFail(full, dest);
    }
  }
}

}  // namespace startup
