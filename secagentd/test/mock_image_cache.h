// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_IMAGE_CACHE_H_
#define SECAGENTD_TEST_MOCK_IMAGE_CACHE_H_

#include <cstdint>

#include "gmock/gmock.h"
#include "secagentd/image_cache.h"

namespace secagentd::testing {

class MockImageCache : public ImageCacheInterface {
 public:
  MOCK_METHOD(absl::StatusOr<HashValue>,
              InclusiveGetImage,
              (const ImageCacheKeyType& image_key,
               bool force_full_sha256,
               uint64_t pid_for_setns,
               const base::FilePath& image_path_in_ns),
              (override));

  MOCK_METHOD(absl::StatusOr<base::FilePath>,
              GetPathInCurrentMountNs,
              (uint64_t pid_for_setns,
               const base::FilePath& image_path_in_pids_ns),
              (const, override));

  MOCK_METHOD(absl::StatusOr<HashValue>,
              GenerateImageHash,
              (const base::FilePath& image_path_in_curent_ns,
               bool force_full_sha256),
              (override));
};

}  // namespace secagentd::testing
#endif  // SECAGENTD_TEST_MOCK_IMAGE_CACHE_H_
