// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_generator_impl.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/hash/sha1.h>
#include <base/strings/stringprintf.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libsegmentation/feature_management_fake.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/runtime_hwid_generator.h"
#include "hardware_verifier/test_utils.h"

namespace hardware_verifier {
namespace {

using ::testing::NiceMock;
using ::testing::Return;

constexpr char kGenericComponent[] = "generic";

class MockFactoryHWIDProcessor : public FactoryHWIDProcessor {
 public:
  MOCK_METHOD(std::optional<CategoryMapping<std::vector<std::string>>>,
              DecodeFactoryHWID,
              (),
              (const, override));
  MOCK_METHOD(std::set<runtime_probe::ProbeRequest_SupportCategory>,
              GetSkipZeroBitCategories,
              (),
              (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GenerateMaskedFactoryHWID,
              (),
              (const, override));
};

class RuntimeHWIDGeneratorImplTest : public BaseFileTest {
 protected:
  void SetUp() override {
    mock_factory_hwid_processor_ =
        std::make_unique<NiceMock<MockFactoryHWIDProcessor>>();
    // Default skip zero bit categories to empty.
    ON_CALL(*mock_factory_hwid_processor_, GetSkipZeroBitCategories())
        .WillByDefault(
            Return(std::set<runtime_probe::ProbeRequest_SupportCategory>{}));
  }

  void SetModelName(const std::string& model_name) {
    mock_context()->fake_cros_config()->SetString("/", "name", model_name);
  }

  void SetFeatureManagement(
      segmentation::FeatureManagementInterface::FeatureLevel feature_level,
      segmentation::FeatureManagementInterface::ScopeLevel scope_level) {
    auto fake_feature_management =
        std::make_unique<segmentation::fake::FeatureManagementFake>();
    fake_feature_management->SetFeatureLevel(feature_level);
    fake_feature_management->SetScopeLevel(scope_level);
    mock_context()->InitializeFeatureManagementForTest(
        std::move(fake_feature_management));
  }

  template <typename T>
  void AddProbeComponent(runtime_probe::ProbeResult* probe_result,
                         std::string_view name,
                         std::string_view category_name = "",
                         std::string_view comp_group = "",
                         std::string_view comp_pos = "") {
    T* component = nullptr;
    T* generic_component = nullptr;
    if constexpr (std::is_same_v<T, runtime_probe::Battery>) {
      component = probe_result->add_battery();
      generic_component = probe_result->add_battery();
    } else if constexpr (std::is_same_v<T, runtime_probe::Storage>) {
      component = probe_result->add_storage();
      generic_component = probe_result->add_storage();
    } else if constexpr (std::is_same_v<T, runtime_probe::Camera>) {
      component = probe_result->add_camera();
      generic_component = probe_result->add_camera();
    } else if constexpr (std::is_same_v<T, runtime_probe::Edid>) {
      component = probe_result->add_display_panel();
      generic_component = probe_result->add_display_panel();
    } else if constexpr (std::is_same_v<T, runtime_probe::Memory>) {
      component = probe_result->add_dram();
      generic_component = probe_result->add_dram();
    } else if constexpr (std::is_same_v<T, runtime_probe::InputDevice>) {
      if (category_name == "stylus") {
        component = probe_result->add_stylus();
        generic_component = probe_result->add_stylus();
      } else if (category_name == "touchpad") {
        component = probe_result->add_touchpad();
        generic_component = probe_result->add_touchpad();
      } else if (category_name == "touchscreen") {
        component = probe_result->add_touchscreen();
        generic_component = probe_result->add_touchscreen();
      } else {
        CHECK(false) << "Unexpected input device category: " << category_name;
      }
    } else if constexpr (std::is_same_v<T, runtime_probe::Network>) {
      if (category_name == "cellular") {
        component = probe_result->add_cellular();
        generic_component = probe_result->add_cellular();
      } else if (category_name == "ethernet") {
        component = probe_result->add_ethernet();
        generic_component = probe_result->add_ethernet();
      } else if (category_name == "wireless") {
        component = probe_result->add_wireless();
        generic_component = probe_result->add_wireless();
      } else {
        CHECK(false) << "Unexpected network category: " << category_name;
      }
    } else {
      CHECK(false) << "Unexpected component type.";
    }

    component->set_name(name);
    generic_component->set_name(kGenericComponent);
    if (!comp_group.empty()) {
      component->mutable_information()->set_comp_group(comp_group);
    }
    if (!comp_pos.empty()) {
      component->set_position(comp_pos);
    }
  }

