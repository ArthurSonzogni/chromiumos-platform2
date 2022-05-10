// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "init/crossystem.h"
#include "init/crossystem_fake.h"
#include "init/startup/chromeos_startup.h"
#include "init/startup/fake_platform_impl.h"
#include "init/startup/platform_impl.h"

class EarlySetupTest : public ::testing::Test {
 protected:
  EarlySetupTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    kernel_debug_ = base_dir_.Append("sys/kernel/debug");
    kernel_config_ = base_dir_.Append("sys/kernel/config");
    tracing_ = kernel_debug_.Append("tracing/tracing_on");
    security_hardening_ = base_dir_.Append(
        "usr/share/cros/startup/disable_stateful_security_hardening");
    kernel_security_ = base_dir_.Append("sys/kernel/security");
    namespaces_ = base_dir_.Append("run/namespaces");
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir_, base_dir_,
        base_dir_, base_dir_,
        std::unique_ptr<startup::FakePlatform>(platform_));
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath kernel_debug_;
  base::FilePath dev_pts_;
  base::FilePath dev_shm_;
  base::FilePath kernel_config_;
  base::FilePath tracing_;
  base::FilePath security_hardening_;
  base::FilePath kernel_security_;
  base::FilePath namespaces_;
};

TEST_F(EarlySetupTest, NoTracing) {
  platform_->SetMountResultForPath(kernel_debug_, "debugfs");
  platform_->SetMountResultForPath(kernel_config_, "configfs");
  platform_->SetMountResultForPath(kernel_security_, "securityfs");
  platform_->SetMountResultForPath(namespaces_, "");

  startup_->EarlySetup();
}
