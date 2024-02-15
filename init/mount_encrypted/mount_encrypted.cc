/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This tool will attempt to mount or create the encrypted stateful partition,
 * and the various bind mountable subdirectories.
 *
 */
#define _FILE_OFFSET_BITS 64
#define CHROMEOS_ENVIRONMENT

#include <fcntl.h>
#include <sys/time.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/files/file_util.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container_factory.h>

#include "init/metrics/metrics.h"
#include "init/mount_encrypted/encrypted_fs.h"
#include "init/mount_encrypted/encryption_key.h"
#include "init/mount_encrypted/tpm_setup.h"

#if DEBUG_ENABLED
struct timeval tick = {};
struct timeval tick_start = {};
#endif

namespace {
constexpr char kMountEncryptedMetricsPath[] =
    "/run/mount_encrypted/metrics.mount-encrypted";
}  // namespace

static void print_usage(const char process_name[]) {
  fprintf(stderr, "Usage: %s [info|umount|set|mount]\n", process_name);
}

int main(int argc, const char* argv[]) {
  DEFINE_bool(unsafe, false, "mount encrypt partition with well known secret.");
  brillo::FlagHelper::Init(argc, argv, "mount-encrypted");

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  logging::SetLogItems(false,   // process ID
                       false,   // thread ID
                       true,    // timestamp
                       false);  // tickcount

  auto commandline = base::CommandLine::ForCurrentProcess();
  auto args = commandline->GetArgs();

  char* rootdir_env = getenv("MOUNT_ENCRYPTED_ROOT");
  base::FilePath rootdir = base::FilePath(rootdir_env ? rootdir_env : "/");
  libstorage::Platform platform;
  init_metrics::ScopedInitMetricsSingleton scoped_metrics(
      kMountEncryptedMetricsPath);

  libstorage::StorageContainerFactory storage_container_factory(
      &platform, init_metrics::InitMetrics::GetInternal());
  brillo::DeviceMapper device_mapper;
  brillo::LogicalVolumeManager lvm;
  auto encrypted_fs = mount_encrypted::EncryptedFs::Generate(
      rootdir, &platform, &device_mapper, &lvm, &storage_container_factory);

  auto tpm_system_key = mount_encrypted::TpmSystemKey(
      &platform, init_metrics::InitMetrics::Get(), rootdir);

  if (!encrypted_fs) {
    LOG(ERROR) << "Failed to create encrypted fs handler.";
    return 1;
  }

  LOG(INFO) << "Starting.";

  if (args.size() >= 1) {
    if (args[0] == "umount") {
      return encrypted_fs->Teardown() ? 0 : 1;
    } else if (args[0] == "info") {
      // Report info from the encrypted mount.
      tpm_system_key.ReportInfo();
      encrypted_fs->ReportInfo();
      return 0;
    } else if (args[0] == "set") {
      const char* key_material = args.size() >= 2 ? args[1].c_str() : nullptr;
      if (!key_material) {
        LOG(ERROR) << "Key material file not provided.";
        return 1;
      }
      return tpm_system_key.Set(base::FilePath(key_material)) ? 0 : 1;
    } else if (args[0] == "mount") {
      goto mount_encrypted_partition;
    } else {
      print_usage(argv[0]);
      return 1;
    }
  }

mount_encrypted_partition:
  // For the mount operation at boot, return false to trigger
  // chromeos_startup do the stateful wipe.
  if (!encrypted_fs->CheckStates())
    return 1;

  // default operation is mount encrypted partition.
  auto key = tpm_system_key.Load(!FLAGS_unsafe);
  if (!key)
    return 1;

  libstorage::FileSystemKey encryption_key;
  encryption_key.fek = key->encryption_key();
  if (!encrypted_fs->Setup(encryption_key, key->is_fresh()))
    return 1;

  return tpm_system_key.Export() ? 0 : 1;
}
