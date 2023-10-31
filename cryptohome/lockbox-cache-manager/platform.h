// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LOCKBOX_CACHE_MANAGER_PLATFORM_H_
#define CRYPTOHOME_LOCKBOX_CACHE_MANAGER_PLATFORM_H_

#include <string>
#include <vector>

namespace cryptohome {
class Platform {
 public:
  Platform() = default;
  virtual ~Platform() = default;
  virtual bool IsOwnedByRoot(const std::string& path);
  virtual bool GetAppOutputAndError(const std::vector<std::string>& argv,
                                    std::string* output);
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_LOCKBOX_CACHE_MANAGER_PLATFORM_H_
