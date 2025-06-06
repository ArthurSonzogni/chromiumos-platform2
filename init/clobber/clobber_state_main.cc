// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/blkdev_utils/lvm.h>
#include <cros_config/cros_config.h>
#include <libcrossystem/crossystem.h>

#include "init/clobber/clobber_state.h"

namespace {

base::File OpenTerminal() {
  base::FilePath terminal_path;
  if (base::PathExists(base::FilePath("/sbin/frecon"))) {
    terminal_path = base::FilePath("/run/frecon/vt0");
  } else {
    terminal_path = base::FilePath("/dev/tty1");
  }
  base::File terminal =
      base::File(terminal_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

  if (!terminal.IsValid()) {
    PLOG(WARNING) << "Could not open terminal " << terminal_path.value()
                  << " falling back to /dev/null";
    terminal = base::File(base::FilePath("/dev/null"),
                          base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  }

  return terminal;
}

bool MetaDataPartitionNeeded(brillo::CrosConfigInterface* config) {
  std::string dm_default_key_enabled;
  if (!config->GetString("/disk-layout", "default-key-stateful",
                         &dm_default_key_enabled) ||
      dm_default_key_enabled != "true") {
    // No variable present.
    return false;
  }
  return true;
}
}  // namespace

int main(int argc, char* argv[]) {
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file_path = "/tmp/clobber-state.log";
  // All logging happens in the main thread, so there is no need to lock the log
  // file.
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = logging::DELETE_OLD_LOG_FILE;
  logging::InitLogging(settings);

  if (getuid() != 0) {
    LOG(ERROR) << argv[0] << " must be run as root";
    return 1;
  }

  std::unique_ptr<brillo::CrosConfigInterface> config =
      std::make_unique<brillo::CrosConfig>();
  ClobberState::Arguments args = ClobberState::ParseArgv(
      argc, argv, MetaDataPartitionNeeded(config.get()));

  std::unique_ptr<ClobberUi> ui = std::make_unique<ClobberUi>(OpenTerminal());
  std::unique_ptr<ClobberWipe> wipe = std::make_unique<ClobberWipe>(ui.get());
  std::unique_ptr<ClobberLvm> clobber_lvm =
      USE_DEVICE_MAPPER
          ? std::make_unique<ClobberLvm>(
                wipe.get(), std::make_unique<brillo::LogicalVolumeManager>())
          : std::unique_ptr<ClobberLvm>();

  ClobberState clobber(args, std::make_unique<crossystem::Crossystem>(),
                       std::move(ui), std::move(wipe), std::move(clobber_lvm));

  if (args.dry_run) {
    LOG(INFO) << "This is a dry run, only listing files to preserve";
    std::vector<base::FilePath> preserved_files =
        clobber.GetPreservedFilesList();
    for (const base::FilePath& fp : preserved_files) {
      LOG(INFO) << "Preserving file: " << fp.value();
    }
    return 0;
  }

  return clobber.Run();
}
