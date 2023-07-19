// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/segmentation/segmentation_utils_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <libsegmentation/feature_management_fake.h>

#include "rmad/system/mock_tpm_manager_client.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_gsc_utils.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace {

constexpr char kDevicesTextProtoFilePath[] = "devices.textproto";

constexpr char kTestModelName[] = "test_model";
constexpr char kEmptyBoardIdType[] = "ffffffff";
constexpr char kTestBoardIdType[] = "5a5a4352";

constexpr char kEmptyDevicesTextProto[] = "";
constexpr char kNonEmptyDevicesTextProto[] = R"(
devices: "abcd"
devices: "efgh"
)";
constexpr char kInvalidDevicesTextProto[] = "abcde";

}  // namespace

namespace rmad {

class SegmentationUtilsTest : public testing::Test {
 public:
  struct Options {
    std::optional<std::string> devices_textproto = std::nullopt;
    GscVersion gsc_version = GscVersion::GSC_VERSION_NOT_GSC;
    std::optional<std::string> board_id_type = kEmptyBoardIdType;
    bool is_initial_factory_mode = false;
    segmentation::FeatureManagementInterface::FeatureLevel feature_level =
        segmentation::FeatureManagementInterface::FEATURE_LEVEL_0;
    std::optional<std::tuple<bool, int>> factory_config = std::nullopt;
    bool set_factory_config_succeeded = true;
  };

  SegmentationUtilsTest() = default;
  ~SegmentationUtilsTest() override = default;

