// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/segmentation/fake_segmentation_utils.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cbi_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_regions_utils.h"
#include "rmad/utils/mock_vpd_utils.h"
#include "rmad/utils/mock_write_protect_utils.h"
#include "rmad/utils/rmad_config_utils_impl.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::IsFalse;
using testing::IsTrue;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::StrictMock;

namespace {

constexpr char kSerialNumber[] = "TestSerialNumber";
constexpr char kRegion[] = "TestRegion";
constexpr char kModel[] = "Model";
constexpr uint32_t kSkuId1 = 1234567890;
constexpr uint32_t kSkuId2 = 1234567891;
constexpr char kCustomLabelTag[] = "TestCustomLabelTag";
constexpr char kDramPartNum[] = "TestDramPartNum";

const std::vector<std::string> kRegionList = {"TestRegion", "TestRegion1"};
const std::vector<rmad::DesignConfig> kDesignConfigList = {
    {.model_name = "Model",
     .sku_id = kSkuId1,
     .custom_label_tag = "",
     .hardware_properties = {"Property1: Yes", "Property2: Yes"}},
    {.model_name = "Model",
     .sku_id = kSkuId1,
     .custom_label_tag = kCustomLabelTag,
     .hardware_properties = {"Property1: Yes", "Property2: Yes"}},
    {.model_name = "Model",
     .sku_id = kSkuId2,
     .custom_label_tag = "",
     .hardware_properties = {"Property1: Yes", "Property2: No"}},
    {.model_name = "Model",
     .sku_id = kSkuId2,
     .custom_label_tag = kCustomLabelTag,
     .hardware_properties = {"Property1: Yes", "Property2: No"}}};
const std::vector<uint32_t> kSkuList = {kSkuId1, kSkuId2};
const std::vector<std::string> kCustomLabelTagList = {"", kCustomLabelTag};
const std::vector<std::string> kSkuDescriptionList = {"Property2: Yes",
                                                      "Property2: No"};

constexpr uint32_t kOriginalRegionSelection = 0;
constexpr uint32_t kOriginalSkuSelection = 0;
constexpr uint32_t kOriginalCustomLabelSelection = 0;

constexpr char kNewSerialNumber[] = "NewTestSerialNumber";
constexpr uint32_t kNewRegionSelection = 1;
constexpr uint32_t kNewSkuSelection = 1;
constexpr uint32_t kNewCustomLabelSelection = 1;
constexpr char kNewDramPartNum[] = "NewTestDramPartNum";
constexpr bool kNewIsChassisBranded = true;
constexpr int kNewHwComplianceVersion = 1;

}  // namespace

namespace rmad {

class UpdateDeviceInfoStateHandlerTest : public StateHandlerTest {
 public:
  struct StateHandlerArgs {
    std::vector<bool> wp_status_list = {false};
    bool has_serial_number = true;
    bool has_region = true;
    bool has_sku = true;
    std::optional<std::string> custom_label = "";
    bool has_dram_part_num = true;
    bool has_region_list = true;
    std::optional<std::vector<DesignConfig>> design_config_list =
        kDesignConfigList;
    bool is_feature_enabled = true;
    bool is_feature_mutable = false;
    int feature_level = 0;
    std::optional<int> looked_up_feature_level = std::nullopt;
    bool set_serial_number_success = true;
    bool set_region_success = true;
    bool set_sku_success = true;
    bool set_custom_label_success = true;
    bool set_dram_part_num_success = true;
    bool flush_out_vpd_success = true;
    bool has_cbi = true;
    bool use_legacy_custom_label = false;
    std::optional<std::string> sku_filter_textproto = std::nullopt;
    std::string rmad_config_text = "";
  };

