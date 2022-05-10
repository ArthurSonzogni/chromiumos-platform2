// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_PLATFORM_IMPL_H_
#define INIT_STARTUP_PLATFORM_IMPL_H_

#include <string>

#include <base/files/file_path.h>

namespace startup {

// Platform defines functions that interface with the filesystem and
// other utilities that we want to override for testing. That includes
// wrappers functions for syscalls.
class Platform {
 public:
  Platform() {}
  virtual ~Platform() = default;

  // Wrapper around stat(2).
  virtual bool Stat(const base::FilePath& path, struct stat* st);

  // Wrapper around mount(2).
  virtual bool Mount(const base::FilePath& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     unsigned long flags,
                     const std::string& data);
  virtual bool Mount(const std::string& src,
                     const base::FilePath& dst,
                     const std::string& type,
                     unsigned long flags,
                     const std::string& data);

  // Wrapper around umount(2).
  virtual bool Umount(const base::FilePath& path);
};

}  // namespace startup

#endif  // INIT_STARTUP_PLATFORM_IMPL_H_
