/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This tool will attempt to mount or create the encrypted stateful partition,
 * and the various bind mountable subdirectories.
 *
 */

#include "init/tpm_encryption/tpm_setup.h"

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/tlcl_wrapper/fake_tlcl_wrapper.h>
#include <libstorage/platform/fake_platform.h>

#include "init/metrics/metrics.h"

using ::testing::_;
using ::testing::Return;

namespace encryption {

class TpmSystemKeyTest : public testing::Test {
 public:
  void SetUp() override {
    platform_ = std::make_unique<libstorage::FakePlatform>();
    tlcl_ = std::make_unique<hwsec_foundation::FakeTlclWrapper>();
    ASSERT_TRUE(platform_->CreateDirectory(rootdir_));
    ASSERT_TRUE(platform_->CreateDirectory(stateful_mount_));
    metrics_singleton_ =
        std::make_unique<init_metrics::ScopedInitMetricsSingleton>(
            rootdir_.Append("metrics").value());

    tpm_system_key_ = std::make_unique<TpmSystemKey>(
        platform_.get(), tlcl_.get(), init_metrics::InitMetrics::Get(),
        rootdir_, stateful_mount_);
  }

 protected:
  base::FilePath rootdir_{"/test1"};
  base::FilePath stateful_mount_{"/test2"};
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<hwsec_foundation::FakeTlclWrapper> tlcl_;
  std::unique_ptr<init_metrics::ScopedInitMetricsSingleton> metrics_singleton_;
  std::unique_ptr<TpmSystemKey> tpm_system_key_;
};

TEST_F(TpmSystemKeyTest, MigrateTpmOwnerShipAbsent) {
  // Call Load, verify no migration occurs.
  EXPECT_TRUE(tpm_system_key_->Load(false /* safe_mount */, base::FilePath()));
  EXPECT_FALSE(platform_->FileExists(
      stateful_mount_.Append("unencrypted/tpm_manager/tpm_owned")));
  EXPECT_FALSE(platform_->FileExists(stateful_mount_.Append(".tpm_owned")));
}

TEST_F(TpmSystemKeyTest, MigrateTpmOwnerShipPresent) {
  ASSERT_TRUE(
      platform_->TouchFileDurable(stateful_mount_.Append(".tpm_owned")));
  // Call Load, verify a migration does occur.
  EXPECT_TRUE(tpm_system_key_->Load(false /* safe_mount */, base::FilePath()));
  EXPECT_TRUE(platform_->FileExists(
      stateful_mount_.Append("unencrypted/tpm_manager/tpm_owned")));
  EXPECT_FALSE(platform_->FileExists(stateful_mount_.Append(".tpm_owned")));
}

}  // namespace encryption