  std::unique_ptr<NiceMock<MockFactoryHWIDProcessor>>
      mock_factory_hwid_processor_;
};

TEST_F(RuntimeHWIDGeneratorImplTest, Create_Success) {
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  EXPECT_NE(generator, nullptr);
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       Create_FactoryHWIDProcessorIsNull_Failure) {
  auto generator = RuntimeHWIDGeneratorImpl::Create(nullptr, {});

  EXPECT_EQ(generator, nullptr);
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_WithComponentDiff_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_2");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerate_WithUnidentifiedComponent_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  auto* unidentified_comp = probe_result.add_storage();
  unidentified_comp->set_name(kGenericComponent);

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerate_GetModelNameFailed_ShouldReturnFalse) {
  CategoryMapping<std::vector<std::string>> factory_hwid = {};
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID({}));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_NoDiffAfterNormalization_ShouldReturnFalse) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
      {runtime_probe::ProbeRequest_SupportCategory_battery, {"battery_2_2"}},
      {runtime_probe::ProbeRequest_SupportCategory_touchscreen,
       {"touchscreen_3_3#3"}},
      {runtime_probe::ProbeRequest_SupportCategory_stylus, {"stylus_4#4"}},
      {runtime_probe::ProbeRequest_SupportCategory_touchpad,
       {"INVALID_FORMAT_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1_1");
  AddProbeComponent<runtime_probe::Battery>(&probe_result,
                                            "MODEL_battery_2_2#2");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_touchscreen_3", "touchscreen");
  AddProbeComponent<runtime_probe::InputDevice>(&probe_result,
                                                "MODEL_stylus_4_4#5", "stylus");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "INVALID_FORMAT_2");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_MultipleMatchedComponents_ShouldReturnFalse) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_camera,
       {"camera_1_1", "camera_2_2"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_camera_1_1#2");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_camera_2_2#4");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_ShouldSkipDramComponents) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
      {runtime_probe::ProbeRequest_SupportCategory_dram, {"dram_2_2"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  AddProbeComponent<runtime_probe::Memory>(&probe_result, "MODEL_dram_3_3");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_ShouldApplyComponentGroup) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_2",
                                            "storage", "MODEL_storage_1");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_ShouldApplySkipZeroBitCategories) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  std::set<runtime_probe::ProbeRequest_SupportCategory>
      skip_zero_bit_categories = {
          runtime_probe::ProbeRequest_SupportCategory_battery};
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  EXPECT_CALL(*mock_factory_hwid_processor_, GetSkipZeroBitCategories())
      .WillOnce(Return(skip_zero_bit_categories));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  auto* unidentified_comp = probe_result.add_battery();
  unidentified_comp->set_name(kGenericComponent);

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_ShouldApplyWaivedCategories) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
      {runtime_probe::ProbeRequest_SupportCategory_battery, {"battery_2"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  EncodingSpec encoding_spec;
  encoding_spec.add_waived_categories(
      runtime_probe::ProbeRequest_SupportCategory_battery);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), encoding_spec);

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  auto* unidentified_comp = probe_result.add_battery();
  unidentified_comp->set_name(kGenericComponent);

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerate_ExtraDisplayPanelInProbeResult_ShouldReturnFalse) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {{}};
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Edid>(&probe_result,
                                         "MODEL_display_panel_1");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerate_UnidentifiedDisplayPanelInProbeResult_ShouldReturnFalse) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {{}};
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  auto* unidentified_comp = probe_result.add_display_panel();
  unidentified_comp->set_name(kGenericComponent);

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerate_ExtraDisplayPanelInDecodeResult_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_display_panel,
       {"display_panel_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerate_MismatchedDisplayPanel_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_display_panel,
       {"display_panel_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "display_panel_2");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_MismatchedCamera_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_camera, {"camera_1_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_camera_2_2");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_MismatchedVideo_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_camera, {"video_1_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_video_2_2");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       ShouldGenerateRuntimeHWID_DecodeFactoryHWIDFailed_ShouldReturnFalse) {
  SetModelName("MODEL");
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(std::nullopt));
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});
  runtime_probe::ProbeResult probe_result;

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorImplTest, Generate) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1_1",
                                            "", "", "1");
  AddProbeComponent<runtime_probe::Battery>(&probe_result,
                                            "MODEL_battery_2_2#2", "", "", "2");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_touchscreen_3", "touchscreen", "", "3");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_stylus_4_4#5", "stylus", "", "4");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_touchpad_6_6", "touchpad", "", "5");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_5_5", "", "",
                                           "6");
  AddProbeComponent<runtime_probe::Memory>(&probe_result, "dram_7_7", "", "",
                                           "7");
  AddProbeComponent<runtime_probe::Network>(&probe_result, "cellular_8_8",
                                            "cellular", "", "8");
  AddProbeComponent<runtime_probe::Network>(&probe_result, "wireless_9_9",
                                            "wireless", "", "9");
  AddProbeComponent<runtime_probe::Network>(&probe_result, "ethernet_10_10",
                                            "ethernet", "", "10");
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "display_panel_11_11",
                                         "", "", "11");

  auto res = generator->Generate(probe_result);

  EXPECT_EQ(res, "MODEL-RLZ A2A R:1-1-2-6-11-4-5-3-7-8-10-9-1");
}

