// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::CachedVpdInfoPtr;

const char kCachedVpdPropertiesPath[] = "/cros-healthd/cached-vpd";
const char kHasSkuNumberProperty[] = "has-sku-number";
const char kRelativeSKUNumberPath[] = "sys/firmware/vpd/ro/sku_number";
const char kFakeSKUNumber[] = "ABCD&^A";

}  // namespace

class VpdUtilsTest : public ::testing::Test {
 protected:
  VpdUtilsTest() {
    fake_cros_config_ = std::make_unique<brillo::FakeCrosConfig>();
    cached_vpd_fetcher_ =
        std::make_unique<CachedVpdFetcher>(fake_cros_config_.get());
  }

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  const base::FilePath& GetTempDirPath() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  CachedVpdInfoPtr FetchCachedVpdInfo(const base::FilePath& root_dir) {
    return cached_vpd_fetcher_->FetchCachedVpdInfo(root_dir);
  }

  void SetHasSkuNumberString(const std::string& val) {
    fake_cros_config_->SetString(kCachedVpdPropertiesPath,
                                 kHasSkuNumberProperty, val);
  }

 private:
  std::unique_ptr<brillo::FakeCrosConfig> fake_cros_config_;
  std::unique_ptr<CachedVpdFetcher> cached_vpd_fetcher_;
  base::ScopedTempDir temp_dir_;

  DISALLOW_COPY_AND_ASSIGN(VpdUtilsTest);
};

// Test that we can read the cached VPD info, when it exists.
TEST_F(VpdUtilsTest, TestFetchCachedVpdInfo) {
  base::FilePath root_dir = GetTempDirPath();
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeSKUNumberPath), kFakeSKUNumber));
  SetHasSkuNumberString("true");
  auto vpd_info = FetchCachedVpdInfo(root_dir);
  ASSERT_TRUE(vpd_info->sku_number.has_value());
  EXPECT_EQ(vpd_info->sku_number.value(), kFakeSKUNumber);
}

// Test that reading cached VPD info that does not exist fails gracefully.
TEST_F(VpdUtilsTest, TestFetchCachedVpdInfoNoFile) {
  SetHasSkuNumberString("true");
  auto vpd_info = FetchCachedVpdInfo(GetTempDirPath());
  ASSERT_TRUE(vpd_info->sku_number.has_value());
  EXPECT_EQ(vpd_info->sku_number.value(), "");
}

// Test that no sku_number is returned when the device does not have a SKU
// number.
TEST_F(VpdUtilsTest, TestFetchCachedVpdInfoNoSkuNumber) {
  auto vpd_info = FetchCachedVpdInfo(base::FilePath());
  EXPECT_FALSE(vpd_info->sku_number.has_value());
}

}  // namespace diagnostics
