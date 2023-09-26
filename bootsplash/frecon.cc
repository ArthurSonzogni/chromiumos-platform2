// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string>
#include <tuple>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread.h>
#include <base/time/time.h>

#include "bootsplash/frecon.h"
#include "bootsplash/paths.h"

namespace {

const char kBackgroundThreadName[] = "bootsplash_frecon_background_thread";
constexpr base::TimeDelta kFreconFilesCreateTimeout = base::Seconds(5);

brillo::SafeFD OpenFreconVtFile() {
  base::FilePath frecon_vt_path = paths::Get(paths::kFreconVt);
  brillo::SafeFD frecon_vt_fd;
  brillo::SafeFD::Error err;
  std::tie(frecon_vt_fd, err) =
      brillo::SafeFD::Root().first.OpenExistingFile(frecon_vt_path, O_WRONLY);
  if (brillo::SafeFD::IsError(err)) {
    LOG(ERROR) << "Failed to open \"" << frecon_vt_path.value()
               << "\" with error " << static_cast<int>(err);
    return brillo::SafeFD();
  }

  return frecon_vt_fd;
}

bool FreconFilesCreated() {
  bool hi_res_exists = base::PathExists(paths::Get(paths::kFreconHiRes));
  bool vt_exists = base::PathExists(paths::Get(paths::kFreconVt));

  return hi_res_exists && vt_exists;
}

}  // namespace

namespace bootsplash {

Frecon::Frecon() : frecon_files_created_(&lock_) {}

void Frecon::HandleFreconFilesCreated() {
  frecon_files_created_.Signal();
  run_dir_watcher_.reset();
}

void Frecon::HandleRunDirChanged(const base::FilePath& path, bool error) {
  if (error) {
    LOG(ERROR) << "Error while watching '" << path.value() << "'";
  }

  if (FreconFilesCreated()) {
    HandleFreconFilesCreated();
  }
}

void Frecon::WatchFreconRunDir() {
  const base::FilePath run_path = paths::Get(paths::kFreconRunDir);

  CHECK(base::FilePathWatcher::RecursiveWatchAvailable());
  run_dir_watcher_ = std::make_unique<base::FilePathWatcher>();
  if (!run_dir_watcher_->Watch(run_path,
                               base::FilePathWatcher::Type::kRecursive,
                               base::BindRepeating(&Frecon::HandleRunDirChanged,
                                                   base::Unretained(this)))) {
    LOG(ERROR) << "Failed to start watcher for '" << run_path.value() << "'";
  }

  // It's possible for the files to already exist, so the FilePathWatcher would
  // not see any changes in the directory. Prevent this race condition by
  // double-checking if the necessary files already exist once the Watcher has
  // been started.
  if (FreconFilesCreated()) {
    HandleFreconFilesCreated();
  }
}

bool Frecon::FreconStarted() {
  const base::FilePath frecon_hi_res_path = paths::Get(paths::kFreconHiRes);
  const base::FilePath frecon_vt_path = paths::Get(paths::kFreconVt);

  base::Thread background_thread(kBackgroundThreadName);
  CHECK(background_thread.Start()) << "Failed to start background thread.";
  background_thread.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&Frecon::WatchFreconRunDir, weak_factory_.GetWeakPtr()));

  {
    base::AutoLock auto_lock(lock_);
    frecon_files_created_.TimedWait(kFreconFilesCreateTimeout);
  }

  if (!base::PathExists(frecon_hi_res_path)) {
    LOG(ERROR) << "Frecon path '" << frecon_hi_res_path.value()
               << "' does not exist.";
    return false;
  }
  if (!base::PathExists(frecon_vt_path)) {
    LOG(ERROR) << "Frecon path '" << frecon_vt_path.value()
               << "' does not exist.";
    return false;
  }

  return true;
}

std::unique_ptr<Frecon> Frecon::Create(bool feature_simon_enabled) {
  std::unique_ptr<Frecon> new_frecon = std::make_unique<Frecon>();

  // Wait for frecon to start and create the necessary files.
  if (!new_frecon->FreconStarted()) {
    LOG(ERROR) << "Frecon failed to initialize";
    return nullptr;
  }
  LOG(INFO) << "Frecon initialized";

  // Keep the frecon VT file open, to avoid re-opening every Write().
  new_frecon->frecon_vt_ = OpenFreconVtFile();

  // Draw the splash images to VT0, the splash screen terminal.
  new_frecon->Write("\033]switchvt:0\a");

  new_frecon->boot_splash_assets_dir_ =
      paths::GetBootSplashAssetsDir(feature_simon_enabled);
  if (!base::PathExists(new_frecon->boot_splash_assets_dir_)) {
    LOG(ERROR) << "Boot splash directory does not exist: '"
               << new_frecon->boot_splash_assets_dir_ << "'";
    return nullptr;
  }

  return new_frecon;
}

void Frecon::Write(const std::string& msg) {
  if (!frecon_vt_.is_valid()) {
    LOG(ERROR) << "Frecon VT file descriptor is invalid.";
    return;
  }

  if (!base::WriteFileDescriptor(frecon_vt_.get(), msg.c_str())) {
    LOG(ERROR) << "Failed to write data to frecon VT file.";
  }
}

void Frecon::DropDrmMaster() {
  Write("\033]drmdropmaster\a");
}

void Frecon::UpdateBootLogoDisplay(int frame_number) {
  std::string imageFileName =
      base::StringPrintf("%s%02d%s", paths::kBootSplashFilenamePrefix,
                         frame_number, paths::kImageExtension);
  base::FilePath imagePath =
      paths::Get(boot_splash_assets_dir_.value()).Append(imageFileName);

  // Draw the new image
  Write("\033]image:file=" + imagePath.value() + "\a");
}

}  // namespace bootsplash
