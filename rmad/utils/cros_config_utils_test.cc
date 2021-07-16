// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

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

}  // namespace rmad