  scoped_refptr<UpdateDeviceInfoStateHandler> CreateStateHandler(
      const StateHandlerArgs& args) {
    json_store_->SetValue(kMlbRepair, false);

    // Mock |WriteProtectUtils|.
    auto write_protect_utils =
        std::make_unique<StrictMock<MockWriteProtectUtils>>();
    {
      InSequence seq;
      for (bool enabled : args.wp_status_list) {
        EXPECT_CALL(*write_protect_utils, GetHardwareWriteProtectionStatus())
            .WillOnce(Return(enabled));
      }
    }

    // Mock |VpdUtils|.
    auto vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();
    ON_CALL(*vpd_utils, FlushOutRoVpdCache())
        .WillByDefault(Return(args.flush_out_vpd_success));

    if (args.has_serial_number) {
      ON_CALL(*vpd_utils, GetSerialNumber(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSerialNumber), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetSerialNumber(_)).WillByDefault(Return(false));
    }

    if (args.has_region) {
      ON_CALL(*vpd_utils, GetRegion(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kRegion), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetRegion(_)).WillByDefault(Return(false));
    }

    if (args.set_serial_number_success) {
      ON_CALL(*vpd_utils, SetSerialNumber(_))
          .WillByDefault(DoAll(SaveArg<0>(&serial_number_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetSerialNumber(_)).WillByDefault(Return(false));
    }

    if (args.set_region_success) {
      ON_CALL(*vpd_utils, SetRegion(_))
          .WillByDefault(DoAll(SaveArg<0>(&region_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetRegion(_)).WillByDefault(Return(false));
    }

    if (args.set_custom_label_success) {
      ON_CALL(*vpd_utils, SetCustomLabelTag(_, IsFalse()))
          .WillByDefault(DoAll(SaveArg<0>(&custom_label_set_), Return(true)));
      ON_CALL(*vpd_utils, SetCustomLabelTag(_, IsTrue()))
          .WillByDefault(
              DoAll(SaveArg<0>(&legacy_custom_label_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetCustomLabelTag(_, _)).WillByDefault(Return(false));
    }

    // Mock |CbiUtils|.
    auto cbi_utils = std::make_unique<NiceMock<MockCbiUtils>>();
    if (args.set_sku_success) {
      ON_CALL(*cbi_utils, SetSkuId(_))
          .WillByDefault(DoAll(SaveArg<0>(&sku_set_), Return(true)));
    } else {
      ON_CALL(*cbi_utils, SetSkuId(_)).WillByDefault(Return(false));
    }

    if (args.has_dram_part_num) {
      ON_CALL(*cbi_utils, GetDramPartNum(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kDramPartNum), Return(true)));
    } else {
      ON_CALL(*cbi_utils, GetDramPartNum(_)).WillByDefault(Return(false));
    }

    if (args.set_dram_part_num_success) {
      ON_CALL(*cbi_utils, SetDramPartNum(_))
          .WillByDefault(DoAll(SaveArg<0>(&dram_part_num_set_), Return(true)));
    } else {
      ON_CALL(*cbi_utils, SetDramPartNum(_)).WillByDefault(Return(false));
    }

    // Mock |RegionsUtils|.
    auto regions_utils = std::make_unique<NiceMock<MockRegionsUtils>>();
    if (args.has_region_list) {
      ON_CALL(*regions_utils, GetRegionList(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kRegionList), Return(true)));
    } else {
      ON_CALL(*regions_utils, GetRegionList(_)).WillByDefault(Return(false));
    }

    // Mock |CrosConfigUtils|.
    auto cros_config_utils = std::make_unique<NiceMock<MockCrosConfigUtils>>();
    ON_CALL(*cros_config_utils, GetRmadCrosConfig(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(RmadCrosConfig{
                      .has_cbi = args.has_cbi,
                      .use_legacy_custom_label = args.use_legacy_custom_label}),
                  Return(true)));
    ON_CALL(*cros_config_utils, GetModelName(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kModel), Return(true)));

    if (args.has_sku) {
      ON_CALL(*cros_config_utils, GetSkuId(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuId1), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetSkuId(_)).WillByDefault(Return(false));
    }

    if (args.custom_label.has_value()) {
      ON_CALL(*cros_config_utils, GetCustomLabelTag(_))
          .WillByDefault(
              DoAll(SetArgPointee<0>(args.custom_label.value()), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetCustomLabelTag(_))
          .WillByDefault(Return(false));
    }

    if (args.design_config_list.has_value()) {
      ON_CALL(*cros_config_utils, GetDesignConfigList(_))
          .WillByDefault(DoAll(
              SetArgPointee<0>(args.design_config_list.value()), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetDesignConfigList(_))
          .WillByDefault(Return(false));
    }

    if (args.sku_filter_textproto.has_value()) {
      base::FilePath sku_filter_textproto_dir = GetTempDirPath().Append(kModel);
      base::FilePath sku_filter_textproto_path =
          sku_filter_textproto_dir.Append("sku_filter.textproto");
      EXPECT_TRUE(base::CreateDirectory(sku_filter_textproto_dir));
      EXPECT_TRUE(base::WriteFile(sku_filter_textproto_path,
                                  args.sku_filter_textproto.value()));
    }

    // Fake |SegmentationUtils|.
    WriteFakeFeaturesInput(args.is_feature_enabled, args.is_feature_mutable,
                           args.feature_level, args.looked_up_feature_level);
    auto segmentation_utils =
        std::make_unique<FakeSegmentationUtils>(GetTempDirPath());

    // Inject textproto content for |RmadConfigUtils|.
    auto mock_cros_config_utils =
        std::make_unique<StrictMock<MockCrosConfigUtils>>();
    if (!args.rmad_config_text.empty()) {
      EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
          .WillOnce(DoAll(SetArgPointee<0>("model_name"), Return(true)));

      const base::FilePath textproto_file_path =
          GetTempDirPath()
              .Append("model_name")
              .Append(kDefaultRmadConfigProtoFilePath);

      EXPECT_TRUE(base::CreateDirectory(textproto_file_path.DirName()));
      EXPECT_TRUE(base::WriteFile(textproto_file_path, args.rmad_config_text));
    } else {
      EXPECT_CALL(*mock_cros_config_utils, GetModelName(_))
          .WillOnce(Return(false));
    }
    auto rmad_config_utils = std::make_unique<RmadConfigUtilsImpl>(
        GetTempDirPath(), std::move(mock_cros_config_utils));

    return base::MakeRefCounted<UpdateDeviceInfoStateHandler>(
        json_store_, daemon_callback_, GetTempDirPath(), GetTempDirPath(),
        std::move(cbi_utils), std::move(cros_config_utils),
        std::move(write_protect_utils), std::move(regions_utils),
        std::move(vpd_utils), std::move(segmentation_utils),
        std::move(rmad_config_utils));
  }

  struct StateArgs {
    // Input fields.
    std::string serial_number = kNewSerialNumber;
    int region_index = kNewRegionSelection;
    int sku_index = kNewSkuSelection;
    int whitelabel_index = kNewCustomLabelSelection;
    std::string dram_part_number = kNewDramPartNum;
    int custom_label_index = -1;
    bool is_chassis_branded = kNewIsChassisBranded;
    uint32_t hw_compliance_version = kNewHwComplianceVersion;
    // Read-only fields.
    std::optional<std::vector<std::string>> region_list = std::nullopt;
    std::optional<std::vector<uint32_t>> sku_list = std::nullopt;
    std::optional<std::vector<std::string>> whitelabel_list = std::nullopt;
    std::optional<std::vector<std::string>> custom_label_list = std::nullopt;
    std::optional<std::string> original_serial_number = std::nullopt;
    std::optional<int> original_region_index = std::nullopt;
    std::optional<int> original_sku_index = std::nullopt;
    std::optional<int> original_whitelabel_index = std::nullopt;
    std::optional<std::string> original_dram_part_number = std::nullopt;
    std::optional<int> original_custom_label_index = std::nullopt;
    std::optional<UpdateDeviceInfoState::FeatureLevel> original_feature_level =
        std::nullopt;
    std::optional<bool> mlb_repair = std::nullopt;
  };

  RmadState CreateStateReply(
      scoped_refptr<UpdateDeviceInfoStateHandler> handler,
      const StateArgs& args) {
    RmadState state = handler->GetState();
    // Input fields.
#define SET_FIELD(state, args, field) \
  state.mutable_update_device_info()->set_##field(args.field)
    SET_FIELD(state, args, serial_number);
    SET_FIELD(state, args, region_index);
    SET_FIELD(state, args, sku_index);
    SET_FIELD(state, args, whitelabel_index);
    SET_FIELD(state, args, dram_part_number);
    SET_FIELD(state, args, custom_label_index);
    SET_FIELD(state, args, is_chassis_branded);
    SET_FIELD(state, args, hw_compliance_version);
#undef SET_FIELD

    // Read-only fields.
#define SET_OPTIONAL_FIELD_LIST(state, args, field)       \
  if (args.field.has_value()) {                           \
    state.mutable_update_device_info()->clear_##field();  \
    for (const auto& v : args.field.value()) {            \
      state.mutable_update_device_info()->add_##field(v); \
    }                                                     \
  }
    SET_OPTIONAL_FIELD_LIST(state, args, region_list);
    SET_OPTIONAL_FIELD_LIST(state, args, sku_list);
    SET_OPTIONAL_FIELD_LIST(state, args, whitelabel_list);
    SET_OPTIONAL_FIELD_LIST(state, args, custom_label_list);
#undef SET_OPTIONAL_FIELD_LIST

#define SET_OPTIONAL_FIELD(state, args, field)                           \
  if (args.field.has_value()) {                                          \
    state.mutable_update_device_info()->set_##field(args.field.value()); \
  }
    SET_OPTIONAL_FIELD(state, args, original_serial_number);
    SET_OPTIONAL_FIELD(state, args, original_region_index);
    SET_OPTIONAL_FIELD(state, args, original_sku_index);
    SET_OPTIONAL_FIELD(state, args, original_whitelabel_index);
    SET_OPTIONAL_FIELD(state, args, original_dram_part_number);
    SET_OPTIONAL_FIELD(state, args, original_custom_label_index);
    SET_OPTIONAL_FIELD(state, args, original_feature_level);
    SET_OPTIONAL_FIELD(state, args, mlb_repair);
#undef SET_OPTIONAL_FIELD

    return state;
  }

  void WriteFakeFeaturesInput(bool is_feature_enabled,
                              bool is_feature_mutable,
                              int feature_level,
                              std::optional<int> looked_up_feature_level) {
    auto input = base::MakeRefCounted<JsonStore>(
        GetTempDirPath().AppendASCII(kFakeFeaturesInputFilePath), false);
    input->Clear();
    input->SetValue("is_feature_enabled", is_feature_enabled);
    input->SetValue("is_feature_mutable", is_feature_mutable);
    input->SetValue("feature_level", feature_level);
    if (looked_up_feature_level.has_value()) {
      input->SetValue("looked_up_feature_level",
                      looked_up_feature_level.value());
    }
  }

  bool ReadFakeFeaturesOutput(bool* is_chassis_branded,
                              int* hw_compliance_version) {
    auto output = base::MakeRefCounted<JsonStore>(
        GetTempDirPath().AppendASCII(kFakeFeaturesOutputFilePath), true);
    return output->GetValue("is_chassis_branded", is_chassis_branded) &&
           output->GetValue("hw_compliance_version", hw_compliance_version);
  }

 protected:
  std::string serial_number_set_;
  std::string region_set_;
  uint32_t sku_set_;
  std::string custom_label_set_;
  std::string legacy_custom_label_set_;
  std::string dram_part_num_set_;
};

// |InitializeState| under different conditions.
TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_Mlb_Success) {
  auto handler = CreateStateHandler({});
  json_store_->SetValue(kMlbRepair, true);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Verify read-only data.
  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().region_list().begin(),
                         state.update_device_info().region_list().end(),
                         kRegionList.begin(), kRegionList.end()));
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         kSkuList.begin(), kSkuList.end()));
  EXPECT_TRUE(std::equal(state.update_device_info().whitelabel_list().begin(),
                         state.update_device_info().whitelabel_list().end(),
                         kCustomLabelTagList.begin(),
                         kCustomLabelTagList.end()));
  EXPECT_TRUE(std::equal(state.update_device_info().custom_label_list().begin(),
                         state.update_device_info().custom_label_list().end(),
                         kCustomLabelTagList.begin(),
                         kCustomLabelTagList.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 kSkuDescriptionList.begin(), kSkuDescriptionList.end()));
  EXPECT_EQ(state.update_device_info().original_serial_number(), kSerialNumber);
  EXPECT_EQ(state.update_device_info().original_region_index(),
            kOriginalRegionSelection);
  EXPECT_EQ(state.update_device_info().original_sku_index(),
            kOriginalSkuSelection);
  EXPECT_EQ(state.update_device_info().original_whitelabel_index(),
            kOriginalCustomLabelSelection);
  EXPECT_EQ(state.update_device_info().original_dram_part_number(),
            kDramPartNum);
  EXPECT_EQ(state.update_device_info().original_custom_label_index(),
            kOriginalCustomLabelSelection);
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_0);
  EXPECT_TRUE(state.update_device_info().mlb_repair());
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoMlb_Success) {
  auto handler = CreateStateHandler({});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Verify read-only data.
  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().region_list().begin(),
                         state.update_device_info().region_list().end(),
                         kRegionList.begin(), kRegionList.end()));
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         kSkuList.begin(), kSkuList.end()));
  EXPECT_TRUE(std::equal(state.update_device_info().whitelabel_list().begin(),
                         state.update_device_info().whitelabel_list().end(),
                         kCustomLabelTagList.begin(),
                         kCustomLabelTagList.end()));
  EXPECT_TRUE(std::equal(state.update_device_info().custom_label_list().begin(),
                         state.update_device_info().custom_label_list().end(),
                         kCustomLabelTagList.begin(),
                         kCustomLabelTagList.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 kSkuDescriptionList.begin(), kSkuDescriptionList.end()));
  EXPECT_EQ(state.update_device_info().original_serial_number(), kSerialNumber);
  EXPECT_EQ(state.update_device_info().original_region_index(),
            kOriginalRegionSelection);
  EXPECT_EQ(state.update_device_info().original_sku_index(),
            kOriginalSkuSelection);
  EXPECT_EQ(state.update_device_info().original_whitelabel_index(),
            kOriginalCustomLabelSelection);
  EXPECT_EQ(state.update_device_info().original_dram_part_number(),
            kDramPartNum);
  EXPECT_EQ(state.update_device_info().original_custom_label_index(),
            kOriginalCustomLabelSelection);
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_0);
  EXPECT_FALSE(state.update_device_info().mlb_repair());
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_VarMissing_Failed) {
  auto handler = CreateStateHandler({});
  // No kMlbRepair set in |json_store_|.
  json_store_->RemoveKey(kMlbRepair);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_WpEnabled_Failed) {
  auto handler = CreateStateHandler({.wp_status_list = {true}});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_WP_ENABLED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoSerialNumberSuccess) {
  auto handler = CreateStateHandler({.has_serial_number = false});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoRegion_Success) {
  auto handler = CreateStateHandler({.has_region = false});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoSku_Success) {
  auto handler = CreateStateHandler({.has_sku = false});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoCustomLabel_Success) {
  auto handler = CreateStateHandler({.custom_label = std::nullopt});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoDramPartNum_Success) {
  auto handler = CreateStateHandler({.has_dram_part_num = false});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoRegionList_Failed) {
  auto handler = CreateStateHandler({.has_region_list = false});

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NullSku_Success) {
  std::vector<rmad::DesignConfig> design_configs = {
      {.model_name = "Model",
       .custom_label_tag = "",
       .hardware_properties = {"Property1: Yes", "Property2: Yes"}},
      {.model_name = "Model",
       .sku_id = kSkuId1,
       .custom_label_tag = kCustomLabelTag,
       .hardware_properties = {"Property1: Yes", "Property2: Yes"}}};

  auto handler = CreateStateHandler({.design_config_list = design_configs});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().custom_label_list_size(), 2);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoDesignConfigList_Failed) {
  auto handler = CreateStateHandler({.design_config_list = std::nullopt});

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoCbiNoSkuEmptyDesignConfigList_Success) {
  auto handler =
      CreateStateHandler({.has_sku = false,
                          .design_config_list = std::vector<DesignConfig>{},
                          .has_cbi = false});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoCbiNoSkuHasDesignConfigList_Failed) {
  auto handler = CreateStateHandler({.has_sku = false, .has_cbi = false});

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_CustomLabelListNotMatched_Success) {
  auto handler = CreateStateHandler({.custom_label = "CustomLabelNotMatched"});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().original_whitelabel_index(), -1);
  EXPECT_EQ(state.update_device_info().original_custom_label_index(), -1);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_FeatureNotEnabled_Success) {
  auto handler = CreateStateHandler({.is_feature_enabled = false});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_UNSUPPORTED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_FeatureMutable_Success) {
  auto handler = CreateStateHandler(
      {.is_feature_enabled = true, .is_feature_mutable = true});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_UNKNOWN);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_FeatureLevel0_Success) {
  auto handler = CreateStateHandler({.is_feature_enabled = true,
                                     .is_feature_mutable = false,
                                     .feature_level = 0});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_0);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_FeatureLevel1_Success) {
  auto handler = CreateStateHandler({.is_feature_enabled = true,
                                     .is_feature_mutable = false,
                                     .feature_level = 1});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_1);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_FeatureLevel2_Success) {
  auto handler = CreateStateHandler({.feature_level = 2});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  // Shimless RMA backend can only recognize RMAD_FEATURE_LEVEL_1 at the moment.
  EXPECT_EQ(state.update_device_info().original_feature_level(),
            UpdateDeviceInfoState::RMAD_FEATURE_LEVEL_1);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_DynamicInputsDisabled) {
  auto handler = CreateStateHandler({});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_device_info = handler->GetState().update_device_info();

  // All fields should be modifiable.
  EXPECT_TRUE(update_device_info.serial_number_modifiable());
  EXPECT_TRUE(update_device_info.region_modifiable());
  EXPECT_TRUE(update_device_info.sku_modifiable());
  EXPECT_TRUE(update_device_info.whitelabel_modifiable());
  EXPECT_TRUE(update_device_info.dram_part_number_modifiable());
  EXPECT_TRUE(update_device_info.custom_label_modifiable());
  EXPECT_TRUE(update_device_info.feature_level_modifiable());
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_DynamicInputsEnabled_SpareMlb) {
  std::string textproto = R"(
          dynamic_device_info_inputs: true
        )";
  auto handler = CreateStateHandler({.rmad_config_text = textproto});
  json_store_->SetValue(kSpareMlb, true);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_device_info = handler->GetState().update_device_info();

  // Fields to be greyed out.
  EXPECT_FALSE(update_device_info.dram_part_number_modifiable());
  EXPECT_FALSE(update_device_info.whitelabel_modifiable());
  EXPECT_FALSE(update_device_info.custom_label_modifiable());

  // Other fields should remain modifiable.
  EXPECT_TRUE(update_device_info.serial_number_modifiable());
  EXPECT_TRUE(update_device_info.region_modifiable());
  EXPECT_TRUE(update_device_info.sku_modifiable());
  EXPECT_TRUE(update_device_info.feature_level_modifiable());
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_DynamicInputsEnabled_NonSpareMlb) {
  std::string textproto = R"(
     dynamic_device_info_inputs: true
   )";
  auto handler = CreateStateHandler({.rmad_config_text = textproto});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto update_device_info = handler->GetState().update_device_info();

  // Fields to be greyed out.
  EXPECT_FALSE(update_device_info.dram_part_number_modifiable());
  EXPECT_FALSE(update_device_info.whitelabel_modifiable());
  EXPECT_FALSE(update_device_info.custom_label_modifiable());
  EXPECT_FALSE(update_device_info.serial_number_modifiable());
  EXPECT_FALSE(update_device_info.sku_modifiable());
  EXPECT_FALSE(update_device_info.feature_level_modifiable());

  // Other fields should remain modifiable.
  EXPECT_TRUE(update_device_info.region_modifiable());
}

