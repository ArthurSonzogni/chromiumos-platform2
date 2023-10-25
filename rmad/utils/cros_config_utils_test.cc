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

#include "rmad/utils/cros_config_constants.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace {

constexpr char kModelName[] = "TestModelName";
constexpr char kModelNameUnused[] = "TestModelNameUnused";

constexpr char kBrandCode[] = "ZZCR";

constexpr uint32_t kSkuId = 1234567890;
constexpr uint32_t kSkuIdUnused = 1111111110;
constexpr uint32_t kSkuIdOther1 = 1111111111;
constexpr uint32_t kSkuIdOther2 = 1111111112;

constexpr char kCustomLabelTagEmpty[] = "";
constexpr char kCustomLabelTag[] = "TestCustomLabelTag";
constexpr char kCustomLabelTagUnused[] = "TestCustomLabelTagUnused";
constexpr char kCustomLabelTagOther[] = "TestCustomLabelTagOther";

constexpr char kTrueStr[] = "true";

constexpr uint32_t kFirmwareConfig = 55688;

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
const std::vector<uint32_t> kTargetSkuIdList = {kSkuIdOther1, kSkuIdOther2,
                                                kSkuId};

struct UtilArgs {
  bool custom_label = true;
  bool enable_rmad = true;
  bool set_optional = true;
};

}  // namespace

namespace rmad {

class CrosConfigUtilsImplTest : public testing::Test {
 public:
  CrosConfigUtilsImplTest() {}

  base::FilePath CreateCrosConfigFs(
      const std::vector<DesignConfig>& design_configs) {
    base::FilePath root_path = temp_dir_.GetPath();

    for (size_t i = 0; i < design_configs.size(); ++i) {
      base::FilePath config_path =
          root_path.AppendASCII(base::NumberToString(i));
      EXPECT_TRUE(base::CreateDirectory(config_path));

      base::FilePath model_path = config_path.AppendASCII(kCrosModelNameKey);
      EXPECT_TRUE(base::WriteFile(model_path, design_configs[i].model_name));

      base::FilePath identity_path = config_path.Append(kCrosIdentityPath);
      EXPECT_TRUE(base::CreateDirectory(identity_path));

      if (design_configs[i].sku_id.has_value()) {
        base::FilePath sku_path =
            identity_path.AppendASCII(kCrosIdentitySkuKey);
        EXPECT_TRUE(base::WriteFile(
            sku_path, base::NumberToString(design_configs[i].sku_id.value())));
      }

      if (design_configs[i].custom_label_tag.has_value()) {
        base::FilePath custom_label_tag_path =
            identity_path.AppendASCII(kCrosIdentityCustomLabelTagKey);
        EXPECT_TRUE(base::WriteFile(
            custom_label_tag_path, design_configs[i].custom_label_tag.value()));
      }
    }

    return root_path;
  }

  std::unique_ptr<CrosConfigUtils> CreateCrosConfigUtils(
      const UtilArgs& args = {}) {
    // Define all path constants here.
    const base::FilePath root_path = base::FilePath(kCrosRootPath);
    const base::FilePath identity_path = root_path.Append(kCrosIdentityPath);
    const base::FilePath firmware_path = root_path.Append(kCrosFirmwarePath);
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
    fake_cros_config->SetString(firmware_path.value(),
                                kCrosFirmwareFirmwareConfigKey,
                                base::NumberToString(kFirmwareConfig));

    base::FilePath cros_config_root_path;
    if (args.custom_label) {
      cros_config_root_path =
          CreateCrosConfigFs({{.model_name = kModelName,
                               .sku_id = kSkuId,
                               .custom_label_tag = kCustomLabelTagEmpty},
                              {.model_name = kModelName,
                               .sku_id = kSkuIdOther1,
                               .custom_label_tag = kCustomLabelTag},
                              {.model_name = kModelName,
                               .sku_id = kSkuIdOther2,
                               .custom_label_tag = kCustomLabelTag},
                              {.model_name = kModelName,
                               .sku_id = kSkuIdOther1,
                               .custom_label_tag = kCustomLabelTagOther},
                              {.model_name = kModelNameUnused,
                               .sku_id = kSkuIdUnused,
                               .custom_label_tag = kCustomLabelTagUnused},
                              {.model_name = kModelName,
                               .sku_id = std::nullopt,
                               .custom_label_tag = kCustomLabelTagOther}});
      fake_cros_config->SetString(identity_path.value(),
                                  kCrosIdentityCustomLabelTagKey,
                                  kCustomLabelTag);
    } else {
      cros_config_root_path =
          CreateCrosConfigFs({{.model_name = kModelName,
                               .sku_id = kSkuId,
                               .custom_label_tag = kCustomLabelTagEmpty},
                              {.model_name = kModelNameUnused,
                               .sku_id = kSkuIdOther1,
                               .custom_label_tag = kCustomLabelTag},
                              {.model_name = kModelNameUnused,
                               .sku_id = kSkuIdOther2,
                               .custom_label_tag = kCustomLabelTagOther},
                              {.model_name = kModelNameUnused,
                               .sku_id = kSkuIdUnused,
                               .custom_label_tag = kCustomLabelTagUnused}});
    }

    if (args.enable_rmad) {
      fake_cros_config->SetString(rmad_path.value(), kCrosRmadEnabledKey,
                                  kTrueStr);
      fake_cros_config->SetString(rmad_path.value(), kCrosRmadHasCbiKey,
                                  kTrueStr);
      fake_cros_config->SetString(rmad_path.value(),
                                  kCrosRmadUseLegacyCustomLabelKey, kTrueStr);
      if (args.set_optional) {
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
  auto cros_config_utils = CreateCrosConfigUtils();

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
  auto cros_config_utils = CreateCrosConfigUtils({.set_optional = false});

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
  auto cros_config_utils = CreateCrosConfigUtils({.enable_rmad = false});

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

  uint32_t sku_id;
  EXPECT_TRUE(cros_config_utils->GetSkuId(&sku_id));
  EXPECT_EQ(sku_id, kSkuId);
}

TEST_F(CrosConfigUtilsImplTest, GetSkuIdList_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  std::vector<uint32_t> sku_id_list;
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
  auto cros_config_utils = CreateCrosConfigUtils({.custom_label = false});

  std::vector<std::string> custom_label_tag_list;
  EXPECT_TRUE(cros_config_utils->GetCustomLabelTagList(&custom_label_tag_list));
  EXPECT_EQ(custom_label_tag_list, kEmptyCustomLabelTagList);
}

TEST_F(CrosConfigUtilsImplTest, HasCustomLabel_True) {
  auto cros_config_utils = CreateCrosConfigUtils();

  EXPECT_TRUE(cros_config_utils->HasCustomLabel());
}

TEST_F(CrosConfigUtilsImplTest, HasCustomLabel_False) {
  auto cros_config_utils = CreateCrosConfigUtils({.custom_label = false});

  EXPECT_FALSE(cros_config_utils->HasCustomLabel());
}

TEST_F(CrosConfigUtilsImplTest, GetFirmwareConfig_Success) {
  auto cros_config_utils = CreateCrosConfigUtils();

  uint32_t firmware_config;
  EXPECT_TRUE(cros_config_utils->GetFirmwareConfig(&firmware_config));
  EXPECT_EQ(firmware_config, kFirmwareConfig);
}

}  // namespace rmad