TEST_F(RuntimeHWIDGeneratorImplTest, Generate_WithUnidentifiedComponent) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Battery>(&probe_result,
                                            "MODEL_battery_2_2#2", "", "", "2");
  auto* unidentified_comp1 = probe_result.add_battery();
  unidentified_comp1->set_name(kGenericComponent);
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_5_5", "", "",
                                           "5");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_6_6", "", "",
                                           "6");
  auto* unidentified_comp2 = probe_result.add_camera();
  auto* unidentified_comp3 = probe_result.add_camera();
  unidentified_comp2->set_name(kGenericComponent);
  unidentified_comp3->set_name(kGenericComponent);

  auto res = generator->Generate(probe_result);

  EXPECT_EQ(res, "MODEL-RLZ A2A R:1-1-2,?-5,6,?,?-X-X-X-X-X-X-X-X-X");
}

TEST_F(RuntimeHWIDGeneratorImplTest, Generate_WithSkipComponent) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  EncodingSpec encoding_spec;
  encoding_spec.add_waived_categories(
      runtime_probe::ProbeRequest_SupportCategory_battery);
  encoding_spec.add_waived_categories(
      runtime_probe::ProbeRequest_SupportCategory_dram);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), encoding_spec);

  runtime_probe::ProbeResult probe_result;

  auto res = generator->Generate(probe_result);

  EXPECT_EQ(res, "MODEL-RLZ A2A R:1-1-#-X-X-X-X-X-#-X-X-X-X");
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       Generate_ComponentPositionsShouldBeSorted) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_5_5", "", "",
                                           "100");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_6_6", "", "",
                                           "1");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_6_6", "", "",
                                           "9");
  auto* unidentified_comp1 = probe_result.add_camera();
  auto* unidentified_comp2 = probe_result.add_camera();
  unidentified_comp1->set_name(kGenericComponent);
  unidentified_comp2->set_name(kGenericComponent);

  auto res = generator->Generate(probe_result);

  EXPECT_EQ(res, "MODEL-RLZ A2A R:1-1-X-1,9,100,?,?-X-X-X-X-X-X-X-X-X");
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       Generate_GenerateMaskedFactoryHWIDFailed_Failure) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return(std::nullopt));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;

  auto res = generator->Generate(probe_result);

  EXPECT_EQ(res, std::nullopt);
}

