// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_MOCK_PLATFORM_H_
#define CROS_DISKS_MOCK_PLATFORM_H_

#include <string>
#include <unordered_set>

#include "cros-disks/platform.h"

namespace cros_disks {

class MockPlatform : public Platform {
 public:
  MOCK_METHOD(MountErrorType,
              Mount,
              (const std::string& source,
               const std::string& target,
               const std::string& filesystem_type,
               uint64_t flags,
               const std::string& options),
              (const, override));
  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string& path, int flags),
              (const, override));
  MOCK_METHOD(bool,
              GetUserAndGroupId,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              GetGroupId,
              (const std::string&, gid_t*),
              (const, override));
  MOCK_METHOD(bool, DirectoryExists, (const std::string&), (const, override));
  MOCK_METHOD(bool, CreateDirectory, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const std::string&, mode_t),
              (const, override));
  MOCK_METHOD(bool,
              CreateTemporaryDirInDir,
              (const std::string&, const std::string&, std::string*),
              (const, override));
  MOCK_METHOD(bool,
              CreateOrReuseEmptyDirectory,
              (const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              CreateOrReuseEmptyDirectoryWithFallback,
              (std::string*, unsigned, const std::unordered_set<std::string>&),
              (const, override));
  MOCK_METHOD(bool,
              RemoveEmptyDirectory,
              (const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              SetOwnership,
              (const std::string&, uid_t, gid_t),
              (const, override));
  MOCK_METHOD(bool, PathExists, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              GetRealPath,
              (const std::string&, std::string*),
              (const, override));
  MOCK_METHOD(bool,
              GetOwnership,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOCK_PLATFORM_H_
