// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string>
#include <tuple>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "bootsplash/frecon.h"
#include "bootsplash/paths.h"

namespace bootsplash {

namespace {

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

}  // namespace

std::unique_ptr<Frecon> Frecon::Create(bool feature_simon_enabled) {
  std::unique_ptr<Frecon> new_frecon = std::make_unique<Frecon>();

  /* Keep the frecon VT file open, to avoid re-opening every Write(). */
  new_frecon->frecon_vt_ = OpenFreconVtFile();

  /* Draw the splash images to VT0, the splash screen terminal. */
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

  /* Draw the new image */
  Write("\033]image:file=" + imagePath.value() + "\a");
}

}  // namespace bootsplash