// Sku filter and description override.
TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_SkuFilterSingleSku_Success) {
  constexpr char textproto[] = R"(
sku_list {
  sku: 1234567891
  description: "abc"
}
  )";
  const std::vector<uint32_t> expected_sku_list = {1234567891};
  const std::vector<std::string> expected_sku_description_list = {"abc"};

  auto handler = CreateStateHandler({.sku_filter_textproto = textproto});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         expected_sku_list.begin(), expected_sku_list.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 expected_sku_description_list.begin(),
                 expected_sku_description_list.end()));
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_SkuFilterEmptyDescription_Success) {
  constexpr char textproto[] = R"(
sku_list {
  sku: 1234567890
}
sku_list {
  sku: 1234567891
}
  )";
  const std::vector<uint32_t> expected_sku_list = {1234567890, 1234567891};
  const std::vector<std::string> expected_sku_description_list = {"", ""};

  auto handler = CreateStateHandler({.sku_filter_textproto = textproto});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         expected_sku_list.begin(), expected_sku_list.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 expected_sku_description_list.begin(),
                 expected_sku_description_list.end()));
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_EmptySkuFilter_Success) {
  auto handler = CreateStateHandler({.sku_filter_textproto = ""});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // An empty filter has no effect.
  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         kSkuList.begin(), kSkuList.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 kSkuDescriptionList.begin(), kSkuDescriptionList.end()));
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_IncorrectSkuFilter_Success) {
  constexpr char textproto[] = "!@#$%";

  auto handler = CreateStateHandler({.sku_filter_textproto = textproto});

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         kSkuList.begin(), kSkuList.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 kSkuDescriptionList.begin(), kSkuDescriptionList.end()));
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_SkuFilterInTestMode_Success) {
  constexpr char textproto[] = R"(
sku_list {
  sku: 1234567891
  description: "abc"
}
  )";
  const std::vector<uint32_t> expected_sku_list = {1234567890, 1234567891};
  const std::vector<std::string> expected_sku_description_list = {"", "abc"};

  auto handler = CreateStateHandler({.sku_filter_textproto = textproto});

  // Populate SKUs not in OEMs' list in testing mode.
  EXPECT_TRUE(brillo::TouchFile(GetTempDirPath().AppendASCII(kTestDirPath)));

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_TRUE(std::equal(state.update_device_info().sku_list().begin(),
                         state.update_device_info().sku_list().end(),
                         expected_sku_list.begin(), expected_sku_list.end()));
  EXPECT_TRUE(
      std::equal(state.update_device_info().sku_description_list().begin(),
                 state.update_device_info().sku_description_list().end(),
                 expected_sku_description_list.begin(),
                 expected_sku_description_list.end()));
}

