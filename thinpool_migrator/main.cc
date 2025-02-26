// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sysexits.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <thinpool_migrator/migration_metrics.h>

#include "thinpool_migrator/thinpool_migrator.h"

std::optional<uint64_t> GetBlkSize(const base::FilePath& device) {
  DCHECK(device.IsAbsolute()) << "device=" << device;

  uint64_t size;
  base::ScopedFD fd(
      HANDLE_EINTR(open(device.value().c_str(), O_RDONLY | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open " << device.value();
    return std::nullopt;
  }
  if (ioctl(fd.get(), BLKGETSIZE64, &size)) {
    PLOG(ERROR) << "ioctl(BLKGETSIZE64): " << device.value();
    return std::nullopt;
  }
  return size;
}

constexpr char kLogFile[] = "/run/thinpool_migrator/migrator.log";

int main(int argc, char** argv) {
  DEFINE_string(device, "", "Path of the device to run the migration tool on");
  DEFINE_bool(dry_run, false, "Perform dry-run for migration");
  DEFINE_bool(use_vpd, true, "Store migration status in VPD");
  DEFINE_bool(enable, false, "Enable migration");
  DEFINE_bool(silent, false, "Remove boot alert");
  DEFINE_bool(cleanup, false, "Cleanup migration state");

  brillo::FlagHelper::Init(argc, argv, "Chromium OS Thinpool Migrator");

  // Set up logging to a file to record any unexpected but non-fatal
  // behavior.
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file_path = kLogFile;
  logging::InitLogging(settings);

  const base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->GetArgs().size() > 0) {
    LOG(ERROR) << "Usage: thinpool_migrator --device=<block device> "
                  "[--dry_run]";
    return EXIT_FAILURE;
  }

  thinpool_migrator::InitializeMetrics();

  if (FLAGS_enable) {
    return thinpool_migrator::ThinpoolMigrator().EnableMigration();
  }

  if (FLAGS_cleanup) {
    return thinpool_migrator::ThinpoolMigrator().CleanupState();
  }

  std::optional<uint64_t> size = GetBlkSize(base::FilePath(FLAGS_device));
  if (!size) {
    LOG(ERROR) << "Failed to get device size for " << FLAGS_device;
    return EXIT_FAILURE;
  }

  std::unique_ptr vpd = FLAGS_use_vpd ? std::make_unique<vpd::Vpd>() : nullptr;

  thinpool_migrator::ThinpoolMigrator migrator(
      base::FilePath(FLAGS_device), *size,
      std::make_unique<brillo::DeviceMapper>(), std::move(vpd));
  return migrator.Migrate(FLAGS_dry_run, FLAGS_silent) ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
}
