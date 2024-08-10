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
#include <base/json/json_reader.h>
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
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>
#include <metrics/bootstat.h>
#include <rootdev/rootdev.h>

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

constexpr char kVar[] = "var";
constexpr char kNew[] = "_new";
constexpr char kOverlay[] = "_overlay";

constexpr char kVarLogAsan[] = "var/log/asan";
constexpr char kStatefulDevImage[] = "dev_image";
constexpr char kUsrLocal[] = "usr/local";
constexpr char kDisableStatefulSecurityHardening[] =
    "usr/share/cros/startup/disable_stateful_security_hardening";
constexpr char kTmpPortage[] = "var/tmp/portage";
constexpr char kProcMounts[] = "proc/mounts";
constexpr char kMountOptionsLog[] = "var/log/mount_options.log";
constexpr char kPartitionsVars[] = "usr/sbin/partition_vars.json";
const std::vector<const char*> kMountDirs = {"db/pkg", "lib/portage",
                                             "cache/dlc-images"};

// TODO(asavery): update the check for removable devices to be
// more advanced, b/209476959
bool RemovableRootdev(const base::FilePath& path, int* ret) {
  base::FilePath removable("/sys/block");
  removable = removable.Append(path.BaseName());
  removable = removable.Append("removable");
  return utils::ReadFileToInt(removable, ret);
}

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

// Get the partition number for the given key,
// e.g. "PARTITION_NUM_STATE". Fails with a `CHECK()` if any error occurs.
int GetPartitionNumFromImageVars(const base::Value::Dict& image_dict,
                                 std::string_view key) {
  const std::string* value = image_dict.FindString(key);
  CHECK_NE(value, nullptr);
  int num = 0;
  CHECK(base::StringToInt(*value, &num));
  return num;
}

}  // namespace