// Successful |GetNextStateCase|.
TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, kCustomLabelTagList[kNewCustomLabelSelection]);
  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_LegacyCustomLabel_Success) {
  auto handler = CreateStateHandler({.use_legacy_custom_label = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(legacy_custom_label_set_,
            kCustomLabelTagList[kNewCustomLabelSelection]);
  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CustomLabelOverride_Success) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // |custom_label_index| overrides |whitelabel_index|.
  auto state = CreateStateReply(
      handler,
      {.whitelabel_index = -1, .custom_label_index = kNewCustomLabelSelection});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, kCustomLabelTagList[kNewCustomLabelSelection]);
  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

// |GetNextStateCase| fails with invalid input fields or write errors.
TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->GetNextStateCase(RmadState());

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetSerialNumber_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, false}, .set_serial_number_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetRegion_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, false}, .set_region_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetRegion_WpEnabled_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, true}, .set_region_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_WP_ENABLED);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_CannotSetSku_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, false}, .set_sku_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetCustomLabel_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, false}, .set_custom_label_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetDramPartNum_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, false}, .set_dram_part_num_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, kCustomLabelTagList[kNewCustomLabelSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_RegionEmpty_Faled) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {.region_index = -1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_RegionWrongIndex_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler, {.region_index = static_cast<int>(kRegionList.size())});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuEmpty_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {.sku_index = -1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_SkuWrongIndex_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler, {.sku_index = static_cast<int>(kSkuList.size())});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_LegacyCustomLabelEmpty_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {.whitelabel_index = -1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_LegacyCustomLabelWrongIndex_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler,
      {.whitelabel_index = static_cast<int>(kCustomLabelTagList.size())});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CustomLabelEmpty_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler, {.whitelabel_index = -1, .custom_label_index = -1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CustomLabelWrongIndex_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler,
      {.whitelabel_index = -1,
       .custom_label_index = static_cast<int>(kCustomLabelTagList.size())});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

