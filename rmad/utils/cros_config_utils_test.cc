// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cros_config_utils_impl.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

// cros_config root path.
constexpr char kCrosRootPath[] = "/";
constexpr char kCrosModelNameKey[] = "name";
constexpr char kCrosBrandCodeKey[] = "brand-code";

constexpr char kModelName[] = "TestModelName";
constexpr char kModelNameUnused[] = "TestModelNameUnused";

constexpr char kBrandCode[] = "ZZCR";

// cros_config identity path.
constexpr char kCrosIdentityPath[] = "identity";
constexpr char kCrosIdentitySkuKey[] = "sku-id";
constexpr char kCrosIdentityCustomLabelTagKey[] = "custom-label-tag";

constexpr uint64_t kSkuId = 1234567890;
constexpr uint64_t kSkuIdUnused = 1111111110;
constexpr uint64_t kSkuIdOther1 = 1111111111;
constexpr uint64_t kSkuIdOther2 = 1111111112;

constexpr char kCustomLabelTagEmpty[] = "";
constexpr char kCustomLabelTag[] = "TestCustomLabelTag";
constexpr char kCustomLabelTagUnused[] = "TestCustomLabelTagUnused";
constexpr char kCustomLabelTagOther[] = "TestCustomLabelTagOther";

// cros_config rmad path.
constexpr char kCrosRmadPath[] = "rmad";
constexpr char kCrosRmadEnabledKey[] = "enabled";
constexpr char kCrosRmadHasCbiKey[] = "has-cbi";
constexpr char kCrosRmadUseLegacyCustomLabelKey[] = "use-legacy-custom-label";

constexpr char kTrueStr[] = "true";

// cros_config rmad/ssfc path.
constexpr char kCrosSsfcPath[] = "ssfc";
constexpr char kCrosSsfcMaskKey[] = "mask";
constexpr char kCrosComponentTypeConfigsPath[] = "component-type-configs";
constexpr char kCrosComponentTypeConfigsComponentTypeKey[] = "component-type";
constexpr char kCrosComponentTypeConfigsDefaultValueKey[] = "default-value";
constexpr char kCrosProbeableComponentsPath[] = "probeable-components";
constexpr char kCrosProbeableComponentsIdentifierKey[] = "identifier";
constexpr char kCrosProbeableComponentsValueKey[] = "value";

constexpr char kUndefinedComponentType[] = "undefined_component_type";
constexpr uint32_t kSsfcMask = 0x8;
constexpr char kSsfcComponentType[] = "TestComponentType";
constexpr uint32_t kSsfcDefaultValue = 0x4;
constexpr char kSsfcIdentifier1[] = "TestComponent_1";
constexpr uint32_t kSsfcValue1 = 0x1;
constexpr char kSsfcIdentifier2[] = "TestComponent_2";
constexpr uint32_t kSsfcValue2 = 0x2;

const std::vector<std::string> kTargetCustomLabelTagList = {
    kCustomLabelTagEmpty, kCustomLabelTag, kCustomLabelTagOther};
const std::vector<std::string> kEmptyCustomLabelTagList = {
    kCustomLabelTagEmpty};
const std::vector<uint64_t> kTargetSkuIdList = {kSkuIdOther1, kSkuIdOther2,
                                                kSkuId};

class CrosConfigUtilsImplTest : public testing::Test {
 public:
  CrosConfigUtilsImplTest() {}

  base::FilePath CreateCrosConfigFs(
      const std::vector<std::string>& model_names,
      const std::vector<std::optional<uint64_t>>& sku_ids,
      const std::vector<std::string>& custom_label_tags) {
    EXPECT_EQ(model_names.size(), sku_ids.size());
    EXPECT_EQ(model_names.size(), custom_label_tags.size());

    base::FilePath root_path = temp_dir_.GetPath();

    for (size_t i = 0; i < model_names.size(); ++i) {
      base::FilePath config_path =
          root_path.AppendASCII(base::NumberToString(i));
      EXPECT_TRUE(base::CreateDirectory(config_path));

      base::FilePath model_path = config_path.AppendASCII(kCrosModelNameKey);
      EXPECT_TRUE(base::WriteFile(model_path, model_names[i]));

      base::FilePath identity_path = config_path.Append(kCrosIdentityPath);
      EXPECT_TRUE(base::CreateDirectory(identity_path));

      if (sku_ids[i].has_value()) {
        base::FilePath sku_path =
            identity_path.AppendASCII(kCrosIdentitySkuKey);
        EXPECT_TRUE(base::WriteFile(sku_path,
                                    base::NumberToString(sku_ids[i].value())));
      }

      if (!custom_label_tags[i].empty()) {
        base::FilePath custom_label_tag_path =
            identity_path.AppendASCII(kCrosIdentityCustomLabelTagKey);
        EXPECT_TRUE(
            base::WriteFile(custom_label_tag_path, custom_label_tags[i]));
      }
    }

    return root_path;
  }

