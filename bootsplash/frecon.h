// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_FRECON_H_
#define BOOTSPLASH_FRECON_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <brillo/files/safe_fd.h>

namespace bootsplash {

class Frecon {
 public:
  Frecon();
  Frecon(const Frecon&) = delete;
  Frecon& operator=(const Frecon&) = delete;
  ~Frecon() = default;

  static std::unique_ptr<Frecon> Create(bool feature_simon_enabled);

  // Write a string to the frecon VT file.
  void Write(const std::string& msg);

  // Drop DRM Master.
  void DropDrmMaster();

  // Update the display with the current boot logo frame.
  void UpdateBootLogoDisplay(int frame_number);

 private:
  void HandleRunDirChanged(const base::FilePath& path, bool error);
  void WatchFreconRunDir();
  void HandleFreconFilesCreated();
  bool FreconStarted();

  base::FilePath boot_splash_assets_dir_;
  brillo::SafeFD frecon_vt_;
  std::unique_ptr<base::FilePathWatcher> run_dir_watcher_;
  base::Lock lock_;
  base::ConditionVariable frecon_files_created_;

  base::WeakPtrFactory<Frecon> weak_factory_{this};
};

}  // namespace bootsplash

#endif  // BOOTSPLASH_FRECON_H_
