// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "privacy/log.h"

#include <gtest/gtest.h>

namespace privacy {

TEST(LogTest, LogWithOperatorOverloading) {
  privacy::PrivacyMetadata privacy_metadata;
  privacy_metadata.piiType = privacy::PIIType::NOT_REQUIRED;
  privacy_metadata.value = "test";
  std::ostringstream ostream;
  ostream << privacy_metadata;
  EXPECT_EQ(
      ostream.str(),
      '[' + std::to_string(privacy::PIIType::NOT_REQUIRED) + "] " + "test");
}
}  // namespace privacy
