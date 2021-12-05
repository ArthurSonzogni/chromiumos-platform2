// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/mock_cbi_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_regions_utils.h"
#include "rmad/utils/mock_vpd_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;

namespace rmad {

constexpr char kSerialNumber[] = "TestSerialNumber";
constexpr char kRegion[] = "TestRegion";
// We get the value of type int from cros_config, but we set uint64_t in cbi.
// This is used to mock the result of cbi, so it is an int.
constexpr uint64_t kSkuId = 1234567890;
// Test for overflow, because we get uint64_t from cbi and int from cros_config
constexpr uint64_t kSkuIdOverflow =
    static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1;
constexpr char kWhitelabelTag[] = "TestWhitelabelTag";
constexpr char kDramPartNum[] = "TestDramPartNum";

const std::vector<std::string> kRegionList = {"TestRegion", "TestRegion1"};
// We get the value of type int from cros_config, but we set uint64_t in cbi.
// This is used to mock the result of cros_config, so it is a vector<int>.
const std::vector<int> kSkuList = {1234567890, 1234567891};
// The last option is always an empty string, because it is always valid.
const std::vector<std::string> kWhitelabelTagList = {
    "TestWhitelabelTag", "TestWhitelabelTag0", "TestWhitelabelTag1", ""};
constexpr uint32_t kOriginalRegionSelection = 0;
constexpr uint32_t kOriginalSkuSelection = 0;
constexpr uint32_t kOriginalWhitelabelSelection = 0;

constexpr char kNewSerialNumber[] = "NewTestSerialNumber";
constexpr uint32_t kNewRegionSelection = 1;
constexpr uint32_t kNewSkuSelection = 1;
constexpr uint32_t kNewWhitelabelSelection = 3;
constexpr char kNewDramPartNum[] = "NewTestDramPartNum";

class UpdateDeviceInfoStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<UpdateDeviceInfoStateHandler> CreateStateHandler(
      bool serial_number = true,
      bool region = true,
      bool sku = true,
      bool whitelabel = true,
      bool dram_part_num = true,
      bool region_list = true,
      bool sku_list = true,
      bool whitelabel_list = true,
      bool set_serial_number = true,
      bool set_region = true,
      bool set_sku = true,
      bool set_whitelabel = true,
      bool set_dram_part_num = true,
      bool sku_overflow = false) {
    auto cbi_utils = std::make_unique<NiceMock<MockCbiUtils>>();
    auto cros_config_utils = std::make_unique<NiceMock<MockCrosConfigUtils>>();
    auto regions_utils = std::make_unique<NiceMock<MockRegionsUtils>>();
    auto vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();

    if (serial_number) {
      ON_CALL(*vpd_utils, GetSerialNumber(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSerialNumber), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetSerialNumber(_)).WillByDefault(Return(false));
    }

    if (region) {
      ON_CALL(*vpd_utils, GetRegion(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kRegion), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetRegion(_)).WillByDefault(Return(false));
    }

    if (sku && !sku_overflow) {
      ON_CALL(*cbi_utils, GetSku(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuId), Return(true)));
    } else if (sku_overflow) {
      ON_CALL(*cbi_utils, GetSku(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuIdOverflow), Return(true)));
    } else {
      ON_CALL(*cbi_utils, GetSku(_)).WillByDefault(Return(false));
    }

    if (whitelabel) {
      ON_CALL(*vpd_utils, GetWhitelabelTag(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kWhitelabelTag), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetWhitelabelTag(_)).WillByDefault(Return(false));
    }

    if (dram_part_num) {
      ON_CALL(*cbi_utils, GetDramPartNum(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kDramPartNum), Return(true)));
    } else {
      ON_CALL(*cbi_utils, GetDramPartNum(_)).WillByDefault(Return(false));
    }

    if (region_list) {
      ON_CALL(*regions_utils, GetRegionList(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kRegionList), Return(true)));
    } else {
      ON_CALL(*regions_utils, GetRegionList(_)).WillByDefault(Return(false));
    }

    if (sku_list) {
      ON_CALL(*cros_config_utils, GetSkuIdList(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuList), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetSkuIdList(_)).WillByDefault(Return(false));
    }

    if (whitelabel_list) {
      ON_CALL(*cros_config_utils, GetWhitelabelTagList(_))
          .WillByDefault(
              DoAll(SetArgPointee<0>(kWhitelabelTagList), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetWhitelabelTagList(_))
          .WillByDefault(Return(false));
    }

    if (set_serial_number) {
      ON_CALL(*vpd_utils, SetSerialNumber(_))
          .WillByDefault(DoAll(SaveArg<0>(&serial_number_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetSerialNumber(_)).WillByDefault(Return(false));
    }

    if (set_region) {
      ON_CALL(*vpd_utils, SetRegion(_))
          .WillByDefault(DoAll(SaveArg<0>(&region_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetRegion(_)).WillByDefault(Return(false));
    }

    if (set_sku) {
      ON_CALL(*cbi_utils, SetSku(_))
          .WillByDefault(DoAll(SaveArg<0>(&sku_set_), Return(true)));
    } else {
      ON_CALL(*cbi_utils, SetSku(_)).WillByDefault(Return(false));
    }

    if (set_whitelabel) {
      ON_CALL(*vpd_utils, SetWhitelabelTag(_))
          .WillByDefault(DoAll(SaveArg<0>(&whitelabel_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetWhitelabelTag(_)).WillByDefault(Return(false));
    }

    if (set_dram_part_num) {
      ON_CALL(*cbi_utils, SetDramPartNum(_))
          .WillByDefault(DoAll(SaveArg<0>(&dram_part_num_set_), Return(true)));
    } else {
      ON_CALL(*cbi_utils, SetDramPartNum(_)).WillByDefault(Return(false));
    }

    ON_CALL(*vpd_utils, FlushOutRoVpdCache()).WillByDefault(Return(true));

    return base::MakeRefCounted<UpdateDeviceInfoStateHandler>(
        json_store_, std::move(cbi_utils), std::move(cros_config_utils),
        std::move(regions_utils), std::move(vpd_utils));
  }

 protected:
  std::string serial_number_set_;
  std::string region_set_;
  uint64_t sku_set_;
  std::string whitelabel_set_;
  std::string dram_part_num_set_;
};

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_Mlb_Success) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, true);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoMlb_Success) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NonMlb_Failed) {
  auto handler = CreateStateHandler();

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_Noserial_number_Success) {
  // serial_number
  auto handler = CreateStateHandler(false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoRegion_Success) {
  // serial_number, region
  auto handler = CreateStateHandler(true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoSku_Success) {
  // serial_number, region, sku
  auto handler = CreateStateHandler(true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoWhitelabel_Success) {
  // serial_number, region, sku, whitelabel
  auto handler = CreateStateHandler(true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoDramPartNum_Success) {
  // serial_number, region, sku, whitelabel, dram_part_num
  auto handler = CreateStateHandler(true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoRegionList_Failed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list
  auto handler = CreateStateHandler(true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoSkuList_Failed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list
  auto handler = CreateStateHandler(true, true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoWhitelabelList_Failed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list
  auto handler =
      CreateStateHandler(true, true, true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(whitelabel_set_, kWhitelabelTagList[kNewWhitelabelSelection]);

  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->GetNextStateCase(RmadState());

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_serial_numberFailed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list, set_serial_number
  auto handler =
      CreateStateHandler(true, true, true, true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_RegionFailed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list, set_serial_number, set_region
  auto handler = CreateStateHandler(true, true, true, true, true, true, true,
                                    true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuFailed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list, set_serial_number, set_region, set_sku
  auto handler = CreateStateHandler(true, true, true, true, true, true, true,
                                    true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_WhitelabelFailed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list, set_serial_number, set_region, set_sku,
  // set_whitelabel
  auto handler = CreateStateHandler(true, true, true, true, true, true, true,
                                    true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_DramPartNumFailed) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list, set_serial_number, set_region, set_sku,
  // set_whitelabel, set_dram_part_num
  auto handler = CreateStateHandler(true, true, true, true, true, true, true,
                                    true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(whitelabel_set_, kWhitelabelTagList[kNewWhitelabelSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_RegionEmptyFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(0);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_RegionWrongIndex) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);

  // The first option is always reserved for empty option, so we should add
  // one to exceed the size.
  state.mutable_update_device_info()->set_region_index(kRegionList.size() + 1);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuEmptyFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(0);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuWrongIndex) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);

  // The first option is always reserved for empty option, so we should add
  // one to exceed the size.
  state.mutable_update_device_info()->set_sku_index(kSkuList.size() + 1);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_WhitelabelEmptySuccess) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kWhitelabelTagList.size() - 1);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always empty and reserved, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(whitelabel_set_, "");

  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_WhitelabelWrongIndex) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);

  // The first option is always reserved for empty option, so we should add
  // one to exceed the size.
  state.mutable_update_device_info()->set_whitelabel_index(
      kWhitelabelTagList.size());

  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyserial_numberFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_serial_number("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_region_index(
      kOriginalRegionSelection + 1);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlySkuFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_sku_index(
      kOriginalSkuSelection + 1);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyWhitelabelFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_whitelabel_index(
      kOriginalWhitelabelSelection + 1);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlyDramFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_dram_part_number("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionListSizeFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->add_region_list("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionListItemFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_region_list(1, "");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySkuListSizeFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->add_sku_list(0);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySkuListItemFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_sku_list(1, 0);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyWhitelabelListSizeFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->add_whitelabel_list("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyWhitelabelListItemFailed) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_whitelabel_list(1, "");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_DEVICE_INFO_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuOverflow_Success) {
  // serial_number, region, sku, whitelabel, dram_part_num, region_list,
  // sku_list, whitelabel_list, set_serial_number, set_region, set_sku,
  // set_whitelabel, set_dram_part_num, sku_overflow
  auto handler = CreateStateHandler(true, true, true, true, true, true, true,
                                    true, true, true, true, true, true, true);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  // We check if the original_sku is set to unmatched by trying
  // GetNextStateCase with |original_sku_index| = -1.
  state.mutable_update_device_info()->set_original_sku_index(-1);
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewWhitelabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kCheckCalibration);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  // The first option is always reserved for empty option, so subtract one.
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(whitelabel_set_, kWhitelabelTagList[kNewWhitelabelSelection]);

  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

}  // namespace rmad
