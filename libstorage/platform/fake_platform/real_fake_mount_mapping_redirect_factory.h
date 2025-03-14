// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_FAKE_PLATFORM_REAL_FAKE_MOUNT_MAPPING_REDIRECT_FACTORY_H_
#define LIBSTORAGE_PLATFORM_FAKE_PLATFORM_REAL_FAKE_MOUNT_MAPPING_REDIRECT_FACTORY_H_

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include "libstorage/platform/fake_platform/fake_mount_mapping_redirect_factory.h"

namespace libstorage {

// Real implementation of the factory creates a new unique directory on tmpfs.
class BRILLO_EXPORT RealFakeMountMappingRedirectFactory final
    : public FakeMountMappingRedirectFactory {
 public:
  RealFakeMountMappingRedirectFactory() = default;
  ~RealFakeMountMappingRedirectFactory() override = default;

  base::FilePath Create() override;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_FAKE_PLATFORM_REAL_FAKE_MOUNT_MAPPING_REDIRECT_FACTORY_H_
