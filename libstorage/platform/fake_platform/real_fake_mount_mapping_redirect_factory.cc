// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/platform/fake_platform/real_fake_mount_mapping_redirect_factory.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "libstorage/platform/util/get_random_suffix.h"

namespace libstorage {

base::FilePath RealFakeMountMappingRedirectFactory::Create() {
  base::FilePath redirect;
  base::GetTempDir(&redirect);
  redirect = redirect.Append(GetRandomSuffix());
  CHECK(base::CreateDirectory(redirect));
  return redirect;
}

}  // namespace libstorage
