// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_OOBE_CONFIG_TEST_H_
#define OOBE_CONFIG_OOBE_CONFIG_TEST_H_

#include "oobe_config/oobe_config.h"

#include <memory>

#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace oobe_config {

class OobeConfigTest : public ::testing::Test {
 protected:
  void SetUp() override;

  base::ScopedTempDir fake_root_dir_;
  std::unique_ptr<OobeConfig> oobe_config_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_OOBE_CONFIG_TEST_H_
