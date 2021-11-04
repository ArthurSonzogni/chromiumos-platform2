// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"
#include "rmad/utils/fake_cros_config_utils.h"

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/values.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {
constexpr char kCrosRootKey[] = "/";
constexpr char kCrosModelNameKey[] = "name";
constexpr char kCrosIdentityKey[] = "identity";
constexpr char kCrosIdentitySkuKey[] = "sku-id";
constexpr char kCrosIdentityWhitelabelKey[] = "whitelabel-tag";

constexpr char kModelName[] = "TestModelName";

constexpr int kSkuId = 1234567890;
constexpr char kSkuIdStr[] = "1234567890";

constexpr char kWhitelabelTag[] = "TestWhiteLabelTag";

constexpr char kJsonStoreFileName[] = "json_store_file";
constexpr char kCrosConfigJson[] =
    R"({
      "chromeos": {
        "configs": [
          {
            "name": "TestModelName",
            "identity": {
              "sku-id": 1234567890
            }
          },
          {
            "name": "TestModelName-1",
            "identity": {
              "sku-id": 1111111111,
              "whitelabel-tag": "TestWhiteLabelTag-1"
            }
          },
          {
            "name": "TestModelName",
            "identity": {
              "sku-id": 1111111112,
              "whitelabel-tag": "TestWhiteLabelTag"
            }
          },
          {
            "name": "TestModelName",
            "identity": {
              "sku-id": 1111111113,
              "whitelabel-tag": "TestWhiteLabelTag-2"
            }
          }
        ]
      }
    })";

const std::vector<std::string> kTargetWhitelabelTagList = {
    "TestWhiteLabelTag", "TestWhiteLabelTag-2"};
const std::vector<int> kTargetSkuIdList = {1111111112, 1111111113, 1234567890};

class CrosConfigUtilsImplTest : public testing::Test {
 public:
  CrosConfigUtilsImplTest() {}

  base::FilePath CreateInputFile(const std::string& filename,
                                 const char* str,
                                 int size) {
    base::FilePath file_path = temp_dir_.GetPath().AppendASCII(filename);
    base::WriteFile(file_path, str, size);
    return file_path;
  }

  std::unique_ptr<CrosConfigUtils> CreateCrosConfigUtils() {
    auto cros_config_path = CreateInputFile(kJsonStoreFileName, kCrosConfigJson,
                                            std::size(kCrosConfigJson) - 1);
    auto fake_cros_config = std::make_unique<brillo::FakeCrosConfig>();
    fake_cros_config->SetString(kCrosRootKey, kCrosModelNameKey, kModelName);
    fake_cros_config->SetString(
        std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
        kCrosIdentitySkuKey, kSkuIdStr);
    fake_cros_config->SetString(
        std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
        kCrosIdentityWhitelabelKey, kWhitelabelTag);

    return std::make_unique<CrosConfigUtilsImpl>(
        cros_config_path.MaybeAsASCII(), std::move(fake_cros_config));
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(CrosConfigUtilsImplTest, GetModelName_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string model_name;
  EXPECT_TRUE(cros_config_utils->GetModelName(&model_name));
  EXPECT_EQ(model_name, kModelName);
}

TEST_F(CrosConfigUtilsImplTest, GetWhitelabelTag_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string whitelabel_tag;
  EXPECT_TRUE(cros_config_utils->GetCurrentWhitelabelTag(&whitelabel_tag));
  EXPECT_EQ(whitelabel_tag, kWhitelabelTag);
}

TEST_F(CrosConfigUtilsImplTest, GetSkuId_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  int sku_id;
  EXPECT_TRUE(cros_config_utils->GetCurrentSkuId(&sku_id));
  EXPECT_EQ(sku_id, kSkuId);
}

TEST_F(CrosConfigUtilsImplTest, GetSkuIdList_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::vector<int> sku_id_list;
  EXPECT_TRUE(cros_config_utils->GetSkuIdList(&sku_id_list));
  EXPECT_EQ(sku_id_list, kTargetSkuIdList);
}

TEST_F(CrosConfigUtilsImplTest, GetWhitelabelTagList_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::vector<std::string> whitelabel_tag_list;
  EXPECT_TRUE(cros_config_utils->GetWhitelabelTagList(&whitelabel_tag_list));
  EXPECT_EQ(whitelabel_tag_list, kTargetWhitelabelTagList);
}

namespace fake {

class FakeCrosConfigUtilsTest : public testing::Test {
 public:
  FakeCrosConfigUtilsTest() = default;
  ~FakeCrosConfigUtilsTest() override = default;

 protected:
  void SetUp() override {
    fake_cros_config_utils_ = std::make_unique<FakeCrosConfigUtils>();
  }

  std::unique_ptr<FakeCrosConfigUtils> fake_cros_config_utils_;
};

TEST_F(FakeCrosConfigUtilsTest, GetModelName_Success) {
  std::string model_name;
  EXPECT_TRUE(fake_cros_config_utils_->GetModelName(&model_name));
  EXPECT_EQ(model_name, "fake_model");
}

TEST_F(FakeCrosConfigUtilsTest, GetModelName_Nullptr) {
  EXPECT_DEATH(fake_cros_config_utils_->GetModelName(nullptr), "");
}

TEST_F(FakeCrosConfigUtilsTest, GetCurrentSkuId_Success) {
  int sku_id;
  EXPECT_TRUE(fake_cros_config_utils_->GetCurrentSkuId(&sku_id));
  EXPECT_EQ(sku_id, 1);
}

TEST_F(FakeCrosConfigUtilsTest, GetCurrentSkuId_Nullptr) {
  EXPECT_DEATH(fake_cros_config_utils_->GetCurrentSkuId(nullptr), "");
}

TEST_F(FakeCrosConfigUtilsTest, GetCurrentWhitelabelTag_Success) {
  std::string wl_tag;
  EXPECT_TRUE(fake_cros_config_utils_->GetCurrentWhitelabelTag(&wl_tag));
  EXPECT_EQ(wl_tag, "fake_whitelabel_1");
}

TEST_F(FakeCrosConfigUtilsTest, GetCurrentWhitelabelTag_Nullptr) {
  EXPECT_DEATH(fake_cros_config_utils_->GetCurrentWhitelabelTag(nullptr), "");
}

TEST_F(FakeCrosConfigUtilsTest, GetSkuIdList_Success) {
  std::vector<int> sku_id_list;
  const std::vector<int> expected_sku_id_list = {1, 2, 3, 4};
  EXPECT_TRUE(fake_cros_config_utils_->GetSkuIdList(&sku_id_list));
  EXPECT_EQ(sku_id_list, expected_sku_id_list);
}

TEST_F(FakeCrosConfigUtilsTest, GetSkuIdList_Nullptr) {
  EXPECT_DEATH(fake_cros_config_utils_->GetSkuIdList(nullptr), "");
}

TEST_F(FakeCrosConfigUtilsTest, GetWhitelabelTagList_Success) {
  std::vector<std::string> wl_tag_list;
  const std::vector<std::string> expected_wl_tag_list = {"fake_whitelabel_1",
                                                         "fake_whitelabel_2"};
  EXPECT_TRUE(fake_cros_config_utils_->GetWhitelabelTagList(&wl_tag_list));
  EXPECT_EQ(wl_tag_list, expected_wl_tag_list);
}

TEST_F(FakeCrosConfigUtilsTest, GetWhitelabelTagList_Nullptr) {
  EXPECT_DEATH(fake_cros_config_utils_->GetWhitelabelTagList(nullptr), "");
}

}  // namespace fake

}  // namespace rmad
