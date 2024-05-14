// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <absl/cleanup/cleanup.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/values.h>
#include <brillo/key_value_store.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>
#include <libcrossystem/crossystem.h>
#include <libhwsec-foundation/tlcl_wrapper/tlcl_wrapper_impl.h>
#include <libstorage/platform/platform.h>
#include <vpd/vpd.h>

#include "init/startup/chromeos_startup.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_helper_factory.h"
#include "init/startup/startup_dep_impl.h"

namespace {

// Given that we want to be able to log even if the stateful_partition fails to
// mount we write the logs to /dev/kmsg so they are included in the kernel
// console output. This is especially useful for troubleshooting boot loops.
constexpr char kLogFile[] = "/dev/kmsg";
constexpr char kLsbRelease[] = "/etc/lsb-release";
constexpr char kPrintkDevkmsg[] = "/proc/sys/kernel/printk_devkmsg";
constexpr char kStatefulPartition[] = "/mnt/stateful_partition";

}  // namespace

int main(int argc, char* argv[]) {
  // Set up logging to a file to record any unexpected but non-fatal
  // behavior.
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file_path = kLogFile;
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
  logging::InitLogging(settings);

  std::unique_ptr<libstorage::Platform> platform =
      std::make_unique<libstorage::Platform>();

  // dmesg already handles the timestamp.
  logging::SetLogItems(false, false, false, false);

  std::string printk_devkmsg_value("ratelimit\n");
  // Temporarily disable printk ratelimiting until this exits.
  if (!platform->ReadFileToString(base::FilePath(kPrintkDevkmsg),
                                  &printk_devkmsg_value)) {
    PLOG(ERROR) << "Failed to read " << kPrintkDevkmsg;
  }
  absl::Cleanup restore_rate_limit = [&]() {
    if (!platform->WriteStringToFile(base::FilePath(kPrintkDevkmsg),
                                     printk_devkmsg_value)) {
      PLOG(ERROR) << "Failed to restore " << kPrintkDevkmsg;
    }
  };
  if (!platform->WriteStringToFile(base::FilePath(kPrintkDevkmsg), "on\n")) {
    PLOG(ERROR) << "Failed to write " << kPrintkDevkmsg;
  }

  startup::Flags flags;
  startup::ChromeosStartup::ParseFlags(&flags, argc, argv);
  // A decreasing number is more verbose and numbers below zero are OK.
  logging::SetMinLogLevel(logging::LOGGING_WARNING - flags.verbosity);

  std::unique_ptr<startup::StartupDep> startup_dep =
      std::make_unique<startup::StartupDep>(platform.get());

  startup::MountHelperFactory mount_helper_factory(
      platform.get(), startup_dep.get(), flags, base::FilePath("/"),
      base::FilePath(kStatefulPartition), base::FilePath(kLsbRelease));
  std::unique_ptr<startup::MountHelper> mount_helper =
      mount_helper_factory.Generate(platform->GetCrosssystem());
  std::unique_ptr<hwsec_foundation::TlclWrapper> tlcl =
      std::make_unique<hwsec_foundation::TlclWrapperImpl>();
  std::unique_ptr<startup::ChromeosStartup> startup =
      std::make_unique<startup::ChromeosStartup>(
          std::make_unique<vpd::Vpd>(), flags, base::FilePath("/"),
          base::FilePath(kStatefulPartition), base::FilePath(kLsbRelease),
          platform.get(), startup_dep.get(), std::move(mount_helper),
          std::move(tlcl));

  return startup->Run();
}