  std::unique_ptr<CrosConfigUtils> CreateCrosConfigUtils(
      bool custom_label = true,
      bool enable_rmad = true,
      bool set_optional = true) {
    // Define all path constants here.
    const base::FilePath root_path = base::FilePath(kCrosRootPath);
    const base::FilePath identity_path = root_path.Append(kCrosIdentityPath);
    const base::FilePath rmad_path = root_path.Append(kCrosRmadPath);
    const base::FilePath ssfc_path = rmad_path.Append(kCrosSsfcPath);
    const base::FilePath component_type_configs_path =
        ssfc_path.Append(kCrosComponentTypeConfigsPath);
    const base::FilePath component_type_config_0_path =
        component_type_configs_path.Append("0");
    const base::FilePath probeable_components_path =
        component_type_config_0_path.Append(kCrosProbeableComponentsPath);
    const base::FilePath probeable_component_0_path =
        probeable_components_path.Append("0");
    const base::FilePath probeable_component_1_path =
        probeable_components_path.Append("1");

    auto fake_cros_config = std::make_unique<brillo::FakeCrosConfig>();
    fake_cros_config->SetString(root_path.value(), kCrosModelNameKey,
                                kModelName);
    fake_cros_config->SetString(root_path.value(), kCrosBrandCodeKey,
                                kBrandCode);
    fake_cros_config->SetString(identity_path.value(), kCrosIdentitySkuKey,
                                base::NumberToString(kSkuId));

    base::FilePath cros_config_root_path;
    if (custom_label) {
      cros_config_root_path = CreateCrosConfigFs(
          {kModelName, kModelName, kModelName, kModelName, kModelNameUnused,
           kModelName},
          {kSkuId, kSkuIdOther1, kSkuIdOther2, kSkuIdOther1, kSkuIdUnused,
           std::nullopt},
          {kCustomLabelTagEmpty, kCustomLabelTag, kCustomLabelTag,
           kCustomLabelTagOther, kCustomLabelTagUnused, kCustomLabelTagOther});
      fake_cros_config->SetString(identity_path.value(),
                                  kCrosIdentityCustomLabelTagKey,
                                  kCustomLabelTag);
    } else {
      cros_config_root_path = CreateCrosConfigFs(
          {kModelName, kModelNameUnused, kModelNameUnused, kModelNameUnused},
          {kSkuId, kSkuIdOther1, kSkuIdOther2, kSkuIdUnused},
          {kCustomLabelTagEmpty, kCustomLabelTag, kCustomLabelTagOther,
           kCustomLabelTagUnused});
    }

    if (enable_rmad) {
      fake_cros_config->SetString(rmad_path.value(), kCrosRmadEnabledKey,
                                  kTrueStr);
      fake_cros_config->SetString(rmad_path.value(), kCrosRmadHasCbiKey,
                                  kTrueStr);
      fake_cros_config->SetString(rmad_path.value(),
                                  kCrosRmadUseLegacyCustomLabelKey, kTrueStr);
      if (set_optional) {
        fake_cros_config->SetString(ssfc_path.value(), kCrosSsfcMaskKey,
                                    base::NumberToString(kSsfcMask));
        fake_cros_config->SetString(component_type_config_0_path.value(),
                                    kCrosComponentTypeConfigsComponentTypeKey,
                                    kSsfcComponentType);
        fake_cros_config->SetString(component_type_config_0_path.value(),
                                    kCrosComponentTypeConfigsDefaultValueKey,
                                    base::NumberToString(kSsfcDefaultValue));
      }

      fake_cros_config->SetString(probeable_component_0_path.value(),
                                  kCrosProbeableComponentsIdentifierKey,
                                  kSsfcIdentifier1);
      fake_cros_config->SetString(probeable_component_0_path.value(),
                                  kCrosProbeableComponentsValueKey,
                                  base::NumberToString(kSsfcValue1));
      fake_cros_config->SetString(probeable_component_1_path.value(),
                                  kCrosProbeableComponentsIdentifierKey,
                                  kSsfcIdentifier2);
      fake_cros_config->SetString(probeable_component_1_path.value(),
                                  kCrosProbeableComponentsValueKey,
                                  base::NumberToString(kSsfcValue2));
    }

    return std::make_unique<CrosConfigUtilsImpl>(
        cros_config_root_path.MaybeAsASCII(), std::move(fake_cros_config));
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(CrosConfigUtilsImplTest, GetRmadConfig_Enabled) {
  auto cros_config_utils = CreateCrosConfigUtils(true, true);

  RmadConfig config;
  EXPECT_TRUE(cros_config_utils->GetRmadConfig(&config));
  EXPECT_TRUE(config.enabled);
  EXPECT_TRUE(config.has_cbi);
  EXPECT_EQ(config.ssfc.mask, kSsfcMask);
  EXPECT_TRUE(config.use_legacy_custom_label);

  const auto& component_type_configs = config.ssfc.component_type_configs;
  EXPECT_EQ(component_type_configs.size(), 1);
  EXPECT_EQ(component_type_configs[0].component_type, kSsfcComponentType);
  EXPECT_EQ(component_type_configs[0].default_value, kSsfcDefaultValue);

  const auto& probeable_components =
      component_type_configs[0].probeable_components;
  EXPECT_EQ(probeable_components.size(), 2);
  EXPECT_EQ(probeable_components.at(kSsfcIdentifier1), kSsfcValue1);
  EXPECT_EQ(probeable_components.at(kSsfcIdentifier2), kSsfcValue2);
}

TEST_F(CrosConfigUtilsImplTest, GetRmadConfig_Enabled_NoOptionalValues) {
  auto cros_config_utils = CreateCrosConfigUtils(true, true, false);

  RmadConfig config;
  EXPECT_TRUE(cros_config_utils->GetRmadConfig(&config));
  EXPECT_TRUE(config.enabled);
  EXPECT_TRUE(config.has_cbi);
  EXPECT_EQ(config.ssfc.mask, 0);
  EXPECT_TRUE(config.use_legacy_custom_label);

  const auto& component_type_configs = config.ssfc.component_type_configs;
  EXPECT_EQ(component_type_configs.size(), 1);
  EXPECT_EQ(component_type_configs[0].component_type, kUndefinedComponentType);
  EXPECT_EQ(component_type_configs[0].default_value, 0);

  const auto& probeable_components =
      component_type_configs[0].probeable_components;
  EXPECT_EQ(probeable_components.size(), 2);
  EXPECT_EQ(probeable_components.at(kSsfcIdentifier1), kSsfcValue1);
  EXPECT_EQ(probeable_components.at(kSsfcIdentifier2), kSsfcValue2);
}

TEST_F(CrosConfigUtilsImplTest, GetRmadConfig_Disabled) {
  auto cros_config_utils = CreateCrosConfigUtils(true, false);

  RmadConfig config;
  EXPECT_TRUE(cros_config_utils->GetRmadConfig(&config));
  EXPECT_FALSE(config.enabled);
  EXPECT_FALSE(config.has_cbi);
  EXPECT_EQ(config.ssfc.mask, 0);
  EXPECT_EQ(config.ssfc.component_type_configs.size(), 0);
  EXPECT_FALSE(config.use_legacy_custom_label);
}

TEST_F(CrosConfigUtilsImplTest, GetModelName_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string model_name;
  EXPECT_TRUE(cros_config_utils->GetModelName(&model_name));
  EXPECT_EQ(model_name, kModelName);
}

TEST_F(CrosConfigUtilsImplTest, GetBrandCode_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string brand_code;
  EXPECT_TRUE(cros_config_utils->GetBrandCode(&brand_code));
  EXPECT_EQ(brand_code, kBrandCode);
}

TEST_F(CrosConfigUtilsImplTest, GetCustomLabelTag_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::string custom_label_tag;
  EXPECT_TRUE(cros_config_utils->GetCustomLabelTag(&custom_label_tag));
  EXPECT_EQ(custom_label_tag, kCustomLabelTag);
}

TEST_F(CrosConfigUtilsImplTest, GetSkuId_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  uint64_t sku_id;
  EXPECT_TRUE(cros_config_utils->GetSkuId(&sku_id));
  EXPECT_EQ(sku_id, kSkuId);
}

TEST_F(CrosConfigUtilsImplTest, GetSkuIdList_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::vector<uint64_t> sku_id_list;
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
  auto cros_config_utils = CreateCrosConfigUtils(false);

  std::vector<std::string> custom_label_tag_list;
  EXPECT_TRUE(cros_config_utils->GetCustomLabelTagList(&custom_label_tag_list));
  EXPECT_EQ(custom_label_tag_list, kEmptyCustomLabelTagList);
}

TEST_F(CrosConfigUtilsImplTest, HasCustomLabel_True) {
  auto cros_config_utils = CreateCrosConfigUtils();

  EXPECT_TRUE(cros_config_utils->HasCustomLabel());
}

TEST_F(CrosConfigUtilsImplTest, HasCustomLabel_False) {
  auto cros_config_utils = CreateCrosConfigUtils(false);

  EXPECT_FALSE(cros_config_utils->HasCustomLabel());
}

}  // namespace rmad
