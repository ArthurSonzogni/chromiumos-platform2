// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_FAKE_PLATFORM_FAKE_MOUNT_MAPPING_REDIRECT_FACTORY_H_
#define LIBSTORAGE_PLATFORM_FAKE_PLATFORM_FAKE_MOUNT_MAPPING_REDIRECT_FACTORY_H_

#include <base/files/file_path.h>

namespace libstorage {

// An interface for generating redirects for FakeMoountMapping.
class FakeMountMappingRedirectFactory {
 public:
  virtual ~FakeMountMappingRedirectFactory() = default;

  // Returns a newly created redirect within tmpfs.
  virtual base::FilePath Create() = 0;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_FAKE_PLATFORM_FAKE_MOUNT_MAPPING_REDIRECT_FACTORY_H_
