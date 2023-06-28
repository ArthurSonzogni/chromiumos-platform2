// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "bootsplash/frecon.h"
#include "bootsplash/paths.h"

namespace bootsplash {

std::unique_ptr<Frecon> Frecon::Create(bool feature_simon_enabled) {
  std::unique_ptr<Frecon> new_frecon = std::make_unique<Frecon>();

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
  base::FilePath frecon_vt_path = paths::Get(paths::kFreconVt);

  if (!base::WriteFile(frecon_vt_path, msg.c_str())) {
    LOG(ERROR) << "Failed to write data to file: '" << frecon_vt_path.value()
               << "'";
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
