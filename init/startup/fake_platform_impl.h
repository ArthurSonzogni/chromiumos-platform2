// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_FAKE_PLATFORM_IMPL_H_
#define INIT_STARTUP_FAKE_PLATFORM_IMPL_H_

#include <stdlib.h>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

#include <base/files/file_util.h>

#include "init/startup/platform_impl.h"

namespace startup {

// This class is utilized by tests to override functions that
// use of system calls or other functionality that utilizes
// system command outputs.

class FakePlatform : public Platform {
 public:
  FakePlatform();

  void SetStatResultForPath(const base::FilePath& path, const struct stat& st);

  void SetMountResultForPath(const base::FilePath& path,
                             const std::string& output);
  // Wrapper around stat(2).
  bool Stat(const base::FilePath& path, struct stat* st) override;

  // Wrapper around mount(2).
  bool Mount(const base::FilePath& src,
             const base::FilePath& dst,
             const std::string& type,
             unsigned long flags,
             const std::string& data) override;
  bool Mount(const std::string& src,
             const base::FilePath& dst,
             const std::string& type,
             unsigned long flags,
             const std::string& data) override;

 private:
  std::unordered_map<std::string, struct stat> result_map_;
  std::unordered_map<std::string, std::string> mount_result_map_;
};

}  // namespace startup

#endif  // INIT_STARTUP_FAKE_PLATFORM_IMPL_H_