// |GetNextStateCase| fails with inconsistent read-only fields.
TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlyMlb_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {.mlb_repair = true});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySerialNumber_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {.original_serial_number = ""});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegion_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler, {.original_region_index = kOriginalRegionSelection + 1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlySku_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler, {.original_sku_index = kOriginalSkuSelection + 1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyLegacyCustomLabel_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler,
      {.original_whitelabel_index = kOriginalCustomLabelSelection + 1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlyDram_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {.original_dram_part_number = ""});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyCustomLabel_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(
      handler,
      {.original_custom_label_index = kOriginalCustomLabelSelection + 1});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionListSize_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Append a new region.
  auto region_list = kRegionList;
  region_list.push_back("");

  auto state = CreateStateReply(handler, {.region_list = region_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionListItem_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Change a region.
  auto region_list = kRegionList;
  region_list[1] = "";

  auto state = CreateStateReply(handler, {.region_list = region_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySkuListSize_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Append a new SKU.
  auto sku_list = kSkuList;
  sku_list.push_back(0);

  auto state = CreateStateReply(handler, {.sku_list = sku_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySkuListItem_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Change a SKU.
  auto sku_list = kSkuList;
  sku_list[1] = 0;

  auto state = CreateStateReply(handler, {.sku_list = sku_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyLegacyCustomLabelListSize_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Append a new custom label.
  auto custom_label_list = kCustomLabelTagList;
  custom_label_list.push_back("");

  auto state =
      CreateStateReply(handler, {.whitelabel_list = custom_label_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyLegacyCustomLabelListItem_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Change a custom label.
  auto custom_label_list = kCustomLabelTagList;
  custom_label_list[1] = "";

  auto state =
      CreateStateReply(handler, {.whitelabel_list = custom_label_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyCustomLabelListSize_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Append a new custom label.
  auto custom_label_list = kCustomLabelTagList;
  custom_label_list.push_back("");

  auto state =
      CreateStateReply(handler, {.custom_label_list = custom_label_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyCustomLabelListItem_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // Change a custom label.
  auto custom_label_list = kCustomLabelTagList;
  custom_label_list[1] = "";

  auto state =
      CreateStateReply(handler, {.custom_label_list = custom_label_list});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_FlushOutVpd_Failed) {
  auto handler = CreateStateHandler(
      {.wp_status_list = {false, false}, .flush_out_vpd_success = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

// |GetNextStateCase| succeeds under different feature states.
TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_FeatureNotEnabled_Success) {
  auto handler = CreateStateHandler({.is_feature_enabled = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  // Feature bits are not set.
  bool is_chassis_branded;
  int hw_compliance_version;
  EXPECT_FALSE(
      ReadFakeFeaturesOutput(&is_chassis_branded, &hw_compliance_version));
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_FeatureNotMutable_Success) {
  auto handler = CreateStateHandler(
      {.is_feature_enabled = true, .is_feature_mutable = false});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  // Feature bits are not set.
  bool is_chassis_branded;
  int hw_compliance_version;
  EXPECT_FALSE(
      ReadFakeFeaturesOutput(&is_chassis_branded, &hw_compliance_version));
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_FeatureMutable_Success) {
  auto handler = CreateStateHandler(
      {.is_feature_enabled = true, .is_feature_mutable = true});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  // Feature bits are set.
  bool is_chassis_branded;
  int hw_compliance_version;
  EXPECT_TRUE(
      ReadFakeFeaturesOutput(&is_chassis_branded, &hw_compliance_version));
  EXPECT_EQ(is_chassis_branded, kNewIsChassisBranded);
  EXPECT_EQ(hw_compliance_version, kNewHwComplianceVersion);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_FeatureMutable_SpecifiedVersion_Success) {
  auto handler = CreateStateHandler(
      {.is_feature_mutable = true, .looked_up_feature_level = 2});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = CreateStateReply(handler, {});
  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateRoFirmware);

  // Feature bits are set.
  bool is_chassis_branded;
  int hw_compliance_version;
  EXPECT_TRUE(
      ReadFakeFeaturesOutput(&is_chassis_branded, &hw_compliance_version));
  EXPECT_EQ(is_chassis_branded, kNewIsChassisBranded);
  EXPECT_EQ(hw_compliance_version, 2);
}

// |TryGetNextStateCaseAtBoot| should always fail.
TEST_F(UpdateDeviceInfoStateHandlerTest, TryGetNextStateCaseAtBoot_Failed) {
  auto handler = CreateStateHandler({});
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

}  // namespace rmad
