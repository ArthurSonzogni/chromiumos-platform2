// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/metrics.h"

#include <algorithm>
#include <string>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>

namespace cros_disks {
namespace {

// Returns a view into the input string `fs_type` with the given `prefix`
// removed, but leaving the original string intact. If the prefix does not match
// at the start of the string, returns the original string view instead.
std::string_view StripPrefix(std::string_view fs_type,
                             const std::string_view prefix) {
  if (fs_type.starts_with(prefix)) {
    fs_type.remove_prefix(prefix.size());
  }

  return fs_type;
}

}  // namespace

Metrics::ArchiveType Metrics::GetArchiveType(const std::string_view path) {
  struct Entry {
    std::string_view ext;
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
    const std::string_view fs_type) {
  static const auto map =
      base::MakeFixedFlatMap<std::string_view, FilesystemType>({
          {"", kFilesystemUnknown},         //
          {"exfat", kFilesystemExFAT},      //
          {"ext2", kFilesystemExt2},        //
          {"ext3", kFilesystemExt3},        //
          {"ext4", kFilesystemExt4},        //
          {"hfsplus", kFilesystemHFSPlus},  //
          {"iso9660", kFilesystemISO9660},  //
          {"ntfs", kFilesystemNTFS},        //
          {"ntfs3", kFilesystemNTFS},       //
          {"udf", kFilesystemUDF},          //
          {"vfat", kFilesystemVFAT},        //
      });

  const auto it = map.find(StripPrefix(fs_type, "fuseblk."));
  return it != map.end() ? it->second : kFilesystemOther;
}

void Metrics::RecordArchiveType(const base::FilePath& path) {
  metrics_library_.SendEnumToUMA(
      "CrosDisks.ArchiveType", GetArchiveType(path.value()), kArchiveMaxValue);
}

void Metrics::RecordFilesystemType(const std::string_view fs_type) {
  metrics_library_.SendEnumToUMA("CrosDisks.FilesystemType",
                                 GetFilesystemType(fs_type),
                                 kFilesystemMaxValue);
}

void Metrics::RecordSysCall(const std::string_view syscall,
                            std::string_view fs_type,
                            const error_t error,
                            base::TimeDelta elapsed_time) {
  fs_type = StripPrefix(fs_type, "fuse.");
  metrics_library_.SendSparseToUMA(
      base::StrCat({"CrosDisks.Error.", syscall, ".", fs_type}), error);
  metrics_library_.SendTimeToUMA(
      base::StrCat({"CrosDisks.Time.", syscall, ".", fs_type}),
      std::move(elapsed_time), base::Milliseconds(1), base::Days(1), 200);
}

void Metrics::RecordAction(std::string_view action,
                           const std::string_view fs_type,
                           const Process::ExitCode exit_code,
                           base::TimeDelta elapsed_time) {
  RecordSysCall(action, fs_type, static_cast<int>(exit_code),
                std::move(elapsed_time));
}

void Metrics::RecordReadOnlyFileSystem(const std::string_view fs_type) {
  metrics_library_.SendEnumToUMA("CrosDisks.ReadOnlyFileSystemAfterError",
                                 GetFilesystemType(fs_type),
                                 kFilesystemMaxValue);
}

void Metrics::RecordDeviceMediaType(const DeviceType device_media_type) {
  metrics_library_.SendEnumToUMA("CrosDisks.DeviceMediaType",
                                 device_media_type);
}

}  // namespace cros_disks
