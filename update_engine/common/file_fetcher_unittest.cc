// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/file_fetcher.h"

#include <string>

#include <gtest/gtest.h>

#include "update_engine/common/test_utils.h"

namespace chromeos_update_engine {

class FileFetcherUnitTest : public ::testing::Test {};

TEST_F(FileFetcherUnitTest, SupporterUrlsTest) {
  EXPECT_TRUE(FileFetcher::SupportedUrl("file:///path/to/somewhere.bin"));
  EXPECT_TRUE(FileFetcher::SupportedUrl("FILE:///I/LIKE/TO/SHOUT"));

  EXPECT_FALSE(FileFetcher::SupportedUrl("file://relative"));
  EXPECT_FALSE(FileFetcher::SupportedUrl("http:///no_http_here"));
}

}  // namespace chromeos_update_engine
