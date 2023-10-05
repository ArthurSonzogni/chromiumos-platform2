// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber_lvm.h"

#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/fs.h>

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
#include <base/strings/stringprintf.h>
#include <chromeos/constants/imageloader.h>
#include <crypto/random.h>
#include <libdlcservice/utils.h>

namespace {

// Size of string for volume group name.
constexpr int kVolumeGroupNameSize = 16;

// Minimal physical volume size (1 default sized extent).
constexpr uint64_t kMinStatefulPartitionSizeMb = 4;
// Percent size of thinpool compared to the physical volume.
constexpr size_t kThinpoolSizePercent = 98;
// thin_metadata_size estimates <2% of the thinpool size can be used safely to
// store metadata for up to 200 logical volumes.
constexpr size_t kThinpoolMetadataSizePercent = 1;
// Create thin logical volumes at 95% of the thinpool's size.
constexpr size_t kLogicalVolumeSizePercent = 95;

}  // namespace

ClobberLvm::ClobberLvm(ClobberWipe* clobber_wipe,
                       std::unique_ptr<brillo::LogicalVolumeManager> lvm)
    : clobber_wipe_(clobber_wipe), lvm_(std::move(lvm)) {}

// Use a random 16 character name for the volume group.
std::string ClobberLvm::GenerateRandomVolumeGroupName() {
  const char kCharset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  unsigned char vg_random_value[kVolumeGroupNameSize];
  crypto::RandBytes(vg_random_value, kVolumeGroupNameSize);

  std::string vg_name(kVolumeGroupNameSize, '0');
  for (int i = 0; i < kVolumeGroupNameSize; ++i) {
    vg_name[i] = kCharset[vg_random_value[i] % 36];
  }
  return vg_name;
}

void ClobberLvm::RemoveLogicalVolumeStack(
    const base::FilePath stateful_partition_device) {
  // For logical volume stateful partition, deactivate the volume group before
  // wiping the device.
  std::optional<brillo::PhysicalVolume> pv =
      lvm_->GetPhysicalVolume(stateful_partition_device);
  if (!pv || !pv->IsValid()) {
    LOG(WARNING) << "Failed to get physical volume.";
    return;
  }
  std::optional<brillo::VolumeGroup> vg = lvm_->GetVolumeGroup(*pv);
  if (!vg || !vg->IsValid()) {
    LOG(WARNING) << "Failed to get volume group.";
    return;
  }

  LOG(INFO) << "Deactivating volume group.";
  vg->Deactivate();
  LOG(INFO) << "Removing volume group.";
  vg->Remove();
  LOG(INFO) << "Removing physical volume.";
  pv->Remove();
}

bool ClobberLvm::ProcessInfo(
    const brillo::VolumeGroup& vg,
    const PreserveLogicalVolumesWipeInfo& info,
    std::unique_ptr<dlcservice::UtilsInterface> utils) {
  auto lv = lvm_->GetLogicalVolume(vg, info.lv_name);
  if (!lv || !lv->IsValid()) {
    LOG(INFO) << "Skipping over logical volume: " << info.lv_name;
    return true;
  }

  // Active logical volumes as not all have udev rule to active by default.
  if (!lv->Activate()) {
    LOG(ERROR) << "Failed to active logical volume: " << info.lv_name;
    return false;
  }

  // Zero the logical volume.
  if (info.zero) {
    if (!clobber_wipe_->WipeDevice(lv->GetPath(), /*discard=*/true)) {
      LOG(ERROR) << "Failed to wipe logical volume: " << info.lv_name;
      return false;
    }
  }

  // Preserve the logical volume.
  if (info.preserve) {
    LOG(INFO) << "Preserving logical volume: " << info.lv_name;
  } else if (!lv->Remove()) {
    LOG(ERROR) << "Failed to remove logical volume: " << info.lv_name;
    return false;
  }

  bool remove_lv = false;
  // Verify digest of the logical volume.
  if (info.digest_info) {
    std::vector<uint8_t> actual_digest;
    // Logical volumes MUST skip size checking. Stats on it are going to return
    // the wrong size or 0.
    if (!utils->HashFile(lv->GetPath(), info.digest_info->bytes, &actual_digest,
                         /*skip_size_check=*/true)) {
      LOG(ERROR) << "Failed to check digest of logical volume: "
                 << info.lv_name;
      // Continue to return `true`, as we DO NOT want all other preservations to
      // fail due to a bad digest.
      remove_lv = true;
    } else if (info.digest_info->digest != actual_digest) {
      LOG(ERROR) << "Digests do not match for logical volume: " << info.lv_name;
      // Continue to return `true`, as we DO NOT want all other preservations to
      // fail due to a bad digest.
      remove_lv = true;
    }
  }

  if (remove_lv && !lv->Remove()) {
    LOG(ERROR) << "Failed to remove logical volume: " << info.lv_name;
  }

  return true;
}

