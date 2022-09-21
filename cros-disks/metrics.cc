// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/metrics.h"

#include <base/containers/fixed_flat_map.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>

namespace cros_disks {

Metrics::ArchiveType Metrics::GetArchiveType(base::StringPiece path) {
  struct Entry {
    base::StringPiece ext;
    ArchiveType type;
  };

  static const Entry entries[] = {
      {".tar.bz", kArchiveTarBzip2},   //
      {".tar.bz2", kArchiveTarBzip2},  //
      {".tar.gz", kArchiveTarGzip},    //
      {".tar.lz", kArchiveTarLz},      //
      {".tar.lzma", kArchiveTarLzma},  //
      {".tar.xz", kArchiveTarXz},      //
      {".tar.z", kArchiveTarZ},        //
      {".tar.zst", kArchiveTarZst},    //
      {".7z", kArchive7z},             //
      {".bz", kArchiveBzip2},          //
      {".bz2", kArchiveBzip2},         //
      {".crx", kArchiveCrx},           //
      {".gz", kArchiveGzip},           //
      {".iso", kArchiveIso},           //
      {".lz", kArchiveLz},             //
      {".lzma", kArchiveLzma},         //
      {".rar", kArchiveRar},           //
      {".tar", kArchiveTar},           //
      {".taz", kArchiveTarZ},          //
      {".tb2", kArchiveTarBzip2},      //
      {".tbz", kArchiveTarBzip2},      //
      {".tbz2", kArchiveTarBzip2},     //
      {".tgz", kArchiveTarGzip},       //
      {".tlz", kArchiveTarLzma},       //
      {".tlzma", kArchiveTarLzma},     //
      {".txz", kArchiveTarXz},         //
      {".tz", kArchiveTarZ},           //
      {".tz2", kArchiveTarBzip2},      //
      {".tzst", kArchiveTarZst},       //
      {".xz", kArchiveXz},             //
      {".z", kArchiveZ},               //
      {".zip", kArchiveZip},           //
      {".zst", kArchiveZst},           //
  };

  for (const auto [ext, type] : entries) {
    if (base::EndsWith(path, ext, base::CompareCase::INSENSITIVE_ASCII))
      return type;
  }

  return kArchiveUnknown;
}

Metrics::FilesystemType Metrics::GetFilesystemType(
    const base::StringPiece fs_type) {
  static const auto map =
      base::MakeFixedFlatMap<base::StringPiece, FilesystemType>({
          {"", kFilesystemUnknown},         //
          {"exfat", kFilesystemExFAT},      //
          {"ext2", kFilesystemExt2},        //
          {"ext3", kFilesystemExt3},        //
          {"ext4", kFilesystemExt4},        //
          {"hfsplus", kFilesystemHFSPlus},  //
          {"iso9660", kFilesystemISO9660},  //
          {"ntfs", kFilesystemNTFS},        //
          {"udf", kFilesystemUDF},          //
          {"vfat", kFilesystemVFAT},        //
      });
  const auto it = map.find(fs_type);
  return it != map.end() ? it->second : kFilesystemOther;
}

void Metrics::RecordArchiveType(const base::FilePath& path) {
  if (!metrics_library_.SendEnumToUMA("CrosDisks.ArchiveType",
                                      GetArchiveType(path.value()),
                                      kArchiveMaxValue))
    LOG(ERROR) << "Cannot send archive type to UMA";
}

void Metrics::RecordFilesystemType(const base::StringPiece fs_type) {
  if (!metrics_library_.SendEnumToUMA("CrosDisks.FilesystemType",
                                      GetFilesystemType(fs_type),
                                      kFilesystemMaxValue))
    LOG(ERROR) << "Cannot send filesystem type to UMA";
}

void Metrics::RecordReadOnlyFileSystem(const base::StringPiece fs_type) {
  if (!metrics_library_.SendEnumToUMA("CrosDisks.ReadOnlyFileSystemAfterError",
                                      GetFilesystemType(fs_type),
                                      kFilesystemMaxValue))
    LOG(ERROR) << "Cannot send filesystem type to UMA";
}

void Metrics::RecordDeviceMediaType(DeviceMediaType device_media_type) {
  if (!metrics_library_.SendEnumToUMA("CrosDisks.DeviceMediaType",
                                      device_media_type,
                                      DEVICE_MEDIA_NUM_VALUES))
    LOG(ERROR) << "Cannot send device media type to UMA";
}

void Metrics::RecordFuseMounterErrorCode(const base::StringPiece mounter_name,
                                         const int error_code) {
  if (!metrics_library_.SendSparseToUMA(
          base::StrCat({"CrosDisks.Fuse.", mounter_name}), error_code))
    LOG(ERROR) << "Cannot send FUSE mounter error code to UMA";
}

}  // namespace cros_disks
