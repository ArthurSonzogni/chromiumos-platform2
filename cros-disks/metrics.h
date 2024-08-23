// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_METRICS_H_
#define CROS_DISKS_METRICS_H_

#include <string_view>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>
#include <metrics/metrics_library.h>

namespace cros_disks {

// A class for collecting cros-disks related UMA metrics.
class Metrics {
 public:
  Metrics() = default;
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  ~Metrics() = default;

  // Records the type of archive that cros-disks is trying to mount.
  void RecordArchiveType(const base::FilePath& path);

  // Records the type of filesystem that cros-disks is trying to mount.
  void RecordFilesystemType(std::string_view fs_type);

  // Records the error (or success) and the elapsed time of a system call
  // related to a filesystem.
  void RecordSysCall(std::string_view syscall,
                     std::string_view fs_type,
                     error_t error,
                     base::TimeDelta elapsed_time);

  // Records the error returned by a FUSE daemon when it unexpectedly
  // terminates.
  void RecordDaemonError(std::string_view program_name, int error);

  // Records a filesystem type that cros-disks had to mount in read-only mode
  // because of an error when trying to mount it in read-write mode.
  void RecordReadOnlyFileSystem(std::string_view fs_type);

  // Records the type of device media that cros-disks is trying to mount.
  void RecordDeviceMediaType(DeviceType device_media_type);

  // Records the error code returned by a FUSE mounter program.
  void RecordFuseMounterErrorCode(std::string_view mounter_name,
                                  int error_code);

 private:
  // Don't renumber these values. They are recorded in UMA metrics.
  // See enum CrosDisksArchiveType in enums.xml.
  enum ArchiveType {
    kArchiveUnknown = 0,
    kArchiveZip = 1,
    kArchiveRar = 2,
    kArchiveTar = 3,
    kArchiveTarBzip2 = 4,
    kArchiveTarGzip = 5,
    kArchiveBzip2 = 6,
    kArchiveGzip = 7,
    kArchive7z = 8,
    kArchiveCrx = 9,
    kArchiveIso = 10,
    kArchiveTarXz = 11,
    kArchiveXz = 12,
    kArchiveTarLzma = 13,
    kArchiveLzma = 14,
    kArchiveTarZ = 15,
    kArchiveZ = 16,
    kArchiveTarZst = 17,
    kArchiveZst = 18,
    kArchiveTarLz = 19,
    kArchiveLz = 20,
    kArchiveMaxValue = 21,
  };

  // Don't renumber these values. They are recorded in UMA metrics.
  // See enum CrosDisksFilesystemType in enums.xml.
  enum FilesystemType {
    kFilesystemUnknown = 0,
    kFilesystemOther = 1,
    kFilesystemVFAT = 2,
    kFilesystemExFAT = 3,
    kFilesystemNTFS = 4,
    kFilesystemHFSPlus = 5,
    kFilesystemExt2 = 6,
    kFilesystemExt3 = 7,
    kFilesystemExt4 = 8,
    kFilesystemISO9660 = 9,
    kFilesystemUDF = 10,
    kFilesystemMaxValue = 11,
  };

  // Returns the ArchiveType for the specified path.
  static ArchiveType GetArchiveType(std::string_view path);

  // Returns the MetricsFilesystemType enum value for the specified filesystem
  // type string.
  static FilesystemType GetFilesystemType(std::string_view fs_type);

  MetricsLibrary metrics_library_;

  FRIEND_TEST(MetricsTest, GetArchiveType);
  FRIEND_TEST(MetricsTest, GetFilesystemType);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_METRICS_H_