bool ClobberLvm::PreserveLogicalVolumesWipe(
    const base::FilePath stateful_partition_device,
    const PreserveLogicalVolumesWipeInfos& infos) {
  auto pv = lvm_->GetPhysicalVolume({stateful_partition_device});
  if (!pv || !pv->IsValid()) {
    LOG(WARNING) << "Failed to get physical volume.";
    return false;
  }
  auto vg = lvm_->GetVolumeGroup(*pv);
  if (!vg || !vg->IsValid()) {
    LOG(WARNING) << "Failed to get volume group.";
    return false;
  }

  // Remove all logical volumes we don't need to handle with care.
  for (auto& lv : lvm_->ListLogicalVolumes(*vg)) {
    const auto& lv_raw_name = lv.GetRawName();
    auto it = infos.find({.lv_name = lv_raw_name});
    bool found = it != infos.end();

    if (found)
      continue;

    if (!lv.Remove()) {
      LOG(ERROR) << "Failed to remove logical volume: " << lv_raw_name;
      return false;
    }
  }

  // We must handle logical volume with additional care based on the
  // `PreserveLogicalVolumesWipeInfo`.
  for (const auto& info : infos) {
    if (info.lv_name == kUnencrypted)
      continue;
    if (!ProcessInfo(*vg, info, std::make_unique<dlcservice::Utils>()))
      return false;
  }
  // Note: Always process unencrypted stateful last.
  // This is so when there are crashes, the powerwash file is still accessible
  // within unencrypted logical volume to go through and perform the powerwash
  // again.
  {
    auto lv_name = kUnencrypted;
    auto info_it = infos.find({.lv_name = lv_name});
    if (info_it == infos.end()) {
      LOG(ERROR) << "Missing " << lv_name
                 << " in preserve logical volumes wipe info.";
      return false;
    }
    if (!ProcessInfo(*vg, *info_it, std::make_unique<dlcservice::Utils>()))
      return false;
  }

  auto old_vg_name = vg->GetName();
  auto new_vg_name = GenerateRandomVolumeGroupName();
  if (!vg->Rename(new_vg_name)) {
    LOG(ERROR) << "Failed to rename volume group from=" << old_vg_name
               << " to=" << new_vg_name;
    return false;
  }

  return true;
}

bool ClobberLvm::CreateUnencryptedStatefulLV(const brillo::VolumeGroup& vg,
                                             const brillo::Thinpool& thinpool,
                                             uint64_t lv_size) {
  base::Value::Dict lv_config;
  lv_config.Set("name", kUnencrypted);
  lv_config.Set("size", base::NumberToString(lv_size));

  std::optional<brillo::LogicalVolume> lv =
      lvm_->CreateLogicalVolume(vg, thinpool, lv_config);
  if (!lv || !lv->IsValid()) {
    LOG(ERROR) << "Failed to create " << kUnencrypted << " logical volume.";
    return false;
  }

  if (!lv->Activate()) {
    LOG(ERROR) << "Failed to activate thinpool.";
    return false;
  }

  return true;
}

