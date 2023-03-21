// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include "libec/fingerprint/fp_preload_template_params.h"

namespace ec {
namespace {

TEST(FpPreloadTemplateParams, HeaderSize) {
  EXPECT_EQ(sizeof(fp_preload_template::Header),
            sizeof(ec_params_fp_preload_template));
}

TEST(FpPreloadTemplateParams, ParamsSize) {
  EXPECT_EQ(sizeof(fp_preload_template::Params), kMaxPacketSize);
}

}  // namespace
}  // namespace ec
