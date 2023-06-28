// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_FRECON_H_
#define BOOTSPLASH_FRECON_H_

#include <memory>
#include <string>

namespace bootsplash {

class Frecon {
 public:
  Frecon() = default;
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
  base::FilePath boot_splash_assets_dir_;
};

}  // namespace bootsplash

#endif  // BOOTSPLASH_FRECON_H_