uint64_t ClobberLvm::GetBlkSize(const base::FilePath& device) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(device.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open " << device.value();
    return 0;
  }

  uint64_t size;
  if (ioctl(fd.get(), BLKGETSIZE64, &size)) {
    PLOG(ERROR) << "ioctl(BLKGETSIZE): " << device.value();
    return 0;
  }
  return size;
}

std::optional<uint64_t> ClobberLvm::GetPartitionSize(
    const base::FilePath& base_device) {
  uint64_t partition_size = GetBlkSize(base_device) / (1024 * 1024);
  if (partition_size < kMinStatefulPartitionSizeMb) {
    LOG(ERROR) << "Invalid partition size (" << partition_size
               << ") for: " << base_device.value();
    return std::nullopt;
  }
  return {partition_size};
}

std::optional<base::FilePath> ClobberLvm::CreateLogicalVolumeStackForPreserved(
    const base::FilePath stateful_partition_device) {
  std::optional<uint64_t> partition_size =
      GetPartitionSize(stateful_partition_device);
  if (!partition_size) {
    LOG(ERROR) << "Failed to get partition size.";
    return std::nullopt;
  }

  auto pv = lvm_->GetPhysicalVolume({stateful_partition_device});
  if (!pv || !pv->IsValid()) {
    LOG(WARNING) << "Failed to get physical volume.";
    return std::nullopt;
  }

  auto vg = lvm_->GetVolumeGroup(*pv);
  if (!vg || !vg->IsValid()) {
    LOG(WARNING) << "Failed to get volume group.";
    return std::nullopt;
  }

  std::optional<brillo::Thinpool> thinpool = lvm_->GetThinpool(*vg, kThinpool);
  if (!thinpool || !thinpool->IsValid()) {
    LOG(ERROR) << "Failed to get thinpool.";
    return std::nullopt;
  }

  int64_t thinpool_size = partition_size.value() * kThinpoolSizePercent / 100;
  uint64_t lv_size = thinpool_size * kLogicalVolumeSizePercent / 100;
  if (!CreateUnencryptedStatefulLV(*vg, *thinpool, lv_size)) {
    return std::nullopt;
  }
  return base::FilePath(
      base::StringPrintf("/dev/%s/unencrypted", vg->GetName().c_str()));
}

std::optional<base::FilePath> ClobberLvm::CreateLogicalVolumeStack(
    base::FilePath base_device) {
  std::string vg_name = GenerateRandomVolumeGroupName();
  // Get partition size to determine the sizes of the thin pool and the
  // logical volume. Use partition size in megabytes: thinpool (and logical
  // volume) sizes need to be a multiple of 512.
  std::optional<uint64_t> partition_size = GetPartitionSize(base_device);
  if (!partition_size) {
    LOG(ERROR) << "Failed to get partition size.";
    return std::nullopt;
  }

  std::optional<brillo::PhysicalVolume> pv =
      lvm_->CreatePhysicalVolume(base_device);

  if (!pv || !pv->IsValid()) {
    LOG(ERROR) << "Failed to create physical volume.";
    return std::nullopt;
  }

  std::optional<brillo::VolumeGroup> vg = lvm_->CreateVolumeGroup(*pv, vg_name);
  if (!vg || !vg->IsValid()) {
    LOG(ERROR) << "Failed to create volume group.";
    return std::nullopt;
  }

  vg->Activate();

  base::Value thinpool_config(base::Value::Type::DICT);
  int64_t thinpool_size = partition_size.value() * kThinpoolSizePercent / 100;
  int64_t thinpool_metadata_size =
      thinpool_size * kThinpoolMetadataSizePercent / 100;
  auto& dict = thinpool_config.GetDict();
  dict.Set("name", kThinpool);
  dict.Set("size", base::NumberToString(thinpool_size));
  dict.Set("metadata_size", base::NumberToString(thinpool_metadata_size));

  std::optional<brillo::Thinpool> thinpool =
      lvm_->CreateThinpool(*vg, thinpool_config);
  if (!thinpool || !thinpool->IsValid()) {
    LOG(ERROR) << "Failed to create thinpool.";
    return std::nullopt;
  }

  uint64_t lv_size = thinpool_size * kLogicalVolumeSizePercent / 100;
  if (!CreateUnencryptedStatefulLV(*vg, *thinpool, lv_size)) {
    LOG(ERROR) << "Failed to activate thinpool.";
    return std::nullopt;
  }
  return base::FilePath(
      base::StringPrintf("/dev/%s/unencrypted", vg_name.c_str()));
}