TEST_F(RuntimeHWIDGeneratorImplTest,
       Generate_InvalidComponentPosition_Failure) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_5_5", "", "",
                                           "invalid-position");

  auto res = generator->Generate(probe_result);

  EXPECT_EQ(res, std::nullopt);
}

TEST_F(RuntimeHWIDGeneratorImplTest, GenerateToDevice_Success) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1_1",
                                            "", "", "1");
  AddProbeComponent<runtime_probe::Battery>(&probe_result,
                                            "MODEL_battery_2_2#2", "", "", "2");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_touchscreen_3", "touchscreen", "", "3");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_stylus_4_4#5", "stylus", "", "4");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_touchpad_6_6", "touchpad", "", "5");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "camera_5_5", "", "",
                                           "6");
  AddProbeComponent<runtime_probe::Memory>(&probe_result, "dram_7_7", "", "",
                                           "7");
  AddProbeComponent<runtime_probe::Network>(&probe_result, "cellular_8_8",
                                            "cellular", "", "8");
  AddProbeComponent<runtime_probe::Network>(&probe_result, "wireless_9_9",
                                            "wireless", "", "9");
  AddProbeComponent<runtime_probe::Network>(&probe_result, "ethernet_10_10",
                                            "ethernet", "", "10");
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "display_panel_11_11",
                                         "", "", "11");

  EXPECT_TRUE(generator->GenerateToDevice(probe_result));

  std::string file_content;
  std::string expected_file_content =
      R"(MODEL-RLZ A2A R:1-1-2-6-11-4-5-3-7-8-10-9-1
27BBB9DDFA4210711C5ED57400D0311FF89D1C90)";
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  EXPECT_TRUE(base::ReadFileToString(runtime_hwid_path, &file_content));
  EXPECT_EQ(file_content, expected_file_content);

  int file_mode;
  ASSERT_TRUE(base::GetPosixFilePermissions(runtime_hwid_path, &file_mode));
  EXPECT_EQ(file_mode, 0644);
}

TEST_F(RuntimeHWIDGeneratorImplTest, GenerateToDevice_GenerateFailed_Failure) {
  // The generation will be failed due to GenerateMaskedFactoryHWID failure.
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return(std::nullopt));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  EXPECT_FALSE(generator->GenerateToDevice({}));

  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  EXPECT_FALSE(base::PathExists(runtime_hwid_path));
}

TEST_F(RuntimeHWIDGeneratorImplTest, GenerateToDevice_WriteFileFailed_Failure) {
  EXPECT_CALL(*mock_factory_hwid_processor_, GenerateMaskedFactoryHWID())
      .WillOnce(Return("MODEL-RLZ A2A"));
  SetFeatureManagement(
      segmentation::FeatureManagementInterface::FEATURE_LEVEL_1,
      segmentation::FeatureManagementInterface::SCOPE_LEVEL_1);
  auto generator = RuntimeHWIDGeneratorImpl::Create(
      std::move(mock_factory_hwid_processor_), {});

  // Make the path a directory to make the file unwritable.
  const auto runtime_hwid_path = GetPathUnderRoot(kRuntimeHWIDFilePath);
  ASSERT_TRUE(base::CreateDirectory(runtime_hwid_path));

  EXPECT_FALSE(generator->GenerateToDevice({}));
}

}  // namespace
}  // namespace hardware_verifier
