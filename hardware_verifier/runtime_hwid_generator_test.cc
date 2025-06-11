// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_generator.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/test_utils.h"

namespace hardware_verifier {
namespace {

using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;

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

class RuntimeHWIDGeneratorTest : public BaseFileTest {
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

  template <typename T>
  void AddProbeComponent(runtime_probe::ProbeResult* probe_result,
                         const std::string_view& name,
                         const std::string_view& category_name = "",
                         const std::string_view& comp_group = "") {
    T* component = nullptr;
    if constexpr (std::is_same_v<T, runtime_probe::Battery>) {
      component = probe_result->add_battery();
    } else if constexpr (std::is_same_v<T, runtime_probe::Storage>) {
      component = probe_result->add_storage();
    } else if constexpr (std::is_same_v<T, runtime_probe::Camera>) {
      component = probe_result->add_camera();
    } else if constexpr (std::is_same_v<T, runtime_probe::Edid>) {
      component = probe_result->add_display_panel();
    } else if constexpr (std::is_same_v<T, runtime_probe::Memory>) {
      component = probe_result->add_dram();
    } else if constexpr (std::is_same_v<T, runtime_probe::InputDevice>) {
      if (category_name == "stylus") {
        component = probe_result->add_stylus();
      } else if (category_name == "touchpad") {
        component = probe_result->add_touchpad();
      } else if (category_name == "touchscreen") {
        component = probe_result->add_touchscreen();
      } else {
        CHECK(false) << "Unexpected input device category: " << category_name;
      }
    } else {
      CHECK(false) << "Unexpected component type.";
    }

    component->set_name(name);
    if (!comp_group.empty()) {
      component->mutable_information()->set_comp_group(comp_group);
    }
  }

  std::unique_ptr<NiceMock<MockFactoryHWIDProcessor>>
      mock_factory_hwid_processor_;
};

TEST_F(RuntimeHWIDGeneratorTest, Create_Success) {
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  EXPECT_NE(generator, nullptr);
}

TEST_F(RuntimeHWIDGeneratorTest, Create_FactoryHWIDProcessorIsNull_Failure) {
  auto generator = RuntimeHWIDGenerator::Create(nullptr, {});

  EXPECT_EQ(generator, nullptr);
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_WithComponentDiff_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_2");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerate_WithUnidentifiedComponent_ShouldReturnTrue) {
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
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
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1_1");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::Battery>(&probe_result,
                                            "MODEL_battery_2_2#2");
  AddProbeComponent<runtime_probe::Battery>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::InputDevice>(
      &probe_result, "MODEL_touchscreen_3", "touchscreen");
  AddProbeComponent<runtime_probe::InputDevice>(&probe_result, "generic",
                                                "touchscreen");
  AddProbeComponent<runtime_probe::InputDevice>(&probe_result,
                                                "MODEL_stylus_4_4#5", "stylus");
  AddProbeComponent<runtime_probe::InputDevice>(&probe_result, "generic",
                                                "stylus");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "INVALID_FORMAT_2");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_MultipleMatchedComponents_ShouldReturnFalse) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_camera,
       {"camera_1_1", "camera_2_2"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_camera_1_1#2");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_camera_2_2#4");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_ShouldSkipDramComponents) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
      {runtime_probe::ProbeRequest_SupportCategory_dram, {"dram_2_2"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::Memory>(&probe_result, "MODEL_dram_3_3");
  AddProbeComponent<runtime_probe::Memory>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_ShouldApplyComponentGroup) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_storage, {"storage_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_2",
                                            "storage", "MODEL_storage_1");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
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
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::Battery>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
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
  auto generator = RuntimeHWIDGenerator::Create(
      std::move(mock_factory_hwid_processor_), encoding_spec);

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "MODEL_storage_1");
  AddProbeComponent<runtime_probe::Storage>(&probe_result, "generic");
  AddProbeComponent<runtime_probe::Battery>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerate_ExtraDisplayPanelInProbeResult_ShouldReturnFalse) {
  CategoryMapping<std::vector<std::string>> factory_hwid = {{}};
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Edid>(&probe_result,
                                         "MODEL_display_panel_1");
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerate_UnidentifiedDisplayPanelInProbeResult_ShouldReturnFalse) {
  CategoryMapping<std::vector<std::string>> factory_hwid = {{}};
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "generic");

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerate_ExtraDisplayPanelInDecodeResult_ShouldReturnTrue) {
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_display_panel,
       {"display_panel_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerate_MismatchedDisplayPanel_ShouldReturnTrue) {
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_display_panel,
       {"display_panel_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "display_panel_2");
  AddProbeComponent<runtime_probe::Edid>(&probe_result, "generic");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_MismatchedCamera_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_camera, {"camera_1_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_camera_2_2");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "generic");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_MismatchedVideo_ShouldReturnTrue) {
  SetModelName("MODEL");
  CategoryMapping<std::vector<std::string>> factory_hwid = {
      {runtime_probe::ProbeRequest_SupportCategory_camera, {"video_1_1"}},
  };
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(factory_hwid));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});

  runtime_probe::ProbeResult probe_result;
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "MODEL_video_2_2");
  AddProbeComponent<runtime_probe::Camera>(&probe_result, "generic");

  EXPECT_TRUE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

TEST_F(RuntimeHWIDGeneratorTest,
       ShouldGenerateRuntimeHWID_DecodeFactoryHWIDFailed_ShouldReturnFalse) {
  EXPECT_CALL(*mock_factory_hwid_processor_, DecodeFactoryHWID())
      .WillOnce(Return(std::nullopt));
  auto generator =
      RuntimeHWIDGenerator::Create(std::move(mock_factory_hwid_processor_), {});
  runtime_probe::ProbeResult probe_result;

  EXPECT_FALSE(generator->ShouldGenerateRuntimeHWID(probe_result));
}

}  // namespace
}  // namespace hardware_verifier