ClobberLvm::PreserveLogicalVolumesWipeInfos
ClobberLvm::DlcPreserveLogicalVolumesWipeArgs(
    const base::FilePath& ps_file_path,
    const base::FilePath& dlc_manifest_root_path,
    dlcservice::PartitionSlot active_slot,
    std::unique_ptr<dlcservice::UtilsInterface> utils) {
  if (!base::PathExists(ps_file_path)) {
    LOG(WARNING) << "DLC powerwash safe file missing, skipping preservation.";
    return {};
  }
  std::string ps_file_content;
  if (!base::ReadFileToString(ps_file_path, &ps_file_content)) {
    // Don't end PLOGs with (period).
    PLOG(ERROR) << "Failed to read DLC powerwash safe file";
    return {};
  }
  auto dlcs = base::SplitString(ps_file_content, "\n", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY);
  LOG(INFO) << "The powerwash safe DLCs are=" << base::JoinString(dlcs, ",");

  ClobberLvm::PreserveLogicalVolumesWipeInfos verified_dlcs;
  for (const auto& dlc : dlcs) {
    const auto& manifest = utils->GetDlcManifest(dlc_manifest_root_path, dlc,
                                                 dlcservice::kPackage);

    // Verify against rootfs that these DLCs are in fact, powerwash safe.
    if (!manifest->powerwash_safe()) {
      LOG(WARNING) << "DLC=" << dlc << " is not powerwash safe, but list in "
                   << "powerwash safe file, skipping it.";
      continue;
    }

    LOG(INFO) << "DLC=" << dlc << " is set to be preserved.";

    // We add the active DLC logical volume.
    const auto& dlc_active_lv_name = utils->LogicalVolumeName(dlc, active_slot);
    verified_dlcs.emplace(PreserveLogicalVolumesWipeInfo{
        .lv_name = dlc_active_lv_name,
        .preserve = true,
        .zero = false,
        .digest_info =
            PreserveLogicalVolumesWipeInfo::DigestInfo{
                .bytes = manifest->size(),
                .digest = manifest->image_sha256(),
            },
    });

    // We also add the inactive DLC logical volume, but simply clear (zero) it.
    const auto& dlc_inactive_lv_name = utils->LogicalVolumeName(
        dlc, active_slot == dlcservice::PartitionSlot::A
                 ? dlcservice::PartitionSlot::B
                 : dlcservice::PartitionSlot::A);
    verified_dlcs.emplace(PreserveLogicalVolumesWipeInfo{
        .lv_name = dlc_inactive_lv_name,
        .preserve = true,
        .zero = true,
    });
  }

  return verified_dlcs;
}

ClobberLvm::PreserveLogicalVolumesWipeInfos
ClobberLvm::PreserveLogicalVolumesWipeArgs(
    const dlcservice::PartitionSlot slot) {
  PreserveLogicalVolumesWipeInfos infos;
  infos.insert({
      {
          .lv_name = kThinpool,
          .preserve = true,
          .zero = false,
      },
      {
          .lv_name = kUnencrypted,
          .preserve = false,
          .zero = true,
      },
  });
  const auto& dlcs = DlcPreserveLogicalVolumesWipeArgs(
      base::FilePath(dlcservice::kDlcPowerwashSafeFile),
      base::FilePath(imageloader::kDlcManifestRootpath), slot,
      std::make_unique<dlcservice::Utils>());
  infos.insert(std::cbegin(dlcs), std::cend(dlcs));
  return infos;
}
