// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/executor/executor.h"

#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/file_utils.h>

#include "rmad/executor/mojom/executor.mojom.h"
#include "rmad/executor/mount.h"
#include "rmad/utils/ec_utils_impl.h"

namespace {

// The executor process runs in a light sandbox with /tmp mounted as tmpfs.
constexpr char kTmpPath[] = "/tmp";

constexpr char kDevicePathFormat[] = "/dev/sd%c%d";
constexpr char kSourceFirmwareUpdaterRelPath[] =
    "usr/sbin/chromeos-firmwareupdate";
constexpr char kTargetFirmwareUpdaterAbsPath[] =
    "/var/lib/rmad/chromeos-firmwareupdate";

// Partition for writing the log file. Only check the first partition.
constexpr int kWriteLogPartitionIndex = 1;
// Partition for rootfs A in a ChromeOS image. We don't check rootfs B.
constexpr int kRootfsPartitionIndex = 3;

// Log file format.
constexpr char kLogFilenameFormat[] = "rma-%s.log";
// Supported file systems for saving logs.
const std::vector<std::string> kLogFileSystems = {"vfat", "ext4", "ext3",
                                                  "ext2"};

std::string FormatTime(const base::Time& time) {
  base::Time::Exploded e;
  time.UTCExplode(&e);
  // ISO 8601 format.
  return base::StringPrintf("%04d%02d%02dT%02d%02d%02dZ", e.year, e.month,
                            e.day_of_month, e.hour, e.minute, e.second);
}

rmad::Mount TryMount(const base::FilePath& device_file,
                     const base::FilePath& mount_point,
                     const std::vector<std::string>& fs_types,
                     bool read_only) {
  for (const std::string& fs_type : fs_types) {
    rmad::Mount mount(device_file, mount_point, fs_type, read_only);
    if (mount.IsValid()) {
      return mount;
    }
  }
  return rmad::Mount();
}

// Powerwash related constants.
constexpr char kPowerwashRequestFilePath[] =
    "/mnt/stateful_partition/factory_install_reset";
constexpr char kRmaPowerwashArgs[] = "fast safe keepimg rma";

}  // namespace

namespace rmad {

Executor::Executor(
    mojo::PendingReceiver<chromeos::rmad::mojom::Executor> receiver)
    : receiver_{this, std::move(receiver)} {
  // Quit the executor when the communication disconnects.
  receiver_.set_disconnect_handler(
      base::BindOnce([]() { std::exit(EXIT_SUCCESS); }));
  ec_utils_ = std::make_unique<EcUtilsImpl>();
}

void Executor::MountAndWriteLog(uint8_t device_id,
                                const std::string& log_string,
                                MountAndWriteLogCallback callback) {
  // Input argument check.
  if (!islower(device_id)) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  // Create temporary mount point.
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDirUnderPath(base::FilePath(kTmpPath))) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  const base::FilePath device_path(base::StringPrintf(
      kDevicePathFormat, device_id, kWriteLogPartitionIndex));
  const base::FilePath mount_point = temp_dir.GetPath();
  const Mount mount =
      TryMount(device_path, mount_point, kLogFileSystems, false);
  if (mount.IsValid()) {
    const std::string filename = base::StringPrintf(
        kLogFilenameFormat, FormatTime(base::Time::Now()).c_str());
    const base::FilePath log_path = mount_point.Append(filename);
    if (base::WriteFile(log_path, log_string.c_str())) {
      brillo::SyncFileOrDirectory(log_path, false, true);
      // The full log path is not useful because the mount point is a temporary
      // directory. Returning the filename is enough.
      std::move(callback).Run(filename);
      return;
    }
  }
  std::move(callback).Run(std::nullopt);
}

void Executor::MountAndCopyFirmwareUpdater(
    uint8_t device_id, MountAndCopyFirmwareUpdaterCallback callback) {
  // Input argument check.
  if (!islower(device_id)) {
    std::move(callback).Run(false);
    return;
  }
  // Create temporary mount point.
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDirUnderPath(base::FilePath(kTmpPath))) {
    std::move(callback).Run(false);
    return;
  }

  const base::FilePath device_path(
      base::StringPrintf(kDevicePathFormat, device_id, kRootfsPartitionIndex));
  const base::FilePath mount_point = temp_dir.GetPath();
  const Mount mount(device_path, mount_point, "ext2", true);
  if (mount.IsValid()) {
    const base::FilePath source_updater_path =
        mount_point.Append(kSourceFirmwareUpdaterRelPath);
    const base::FilePath target_updater_path(kTargetFirmwareUpdaterAbsPath);
    if (base::PathExists(source_updater_path) &&
        base::CopyFile(source_updater_path, target_updater_path)) {
      brillo::SyncFileOrDirectory(base::FilePath(target_updater_path), false,
                                  true);
      std::move(callback).Run(true);
      return;
    }
  }
  std::move(callback).Run(false);
}

void Executor::RebootEc(RebootEcCallback callback) {
  std::move(callback).Run(ec_utils_->Reboot());
}

void Executor::RequestRmaPowerwash(RequestRmaPowerwashCallback callback) {
  const base::FilePath powerwash_file_path(kPowerwashRequestFilePath);
  if (!base::WriteFile(powerwash_file_path, kRmaPowerwashArgs,
                       std::size(kRmaPowerwashArgs) - 1)) {
    LOG(ERROR) << "Failed to write powerwash request file";
    std::move(callback).Run(false);
  } else if (!brillo::SyncFileOrDirectory(powerwash_file_path, false, true)) {
    LOG(ERROR) << "Failed to sync powerwash request file";
    std::move(callback).Run(false);
  }
  std::move(callback).Run(true);
}

}  // namespace rmad