namespace startup {

StatefulMount::StatefulMount(const Flags& flags,
                             const base::FilePath& root,
                             const base::FilePath& stateful,
                             libstorage::Platform* platform,
                             StartupDep* startup_dep,
                             MountHelper* mount_helper)
    : flags_(flags),
      root_(root),
      stateful_(stateful),
      platform_(platform),
      startup_dep_(startup_dep),
      mount_helper_(mount_helper) {}

std::optional<base::Value> StatefulMount::GetImageVars(base::FilePath json_file,
                                                       std::string key) {
  std::string json_string;
  if (!platform_->ReadFileToString(json_file, &json_string)) {
    PLOG(ERROR) << "Unable to read json file: " << json_file;
    return std::nullopt;
  }
  std::optional<base::Value> part_vars = base::JSONReader::Read(
      json_string, base::JSON_PARSE_RFC, 10 /* max_depth */);
  if (!part_vars) {
    PLOG(ERROR) << "Failed to parse image variables.";
    return std::nullopt;
  }
  if (!part_vars->is_dict()) {
    LOG(ERROR) << "Failed to read json file as a dictionary";
    return std::nullopt;
  }

  base::Value::Dict* image_vars = part_vars->GetDict().FindDict(key);
  if (image_vars == nullptr) {
    LOG(ERROR) << "Failed to get image variables from " << json_file;
    return std::nullopt;
  }
  return base::Value(std::move(*image_vars));
}

void StatefulMount::AppendQuotaFeaturesAndOptions(
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
  if (flags_.prjquota) {
    sb_features->push_back("-Qprjquota");
  } else {
    sb_features->push_back("-Q^prjquota");
  }
}

std::vector<std::string> StatefulMount::GenerateExt4Features() {
  std::vector<std::string> sb_features;
  std::vector<std::string> sb_options;

  base::FilePath encryption = root_.Append(kExt4Features).Append("encryption");
  if (flags_.direncryption && platform_->FileExists(encryption)) {
    sb_options.push_back("encrypt");
  }

  base::FilePath verity_file = root_.Append(kExt4Features).Append("verity");
  if (flags_.fsverity && platform_->FileExists(verity_file)) {
    sb_options.push_back("verity");
  }

  AppendQuotaFeaturesAndOptions(&sb_options, &sb_features);

  if (!sb_features.empty() || !sb_options.empty()) {
    // Ensure to replay the journal first so it doesn't overwrite the flag.
    startup_dep_->ReplayExt4Journal(state_dev_);

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
    const std::vector<std::string>& clobber_args,
    const std::string& clobber_message) {
  startup_dep_->BootAlert("self_repair");
  startup_dep_->ClobberLogRepair(state_dev_, clobber_message);
  startup_dep_->AddClobberCrashReport(
      {"--mount_failure", "--mount_device=stateful"});
  startup_dep_->Clobber(clobber_args);
}

bool StatefulMount::AttemptStatefulMigration() {
  std::unique_ptr<brillo::Process> thinpool_migrator =
      platform_->CreateProcessInstance();
  thinpool_migrator->AddArg("/usr/sbin/thinpool_migrator");
  thinpool_migrator->AddArg(
      base::StringPrintf("--device=%s", state_dev_.value().c_str()));

  if (thinpool_migrator->Run() != 0) {
    LOG(ERROR) << "Failed to run thinpool migrator";
    return false;
  }

  return true;
}

void StatefulMount::MountStateful() {
  // Prepare to mount stateful partition.
  root_device_ = utils::GetRootDevice(true);

  int removable = 0;
  if (root_device_.empty()) {
    PLOG(INFO) << "rootdev could not find root device.";
  } else if (!RemovableRootdev(root_device_, &removable)) {
    PLOG(WARNING)
        << "Unable to read if rootdev is removable; assuming it's not";
  }
  std::string load_vars;
  if (removable == 1) {
    load_vars = "load_partition_vars";
  } else {
    load_vars = "load_base_vars";
  }

  base::FilePath json_file = root_.Append(kPartitionsVars);
  std::optional<base::Value> image_vars = GetImageVars(json_file, load_vars);
  if (!image_vars) {
    PLOG(ERROR) << "Failed to read dictionary from " << json_file;
    // We can not further, since /usr/sbin/partition_vars.json is missing
    // or corrupted.
    // Powerwash won't help, the image is invalid.
    // Reboot until we rollback to the previous image.
    utils::Reboot();
    return;
  }
  if (!image_vars->is_dict()) {
    PLOG(ERROR) << "Failed to parse dictionary from " << json_file;
    // Reboot until we rollback to the previous image.
    utils::Reboot();
    return;
  }
  return MountStateful(root_device_, *image_vars);
}

void StatefulMount::MountStateful(const base::FilePath& root_dev,
                                  const base::Value& image_vars) {
  const auto& image_vars_dict = image_vars.GetDict();
  bool status;
  int32_t stateful_mount_flags;
  std::string stateful_mount_opts;
  libstorage::StorageContainerConfig config;

  // Check if we are booted on physical media. rootdev will fail if we are in
  // an initramfs or tmpfs rootfs (ex, factory installer images. Note recovery
  // image also uses initramfs but it never reaches here). When using
  // initrd+tftpboot (some old netboot factory installer), ROOTDEV_TYPE will be
  // /dev/ram.
  if (!root_dev.empty() && root_dev != base::FilePath("/dev/ram")) {
    // Find our stateful partition mount point.
    stateful_mount_flags = kCommonMountFlags | MS_NOATIME;
    const int part_num_state =
        GetPartitionNumFromImageVars(image_vars_dict, "PARTITION_NUM_STATE");
    const std::string* fs_form_state =
        image_vars_dict.FindString("FS_FORMAT_STATE");
    state_dev_ = brillo::AppendPartition(root_dev, part_num_state);
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
    if (USE_LVM_STATEFUL_PARTITION && flags_.lvm_stateful) {
      brillo::LogicalVolumeManager* lvm = platform_->GetLogicalVolumeManager();

      // Attempt to get a valid volume group name.
      bootstat_.LogEvent("pre-lvm-activation");
      std::optional<brillo::PhysicalVolume> pv =
          lvm->GetPhysicalVolume(state_dev_);

      if (!pv && flags_.lvm_migration) {
        // Attempt to migrate to thinpool if migration is enabled: if the
        // migration fails, then we expect the stateful partition to be back to
        // its original state.

        if (!AttemptStatefulMigration()) {
          LOG(ERROR) << "Failed to migrate stateful partition to a thinpool";
        } else {
          // Reset the physical volume on success from thinpool migration.
          pv = lvm->GetPhysicalVolume(state_dev_);
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
            ClobberStateful({"fast", "keepimg"}, "Invalid thinpool");
            // Not reached, except during unit tests.
            return;
          }

          if (!thinpool->Activate()) {
            LOG(WARNING) << "Failed to activate thinpool, attempting repair";
            if (!thinpool->Activate(/*check=*/true)) {
              LOG(ERROR) << "Failed to repair and activate thinpool";
              ClobberStateful({"fast", "keepimg"}, "Corrupt thinpool");
              // Not reached, except during unit tests.
              return;
            }
          }
          should_mount_lvm = true;
        }
      }
      bootstat_.LogEvent("lvm-activation-complete");
    }

    if (should_mount_lvm) {
      state_dev_ = root_.Append("dev")
                       .Append(volume_group_->GetName())
                       .Append("unencrypted");

      config.unencrypted_config = {
          .backing_device_config = {
              .type =
                  libstorage::BackingDeviceType::kLogicalVolumeBackingDevice,
              .name = "unencrypted",
              .logical_volume = {
                  .vg = std::make_shared<brillo::VolumeGroup>(*volume_group_),
                  .thinpool = std::make_shared<brillo::Thinpool>(*thinpool)}}};

    } else {
      config.unencrypted_config = {
          .backing_device_config = {
              .type = libstorage::BackingDeviceType::kPartition,
              .name = state_dev_.value()}};
    }
    config.filesystem_config = {
        .tune2fs_opts = GenerateExt4Features(),
        .backend_type = libstorage::StorageContainerType::kUnencrypted,
        .recovery = libstorage::RecoveryType::kDoNothing};

    std::unique_ptr<libstorage::StorageContainer> container =
        mount_helper_->GetStorageContainerFactory()->Generate(
            config, libstorage::StorageContainerType::kExt4,
            libstorage::FileSystemKeyReference());

    if (!container || !container->Setup(libstorage::FileSystemKey())) {
      LOG(ERROR) << "Failed to setup unencrypted stateful";

      ClobberStateful({"fast", "keepimg", "preserve_lvs"},
                      "Self-repair corrupted stateful partition");
      // Not reached, except during unit tests.
      return;
    }

    // Mount stateful partition from state_dev.
    if (!platform_->Mount(state_dev_, stateful_, *fs_form_state,
                          stateful_mount_flags, stateful_mount_opts)) {
      // Try to rebuild the stateful partition by clobber-state. (Not using fast
      // mode out of security consideration: the device might have gotten into
      // this state through power loss during dev mode transition).
      platform_->ReportFilesystemDetails(state_dev_,
                                         root_.Append(kDumpe2fsStatefulLog));
      ClobberStateful({"keepimg", "preserve_lvs"},
                      "Self-repair corrupted stateful partition");
      // Not reached, except during unit tests.
      return;
    }

    // Mount the OEM partition.
    // mount_or_fail isn't used since this partition only has a filesystem
    // on some boards.
    int32_t oem_flags = MS_RDONLY | kCommonMountFlags;
    const int part_num_oem =
        GetPartitionNumFromImageVars(image_vars_dict, "PARTITION_NUM_OEM");
    const std::string* fs_form_oem =
        image_vars_dict.FindString("FS_FORMAT_OEM");
    const base::FilePath oem_dev =
        brillo::AppendPartition(root_dev, part_num_oem);
    status = platform_->Mount(oem_dev, base::FilePath("/usr/share/oem"),
                              *fs_form_oem, oem_flags, "");
    if (!status) {
      PLOG(WARNING) << "mount of /usr/share/oem failed with code " << status;
    }
  }  // !rootdev.empty() ...
}

base::FilePath StatefulMount::GetStateDev() {
  return state_dev_;
}

// Updates stateful partition if pending
// update is available.
// Returns true if there is no need to update or successful update.
bool StatefulMount::DevUpdateStatefulPartition(const std::string& args) {
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
  std::string var(kVar);
  std::string dev_image(kStatefulDevImage);
  base::FilePath var_new = stateful_.Append(var + kNew);
  base::FilePath developer_new = stateful_.Append(dev_image + kNew);
  base::FilePath developer_target = stateful_.Append(dev_image);
  base::FilePath var_target = stateful_.Append(var + kOverlay);
  std::vector<base::FilePath> paths_to_rm;

  // Only replace the developer and var_overlay directories if new replacements
  // are available.
  if (platform_->DirectoryExists(developer_new) &&
      platform_->DirectoryExists(var_new)) {
    std::string update = "Updating from " + developer_new.value() + " && " +
                         var_new.value() + ".";
    startup_dep_->ClobberLog(update);

    for (const std::string& path : {var, dev_image}) {
      base::FilePath path_new = stateful_.Append(path + kNew);
      base::FilePath path_target;
      if (path == "var") {
        path_target = stateful_.Append(path + kOverlay);
      } else {
        path_target = stateful_.Append(path);
      }
      if (!platform_->DeletePathRecursively(path_target)) {
        PLOG(WARNING) << "Failed to delete " << path_target.value();
      }

      if (!platform_->CreateDirectory(path_target)) {
        PLOG(WARNING) << "Failed to create " << path_target.value();
      }

      if (!platform_->SetPermissions(path_target, 0755)) {
        PLOG(WARNING) << "chmod failed for " << path_target.value();
      }

      std::unique_ptr<libstorage::FileEnumerator> enumerator(
          platform_->GetFileEnumerator(
              path_new, false /* recursive */,
              base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
                  base::FileEnumerator::SHOW_SYM_LINKS));

      for (base::FilePath fd = enumerator->Next(); !fd.empty();
           fd = enumerator->Next()) {
        // Filesystem crossing if new var comes in a logical volume.
        if (!platform_->Rename(fd, path_target.Append(fd.BaseName()), true)) {
          PLOG(WARNING) << "Failed to copy " << fd.value() << " to "
                        << path_target.value();
        }
      }
      paths_to_rm.push_back(path_new);
    }
    startup_dep_->RemoveInBackground(paths_to_rm);
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

    base::FilePath preserve_dir = stateful_.Append("unencrypted/preserve");

    // Find everything in stateful and delete it, except for protected paths,
    // and non-empty directories. The non-empty directories contain protected
    // content or they would already be empty from depth first traversal.
    std::vector<base::FilePath> preserved_paths = {
        stateful_.Append(kLabMachine), developer_target, var_target,
        preserve_dir};
    std::unique_ptr<libstorage::FileEnumerator> enumerator(
        platform_->GetFileEnumerator(stateful_, true,
                                     base::FileEnumerator::FILES |
                                         base::FileEnumerator::DIRECTORIES |
                                         base::FileEnumerator::SHOW_SYM_LINKS));
    for (auto path = enumerator->Next(); !path.empty();
         path = enumerator->Next()) {
      bool preserve = false;
      for (auto& preserved_path : preserved_paths) {
        if (path == preserved_path || preserved_path.IsParent(path) ||
            path.IsParent(preserved_path)) {
          preserve = true;
          break;
        }
      }

      if (!preserve) {
        if (platform_->DirectoryExists(path)) {
          platform_->DeletePathRecursively(path);
        } else {
          platform_->DeleteFile(path);
        }
      }
    }
    // Let's really be done before coming back.
    sync();
  }

  std::vector<base::FilePath> rm_paths{stateful_update_file};
  startup_dep_->RemoveInBackground(rm_paths);

  return true;
}

// Gather logs.
void StatefulMount::DevGatherLogs(const base::FilePath& base_dir) {
  // For dev/test images, if .gatherme presents, copy files listed in .gatherme
  // to /mnt/stateful_partition/unencrypted/prior_logs.
  base::FilePath lab_preserve_logs = stateful_.Append(".gatherme");
  base::FilePath prior_log_dir = stateful_.Append("unencrypted/prior_logs");
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

void StatefulMount::DevMountPackages() {
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
  if (!platform_->DirectoryExists(stateful_dev_image)) {
    if (!platform_->CreateDirectory(stateful_dev_image)) {
      PLOG(ERROR) << "Failed to create " << stateful_dev_image.value();
    }
    if (!platform_->SetPermissions(stateful_dev_image, 0755)) {
      PLOG(ERROR) << "Failed to set permissions for "
                  << stateful_dev_image.value();
    }
  }

  // Checks and updates stateful partition.
  DevUpdateStatefulPartition("");

  // Mount and then remount to enable exec/suid.
  base::FilePath usrlocal = root_.Append(kUsrLocal);
  mount_helper_->BindMountOrFail(stateful_dev_image, usrlocal);
  if (!platform_->Mount(base::FilePath(), usrlocal, "", MS_REMOUNT, "")) {
    PLOG(WARNING) << "Failed to remount " << usrlocal.value();
  }

  base::FilePath disable_state_sec_hard =
      root_.Append(kDisableStatefulSecurityHardening);
  if (!platform_->FileExists(disable_state_sec_hard)) {
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
        LOG(WARNING) << "Path does not exists, can not mount: " << dest.value();
        continue;
      }
      mount_helper_->BindMountOrFail(full, dest);
    }
  }
}

}  // namespace startup
