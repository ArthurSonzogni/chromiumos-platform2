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
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/fake_platform.h>

#include "init/metrics/metrics.h"
#include "init/tpm_encryption/tlcl_stub.h"

using ::testing::_;
using ::testing::Return;

namespace encryption {

class TpmSystemKeyTest : public testing::Test {
 public:
  void SetUp() override {
    rootdir_ = base::FilePath("/");
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    platform_ = std::make_unique<libstorage::FakePlatform>();
    metrics_singleton_ =
        std::make_unique<init_metrics::ScopedInitMetricsSingleton>(
            tmpdir_.GetPath().Append("metrics").value());

    tpm_system_key_ = std::make_unique<TpmSystemKey>(
        platform_.get(), init_metrics::InitMetrics::Get(), rootdir_);
  }

 protected:
  base::FilePath rootdir_;
  base::ScopedTempDir tmpdir_;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<init_metrics::ScopedInitMetricsSingleton> metrics_singleton_;
  std::unique_ptr<TpmSystemKey> tpm_system_key_;

  // Create a global variable needed by tpm_system_key.
  TlclStub tlcl_;
};

TEST_F(TpmSystemKeyTest, MigrateTpmOwnerShipAbsent) {
  // Call Load, verify no migration occurs.
  EXPECT_TRUE(tpm_system_key_->Load(false /* safe_mount */));
  EXPECT_FALSE(platform_->FileExists(rootdir_.Append(
      "mnt/stateful_partition/unencrypted/tpm_manager/tpm_owned")));
  EXPECT_FALSE(platform_->FileExists(
      rootdir_.Append("mnt/stateful_partition/.tpm_owned")));
}

TEST_F(TpmSystemKeyTest, MigrateTpmOwnerShipPresent) {
  ASSERT_TRUE(platform_->TouchFileDurable(
      rootdir_.Append("mnt/stateful_partition/.tpm_owned")));
  // Call Load, verify a migration does occur.
  EXPECT_TRUE(tpm_system_key_->Load(false /* safe_mount */));
  EXPECT_TRUE(platform_->FileExists(rootdir_.Append(
      "mnt/stateful_partition/unencrypted/tpm_manager/tpm_owned")));
  EXPECT_FALSE(platform_->FileExists(
      rootdir_.Append("mnt/stateful_partition/.tpm_owned")));
}

}  // namespace encryption
