// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/executor/executor.h"

#include <cctype>
#include <optional>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/file_utils.h>

#include "rmad/executor/mojom/executor.mojom.h"
#include "rmad/executor/mount.h"

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

}  // namespace

namespace rmad {

Executor::Executor(
    mojo::PendingReceiver<chromeos::rmad::mojom::Executor> receiver)
    : receiver_{this, std::move(receiver)} {
  receiver_.set_disconnect_handler(
      base::BindOnce([]() { std::exit(EXIT_SUCCESS); }));
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
  const Mount mount(device_path, mount_point, "vfat", false);
  if (mount.IsValid()) {
    // TODO(chenghan): Append timestamp to the log filename.
    const base::FilePath log_path = mount_point.Append("rma.log");
    if (base::WriteFile(log_path, log_string.c_str())) {
      brillo::SyncFileOrDirectory(log_path, false, true);
      std::move(callback).Run(log_path.value());
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

}  // namespace rmad
