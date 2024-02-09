// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FAKE_PLATFORM_H_
#define CRYPTOHOME_FAKE_PLATFORM_H_

#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/unguessable_token.h>
#include <brillo/blkdev_utils/loop_device_fake.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/fake_platform.h>

namespace cryptohome {

class FakePlatform final : public libstorage::FakePlatform {
 public:
  FakePlatform();
  ~FakePlatform() override;

  // Prohibit copy/move/assignment.
  FakePlatform(const FakePlatform&) = delete;
  FakePlatform(const FakePlatform&&) = delete;
  FakePlatform& operator=(const FakePlatform&) = delete;
  FakePlatform& operator=(const FakePlatform&&) = delete;

  // Test API

  // TODO(chromium:1141301, dlunev): this is a workaround of the fact that
  // libbrillo reads and caches system salt on it own and we are unable to
  // inject the tmpfs path to it.
  void SetSystemSaltForLibbrillo(const brillo::SecureBlob& salt);
  void RemoveSystemSaltForLibbrillo();

 private:
  std::string* old_salt_ = nullptr;

  // Pseudo-random engine for generating stable and predictable values. Note
  // that the default constructor uses hardcoded seed.
  std::mt19937_64 random_engine_64_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FAKE_PLATFORM_H_
