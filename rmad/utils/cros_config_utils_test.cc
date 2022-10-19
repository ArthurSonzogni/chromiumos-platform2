// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
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
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";
constexpr char kCrosRmadKey[] = "rmad";
constexpr char kCrosRmadEnabledKey[] = "enabled";

constexpr char kModelName[] = "TestModelName";

constexpr int kSkuId = 1234567890;
constexpr char kSkuIdStr[] = "1234567890";

constexpr char kCustomLabelTag[] = "TestCustomLabelTag";

constexpr char kTrueStr[] = "true";

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
              "custom-label-tag": "TestCustomLabelTag-1"
            }
          },
          {
            "name": "TestModelName",
            "identity": {
              "sku-id": 1111111112,
              "custom-label-tag": "TestCustomLabelTag"
            }
          },
          {
            "name": "TestModelName",
            "identity": {
              "sku-id": 1111111113,
              "custom-label-tag": "TestCustomLabelTag-2"
            }
          }
        ]
      }
    })";

constexpr char kCrosConfigJson2[] =
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
              "custom-label-tag": "TestCustomLabelTag-1"
            }
          },
          {
            "name": "TestModelName-1",
            "identity": {
              "sku-id": 1111111112,
              "custom-label-tag": "TestCustomLabelTag"
            }
          },
          {
            "name": "TestModelName-1",
            "identity": {
              "sku-id": 1111111113,
              "custom-label-tag": "TestCustomLabelTag-2"
            }
          }
        ]
      }
    })";

// The first option of the WL list is always an empty string.
const std::vector<std::string> kTargetCustomLabelTagList = {
    "TestCustomLabelTag", "TestCustomLabelTag-2"};
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

  std::unique_ptr<CrosConfigUtils> CreateCrosConfigUtils(
      bool enable_rmad = true) {
    auto cros_config_path = CreateInputFile(kJsonStoreFileName, kCrosConfigJson,
                                            std::size(kCrosConfigJson) - 1);
    auto fake_cros_config = std::make_unique<brillo::FakeCrosConfig>();
    fake_cros_config->SetString(kCrosRootKey, kCrosModelNameKey, kModelName);
    fake_cros_config->SetString(
        std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
        kCrosIdentitySkuKey, kSkuIdStr);
    fake_cros_config->SetString(
        std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
        kCrosIdentityCustomLabelTagKey, kCustomLabelTag);
    if (enable_rmad) {
      fake_cros_config->SetString(
          std::string(kCrosRootKey) + std::string(kCrosRmadKey),
          kCrosRmadEnabledKey, kTrueStr);
    }

    return std::make_unique<CrosConfigUtilsImpl>(
        cros_config_path.MaybeAsASCII(), std::move(fake_cros_config));
  }

  std::unique_ptr<CrosConfigUtils> CreateCrosConfigUtilsWithoutCustomLabel(
      bool enable_rmad = true) {
    auto cros_config_path = CreateInputFile(
        kJsonStoreFileName, kCrosConfigJson2, std::size(kCrosConfigJson2) - 1);
    auto fake_cros_config = std::make_unique<brillo::FakeCrosConfig>();
    fake_cros_config->SetString(kCrosRootKey, kCrosModelNameKey, kModelName);
    fake_cros_config->SetString(
        std::string(kCrosRootKey) + std::string(kCrosIdentityKey),
        kCrosIdentitySkuKey, kSkuIdStr);
    if (enable_rmad) {
      fake_cros_config->SetString(
          std::string(kCrosRootKey) + std::string(kCrosRmadKey),
          kCrosRmadEnabledKey, kTrueStr);
    }

    return std::make_unique<CrosConfigUtilsImpl>(
        cros_config_path.MaybeAsASCII(), std::move(fake_cros_config));
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(CrosConfigUtilsImplTest, GetRmadEnabled_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  bool enabled;
  EXPECT_TRUE(cros_config_utils->GetRmadEnabled(&enabled));
  EXPECT_TRUE(enabled);
}

TEST_F(CrosConfigUtilsImplTest, GetRmadEnabled_Fail) {
  auto cros_config_utils = CreateCrosConfigUtils(false);

  bool enabled;
  EXPECT_FALSE(cros_config_utils->GetRmadEnabled(&enabled));
}

TEST_F(CrosConfigUtilsImplTest, GetModelName_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string model_name;
  EXPECT_TRUE(cros_config_utils->GetModelName(&model_name));
  EXPECT_EQ(model_name, kModelName);
}

TEST_F(CrosConfigUtilsImplTest, GetCustomLabelTag_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string custom_label_tag;
  EXPECT_TRUE(cros_config_utils->GetCurrentCustomLabelTag(&custom_label_tag));
  EXPECT_EQ(custom_label_tag, kCustomLabelTag);
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

TEST_F(CrosConfigUtilsImplTest, GetCustomLabelTagList_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::vector<std::string> custom_label_tag_list;
  EXPECT_TRUE(cros_config_utils->GetCustomLabelTagList(&custom_label_tag_list));
  EXPECT_EQ(custom_label_tag_list, kTargetCustomLabelTagList);
}

TEST_F(CrosConfigUtilsImplTest, GetEmptyCustomLabelTagList_Success) {
  auto cros_config_utils = CreateCrosConfigUtilsWithoutCustomLabel();

  std::vector<std::string> custom_label_tag_list;
  EXPECT_TRUE(cros_config_utils->GetCustomLabelTagList(&custom_label_tag_list));
  EXPECT_TRUE(custom_label_tag_list.empty());
}

TEST_F(CrosConfigUtilsImplTest, IsCustomLabel_True) {
  auto cros_config_utils = CreateCrosConfigUtils();

  EXPECT_TRUE(cros_config_utils->IsCustomLabel());
}

TEST_F(CrosConfigUtilsImplTest, IsCustomLabel_False) {
  auto cros_config_utils = CreateCrosConfigUtilsWithoutCustomLabel();

  EXPECT_FALSE(cros_config_utils->IsCustomLabel());
}

}  // namespace rmad
