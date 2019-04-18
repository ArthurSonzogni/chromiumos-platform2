// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/metrics.h"

#include <base/logging.h>

namespace {

const char kArchiveTypeMetricName[] = "CrosDisks.ArchiveType";
const char kDeviceMediaTypeMetricName[] = "CrosDisks.DeviceMediaType";
const char kFilesystemTypeMetricName[] = "CrosDisks.FilesystemType";

}  // namespace

namespace cros_disks {

Metrics::Metrics() {
  InitializeArchiveTypeMap();
  InitializeFilesystemTypeMap();
}

void Metrics::InitializeArchiveTypeMap() {
  archive_type_map_["zip"] = kArchiveZip;
  archive_type_map_["tar"] = kArchiveTar;
  archive_type_map_["tar.bz2"] = kArchiveTarBzip2;
  archive_type_map_["tbz"] = kArchiveTarBzip2;
  archive_type_map_["tbz2"] = kArchiveTarBzip2;
  archive_type_map_["tar.gz"] = kArchiveTarGzip;
  archive_type_map_["tgz"] = kArchiveTarGzip;
  archive_type_map_["rar"] = kArchiveRar;
}

void Metrics::InitializeFilesystemTypeMap() {
  filesystem_type_map_["vfat"] = kFilesystemVFAT;
  filesystem_type_map_["exfat"] = kFilesystemExFAT;
  filesystem_type_map_["ntfs"] = kFilesystemNTFS;
  filesystem_type_map_["hfsplus"] = kFilesystemHFSPlus;
  filesystem_type_map_["ext2"] = kFilesystemExt2;
  filesystem_type_map_["ext3"] = kFilesystemExt3;
  filesystem_type_map_["ext4"] = kFilesystemExt4;
  filesystem_type_map_["iso9660"] = kFilesystemISO9660;
  filesystem_type_map_["udf"] = kFilesystemUDF;
  filesystem_type_map_[""] = kFilesystemUnknown;
}

Metrics::ArchiveType Metrics::GetArchiveType(
    const std::string& archive_type) const {
  std::map<std::string, ArchiveType>::const_iterator map_iter =
      archive_type_map_.find(archive_type);
  if (map_iter != archive_type_map_.end())
    return map_iter->second;
  return kArchiveUnknown;
}

Metrics::FilesystemType Metrics::GetFilesystemType(
    const std::string& filesystem_type) const {
  std::map<std::string, FilesystemType>::const_iterator map_iter =
      filesystem_type_map_.find(filesystem_type);
  if (map_iter != filesystem_type_map_.end())
    return map_iter->second;
  return kFilesystemOther;
}

void Metrics::RecordArchiveType(const std::string& archive_type) {
  if (!metrics_library_.SendEnumToUMA(kArchiveTypeMetricName,
                                      GetArchiveType(archive_type),
                                      kArchiveMaxValue))
    LOG(WARNING) << "Failed to send archive type sample to UMA";
}

void Metrics::RecordFilesystemType(const std::string& filesystem_type) {
  if (!metrics_library_.SendEnumToUMA(kFilesystemTypeMetricName,
                                      GetFilesystemType(filesystem_type),
                                      kFilesystemMaxValue))
    LOG(WARNING) << "Failed to send filesystem type sample to UMA";
}

void Metrics::RecordDeviceMediaType(DeviceMediaType device_media_type) {
  if (!metrics_library_.SendEnumToUMA(kDeviceMediaTypeMetricName,
                                      device_media_type,
                                      DEVICE_MEDIA_NUM_VALUES))
    LOG(WARNING) << "Failed to send device media type sample to UMA";
}

}  // namespace cros_disks