  std::unique_ptr<SegmentationUtils> CreateSegmentationUtils(
      const Options& options) const {
    // Set up textproto.
    if (options.devices_textproto.has_value()) {
      base::FilePath textproto_file_path =
          GetTextProtoDirPath().Append(kDevicesTextProtoFilePath);
      base::WriteFile(textproto_file_path, options.devices_textproto.value());
    }

    auto fake_feature_management_interface =
        std::make_unique<segmentation::fake::FeatureManagementFake>();
    fake_feature_management_interface->SetFeatureLevel(options.feature_level);

    auto mock_tpm_manager_client =
        std::make_unique<NiceMock<MockTpmManagerClient>>();
    ON_CALL(*mock_tpm_manager_client, GetGscVersion(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(options.gsc_version), Return(true)));

    auto mock_cros_config_utils =
        std::make_unique<NiceMock<MockCrosConfigUtils>>();
    ON_CALL(*mock_cros_config_utils, GetModelName(_))
        .WillByDefault(DoAll(SetArgPointee<0>(kTestModelName), Return(true)));

    auto mock_gsc_utils = std::make_unique<NiceMock<MockGscUtils>>();
    if (options.board_id_type.has_value()) {
      ON_CALL(*mock_gsc_utils, GetBoardIdType(_))
          .WillByDefault(DoAll(SetArgPointee<0>(options.board_id_type.value()),
                               Return(true)));
    } else {
      ON_CALL(*mock_gsc_utils, GetBoardIdType(_)).WillByDefault(Return(false));
    }
    ON_CALL(*mock_gsc_utils, IsInitialFactoryModeEnabled())
        .WillByDefault(Return(options.is_initial_factory_mode));
    if (options.factory_config.has_value()) {
      const auto [is_chassis_branded, hw_compliance_version] =
          options.factory_config.value();
      ON_CALL(*mock_gsc_utils, GetFactoryConfig(_, _))
          .WillByDefault(DoAll(SetArgPointee<0>(is_chassis_branded),
                               SetArgPointee<1>(hw_compliance_version),
                               Return(true)));
    } else {
      ON_CALL(*mock_gsc_utils, GetFactoryConfig(_, _))
          .WillByDefault(Return(false));
    }
    ON_CALL(*mock_gsc_utils, SetFactoryConfig(_, _))
        .WillByDefault(Return(options.set_factory_config_succeeded));

    return std::make_unique<SegmentationUtilsImpl>(
        temp_dir_.GetPath(), std::move(fake_feature_management_interface),
        std::move(mock_tpm_manager_client), std::move(mock_cros_config_utils),
        std::move(mock_gsc_utils));
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreateDirectory(GetTextProtoDirPath()));
  }
  base::FilePath GetTextProtoDirPath() const {
    return temp_dir_.GetPath().Append(kTestModelName);
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(SegmentationUtilsTest, IsFeatureEnabled_NoTextProto) {
  auto segmentation_utils = CreateSegmentationUtils({});
  EXPECT_FALSE(segmentation_utils->IsFeatureEnabled());
}

TEST_F(SegmentationUtilsTest, IsFeatureEnabled_EmptyDeviceList) {
  auto segmentation_utils =
      CreateSegmentationUtils({.devices_textproto = kEmptyDevicesTextProto});
  EXPECT_FALSE(segmentation_utils->IsFeatureEnabled());
}

TEST_F(SegmentationUtilsTest, IsFeatureEnabled_NonEmptyDeviceList) {
  auto segmentation_utils =
      CreateSegmentationUtils({.devices_textproto = kNonEmptyDevicesTextProto});
  EXPECT_TRUE(segmentation_utils->IsFeatureEnabled());
}

TEST_F(SegmentationUtilsTest, IsFeatureEnabled_InvalidDeviceList) {
  auto segmentation_utils =
      CreateSegmentationUtils({.devices_textproto = kInvalidDevicesTextProto});
  EXPECT_FALSE(segmentation_utils->IsFeatureEnabled());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable_NotGsc) {
  auto segmentation_utils =
      CreateSegmentationUtils({.gsc_version = GscVersion::GSC_VERSION_NOT_GSC});
  EXPECT_FALSE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable_Cr50_BoardIdTypeEmpty) {
  auto segmentation_utils =
      CreateSegmentationUtils({.gsc_version = GscVersion::GSC_VERSION_CR50,
                               .board_id_type = kEmptyBoardIdType});
  EXPECT_TRUE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable_Cr50_BoardIdTypeNotEmpty) {
  auto segmentation_utils =
      CreateSegmentationUtils({.gsc_version = GscVersion::GSC_VERSION_CR50,
                               .board_id_type = kTestBoardIdType});
  EXPECT_FALSE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable_Cr50_BoardIdTypeFailed) {
  auto segmentation_utils =
      CreateSegmentationUtils({.gsc_version = GscVersion::GSC_VERSION_CR50,
                               .board_id_type = std::nullopt});
  EXPECT_FALSE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable_Ti50_InitialFactoryMode) {
  auto segmentation_utils =
      CreateSegmentationUtils({.gsc_version = GscVersion::GSC_VERSION_TI50,
                               .is_initial_factory_mode = true});
  EXPECT_TRUE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, IsFeatureMutable_Ti50_NotInitialFactoryMode) {
  auto segmentation_utils =
      CreateSegmentationUtils({.gsc_version = GscVersion::GSC_VERSION_TI50,
                               .is_initial_factory_mode = false});
  EXPECT_FALSE(segmentation_utils->IsFeatureMutable());
}

TEST_F(SegmentationUtilsTest, GetFeatureLevel_0) {
  auto segmentation_utils = CreateSegmentationUtils(
      {.feature_level =
           segmentation::FeatureManagementInterface::FEATURE_LEVEL_0});
  EXPECT_EQ(0, segmentation_utils->GetFeatureLevel());
}

TEST_F(SegmentationUtilsTest, GetFeatureLevel_1) {
  auto segmentation_utils = CreateSegmentationUtils(
      {.feature_level =
           segmentation::FeatureManagementInterface::FEATURE_LEVEL_1});
  EXPECT_EQ(1, segmentation_utils->GetFeatureLevel());
}

TEST_F(SegmentationUtilsTest, GetFeatureFlags_Succeeded) {
  auto segmentation_utils =
      CreateSegmentationUtils({.factory_config = std::make_tuple(true, 1)});
  bool is_chassis_branded;
  int hw_compliance_version;
  EXPECT_TRUE(segmentation_utils->GetFeatureFlags(&is_chassis_branded,
                                                  &hw_compliance_version));
  EXPECT_TRUE(is_chassis_branded);
  EXPECT_EQ(1, hw_compliance_version);
}

TEST_F(SegmentationUtilsTest, GetFeatureFlags_Failed) {
  auto segmentation_utils =
      CreateSegmentationUtils({.factory_config = std::nullopt});
  bool is_chassis_branded;
  int hw_compliance_version;
  EXPECT_FALSE(segmentation_utils->GetFeatureFlags(&is_chassis_branded,
                                                   &hw_compliance_version));
}

TEST_F(SegmentationUtilsTest, SetFeatureFlags_Succeeded) {
  auto segmentation_utils =
      CreateSegmentationUtils({.set_factory_config_succeeded = true});
  EXPECT_TRUE(segmentation_utils->SetFeatureFlags(true, 1));
}

TEST_F(SegmentationUtilsTest, SetFeatureFlags_Failed) {
  auto segmentation_utils =
      CreateSegmentationUtils({.set_factory_config_succeeded = false});
  EXPECT_FALSE(segmentation_utils->SetFeatureFlags(true, 1));
}

}  // namespace rmad
